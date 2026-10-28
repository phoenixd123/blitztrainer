# v1.1.0

- New: **Route order** setting. Auto-move and Go to target can go left to right, pick your weakest range first (lowest pass rate), or pick at random
- New: **Daily pass goal** with a day streak, shown in the HUD and the Blitz Trainer menu
- New: **History** screen showing when each stage was cleared, its attempts and time, plus a rough time-left estimate
- New: **Daily backups** of each level's progress (kept 7 days by default), restorable from Manage
- New: **Manage** screen for export, import, reset and backups; importing or restoring can be undone
- Go to target now moves on to the next unfinished range when you're already on one
- Fixed: passes that finished a stage weren't counted in the session pass total
- Fixed: undo could leave the "start positions changed" warning in the wrong state

# v1.0.0

- First release for Geometry Dash 2.2081 (Geode 5.10.1+)
- Start-position-based run tracking
- Spawn detection from the player's real spawn point, cross-checked with the selected start position
- Start positions collected from the loader and a full level scan; disabled ones skipped
- Percentages calibrated to the game's own 2.1 / 2.2 percentage display
- Auto-move spawn, start position keybinds (Q / E / T / H)
- Classic and Lite stage styles
- Passes-per-range and consecutive-pass rules
- In-level HUD with mini map
- Death heatmap and hardest ranges
- Rebuild with progress merge when start positions change
- Undo last run, clipboard export/import
- Noclip and speedhack detection that ignores the game's own startup check, lag spikes and time-warp triggers
