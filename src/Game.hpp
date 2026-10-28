#pragma once

#include <optional>
#include <vector>

// Small API the UI uses to talk to the running level (implemented in PlayHooks.cpp).
namespace bt::game {

bool inLevel();
bool isPlatformer();

// Checkpoints detected in the current level: 0, start positions..., end.
std::vector<float> const& checkpoints();
float levelLength();
int startPosCount();

// True when a saved profile no longer matches the level's start positions.
bool profileMismatch();
void clearMismatch();

// Re-checks the active profile against the level's start positions
// (e.g. after an undo brought back an older profile).
void recheckProfile();

// Checkpoint index of the current spawn point, or -1.
int currentSpawnIndex();

// Respawns at checkpoint `idx`. When `fromPause` the pause menu is closed.
bool switchTo(int idx, bool fromPause);

// Spawns at the next unfinished range of the current stage.
bool goToTarget(bool fromPause);

void refreshHud();

// Copies the percentages GD displays into the active profile (for the UI).
void syncDisplay();

// True when the level has start positions with a target order or reverse
// gameplay, where play order may not follow X position.
bool hasOrderedStartPos();

// Displayed percent at checkpoint `idx` of the current level.
float shownPct(int idx);

}
