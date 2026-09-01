# Live runtime evidence — 2026-09-01

These files are the unmodified logs captured from the Alpha 2 Fallout 4 profile while debugging PR #61.

- `FalloutAnomaly.log` is the Rad Overhaul plugin log.
- `PrismaUI_F4.log` is the PrismaUI runtime log from the same session.

The relevant failed session starts at `2026-09-01 14:13:32`. It shows the plugin loading and the fallback main-thread dispatcher installing at `14:14:02`, but no `Initializing Radiation Engine` entry afterward. The post-load task later enters successfully at `14:14:36`. This is the evidence for the callback-preservation fix in `Rad-Overhaul-Fallout-4/src/Hooks.cpp:Hooks::QueueMainThreadTask`, pushed in commit `9c161dc`.

These logs were captured before that fix was loaded by a fresh Fallout 4 process; they document the failure, not a successful post-fix runtime test.
