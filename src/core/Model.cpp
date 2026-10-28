#include "Model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace bt {

namespace {
    constexpr float kEps = 0.5f;

    Range mkRange(int a, int b) { Range r; r.from = a; r.to = b; return r; }

    float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    void addWindows(std::vector<Range>& out, std::set<std::pair<int, int>>& seen, int n, int size, int stride) {
        if (size >= n) {
            if (seen.insert({0, n}).second) out.push_back(mkRange(0, n));
            return;
        }
        for (int i = 0; i + size <= n; i += stride) {
            if (seen.insert({i, i + size}).second) out.push_back(mkRange(i, i + size));
        }
        // make sure the tail of the level is always covered by this stage
        if (seen.insert({n - size, n}).second) out.push_back(mkRange(n - size, n));
    }
}

bool Stage::done() const {
    if (ranges.empty()) return true;
    for (auto const& r : ranges) if (!r.done) return false;
    return true;
}

int Stage::doneCount() const {
    int c = 0;
    for (auto const& r : ranges) if (r.done) ++c;
    return c;
}

float Profile::pct(int idx) const {
    if (xs.empty() || levelLength <= 0.f) return 0.f;
    idx = std::clamp(idx, 0, static_cast<int>(xs.size()) - 1);
    if (shownPct.size() == xs.size()) return std::clamp(shownPct[idx], 0.f, 100.f);
    return std::clamp(xs[idx] / levelLength * 100.f, 0.f, 100.f);
}

float Profile::pctOfX(float x) const {
    if (levelLength <= 0.f) return 0.f;
    if (shownPct.size() == xs.size() && xs.size() >= 2) {
        // piecewise-linear between checkpoints: exact at every start position
        if (x <= xs.front()) return std::clamp(shownPct.front(), 0.f, 100.f);
        for (size_t i = 0; i + 1 < xs.size(); ++i) {
            if (x <= xs[i + 1]) {
                float span = xs[i + 1] - xs[i];
                float t = span > 0.f ? (x - xs[i]) / span : 0.f;
                return std::clamp(shownPct[i] + (shownPct[i + 1] - shownPct[i]) * t, 0.f, 100.f);
            }
        }
        return 100.f;
    }
    return std::clamp(x / levelLength * 100.f, 0.f, 100.f);
}

int Profile::currentStage() const {
    for (size_t i = 0; i < stages.size(); ++i)
        if (!stages[i].done()) return static_cast<int>(i);
    return -1;
}

int Profile::totalRanges() const {
    int c = 0;
    for (auto const& s : stages) c += static_cast<int>(s.ranges.size());
    return c;
}

int Profile::doneRanges() const {
    int c = 0;
    for (auto const& s : stages) c += s.doneCount();
    return c;
}

float Profile::overallProgress() const {
    int total = totalRanges();
    return total ? static_cast<float>(doneRanges()) / static_cast<float>(total) : 0.f;
}

std::vector<float> buildCheckpoints(std::vector<float> sps, float levelLength) {
    std::vector<float> xs;
    xs.push_back(0.f);
    std::sort(sps.begin(), sps.end());
    for (float x : sps) {
        if (!std::isfinite(x)) continue;
        if (x <= 1.f || x >= levelLength - 1.f) continue; // at the very start/end: useless
        if (std::fabs(x - xs.back()) < 1.f) continue;      // stacked start positions
        xs.push_back(x);
    }
    xs.push_back(levelLength);
    return xs;
}

std::vector<Stage> generateStages(int n, StageStyle style) {
    std::vector<Stage> stages;
    if (n <= 0) return stages;
    std::set<std::pair<int, int>> seen;

    std::vector<int> sizes;
    if (style == StageStyle::Lite) {
        for (int s = 1; s < n; s *= 2) sizes.push_back(s);
        sizes.push_back(n);
    } else {
        for (int s = 1; s <= n; ++s) sizes.push_back(s);
    }

    for (int size : sizes) {
        Stage st;
        int stride = (style == StageStyle::Lite && size > 1) ? std::max(1, size / 2) : 1;
        addWindows(st.ranges, seen, n, size, stride);
        std::sort(st.ranges.begin(), st.ranges.end(), [](Range const& a, Range const& b) {
            return a.from != b.from ? a.from < b.from : a.to < b.to;
        });
        if (!st.ranges.empty()) stages.push_back(std::move(st));
    }
    return stages;
}

Profile makeProfile(
    std::string levelKey, std::string levelName, std::vector<float> startPosXs,
    float levelLength, StageStyle style, int64_t now
) {
    Profile p;
    p.levelKey = std::move(levelKey);
    p.levelName = std::move(levelName);
    p.levelLength = levelLength;
    p.xs = buildCheckpoints(std::move(startPosXs), levelLength);
    p.style = style;
    p.stages = generateStages(p.sections(), style);
    p.createdAt = now;
    return p;
}

bool checkpointsMatch(Profile const& p, std::vector<float> const& xs, float tolerance) {
    if (p.xs.size() != xs.size()) return false;
    for (size_t i = 0; i < xs.size(); ++i)
        if (std::fabs(p.xs[i] - xs[i]) > tolerance) return false;
    return true;
}

int reachedIndex(Profile const& p, float endX, bool completed) {
    int n = p.sections();
    if (completed) return n;
    // you can only "reach" the end checkpoint by completing the level
    int r = 0;
    for (int i = 0; i < n; ++i) {
        if (p.xs[i] <= endX + kEps) r = i;
        else break;
    }
    return r;
}

int matchStartIndex(Profile const& p, float startX, float tolerance) {
    int best = -1;
    float bestDist = tolerance;
    for (int i = 0; i < p.sections(); ++i) {
        float d = std::fabs(p.xs[i] - startX);
        if (d <= bestDist) { bestDist = d; best = i; }
    }
    return best;
}

bool isRangeDone(RangeStats const& s, TrainingRules const& rules) {
    int need = std::max(1, rules.requiredPasses);
    return rules.consecutive ? s.bestStreak >= need : s.passes >= need;
}

namespace {
    void creditPass(Range& r, int64_t now) {
        r.s.passes += 1;
        r.s.streak += 1;
        r.s.bestStreak = std::max(r.s.bestStreak, r.s.streak);
        r.s.bestProgress = 1.f;
        if (r.s.firstPassAt == 0) {
            r.s.firstPassAt = now;
            r.s.attemptsToFirstPass = std::max(1, r.s.attempts);
        }
        r.s.lastPassAt = now;
    }
}

RunResult applyRun(Profile& p, RunInput const& run, TrainingRules const& rules, int64_t now) {
    RunResult res;
    int n = p.sections();

    p.totalAttempts += 1;
    p.totalTime += run.time;
    if (!run.completed && p.levelLength > 0.f) {
        int bucket = static_cast<int>(p.pctOfX(run.endX));
        p.deaths[std::clamp(bucket, 0, 99)] += 1;
    }

    if (n <= 0 || run.startIdx < 0 || run.startIdx >= n || p.stages.empty()) return res;

    bool wasComplete = p.complete();
    int si = p.currentStage();
    if (si < 0) si = static_cast<int>(p.stages.size()) - 1; // everything done: keep stats on the final stage
    Stage& stage = p.stages[si];
    bool stageWasDone = stage.done();

    int s = run.startIdx;
    // dying right at (or a hair behind) the spawn still counts as an attempt there
    int r = std::max(s, reachedIndex(p, run.endX, run.completed));

    auto passed = [&](Range const& rg) { return rg.from >= s && rg.to <= r; };
    auto touched = [&](Range const& rg) { return rg.from >= s && rg.from <= r; };
    auto coverage = [&](Range const& rg) {
        float a = p.xs[rg.from], b = p.xs[rg.to];
        float end = run.completed ? b : run.endX;
        return b > a ? clamp01((end - a) / (b - a)) : 0.f;
    };

    // Pick exactly one target range for this attempt's stats.
    int target = -1;
    auto pickBest = [&](bool wantDone, bool wantPassed) {
        int best = -1;
        for (int i = 0; i < static_cast<int>(stage.ranges.size()); ++i) {
            auto const& rg = stage.ranges[i];
            if (rg.done != wantDone) continue;
            if (wantPassed ? !passed(rg) : !touched(rg)) continue;
            if (best < 0) { best = i; continue; }
            auto const& b = stage.ranges[best];
            if (wantPassed) {
                if (rg.from < b.from || (rg.from == b.from && rg.to < b.to)) best = i;
            } else {
                // prefer the range you deliberately started at, then the one you got furthest through
                bool rgAtStart = rg.from == s, bAtStart = b.from == s;
                if (rgAtStart != bAtStart) { if (rgAtStart) best = i; continue; }
                if (coverage(rg) > coverage(b)) best = i;
            }
        }
        return best;
    };

    target = pickBest(false, true);
    if (target < 0) target = pickBest(false, false);
    if (target < 0) target = pickBest(true, true);
    if (target < 0) target = pickBest(true, false);
    if (target < 0) return res;

    p.trackedAttempts += 1;
    res.counted = true;
    res.stage = si;
    res.target = target;

    Range& t = stage.ranges[target];
    bool tWasDone = t.done;
    t.s.attempts += 1;
    t.s.timePlayed += run.time;
    res.progress = passed(t) ? 1.f : coverage(t);
    t.s.bestProgress = std::max(t.s.bestProgress, res.progress);

    if (passed(t)) {
        creditPass(t, now);
        res.targetPassed = true;
    } else {
        t.s.streak = 0;
    }
    t.done = isRangeDone(t.s, rules);
    if (t.done && !tWasDone) res.newlyDone.push_back(target);

    if (rules.creditAllPassed) {
        for (int i = 0; i < static_cast<int>(stage.ranges.size()); ++i) {
            if (i == target) continue;
            auto& rg = stage.ranges[i];
            if (rg.done || !passed(rg)) continue;
            creditPass(rg, now);
            rg.done = isRangeDone(rg.s, rules);
            if (rg.done) res.newlyDone.push_back(i);
        }
    }

    res.stageCompleted = !stageWasDone && stage.done();
    if (res.stageCompleted) stage.completedAt = now;
    if (!wasComplete && p.complete()) {
        res.profileCompleted = true;
        p.completedAt = now;
    }
    return res;
}

void refreshDone(Profile& p, TrainingRules const& rules) {
    for (auto& st : p.stages) {
        for (auto& rg : st.ranges)
            rg.done = isRangeDone(rg.s, rules);
        if (!st.done()) st.completedAt = 0; // reopened by stricter rules
    }
    if (!p.complete()) p.completedAt = 0;
}

int mergeStats(Profile& fresh, Profile const& old, float tolerance) {
    int merged = 0;
    for (auto& st : fresh.stages) {
        for (auto& rg : st.ranges) {
            float a = fresh.xs[rg.from], b = fresh.xs[rg.to];
            for (auto const& ost : old.stages) {
                bool found = false;
                for (auto const& org : ost.ranges) {
                    if (org.from < 0 || org.to >= static_cast<int>(old.xs.size())) continue;
                    if (std::fabs(old.xs[org.from] - a) <= tolerance && std::fabs(old.xs[org.to] - b) <= tolerance) {
                        rg.s = org.s;
                        rg.done = org.done;
                        found = true;
                        ++merged;
                        break;
                    }
                }
                if (found) break;
            }
        }
    }
    fresh.deaths = old.deaths;
    fresh.totalAttempts = old.totalAttempts;
    fresh.trackedAttempts = old.trackedAttempts;
    fresh.totalTime = old.totalTime;
    fresh.createdAt = old.createdAt;
    return merged;
}

float passRate(RangeStats const& s) {
    return (static_cast<float>(s.passes) + 1.f) / (static_cast<float>(s.attempts) + 2.f);
}

std::optional<int> nextTargetStart(Profile const& p, int afterFrom, RouteOrder order, uint32_t seed) {
    int si = p.currentStage();
    if (si < 0) return std::nullopt;
    auto const& rs = p.stages[si].ranges;

    if (order == RouteOrder::InOrder) {
        // ranges are sorted by `from`; first unfinished one after afterFrom, else wrap
        for (auto const& rg : rs) if (!rg.done && rg.from > afterFrom) return rg.from;
        for (auto const& rg : rs) if (!rg.done) return rg.from;
        return std::nullopt;
    }

    std::vector<Range const*> open;
    for (auto const& rg : rs) if (!rg.done) open.push_back(&rg);
    if (open.empty()) return std::nullopt;
    if (open.size() > 1) {
        open.erase(std::remove_if(open.begin(), open.end(), [&](Range const* r) { return r->from == afterFrom; }), open.end());
    }

    if (order == RouteOrder::Random) return open[seed % open.size()]->from;

    // Weakest: lowest pass rate, then the one you've got least far through, then leftmost
    auto weakest = *std::min_element(open.begin(), open.end(), [](Range const* a, Range const* b) {
        float ra = passRate(a->s), rb = passRate(b->s);
        if (std::fabs(ra - rb) > 1e-6f) return ra < rb;
        if (std::fabs(a->s.bestProgress - b->s.bestProgress) > 1e-6f) return a->s.bestProgress < b->s.bestProgress;
        return a->from < b->from;
    });
    return weakest->from;
}

std::optional<double> estimateRemaining(Profile const& p) {
    int doneCount = 0;
    double doneSections = 0.0, doneTime = 0.0, openSections = 0.0;
    for (auto const& st : p.stages) {
        for (auto const& rg : st.ranges) {
            double len = static_cast<double>(rg.to - rg.from);
            if (rg.done) {
                ++doneCount;
                doneSections += len;
                doneTime += rg.s.timePlayed;
            } else {
                openSections += len;
            }
        }
    }
    if (doneCount < 3 || doneSections <= 0.0 || doneTime <= 0.0) return std::nullopt;
    return doneTime / doneSections * openSections;
}

std::optional<std::pair<int, int>> targetFor(Profile const& p, int startIdx) {
    if (startIdx < 0 || p.stages.empty()) return std::nullopt;
    int si = p.currentStage();
    if (si < 0) si = static_cast<int>(p.stages.size()) - 1;
    auto const& rs = p.stages[si].ranges;
    int best = -1;
    for (int i = 0; i < static_cast<int>(rs.size()); ++i) {
        if (rs[i].from != startIdx) continue;
        if (best < 0 || (rs[best].done && !rs[i].done)) best = i;
    }
    if (best < 0) return std::nullopt;
    return std::make_pair(si, best);
}

std::string formatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    long total = static_cast<long>(seconds);
    long h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%ldh %02ldm", h, m);
    else if (m > 0) std::snprintf(buf, sizeof(buf), "%ldm %02lds", m, s);
    else std::snprintf(buf, sizeof(buf), "%lds", s);
    return buf;
}

std::string formatPct(float pct) {
    char buf[16];
    if (pct >= 99.995f) return "100%";
    std::snprintf(buf, sizeof(buf), "%.1f%%", pct);
    return buf;
}

}
