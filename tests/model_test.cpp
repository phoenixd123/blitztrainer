// Native tests for the training core. Build & run:
//   c++ -std=c++20 -I../src tests/model_test.cpp ../src/core/Model.cpp -o model_test && ./model_test
#include "core/Model.hpp"

#include <cassert>
#include <cstdio>
#include <cmath>

using namespace bt;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
    // 4 start positions -> 5 sections, checkpoints at 0,100,200,300,400,500
    auto p = makeProfile("k", "Test", {300.f, 100.f, 200.f, 400.f, 400.4f}, 500.f, StageStyle::Classic, 1);
    CHECK(p.xs.size() == 6);
    CHECK(p.sections() == 5);
    CHECK(p.stages.size() == 5);
    CHECK(p.stages[0].ranges.size() == 5);
    CHECK(p.stages[4].ranges.size() == 1);
    CHECK(p.totalRanges() == 15); // n(n+1)/2

    // Lite style has fewer ranges
    auto lite = generateStages(16, StageStyle::Lite);
    int liteCount = 0; for (auto& s : lite) liteCount += (int)s.ranges.size();
    CHECK(liteCount < 136);
    CHECK(lite.back().ranges.size() == 1 && lite.back().ranges[0].from == 0 && lite.back().ranges[0].to == 16);

    // start matching
    CHECK(matchStartIndex(p, 101.f) == 1);
    CHECK(matchStartIndex(p, 150.f) == -1);
    CHECK(matchStartIndex(p, 0.f) == 0);

    TrainingRules rules{2, false, true};

    // die inside section 1 -> attempt on range (1,2), no pass
    auto r = applyRun(p, {1, 150.f, false, 3.0}, rules, 10);
    CHECK(r.counted && r.stage == 0 && !r.targetPassed);
    CHECK(p.stages[0].ranges[r.target].from == 1);
    CHECK(std::fabs(r.progress - 0.5f) < 0.01f);

    // pass (1,2) twice -> done
    r = applyRun(p, {1, 210.f, false, 3.0}, rules, 11);
    CHECK(r.targetPassed && r.newlyDone.empty());
    r = applyRun(p, {1, 205.f, false, 3.0}, rules, 12);
    CHECK(r.targetPassed && r.newlyDone.size() == 1);
    CHECK(p.stages[0].ranges[1].done);
    CHECK(p.stages[0].ranges[1].s.attemptsToFirstPass == 2);

    // a long run from 0 to end credits every section (creditAllPassed)
    r = applyRun(p, {0, 0.f, true, 20.0}, rules, 13);
    r = applyRun(p, {0, 0.f, true, 20.0}, rules, 14);
    CHECK(p.stages[0].done());
    CHECK(r.stageCompleted);
    CHECK(p.currentStage() == 1);

    // next target in stage 2 is range starting at 0
    auto nt = nextTargetStart(p);
    CHECK(nt && *nt == 0);
    auto tf = targetFor(p, 2);
    CHECK(tf && tf->first == 1 && p.stages[1].ranges[tf->second].from == 2);

    // consecutive mode: a fail resets the streak
    TrainingRules strict{2, true, false};
    auto q = makeProfile("k2", "T", {100.f}, 200.f, StageStyle::Classic, 1);
    applyRun(q, {0, 120.f, false, 1}, strict, 1); // pass (0,1)
    applyRun(q, {0, 50.f, false, 1}, strict, 2);  // fail
    applyRun(q, {0, 120.f, false, 1}, strict, 3); // pass
    CHECK(!q.stages[0].ranges[0].done);
    applyRun(q, {0, 120.f, false, 1}, strict, 4); // pass -> streak 2
    CHECK(q.stages[0].ranges[0].done);

    // death buckets
    CHECK(q.deaths[25] == 1);

    // merge keeps progress when a start position is added later
    auto moved = makeProfile("k", "Test", {100.f, 200.f, 250.f, 300.f, 400.f}, 500.f, StageStyle::Classic, 1);
    int merged = mergeStats(moved, p);
    CHECK(merged > 0);
    CHECK(moved.stages[0].ranges[1].done); // (100,200) still done

    // cannot reach end without completing
    CHECK(reachedIndex(p, 499.9f, false) == 4);
    CHECK(reachedIndex(p, 0.f, true) == 5);

    // full completion
    auto tiny = makeProfile("t", "T", {}, 100.f, StageStyle::Classic, 1);
    CHECK(tiny.sections() == 1);
    r = applyRun(tiny, {0, 0, true, 1}, TrainingRules{}, 5);
    CHECK(r.profileCompleted && tiny.complete());

    // instant death a hair behind the spawn still counts as an attempt on that range
    auto inst = makeProfile("i", "I", {100.f, 200.f}, 300.f, StageStyle::Classic, 1);
    r = applyRun(inst, {1, 99.6f, false, 0.1}, TrainingRules{}, 1);
    CHECK(r.counted && inst.stages[0].ranges[r.target].from == 1);

    // displayed (time-based) percentages map checkpoints exactly and interpolate between
    inst.shownPct = {0.f, 40.f, 70.f, 100.f};
    CHECK(std::fabs(inst.pct(1) - 40.f) < 0.01f);
    CHECK(std::fabs(inst.pctOfX(150.f) - 55.f) < 0.01f);
    CHECK(std::fabs(inst.pctOfX(300.f) - 100.f) < 0.01f);
    inst.shownPct = {1.f}; // wrong size -> ignored
    CHECK(std::fabs(inst.pct(1) - 100.f / 3.f) < 0.01f);

    std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
