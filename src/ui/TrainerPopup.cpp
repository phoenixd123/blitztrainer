#include "TrainerPopup.hpp"

#include <algorithm>
#include <ctime>

#include "../Game.hpp"
#include "../Store.hpp"
#include "../core/Model.hpp"
#include "../core/Time.hpp"

using namespace geode::prelude;

namespace bt {

namespace {
    constexpr float kW = 440.f;
    constexpr float kH = 290.f;

    CCLabelBMFont* label(std::string const& text, char const* font, float scale, CCPoint anchor = {0.5f, 0.5f}) {
        auto l = CCLabelBMFont::create(text.c_str(), font);
        l->setScale(scale);
        l->setAnchorPoint(anchor);
        return l;
    }

    void limitWidth(CCLabelBMFont* l, float maxWidth) {
        float w = l->getContentWidth() * l->getScale();
        if (w > maxWidth && w > 0.f) l->setScale(l->getScale() * maxWidth / w);
    }

    SimpleTextArea* textBlock(std::string const& text, char const* font, float scale, float width) {
        auto t = SimpleTextArea::create(text, font, scale, width);
        t->setAlignment(kCCTextAlignmentCenter);
        t->setAnchorPoint({0.5f, 0.5f});
        return t;
    }

    std::string rangeText(Profile const& p, Range const& r) {
        return fmt::format("{} -> {}", formatPct(p.pct(r.from)), formatPct(p.pct(r.to)));
    }

    // Start position X values of the current level (checkpoints minus start and end).
    std::vector<float> levelStartPosXs() {
        auto const& cps = game::checkpoints();
        if (cps.size() < 3) return {};
        return std::vector<float>(cps.begin() + 1, cps.end() - 1);
    }

    std::string currentLevelName() {
        auto& s = Store::get();
        if (auto p = s.profile(); p && !p->levelName.empty()) return p->levelName;
        return s.levelName();
    }

    CCMenuItemSpriteExtra* textButton(char const* text, float scale, Function<void(CCMenuItemSpriteExtra*)> cb, char const* bg = "GJ_button_01.png") {
        auto spr = ButtonSprite::create(text, "goldFont.fnt", bg, scale);
        return CCMenuItemExt::createSpriteExtra(spr, std::move(cb));
    }

    // Makes an imported or restored profile the active one. When its start
    // positions differ from the level's, the stats of matching ranges move
    // into a freshly built profile. Returns the message to show.
    Result<std::string> adoptProfile(Profile incoming, bool keepCurrentForUndo) {
        auto const& cps = game::checkpoints();
        if (cps.size() < 2 || game::levelLength() <= 0.f) return Err("Open the level first");
        auto& store = Store::get();
        if (checkpointsMatch(incoming, cps)) {
            store.setProfile(std::move(incoming), keepCurrentForUndo);
            game::clearMismatch();
            return Ok(std::string());
        }
        auto fresh = makeProfile(store.key(), currentLevelName(), levelStartPosXs(), game::levelLength(),
            incoming.style, static_cast<int64_t>(std::time(nullptr)));
        int merged = mergeStats(fresh, incoming);
        store.setProfile(std::move(fresh), keepCurrentForUndo);
        game::clearMismatch();
        return Ok(fmt::format("{} ranges matched this level's start positions", merged));
    }
}

// ============================================================ TrainerPopup

TrainerPopup* TrainerPopup::create(bool fromPause) {
    auto ret = new TrainerPopup();
    if (ret->init(fromPause)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool TrainerPopup::init(bool fromPause) {
    if (!Popup::init(kW, kH)) return false;
    m_fromPause = fromPause;
    this->setTitle("Blitz Trainer");
    m_content = CCNode::create();
    m_content->setContentSize(m_size);
    m_mainLayer->addChild(m_content);
    rebuild();
    return true;
}


void TrainerPopup::rebuild() {
    m_content->removeAllChildren();
    game::syncDisplay();
    if (!game::inLevel()) {
        auto l = label("Open a level to use Blitz Trainer", "bigFont.fnt", 0.45f);
        l->setPosition(m_size / 2);
        m_content->addChild(l);
        return;
    }
    if (game::isPlatformer()) {
        auto l = label("Platformer levels aren't supported", "bigFont.fnt", 0.45f);
        l->setPosition(m_size / 2);
        m_content->addChild(l);
        return;
    }
    if (!Store::get().hasProfile()) buildNoProfile();
    else if (game::profileMismatch()) buildMismatch();
    else buildProfile();
    // menus created after the popup opened need the popup's touch priority
    handleTouchPriority(this);
}

void TrainerPopup::buildNoProfile() {
    int count = game::startPosCount();
    auto name = label(currentLevelName(), "goldFont.fnt", 0.6f);
    name->setPosition({m_size.width / 2, m_size.height - 60.f});
    limitWidth(name, m_size.width - 40.f);
    m_content->addChild(name);

    std::string info = count == 0
        ? "This level has no start positions.\nUse a startpos copy of the level (12-18 start positions works best)."
        : fmt::format(
            "{} start positions found  ->  {} sections.\n\n"
            "Classic: every window of 1, 2, 3 ... sections ({} ranges)\n"
            "Lite: windows of 1, 2, 4, 8 ... sections ({} ranges, faster)",
            count, count + 1,
            [&] { int t = 0; for (auto& s : generateStages(count + 1, StageStyle::Classic)) t += (int)s.ranges.size(); return t; }(),
            [&] { int t = 0; for (auto& s : generateStages(count + 1, StageStyle::Lite)) t += (int)s.ranges.size(); return t; }());
    auto text = textBlock(info, "bigFont.fnt", 0.38f, m_size.width - 50.f);
    text->setPosition({m_size.width / 2, m_size.height / 2 + 5.f});
    m_content->addChild(text);

    if (count == 0) return;
    if (game::hasOrderedStartPos()) {
        auto warn = label("This level has ordered/reverse start positions: sections follow X position", "chatFont.fnt", 0.55f);
        warn->setColor({255, 140, 90});
        warn->setPosition({m_size.width / 2, 88.f});
        limitWidth(warn, m_size.width - 30.f);
        m_content->addChild(warn);
    }
    if (count < 4) {
        auto warn = label("Tip: fewer than 4 start positions makes stages very long", "chatFont.fnt", 0.6f);
        warn->setColor({255, 200, 90});
        warn->setPosition({m_size.width / 2, 72.f});
        m_content->addChild(warn);
    }

    auto menu = CCMenu::create();
    menu->setContentSize({m_size.width - 40.f, 40.f});
    menu->setPosition({m_size.width / 2, 35.f});
    menu->addChild(textButton("Start (Classic)", 0.7f, [this](auto) { createProfile(false); }));
    menu->addChild(textButton("Start (Lite)", 0.7f, [this](auto) { createProfile(true); }, "GJ_button_02.png"));
    menu->addChild(textButton("Import", 0.7f, [this](auto) { onImport(); }, "GJ_button_04.png"));
    menu->setLayout(RowLayout::create()->setGap(8.f));
    m_content->addChild(menu);
}

void TrainerPopup::buildMismatch() {
    auto p = Store::get().profile();
    int now = std::max(0, static_cast<int>(game::checkpoints().size()) - 2);
    int before = p ? std::max(0, static_cast<int>(p->xs.size()) - 2) : 0;

    auto text = textBlock(
        fmt::format(
            "The start positions in this level changed\n({} saved, {} now).\n\n"
            "Rebuild keeps the stats of every range whose start and end didn't move.",
            before, now),
        "bigFont.fnt", 0.4f, m_size.width - 50.f);
    text->setPosition({m_size.width / 2, m_size.height / 2 + 20.f});
    m_content->addChild(text);

    auto menu = CCMenu::create();
    menu->setContentSize({m_size.width - 40.f, 40.f});
    menu->setPosition({m_size.width / 2, 40.f});
    menu->addChild(textButton("Rebuild", 0.75f, [this](auto) { rebuildKeepingProgress(); }));
    menu->addChild(textButton("Start over", 0.75f, [this](auto) {
        createQuickPopup("Start over?", "This deletes all progress for this level.", "Cancel", "Delete",
            [this](auto, bool yes) {
                if (!yes) return;
                Store::get().deleteProfile();
                game::clearMismatch();
                rebuild();
            });
    }, "GJ_button_06.png"));
    menu->setLayout(RowLayout::create()->setGap(10.f));
    m_content->addChild(menu);
}

void TrainerPopup::buildProfile() {
    auto& store = Store::get();
    auto& p = *store.profile();
    auto rules = store.rules();
    int cur = p.currentStage();
    if (m_viewStage < 0 || m_viewStage >= static_cast<int>(p.stages.size()))
        m_viewStage = cur >= 0 ? cur : static_cast<int>(p.stages.size()) - 1;

    // header
    auto name = label(p.levelName.empty() ? currentLevelName() : p.levelName, "goldFont.fnt", 0.5f);
    name->setPosition({m_size.width / 2, m_size.height - 42.f});
    limitWidth(name, m_size.width - 60.f);
    m_content->addChild(name);

    std::string summary = cur < 0
        ? fmt::format("COMPLETE  |  {} attempts  |  {}", p.totalAttempts, formatTime(p.totalTime))
        : fmt::format("Stage {}/{}  |  {:.0f}% ({}/{} ranges)  |  {} attempts  |  {}",
            cur + 1, p.stages.size(), p.overallProgress() * 100.f, p.doneRanges(), p.totalRanges(),
            p.totalAttempts, formatTime(p.totalTime));
    if (cur >= 0) {
        if (auto eta = estimateRemaining(p)) summary += fmt::format("  |  ~{} left", formatTime(*eta));
    }
    auto sum = label(summary, "bigFont.fnt", 0.32f);
    sum->setPosition({m_size.width / 2, m_size.height - 58.f});
    limitWidth(sum, m_size.width - 30.f);
    m_content->addChild(sum);

    std::string sessionText = fmt::format("Session: {} attempts, {} passes, {}",
        store.sessionAttempts, store.sessionPasses, formatTime(store.sessionTime));
    auto d = store.daily();
    if (d.goal > 0) {
        sessionText += fmt::format("   |   Today: {}/{}{}", d.passes, d.goal, d.passes >= d.goal ? " done" : "");
        if (d.streak > 1) sessionText += fmt::format(", {}-day streak", d.streak);
    }
    sessionText += fmt::format("   |   Rule: {} pass{}{}", rules.requiredPasses, rules.requiredPasses == 1 ? "" : "es",
        rules.consecutive ? " in a row" : "");
    auto session = label(sessionText, "chatFont.fnt", 0.6f);
    session->setColor({190, 220, 255});
    session->setPosition({m_size.width / 2, m_size.height - 71.f});
    limitWidth(session, m_size.width - 30.f);
    m_content->addChild(session);

    // overall progress bar
    float barW = m_size.width - 40.f;
    auto barBg = CCLayerColor::create({0, 0, 0, 110}, barW, 4.f);
    barBg->setPosition({20.f, m_size.height - 82.f});
    auto barFill = CCLayerColor::create({60, 220, 110, 255}, barW * p.overallProgress(), 4.f);
    barBg->addChild(barFill);
    m_content->addChild(barBg);

    // stage navigator
    auto nav = CCMenu::create();
    nav->setPosition({0.f, 0.f});
    nav->setContentSize(m_size);
    auto prev = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_arrow_01_001.png", 0.5f, [this](auto) { onPrevStage(); });
    prev->setPosition({30.f, m_size.height - 100.f});
    auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    nextSpr->setFlipX(true);
    nextSpr->setScale(0.5f);
    auto next = CCMenuItemExt::createSpriteExtra(nextSpr, [this](auto) { onNextStage(); });
    next->setPosition({m_size.width - 30.f, m_size.height - 100.f});
    nav->addChild(prev);
    nav->addChild(next);
    m_content->addChild(nav);

    auto const& st = p.stages[m_viewStage];
    std::string stageTitle = fmt::format("Stage {}  -  {}/{} done{}", m_viewStage + 1, st.doneCount(), st.ranges.size(),
        m_viewStage == cur ? "  (current)" : (m_viewStage > cur && cur >= 0 ? "  (locked)" : ""));
    auto stageLabel = label(stageTitle, "goldFont.fnt", 0.5f);
    stageLabel->setPosition({m_size.width / 2, m_size.height - 100.f});
    m_content->addChild(stageLabel);

    buildRangeList();

    // bottom actions
    auto menu = CCMenu::create();
    menu->setContentSize({m_size.width - 20.f, 30.f});
    menu->setPosition({m_size.width / 2, 22.f});
    if (cur >= 0) menu->addChild(textButton("Go to target", 0.55f, [this](auto) { onGoTarget(); }));
    menu->addChild(textButton("Heatmap", 0.55f, [](auto) { HeatmapPopup::create()->show(); }, "GJ_button_02.png"));
    menu->addChild(textButton("History", 0.55f, [](auto) { HistoryPopup::create()->show(); }, "GJ_button_02.png"));
    if (store.canUndo()) menu->addChild(textButton("Undo", 0.55f, [this](auto) { onUndo(); }, "GJ_button_05.png"));
    menu->addChild(textButton("Manage", 0.55f, [this](auto) {
        Ref<TrainerPopup> self = this;
        ManagePopup::create([self] { self->m_viewStage = -1; self->rebuild(); })->show();
    }, "GJ_button_04.png"));
    menu->setLayout(RowLayout::create()->setGap(5.f));
    m_content->addChild(menu);
}

void TrainerPopup::buildRangeList() {
    auto& store = Store::get();
    auto& p = *store.profile();
    auto rules = store.rules();
    int cur = p.currentStage();
    auto const& st = p.stages[m_viewStage];
    bool locked = cur >= 0 && m_viewStage > cur;
    int spawn = game::currentSpawnIndex();

    CCSize listSize = {m_size.width - 30.f, 140.f};
    auto bg = CCLayerColor::create({0, 0, 0, 70}, listSize.width, listSize.height);
    bg->setPosition({15.f, 42.f});
    m_content->addChild(bg);

    auto scroll = ScrollLayer::create(listSize);
    scroll->setPosition({15.f, 42.f});
    m_content->addChild(scroll);

    constexpr float rowH = 26.f;
    float totalH = std::max(listSize.height, rowH * st.ranges.size());
    scroll->m_contentLayer->setContentSize({listSize.width, totalH});

    for (size_t i = 0; i < st.ranges.size(); ++i) {
        auto const& r = st.ranges[i];
        float y = totalH - rowH * (i + 1);

        ccColor4B tint = r.done ? ccColor4B{40, 160, 80, 90} : (i % 2 ? ccColor4B{255, 255, 255, 12} : ccColor4B{255, 255, 255, 25});
        if (!r.done && r.from == spawn && !locked) tint = {255, 200, 60, 70};
        auto row = CCLayerColor::create(tint, listSize.width, rowH - 2.f);
        row->setPosition({0.f, y + 1.f});
        scroll->m_contentLayer->addChild(row);

        auto icon = CCSprite::createWithSpriteFrameName(r.done ? "GJ_completesIcon_001.png" : (locked ? "GJ_lock_001.png" : "GJ_practiceBtn_001.png"));
        if (icon) {
            icon->setScale(r.done ? 0.45f : 0.35f);
            icon->setPosition({14.f, rowH / 2 - 1.f});
            row->addChild(icon);
        }

        auto name = label(rangeText(p, r), "bigFont.fnt", 0.38f, {0.f, 0.5f});
        name->setPosition({28.f, rowH / 2});
        limitWidth(name, 120.f);
        row->addChild(name);

        int need = rules.requiredPasses;
        std::string stats = fmt::format("{}/{} pass   streak {}   {} att   best {:.0f}%   {}",
            std::min(r.s.passes, need), need, r.s.streak, r.s.attempts, r.s.bestProgress * 100.f, formatTime(r.s.timePlayed));
        if (r.s.attemptsToFirstPass > 0) stats += fmt::format("   1st in {}", r.s.attemptsToFirstPass);
        auto statLabel = label(stats, "chatFont.fnt", 0.55f, {0.f, 0.5f});
        statLabel->setPosition({155.f, rowH / 2});
        limitWidth(statLabel, listSize.width - 155.f - 50.f);
        row->addChild(statLabel);

        // mini progress line under the row
        auto line = CCLayerColor::create(r.done ? ccColor4B{80, 255, 120, 200} : ccColor4B{255, 210, 60, 200}, (listSize.width - 4.f) * r.s.bestProgress, 1.5f);
        line->setPosition({2.f, 0.f});
        row->addChild(line);

        if (!locked) {
            auto goMenu = CCMenu::create();
            goMenu->setPosition({listSize.width - 24.f, rowH / 2});
            auto go = textButton("Go", 0.45f, [this, from = r.from](auto) { onGoRange(from); }, "GJ_button_01.png");
            goMenu->addChild(go);
            go->setPosition({0.f, 0.f});
            row->addChild(goMenu);
        }
    }
    scroll->scrollToTop();
}

void TrainerPopup::onPrevStage() {
    auto p = Store::get().profile();
    if (!p || p->stages.empty()) return;
    m_viewStage = (m_viewStage - 1 + static_cast<int>(p->stages.size())) % static_cast<int>(p->stages.size());
    rebuild();
}

void TrainerPopup::onNextStage() {
    auto p = Store::get().profile();
    if (!p || p->stages.empty()) return;
    m_viewStage = (m_viewStage + 1) % static_cast<int>(p->stages.size());
    rebuild();
}

void TrainerPopup::onGoRange(int from) {
    if (!game::switchTo(from, m_fromPause)) {
        Notification::create("That start position isn't available", NotificationIcon::Error)->show();
        return;
    }
    this->onClose(nullptr);
}

void TrainerPopup::onGoTarget() {
    if (!game::goToTarget(m_fromPause)) {
        Notification::create("No unfinished range to go to", NotificationIcon::Info)->show();
        return;
    }
    this->onClose(nullptr);
}

void TrainerPopup::onUndo() {
    if (Store::get().undo()) {
        Notification::create("Undone", NotificationIcon::Success)->show();
        game::recheckProfile();
        game::refreshHud();
    }
    m_viewStage = -1;
    rebuild();
}

void TrainerPopup::onImport() {
    auto res = Store::get().importText(clipboard::read());
    if (!res) {
        Notification::create(res.unwrapErr(), NotificationIcon::Error)->show();
        return;
    }
    auto adopted = adoptProfile(std::move(res.unwrap()), false);
    if (!adopted) {
        Notification::create(adopted.unwrapErr(), NotificationIcon::Error)->show();
        return;
    }
    auto msg = adopted.unwrap();
    Notification::create(msg.empty() ? std::string("Profile imported") : "Imported: " + msg, NotificationIcon::Success)->show();
    game::refreshHud();
    m_viewStage = -1;
    rebuild();
}

void TrainerPopup::createProfile(bool lite) {
    auto xs = levelStartPosXs();
    if (xs.empty() || game::levelLength() <= 0.f) return;
    auto p = makeProfile(Store::get().key(), currentLevelName(), xs, game::levelLength(),
        lite ? StageStyle::Lite : StageStyle::Classic, static_cast<int64_t>(std::time(nullptr)));
    Store::get().setProfile(std::move(p));
    game::clearMismatch();
    game::refreshHud();
    m_viewStage = -1;
    rebuild();
}

void TrainerPopup::rebuildKeepingProgress() {
    auto old = Store::get().profile();
    if (!old) return;
    if (game::levelLength() <= 0.f) return;
    auto fresh = makeProfile(Store::get().key(), old->levelName, levelStartPosXs(), game::levelLength(), old->style,
        static_cast<int64_t>(std::time(nullptr)));
    int merged = mergeStats(fresh, *old);
    Store::get().setProfile(std::move(fresh));
    game::clearMismatch();
    Notification::create(fmt::format("Rebuilt, kept progress on {} ranges", merged), NotificationIcon::Success)->show();
    m_viewStage = -1;
    rebuild();
}

// ============================================================ ManagePopup

ManagePopup* ManagePopup::create(std::function<void()> onChange) {
    auto ret = new ManagePopup();
    if (ret->init(std::move(onChange))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ManagePopup::init(std::function<void()> onChange) {
    if (!Popup::init(360.f, 260.f)) return false;
    m_onChange = std::move(onChange);
    this->setTitle("Manage Progress");
    m_content = CCNode::create();
    m_content->setContentSize(m_size);
    m_mainLayer->addChild(m_content);
    build();
    return true;
}

void ManagePopup::changed() {
    game::refreshHud();
    if (m_onChange) m_onChange();
}

void ManagePopup::build() {
    m_content->removeAllChildren();
    auto& store = Store::get();

    auto top = CCMenu::create();
    top->setContentSize({m_size.width - 30.f, 30.f});
    top->setPosition({m_size.width / 2, m_size.height - 52.f});
    top->addChild(textButton("Export", 0.6f, [](auto) {
        auto text = Store::get().exportText();
        if (text.empty()) return;
        if (clipboard::write(text)) Notification::create("Profile copied to clipboard", NotificationIcon::Success)->show();
        else Notification::create("Couldn't access the clipboard", NotificationIcon::Error)->show();
    }, "GJ_button_04.png"));
    top->addChild(textButton("Import", 0.6f, [this](auto) {
        auto res = Store::get().importText(clipboard::read());
        if (!res) {
            Notification::create(res.unwrapErr(), NotificationIcon::Error)->show();
            return;
        }
        auto adopted = adoptProfile(std::move(res.unwrap()), true);
        if (!adopted) {
            Notification::create(adopted.unwrapErr(), NotificationIcon::Error)->show();
            return;
        }
        auto msg = adopted.unwrap();
        Notification::create(msg.empty() ? std::string("Profile imported") : "Imported: " + msg, NotificationIcon::Success)->show();
        changed();
        build();
    }, "GJ_button_04.png"));
    top->addChild(textButton("Reset", 0.6f, [this](auto) {
        Ref<ManagePopup> self = this;
        createQuickPopup("Reset progress?",
            "This deletes all Blitz Trainer progress for this level. Daily backups are kept.",
            "Cancel", "Reset",
            [self](auto, bool yes) {
                if (!yes) return;
                Store::get().deleteProfile();
                self->changed();
                self->onClose(nullptr);
            });
    }, "GJ_button_06.png"));
    top->setLayout(RowLayout::create()->setGap(8.f));
    m_content->addChild(top);

    auto heading = label("Daily backups", "goldFont.fnt", 0.5f);
    heading->setPosition({m_size.width / 2, m_size.height - 85.f});
    m_content->addChild(heading);

    auto days = store.backups();
    CCSize listSize = {m_size.width - 40.f, 130.f};
    auto bg = CCLayerColor::create({0, 0, 0, 70}, listSize.width, listSize.height);
    bg->setPosition({20.f, 20.f});
    m_content->addChild(bg);

    if (days.empty()) {
        auto empty = textBlock(Mod::get()->getSettingValue<bool>("auto-backups")
                ? "No backups yet. One is saved the first time you open this level each day."
                : "Daily backups are off. Turn them on in the mod's settings.",
            "bigFont.fnt", 0.35f, listSize.width - 30.f);
        empty->setPosition({listSize.width / 2, listSize.height / 2});
        bg->addChild(empty);
        handleTouchPriority(this);
        return;
    }

    auto scroll = ScrollLayer::create(listSize);
    scroll->setPosition({20.f, 20.f});
    m_content->addChild(scroll);
    constexpr float rowH = 28.f;
    float totalH = std::max(listSize.height, rowH * days.size());
    scroll->m_contentLayer->setContentSize({listSize.width, totalH});

    for (size_t i = 0; i < days.size(); ++i) {
        auto const& day = days[i];
        float y = totalH - rowH * (i + 1);
        auto row = CCLayerColor::create(i % 2 ? ccColor4B{255, 255, 255, 12} : ccColor4B{255, 255, 255, 25}, listSize.width, rowH - 2.f);
        row->setPosition({0.f, y + 1.f});
        scroll->m_contentLayer->addChild(row);

        std::string info = day;
        if (auto b = store.loadBackup(day)) {
            auto const& bp = b.unwrap();
            int cs = bp.currentStage();
            info += cs < 0
                ? std::string("   complete")
                : fmt::format("   stage {}/{}, {:.0f}%", cs + 1, bp.stages.size(), bp.overallProgress() * 100.f);
        } else {
            info += "   (unreadable)";
        }
        auto text = label(info, "bigFont.fnt", 0.36f, {0.f, 0.5f});
        text->setPosition({10.f, rowH / 2});
        limitWidth(text, listSize.width - 90.f);
        row->addChild(text);

        auto menu = CCMenu::create();
        menu->setPosition({listSize.width - 38.f, rowH / 2});
        auto btn = textButton("Restore", 0.45f, [this, day](auto) {
            auto loaded = Store::get().loadBackup(day);
            if (!loaded) {
                Notification::create(loaded.unwrapErr(), NotificationIcon::Error)->show();
                return;
            }
            auto adopted = adoptProfile(std::move(loaded.unwrap()), true);
            if (!adopted) {
                Notification::create(adopted.unwrapErr(), NotificationIcon::Error)->show();
                return;
            }
            Notification::create(fmt::format("Restored {}. Undo brings back what you had.", day), NotificationIcon::Success)->show();
            changed();
            this->onClose(nullptr);
        });
        btn->setPosition({0.f, 0.f});
        menu->addChild(btn);
        row->addChild(menu);
    }
    scroll->scrollToTop();
    handleTouchPriority(this);
}

// ============================================================ HistoryPopup

HistoryPopup* HistoryPopup::create() {
    auto ret = new HistoryPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool HistoryPopup::init() {
    if (!Popup::init(400.f, 260.f)) return false;
    this->setTitle("History");
    auto p = Store::get().profile();
    if (!p) return true;
    int cur = p->currentStage();

    std::string head = fmt::format("Started {}  |  {} attempts  |  {} played", shortDate(p->createdAt), p->totalAttempts, formatTime(p->totalTime));
    if (cur < 0) head += fmt::format("  |  finished {}", shortDate(p->completedAt));
    else if (auto eta = estimateRemaining(*p)) head += fmt::format("  |  ~{} left", formatTime(*eta));
    auto headLabel = label(head, "bigFont.fnt", 0.3f);
    headLabel->setPosition({m_size.width / 2, m_size.height - 45.f});
    limitWidth(headLabel, m_size.width - 30.f);
    m_mainLayer->addChild(headLabel);

    CCSize listSize = {m_size.width - 30.f, m_size.height - 80.f};
    auto bg = CCLayerColor::create({0, 0, 0, 70}, listSize.width, listSize.height);
    bg->setPosition({15.f, 18.f});
    m_mainLayer->addChild(bg);

    auto scroll = ScrollLayer::create(listSize);
    scroll->setPosition({15.f, 18.f});
    m_mainLayer->addChild(scroll);
    constexpr float rowH = 24.f;
    float totalH = std::max(listSize.height, rowH * p->stages.size());
    scroll->m_contentLayer->setContentSize({listSize.width, totalH});

    for (size_t i = 0; i < p->stages.size(); ++i) {
        auto const& st = p->stages[i];
        int attempts = 0;
        double time = 0.0;
        for (auto const& r : st.ranges) {
            attempts += r.s.attempts;
            time += r.s.timePlayed;
        }
        bool done = st.done();
        bool current = static_cast<int>(i) == cur;
        float y = totalH - rowH * (i + 1);
        ccColor4B tint = done ? ccColor4B{40, 160, 80, 80} : (current ? ccColor4B{255, 200, 60, 60} : ccColor4B{255, 255, 255, 12});
        auto row = CCLayerColor::create(tint, listSize.width, rowH - 2.f);
        row->setPosition({0.f, y + 1.f});
        scroll->m_contentLayer->addChild(row);

        auto name = label(fmt::format("Stage {}", i + 1), "goldFont.fnt", 0.45f, {0.f, 0.5f});
        name->setPosition({8.f, rowH / 2});
        row->addChild(name);

        std::string status = done
            ? (st.completedAt > 0 ? fmt::format("cleared {}", shortDate(st.completedAt)) : std::string("cleared"))
            : (current ? fmt::format("in progress {}/{}", st.doneCount(), st.ranges.size()) : std::string("locked"));
        std::string stats = fmt::format("{}   {} ranges   {} attempts   {}", status, st.ranges.size(), attempts, formatTime(time));
        auto statLabel = label(stats, "chatFont.fnt", 0.6f, {0.f, 0.5f});
        statLabel->setPosition({80.f, rowH / 2});
        limitWidth(statLabel, listSize.width - 90.f);
        row->addChild(statLabel);
    }
    scroll->scrollToTop();
    return true;
}

// ============================================================ HeatmapPopup

HeatmapPopup* HeatmapPopup::create() {
    auto ret = new HeatmapPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool HeatmapPopup::init() {
    if (!Popup::init(420.f, 250.f)) return false;
    this->setTitle("Death Heatmap");
    auto p = Store::get().profile();
    if (!p) return true;

    float chartW = m_size.width - 40.f, chartH = 90.f;
    CCPoint origin = {20.f, m_size.height - 50.f - chartH};
    auto bg = CCLayerColor::create({0, 0, 0, 90}, chartW, chartH);
    bg->setPosition(origin);
    m_mainLayer->addChild(bg);

    int maxDeaths = *std::max_element(p->deaths.begin(), p->deaths.end());
    int total = 0;
    for (int d : p->deaths) total += d;

    if (maxDeaths == 0) {
        auto l = label("No deaths recorded yet", "bigFont.fnt", 0.4f);
        l->setPosition({chartW / 2, chartH / 2});
        bg->addChild(l);
    } else {
        float colW = chartW / 100.f;
        for (int i = 0; i < 100; ++i) {
            if (!p->deaths[i]) continue;
            float t = static_cast<float>(p->deaths[i]) / maxDeaths;
            auto col = CCLayerColor::create(
                {static_cast<GLubyte>(120 + 135 * t), static_cast<GLubyte>(220 - 170 * t), 60, 230},
                std::max(1.f, colW - 0.6f), std::max(1.f, chartH * t));
            col->setPosition({colW * i, 0.f});
            bg->addChild(col);
        }
    }
    // start position ticks
    for (size_t i = 1; i + 1 < p->xs.size(); ++i) {
        auto tick = CCLayerColor::create({255, 255, 255, 70}, 0.7f, chartH);
        tick->setPosition({chartW * p->pct(static_cast<int>(i)) / 100.f, 0.f});
        bg->addChild(tick);
    }
    for (int pct : {0, 25, 50, 75, 100}) {
        auto l = label(fmt::format("{}%", pct), "chatFont.fnt", 0.5f, {0.5f, 1.f});
        l->setPosition({origin.x + chartW * pct / 100.f, origin.y - 2.f});
        m_mainLayer->addChild(l);
    }

    // top death spots
    std::vector<int> idx(100);
    for (int i = 0; i < 100; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return p->deaths[a] > p->deaths[b]; });
    std::string spots = "Most deaths at: ";
    int shown = 0;
    for (int i : idx) {
        if (!p->deaths[i] || shown == 4) break;
        spots += fmt::format("{}{}% ({}, {:.0f}%)", shown ? "   " : "", i, p->deaths[i], 100.f * p->deaths[i] / std::max(1, total));
        ++shown;
    }
    if (!shown) spots += "-";
    auto spotsLabel = label(spots, "chatFont.fnt", 0.65f);
    spotsLabel->setPosition({m_size.width / 2, origin.y - 22.f});
    limitWidth(spotsLabel, m_size.width - 30.f);
    m_mainLayer->addChild(spotsLabel);

    // hardest ranges by pass rate (with enough data to mean something)
    struct Hard { Range const* r; float rate; };
    std::vector<Hard> hard;
    for (auto const& st : p->stages)
        for (auto const& r : st.ranges)
            if (r.s.attempts >= 5) hard.push_back({&r, static_cast<float>(r.s.passes) / r.s.attempts});
    std::sort(hard.begin(), hard.end(), [](Hard const& a, Hard const& b) { return a.rate < b.rate; });

    std::string lines = "Hardest ranges (pass rate, 5+ attempts):\n";
    if (hard.empty()) lines += "Not enough attempts yet";
    for (size_t i = 0; i < hard.size() && i < 4; ++i) {
        lines += fmt::format("{} -> {}:  {:.0f}%  ({}/{})\n",
            formatPct(p->pct(hard[i].r->from)), formatPct(p->pct(hard[i].r->to)), hard[i].rate * 100.f,
            hard[i].r->s.passes, hard[i].r->s.attempts);
    }
    auto area = textBlock(lines, "chatFont.fnt", 0.7f, m_size.width - 40.f);
    area->setPosition({m_size.width / 2, origin.y - 34.f});
    area->setAnchorPoint({0.5f, 1.f});
    m_mainLayer->addChild(area);
    return true;
}

}
