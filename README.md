<p align="center"><img src="logo.png" width="120" alt="Blitz Trainer logo"></p>

# Blitz Trainer

[![Build](https://github.com/phoenixd123/blitztrainer/actions/workflows/build.yml/badge.svg)](https://github.com/phoenixd123/blitztrainer/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/phoenixd123/blitztrainer)](https://github.com/phoenixd123/blitztrainer/releases)

**[Download from Releases](https://github.com/phoenixd123/blitztrainer/releases)** · [Website](https://phoenixd123.github.io/blitztrainer/) · [Report a bug](https://github.com/phoenixd123/blitztrainer/issues)

A Geode mod for Geometry Dash **2.2081** that automates the Blitzkrieg practice method: it builds stages from a level's start positions, tracks every attempt, and moves your spawn to the next unfinished range for you.

## Install

1. Install [Geode](https://geode-sdk.org) (5.10.1 or newer) for GD 2.2081.
2. Download `phoenixd123.blitz-trainer.geode` from the [latest release](https://github.com/phoenixd123/blitztrainer/releases) and copy it into your `geode/mods` folder, or in game open **Geode > Mods > Install from file**.
3. Geode installs the `geode.node-ids` dependency for you. Restart the game.

## Use

1. Open a startpos copy of a level (12-18 start positions is a good amount).
2. Pause > pink **Blitz Trainer** button > **Start (Classic)** or **Start (Lite)**.
3. Play. Finished ranges move your next respawn to the next unfinished range automatically.

| Key | Action |
| --- | --- |
| `Q` / `E` | Previous / next start position |
| `T` | Go to next unfinished range |
| `H` | Toggle HUD |

All keys and rules are in the mod's settings (passes per range, passes in a row, long runs credit shorter ranges, auto-move spawn, route order, daily goal, practice-mode tracking, HUD position/scale/opacity, noclip and speedhack detection, notifications, sounds, backups).

The Blitz Trainer menu also has:

- **Heatmap**: where you die most and the ranges with the lowest pass rate.
- **History**: when each stage was cleared, with its attempts and time, and a rough estimate of the time left.
- **Manage**: export to or import from the clipboard, reset, and restore one of the daily backups. Importing or restoring can be undone.

**Route order** decides which unfinished range auto-move and Go to target pick next: *In order* (left to right), *Weakest first* (lowest pass rate, so the ranges you struggle with come up more), or *Random*.

## How runs are counted

Checkpoints are the level start, every enabled start position (by X position), and the level end. Each attempt is assigned to one **target range** in the current stage:

1. an unfinished range the run fully covered (smallest start first),
2. otherwise the unfinished range you started at,
3. otherwise the unfinished range you got furthest through,
4. otherwise the same rules for finished ranges (extra passes).

The target gets the attempt, time and best progress. If *Long runs credit shorter ranges* is on, every other unfinished range the run fully covered also gets a pass. Attempts from practice checkpoints, noclip or speed changes never count. Progress is matched by position, not percentage, so 2.1 vs 2.2 percentage settings don't matter for tracking.

**Start position detection.** The mod collects start positions from both the object loader and a full scan of the level (disabled ones are skipped), then works out your spawn from where the player actually is right after respawning, cross-checked with the start position the game selected. All percentages in the HUD and menus are calibrated against the game's own percentage, so they match what GD shows whether it uses 2.1 (position) or 2.2 (time) percentages.

Profiles are saved per level in your GD save folder under `geode/mods/phoenixd123.blitz-trainer/profiles/` (on Windows: `%LOCALAPPDATA%\GeometryDash\geode\mods\...`) (online levels by ID, local copies by name). If a level's start positions change, **Rebuild** keeps the stats of every range whose ends didn't move.

## Website

`docs/index.html` is the project page, served by GitHub Pages at <https://phoenixd123.github.io/blitztrainer/>.

## Releasing

Bump `version` in `mod.json` and add a `# vX.Y.Z` section to `changelog.md`, then push to `main`. On GitHub, open **Releases > Draft a new release**, create the tag `vX.Y.Z` and publish. The workflow builds every platform and attaches the combined `.geode` to that release.

## Build from source

Push to GitHub and the included workflow (`.github/workflows/build.yml`) builds Windows, macOS and Android and uploads a combined `.geode`. Locally:

```sh
export GEODE_SDK=/path/to/geode-sdk   # Geode SDK 5.10.1
geode build                           # or: cmake -B build && cmake --build build
```

`tests/model_test.cpp` covers the training logic and runs on any machine:

```sh
c++ -std=c++20 -Isrc tests/model_test.cpp src/core/Model.cpp src/core/Time.cpp -o model_test && ./model_test
```

## Notes

- GD 2.2091 came out recently and Geode doesn't support it yet. When Geode updates, change the `gd` versions and `geode` version in `mod.json` and rebuild.
- iOS is paused for now: Geode's iOS bindings are mid-update for GD 2.2091 and currently fail to compile. Re-add `"ios": "2.2081"` to `mod.json` and the iOS job to the workflow once that's fixed upstream.
- Platformer levels aren't supported.
- Levels with ordered or reverse-gameplay start positions are split by X position; the mod warns you about them.
- The Blitzkrieg method was created by Rudder. This mod is an independent implementation, inspired by Zhulis' Blitzkrieg mod.

MIT licensed, see [LICENSE](LICENSE).
