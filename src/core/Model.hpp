#pragma once

// Pure C++ core of Blitz Trainer: no Geode or cocos includes here, so the
// training logic can be unit-tested on any machine (see tests/).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bt {

// Checkpoints are the level start (index 0), every enabled start position
// (sorted by X), and the level end (index n). A "section" is the gap between
// two neighbouring checkpoints, and a range [from, to] spans one or more
// sections. Everything is tracked by checkpoint index and X position, never
// by displayed percentage, so 2.1/2.2 percentage modes can't desync it.

struct RangeStats {
    int attempts = 0;            // attempts where this range was the target
    int passes = 0;              // total successful passes (any credit)
    int streak = 0;              // current consecutive passes
    int bestStreak = 0;
    int attemptsToFirstPass = 0; // attempts it took to pass the first time
    double timePlayed = 0.0;     // seconds of in-game time spent on it
    float bestProgress = 0.f;    // 0..1, furthest you got through the range
    int64_t firstPassAt = 0;     // unix seconds
    int64_t lastPassAt = 0;
};

struct Range {
    int from = 0;
    int to = 0;
    bool done = false;
    RangeStats s;
};

struct Stage {
    std::vector<Range> ranges;
    bool done() const;
    int doneCount() const;
};

enum class StageStyle : int {
    Classic = 0, // every window of 1, 2, 3 ... n sections (Rudder's method)
    Lite = 1,    // windows of 1, 2, 4, 8 ... n sections, half-overlapping
};

struct Profile {
    int version = 1;
    std::string levelKey;
    std::string levelName;
    std::vector<float> xs;   // checkpoint X positions, xs.front()=0, xs.back()=levelLength
    float levelLength = 0.f;
    StageStyle style = StageStyle::Classic;
    std::vector<Stage> stages;
    std::array<int, 100> deaths{}; // death count per level percent bucket
    int totalAttempts = 0;
    int trackedAttempts = 0;
    double totalTime = 0.0;
    int64_t createdAt = 0;
    int64_t completedAt = 0;

    // Runtime only (not saved): the percentage GD itself shows at each
    // checkpoint. GD 2.2 can show time-based percentages that differ from
    // X position, so the UI uses these when the game has provided them.
    std::vector<float> shownPct;

    int sections() const { return xs.empty() ? 0 : static_cast<int>(xs.size()) - 1; }
    float pct(int idx) const;          // checkpoint index -> displayed level percent
    float pctOfX(float x) const;       // X -> displayed level percent
    int currentStage() const;          // first unfinished stage, -1 when all done
    bool complete() const { return !stages.empty() && currentStage() < 0; }
    int totalRanges() const;
    int doneRanges() const;
    float overallProgress() const;     // 0..1, finished ranges / all ranges
};

// Builds the checkpoint list: 0, sorted & de-duplicated startpos X values, end.
std::vector<float> buildCheckpoints(std::vector<float> startPosXs, float levelLength);

std::vector<Stage> generateStages(int sections, StageStyle style);

Profile makeProfile(
    std::string levelKey, std::string levelName, std::vector<float> startPosXs,
    float levelLength, StageStyle style, int64_t now
);

// True when the level's start positions still match the profile.
bool checkpointsMatch(Profile const& p, std::vector<float> const& xs, float tolerance = 2.f);

// Index of the furthest checkpoint the player reached.
int reachedIndex(Profile const& p, float endX, bool completed);

// Index of the checkpoint the player started from, or -1 when the spawn
// point isn't a checkpoint (e.g. a practice-mode checkpoint mid-section).
int matchStartIndex(Profile const& p, float startX, float tolerance = 20.f);

struct TrainingRules {
    int requiredPasses = 1;      // passes needed to finish a range
    bool consecutive = false;    // passes must be in a row
    bool creditAllPassed = true; // one long run also credits shorter ranges it covered
};

struct RunInput {
    int startIdx = -1;
    float endX = 0.f;
    bool completed = false; // reached the end of the level
    double time = 0.0;      // seconds
};

struct RunResult {
    bool counted = false;     // a range in the stage received this attempt
    int stage = -1;
    int target = -1;          // index of the target range within the stage
    bool targetPassed = false;
    std::vector<int> newlyDone;   // range indices (within stage) finished by this run
    bool stageCompleted = false;
    bool profileCompleted = false;
    float progress = 0.f;     // 0..1 through the target range
};

bool isRangeDone(RangeStats const& s, TrainingRules const& rules);

RunResult applyRun(Profile& p, RunInput const& run, TrainingRules const& rules, int64_t now);

// Recomputes every "done" flag, e.g. after the required-passes setting changes.
void refreshDone(Profile& p, TrainingRules const& rules);

// Carries stats from an old profile into a freshly generated one wherever a
// range has the same start and end X (used when start positions change).
int mergeStats(Profile& fresh, Profile const& old, float tolerance = 2.f);

// Checkpoint index the player should spawn at next: the first unfinished range
// of the current stage at or after `afterFrom` (wrapping around).
std::optional<int> nextTargetStart(Profile const& p, int afterFrom = -1);

// The range a run starting at `startIdx` would be aimed at (for the HUD).
std::optional<std::pair<int, int>> targetFor(Profile const& p, int startIdx);

std::string formatTime(double seconds);
std::string formatPct(float pct);

}
