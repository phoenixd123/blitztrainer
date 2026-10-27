#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <chrono>
#include <ctime>

#include "Game.hpp"
#include "Store.hpp"
#include "core/Model.hpp"

using namespace geode::prelude;
using namespace bt;

namespace {
    int64_t nowUnix() { return static_cast<int64_t>(std::time(nullptr)); }

    template <class T>
    T setting(char const* key) { return Mod::get()->getSettingValue<T>(key); }

    void playSound(char const* file) {
        if (!setting<bool>("play-sounds")) return;
        FMODAudioEngine::get()->playEffect(file);
    }

    void notify(std::string const& text, NotificationIcon icon = NotificationIcon::Success) {
        if (!setting<bool>("show-notifications")) return;
        Notification::create(text, icon, 1.6f)->show();
    }

    std::string rangeText(Profile const& p, Range const& r) {
        return fmt::format("{} -> {}", formatPct(p.pct(r.from)), formatPct(p.pct(r.to)));
    }

    std::vector<float> const kEmpty;
}

class $modify(BTPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<Ref<StartPosObject>> collected;
        std::vector<StartPosObject*> byCheckpoint; // index = checkpoint idx (0 = level start = nullptr)
        std::vector<float> checkpoints;
        bool attached = false;
        bool mismatch = false;
        bool orderedStartPos = false;   // level uses target-order / reverse startposes
        int spawnIdx = -1;              // checkpoint the current attempt spawned at (cached at respawn)

        // percentages as GD displays them, per checkpoint
        std::vector<float> pctByX;
        std::vector<float> pctByTime;
        bool timePercent = false;
        bool percentCalibrated = false;

        // current attempt
        bool runActive = false;
        int runStart = -1;
        double runTime = 0.0;
        bool noclip = false;
        bool speedhack = false;
        GameObject* firstHit = nullptr; // the game's own startup hazard check per attempt

        // speedhack detection
        double realSum = 0.0;
        double gameSum = 0.0;
        double warp = 1.0;
        std::optional<std::chrono::steady_clock::time_point> lastFrame;

        // spawn point to use on the next respawn
        bool hasPending = false;
        Ref<StartPosObject> pending = nullptr;

        // HUD
        CCNode* hud = nullptr;
        CCLabelBMFont* line1 = nullptr;
        CCLabelBMFont* line2 = nullptr;
        CCLayerColor* bar = nullptr;
        CCLayerColor* barTarget = nullptr;
        CCLayerColor* barProgress = nullptr;
        CCNode* barTicks = nullptr;
        bool hudHidden = false;
    };

    static void onModify(auto& self) {
        // run before cheat mods so a swallowed death can be spotted
        if (!self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First))
            log::warn("Failed to set destroyPlayer hook priority");
    }

    // ---------------------------------------------------------------- setup

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto bind = [this](char const* key, auto fn) {
            this->addEventListener(
                KeybindSettingPressedEventV3(Mod::get(), key),
                [this, fn](Keybind const&, bool down, bool repeat, double) {
                    if (down && !repeat) fn(this);
                }
            );
        };
        bind("key-prev-start", [](BTPlayLayer* pl) { pl->cycleSpawn(-1); });
        bind("key-next-start", [](BTPlayLayer* pl) { pl->cycleSpawn(1); });
        bind("key-go-target", [](BTPlayLayer* pl) { pl->goTarget(false); });
        bind("key-toggle-hud", [](BTPlayLayer* pl) {
            pl->m_fields->hudHidden = !pl->m_fields->hudHidden;
            pl->updateHud();
        });
        return true;
    }

    void addObject(GameObject* obj) {
        PlayLayer::addObject(obj);
        if (!obj || obj->m_objectID != 31) return;
        if (auto sp = typeinfo_cast<StartPosObject*>(obj)) m_fields->collected.push_back(sp);
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        attach();
    }

    static bool usableStartPos(StartPosObject* sp) {
        return sp && !(sp->m_startSettings && sp->m_startSettings->m_disableStartPos);
    }

    void attach() {
        auto f = m_fields.self();
        if (f->attached || m_levelLength <= 0.f) return;
        f->attached = true;

        // Start positions seen by addObject plus a full scan of the level's
        // objects (some loading paths skip addObject), de-duplicated.
        std::vector<StartPosObject*> all;
        auto addUnique = [&](StartPosObject* sp) {
            if (!usableStartPos(sp)) return;
            if (std::find(all.begin(), all.end(), sp) == all.end()) all.push_back(sp);
        };
        for (auto& sp : f->collected) addUnique(sp);
        if (m_objects) {
            for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
                if (obj && obj->m_objectID == 31) addUnique(typeinfo_cast<StartPosObject*>(obj));
            }
        }
        f->collected.clear();
        for (auto sp : all) f->collected.push_back(sp);

        std::vector<float> xs;
        for (auto sp : all) {
            xs.push_back(sp->getPositionX());
            if (sp->m_startSettings && (sp->m_startSettings->m_targetOrder != 0 || sp->m_startSettings->m_reverseGameplay))
                f->orderedStartPos = true;
        }
        f->checkpoints = buildCheckpoints(xs, m_levelLength);

        // map each checkpoint to the start position closest to it
        f->byCheckpoint.assign(f->checkpoints.size(), nullptr);
        for (size_t i = 1; i + 1 < f->checkpoints.size(); ++i) {
            float best = 1.f;
            for (auto sp : all) {
                float d = std::fabs(sp->getPositionX() - f->checkpoints[i]);
                if (d < best) { best = d; f->byCheckpoint[i] = sp; }
            }
        }
        log::info("Blitz Trainer: {} usable start positions, {} checkpoints", all.size(), f->checkpoints.size());

        computeDisplayPercents();

        auto& store = Store::get();
        store.open(levelKeyFor(m_level), m_level->m_levelName);

        if (auto p = store.profile()) {
            f->mismatch = !checkpointsMatch(*p, f->checkpoints);
        } else if (setting<bool>("auto-create-profile") && startPosCount() > 0 && !m_level->isPlatformer()) {
            auto style = setting<std::string>("stage-style") == "Lite" ? StageStyle::Lite : StageStyle::Classic;
            store.setProfile(makeProfile(store.key(), m_level->m_levelName, xs, m_levelLength, style, nowUnix()));
        }
        syncDisplay();
        createHud();
    }

    // ---------------------------------------------------------------- percentages

    float pctFromTime(float x) {
        float total = this->timeForPos({m_levelLength, 0.f}, 0, 0, true, 0);
        if (!(total > 0.f)) return x / m_levelLength * 100.f;
        return std::clamp(this->timeForPos({x, 0.f}, 0, 0, true, 0) / total * 100.f, 0.f, 100.f);
    }

    void computeDisplayPercents() {
        auto f = m_fields.self();
        f->pctByX.clear();
        f->pctByTime.clear();
        for (float x : f->checkpoints) {
            f->pctByX.push_back(std::clamp(x / m_levelLength * 100.f, 0.f, 100.f));
            f->pctByTime.push_back(pctFromTime(x));
        }
        if (!f->checkpoints.empty()) {
            f->pctByX.back() = 100.f;
            f->pctByTime.back() = 100.f;
            f->pctByX.front() = 0.f;
            f->pctByTime.front() = 0.f;
        }
        f->timePercent = Mod::get()->getSavedValue<bool>("time-based-percent", false);
    }

    // Compare GD's own percentage with both formulas and keep the closer one.
    void calibratePercent(float x) {
        auto f = m_fields.self();
        if (m_levelLength <= 0.f) return;
        float actual = this->getCurrentPercent();
        float byX = std::clamp(x / m_levelLength * 100.f, 0.f, 100.f);
        float byTime = pctFromTime(x);
        if (std::fabs(byX - byTime) < 0.5f) return; // indistinguishable here
        bool useTime = std::fabs(actual - byTime) < std::fabs(actual - byX);
        f->percentCalibrated = true;
        if (useTime != f->timePercent) {
            f->timePercent = useTime;
            Mod::get()->setSavedValue("time-based-percent", useTime);
            log::info("Blitz Trainer: using {} percentages", useTime ? "time-based (2.2)" : "position-based (2.1)");
            syncDisplay();
            updateHud();
        }
    }

    // Hands the displayed percentages to the profile the UI reads from.
    void syncDisplay() {
        auto f = m_fields.self();
        auto p = Store::get().profile();
        if (!p) return;
        auto const& table = f->timePercent ? f->pctByTime : f->pctByX;
        if (!f->mismatch && table.size() == p->xs.size()) p->shownPct = table;
        else p->shownPct.clear();
    }

    float shownPctAt(int idx) {
        auto f = m_fields.self();
        auto const& table = f->timePercent ? f->pctByTime : f->pctByX;
        if (idx < 0 || idx >= static_cast<int>(table.size())) return 0.f;
        return table[idx];
    }

    int startPosCount() {
        auto n = static_cast<int>(m_fields->checkpoints.size());
        return n >= 2 ? n - 2 : 0;
    }

    Profile* trackedProfile() {
        auto p = Store::get().profile();
        if (!p || m_fields->mismatch || m_level->isPlatformer()) return nullptr;
        return p;
    }

    int spawnIndex() { return m_fields->spawnIdx; }

    // Nearest checkpoint (not the end) to an X position, within `tolerance`.
    int checkpointNear(float x, float tolerance) {
        auto const& cps = m_fields->checkpoints;
        int best = -1;
        float bestDist = tolerance;
        for (int i = 0; i + 1 < static_cast<int>(cps.size()); ++i) {
            float d = std::fabs(cps[i] - x);
            if (d <= bestDist) { bestDist = d; best = i; }
        }
        return best;
    }

    // Works out which checkpoint this attempt spawned at. Called right after
    // the respawn, when the player is still standing on the spawn point.
    int detectSpawn() {
        auto f = m_fields.self();
        if (f->checkpoints.size() < 2 || !m_player1) return -1;
        float px = m_player1->getPositionX();
        int byPos = checkpointNear(px, 15.f);

        if (m_isPracticeMode && m_checkpointArray && m_checkpointArray->count() > 0) {
            // practice checkpoint: only counts if it sits exactly on a start position
            return checkpointNear(px, 3.f);
        }

        int byPtr = -1;
        if (!m_startPosObject) byPtr = 0;
        else {
            for (size_t i = 1; i < f->byCheckpoint.size(); ++i)
                if (f->byCheckpoint[i] == m_startPosObject) { byPtr = static_cast<int>(i); break; }
            if (byPtr < 0) byPtr = checkpointNear(m_startPosObject->getPositionX(), 2.f);
        }

        // where the player actually is wins over what the game says it picked
        if (byPos >= 0) {
            if (byPtr >= 0 && byPtr != byPos)
                log::debug("Blitz Trainer: spawn pointer says {}, player is at checkpoint {}", byPtr, byPos);
            return byPos;
        }
        if (!m_startPosObject) return 0; // level start (the player may start slightly off X=0)
        return byPtr;
    }

    // ---------------------------------------------------------------- attempts

    void resetLevel() {
        attach();
        auto f = m_fields.self();
        if (f->hasPending) {
            f->hasPending = false;
            bool usingCheckpoint = m_isPracticeMode && m_checkpointArray && m_checkpointArray->count() > 0;
            if (!usingCheckpoint) this->setStartPosObject(f->pending);
            f->pending = nullptr;
        }

        PlayLayer::resetLevel();

        f->runActive = true;
        f->runTime = 0.0;
        f->noclip = false;
        f->speedhack = false;
        f->firstHit = nullptr;
        resetSpeedSamples();

        f->spawnIdx = detectSpawn();
        if (f->spawnIdx > 0 && m_player1) calibratePercent(m_player1->getPositionX());

        f->runStart = -1;
        if (trackedProfile()) {
            bool practiceIgnored = m_isPracticeMode && !setting<bool>("track-practice");
            if (!practiceIgnored) f->runStart = f->spawnIdx;
        }
        updateHud();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (!f->attached) attach();
        if (!f->runActive || m_isPaused || !m_player1 || m_player1->m_isDead) {
            f->lastFrame.reset();
            return;
        }
        f->runTime += dt;
        // time-warp sections change dt on purpose; only check at normal speed
        if (setting<bool>("detect-speedhack") && std::fabs(f->warp - 1.0) < 0.01) checkSpeed(dt);
    }

    void updateTimeWarp(float warp) {
        PlayLayer::updateTimeWarp(warp);
        m_fields->warp = warp > 0.f ? warp : 1.0;
        resetSpeedSamples();
    }

    void resetSpeedSamples() {
        auto f = m_fields.self();
        f->realSum = 0.0;
        f->gameSum = 0.0;
        f->lastFrame.reset();
    }

    void checkSpeed(float dt) {
        auto f = m_fields.self();
        if (f->speedhack) return;
        auto now = std::chrono::steady_clock::now();
        if (!f->lastFrame) { f->lastFrame = now; return; }
        double real = std::chrono::duration<double>(now - *f->lastFrame).count();
        f->lastFrame = now;
        // skip lag spikes (the game clamps dt there) and nonsense samples
        if (real <= 0.0 || real > 0.1) return;
        f->realSum += real;
        f->gameSum += static_cast<double>(dt);
        if (f->realSum < 1.5) return;
        double ratio = f->gameSum / f->realSum;
        if (ratio < 0.8 || ratio > 1.2) {
            f->speedhack = true;
            log::info("Speed change detected (ratio {:.2f}), this attempt won't count", ratio);
        }
        // sliding window
        f->realSum *= 0.5;
        f->gameSum *= 0.5;
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        PlayLayer::destroyPlayer(player, obj);
        auto f = m_fields.self();
        if (!player || !f->runActive) return;
        // the game fires its own anti-cheat hazard at the start of every attempt
        if (obj && obj == m_anticheatSpike) return;
        if (!f->firstHit) {
            f->firstHit = obj ? obj : reinterpret_cast<GameObject*>(player);
            if (!player->m_isDead) return;
        }

        if (!player->m_isDead) {
            if (!m_levelEndAnimationStarted && setting<bool>("detect-noclip")) f->noclip = true;
            return;
        }
        if (m_level->isPlatformer()) { f->runActive = false; return; }
        float x = player->getPositionX();
        calibratePercent(x);
        endRun(x, false);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (m_fields->runActive && !m_level->isPlatformer()) endRun(m_levelLength, true);
    }

    void onQuit() {
        Store::get().save();
        PlayLayer::onQuit();
    }

    void endRun(float endX, bool completed) {
        auto f = m_fields.self();
        f->runActive = false;
        auto& store = Store::get();
        store.sessionAttempts += 1;
        store.sessionTime += f->runTime;

        auto p = trackedProfile();
        if (!p) { updateHud(); return; }

        if (f->noclip || f->speedhack) {
            notify(f->noclip ? "Run ignored: noclip detected" : "Run ignored: speed change detected", NotificationIcon::Warning);
            updateHud();
            return;
        }
        if (f->runStart < 0) {
            // not a training spawn; still useful for the death heatmap
            if (!completed && setting<bool>("record-untracked-deaths")) {
                p->deaths[std::clamp(static_cast<int>(p->pctOfX(endX)), 0, 99)] += 1;
                store.save();
            }
            updateHud();
            return;
        }

        store.snapshot();
        RunInput in;
        in.startIdx = f->runStart;
        in.endX = endX;
        in.completed = completed;
        in.time = f->runTime;
        auto res = applyRun(*p, in, store.rules(), nowUnix());
        store.save();

        if (res.counted) feedback(*p, res);
        autoRoute(*p, res);
        updateHud();
    }

    void feedback(Profile const& p, RunResult const& res) {
        auto const& stage = p.stages[res.stage];
        auto const& t = stage.ranges[res.target];
        int need = Store::get().rules().requiredPasses;

        if (res.profileCompleted) {
            notify("Blitzkrieg complete! Every stage is cleared.");
            playSound("achievement_01.ogg");
            return;
        }
        if (res.stageCompleted) {
            notify(fmt::format("Stage {} cleared!", res.stage + 1));
            playSound("achievement_01.ogg");
            return;
        }
        if (!res.newlyDone.empty()) {
            auto msg = res.newlyDone.size() == 1
                ? fmt::format("Cleared {}  ({}/{} in stage)", rangeText(p, stage.ranges[res.newlyDone[0]]), stage.doneCount(), stage.ranges.size())
                : fmt::format("Cleared {} ranges  ({}/{} in stage)", res.newlyDone.size(), stage.doneCount(), stage.ranges.size());
            notify(msg);
            playSound("highscoreGet02.ogg");
            Store::get().sessionPasses += 1;
            return;
        }
        if (res.targetPassed) {
            notify(fmt::format("Pass {}  ({}/{})", rangeText(p, t), std::min(t.s.passes, need), need));
            playSound("counter003.ogg");
            Store::get().sessionPasses += 1;
        }
    }

    void autoRoute(Profile const& p, RunResult const&) {
        if (!setting<bool>("auto-route")) return;
        if (m_isPracticeMode && m_checkpointArray && m_checkpointArray->count() > 0) return;
        int spawn = spawnIndex();
        auto tf = targetFor(p, spawn);
        bool spawnStillUseful = tf && !p.stages[tf->first].ranges[tf->second].done;
        if (spawnStillUseful) return;
        if (auto next = nextTargetStart(p, spawn)) queueSpawn(*next);
    }

    void queueSpawn(int idx) {
        auto f = m_fields.self();
        if (idx < 0 || idx >= static_cast<int>(f->byCheckpoint.size()) - 1) return;
        f->pending = f->byCheckpoint[idx];
        f->hasPending = true;
    }

    // ---------------------------------------------------------------- switching

    bool canSwitchNow() {
        return m_player1 && !m_player1->m_isDead && !m_isPaused && !m_levelEndAnimationStarted;
    }

    bool switchSpawn(int idx, bool fromPause) {
        attach();
        auto f = m_fields.self();
        if (m_level->isPlatformer()) return false;
        if (idx < 0 || idx >= static_cast<int>(f->byCheckpoint.size()) - 1) return false;
        if (idx > 0 && !f->byCheckpoint[idx]) return false;

        queueSpawn(idx);
        if (m_isPracticeMode) this->removeAllCheckpoints();

        if (fromPause || m_isPaused) {
            // let the game's own restart path handle music and resuming
            if (auto pause = CCScene::get()->getChildByType<PauseLayer>(0)) {
                pause->onRestart(nullptr);
                return true;
            }
            return true; // applied on the next respawn
        }
        if (!canSwitchNow()) return true; // dead or finishing: applied on the next respawn
        f->runActive = false; // abandoned attempt isn't recorded
        this->resetLevel();
        this->startMusic();
        return true;
    }

    void cycleSpawn(int dir) {
        auto f = m_fields.self();
        attach();
        int sections = static_cast<int>(f->checkpoints.size()) - 1;
        if (sections <= 1) return;
        int cur = f->hasPending ? -2 : spawnIndex();
        if (cur == -2) {
            for (int i = 0; i < sections; ++i) if (f->byCheckpoint[i] == f->pending) { cur = i; break; }
        }
        if (cur < 0) cur = 0;
        int next = cur;
        for (int tries = 0; tries < sections; ++tries) {
            next = ((next + dir) % sections + sections) % sections;
            if (switchSpawn(next, false)) {
                notify(fmt::format("Spawn: {}", next == 0 ? std::string("level start") : formatPct(shownPctAt(next))), NotificationIcon::Info);
                return;
            }
        }
    }

    bool goTarget(bool fromPause) {
        auto p = trackedProfile();
        if (!p) return false;
        auto next = nextTargetStart(*p, -1);
        if (!next) return false;
        return switchSpawn(*next, fromPause);
    }

    // ---------------------------------------------------------------- HUD

    void createHud() {
        auto f = m_fields.self();
        if (f->hud) return;
        auto hud = CCNode::create();
        hud->setID("blitz-trainer-hud"_spr);
        hud->setAnchorPoint({0.f, 0.f});

        f->line1 = CCLabelBMFont::create("", "bigFont.fnt");
        f->line2 = CCLabelBMFont::create("", "bigFont.fnt");
        for (auto l : {f->line1, f->line2}) {
            l->setAnchorPoint({0.f, 1.f});
            hud->addChild(l);
        }

        f->bar = CCLayerColor::create({0, 0, 0, 140}, 150.f, 5.f);
        f->barProgress = CCLayerColor::create({60, 220, 110, 220}, 0.f, 5.f);
        f->barTarget = CCLayerColor::create({255, 210, 60, 230}, 0.f, 5.f);
        f->barTicks = CCNode::create();
        f->bar->addChild(f->barProgress);
        f->bar->addChild(f->barTarget);
        f->bar->addChild(f->barTicks);
        hud->addChild(f->bar);

        this->addChild(hud, 999);
        f->hud = hud;
        updateHud();
    }

    void layoutHud() {
        auto f = m_fields.self();
        if (!f->hud) return;
        float scale = static_cast<float>(setting<double>("hud-scale"));
        auto win = CCDirector::get()->getWinSize();
        auto pos = setting<std::string>("hud-position");
        bool right = pos.find("Right") != std::string::npos;
        bool bottom = pos.find("Bottom") != std::string::npos;

        float w1 = f->line1->getContentWidth() * 0.32f * scale;
        float w2 = f->line2->getContentWidth() * 0.27f * scale;
        float barW = 150.f * scale;
        float width = std::max({w1, w2, barW});

        f->line1->setScale(0.32f * scale);
        f->line2->setScale(0.27f * scale);
        f->line1->setPosition({0.f, 0.f});
        f->line2->setPosition({0.f, -11.f * scale});
        f->bar->setScaleX(scale);
        f->bar->setScaleY(scale);
        f->bar->setPosition({0.f, -28.f * scale});
        if (right) {
            f->line1->setAnchorPoint({1.f, 1.f});
            f->line2->setAnchorPoint({1.f, 1.f});
            f->line1->setPositionX(width);
            f->line2->setPositionX(width);
            f->bar->setPositionX(width - barW);
        } else {
            f->line1->setAnchorPoint({0.f, 1.f});
            f->line2->setAnchorPoint({0.f, 1.f});
        }
        float height = 30.f * scale;
        float x = right ? win.width - width - 6.f : 6.f;
        float y = bottom ? height + 6.f : win.height - 6.f;
        f->hud->setPosition({x, y});

        int opacity = static_cast<int>(std::clamp(setting<double>("hud-opacity"), 0.1, 1.0) * 255);
        f->line1->setOpacity(opacity);
        f->line2->setOpacity(opacity);
    }

    void updateHud() {
        auto f = m_fields.self();
        syncDisplay();
        if (!f->hud) return;
        bool show = setting<bool>("show-hud") && !f->hudHidden && !m_level->isPlatformer();
        f->hud->setVisible(show);
        if (!show) return;

        auto& store = Store::get();
        auto p = store.profile();
        std::string l1, l2;
        float targetFrom = 0.f, targetTo = 0.f, progress = 0.f;
        bool showBar = false;

        if (!p) {
            l1 = "Blitz Trainer";
            l2 = startPosCount() > 0
                ? fmt::format("{} start positions found - Pause > Blitz Trainer to begin", startPosCount())
                : std::string("No start positions in this level");
        } else if (f->mismatch) {
            l1 = "Blitz Trainer: start positions changed";
            l2 = "Pause > Blitz Trainer > Rebuild (keeps your progress)";
        } else {
            showBar = true;
            progress = p->overallProgress();
            int si = p->currentStage();
            if (si < 0) {
                l1 = fmt::format("Blitzkrieg complete!  {} attempts, {}", p->totalAttempts, formatTime(p->totalTime));
                l2 = "Every stage cleared - go get that 0-100";
            } else {
                auto const& st = p->stages[si];
                l1 = fmt::format("Stage {}/{}   {}/{} ranges   {:.0f}% total",
                    si + 1, p->stages.size(), st.doneCount(), st.ranges.size(), progress * 100.f);

                int spawn = f->runStart >= 0 ? f->runStart : spawnIndex();
                auto tf = targetFor(*p, spawn);
                int need = store.rules().requiredPasses;
                if (tf && tf->first == si) {
                    auto const& r = p->stages[tf->first].ranges[tf->second];
                    targetFrom = p->pct(r.from);
                    targetTo = p->pct(r.to);
                    l2 = r.done
                        ? fmt::format("{} done  -  {} for next target", rangeText(*p, r), getKeyText("key-go-target"))
                        : fmt::format("Target {}   passes {}/{}   best {:.0f}%   att {}",
                            rangeText(*p, r), std::min(r.s.passes, need), need, r.s.bestProgress * 100.f, r.s.attempts);
                } else {
                    l2 = fmt::format("This spawn isn't a stage {} start - {} for next target", si + 1, getKeyText("key-go-target"));
                }
            }
        }

        f->line1->setString(l1.c_str());
        f->line2->setString(l2.c_str());

        f->bar->setVisible(showBar);
        if (showBar) {
            float w = 150.f;
            f->barProgress->setContentSize({w * progress, 2.f});
            f->barProgress->setPosition({0.f, 0.f});
            f->barTarget->setPosition({w * targetFrom / 100.f, 2.f});
            f->barTarget->setContentSize({std::max(1.f, w * (targetTo - targetFrom) / 100.f), 3.f});
            f->barTarget->setVisible(targetTo > targetFrom);
            f->barTicks->removeAllChildren();
            if (p && p->levelLength > 0.f) {
                for (size_t i = 1; i + 1 < p->xs.size(); ++i) {
                    auto tick = CCLayerColor::create({255, 255, 255, 150}, 0.6f, 5.f);
                    tick->setPosition({w * p->pct(static_cast<int>(i)) / 100.f, 0.f});
                    f->barTicks->addChild(tick);
                }
            }
        }
        layoutHud();
    }

    std::string getKeyText(char const* key) {
        auto binds = Mod::get()->getSettingValue<std::vector<Keybind>>(key);
        if (binds.empty()) return "set a key";
        return binds.front().toString();
    }
};

// ---------------------------------------------------------------- Game API

namespace bt::game {

static BTPlayLayer* pl() { return static_cast<BTPlayLayer*>(PlayLayer::get()); }

bool inLevel() { return PlayLayer::get() != nullptr; }
bool isPlatformer() { return pl() && pl()->m_level->isPlatformer(); }

std::vector<float> const& checkpoints() {
    if (!pl()) return kEmpty;
    pl()->attach();
    return pl()->m_fields->checkpoints;
}

float levelLength() { return pl() ? pl()->m_levelLength : 0.f; }
int startPosCount() { return pl() ? pl()->startPosCount() : 0; }
bool profileMismatch() { return pl() && pl()->m_fields->mismatch; }
void clearMismatch() { if (pl()) { pl()->m_fields->mismatch = false; pl()->updateHud(); } }
int currentSpawnIndex() { return pl() ? pl()->spawnIndex() : -1; }
bool switchTo(int idx, bool fromPause) { return pl() && pl()->switchSpawn(idx, fromPause); }
bool goToTarget(bool fromPause) { return pl() && pl()->goTarget(fromPause); }
void refreshHud() { if (pl()) pl()->updateHud(); }
void syncDisplay() { if (pl()) pl()->syncDisplay(); }
bool hasOrderedStartPos() { return pl() && pl()->m_fields->orderedStartPos; }
float shownPct(int idx) { return pl() ? pl()->shownPctAt(idx) : 0.f; }

}
