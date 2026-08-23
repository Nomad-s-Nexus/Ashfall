# Rad Overhaul v1.0.0

First packaged release of Rad Overhaul (Fallout Anomaly Radiation) for Fallout 4.
The archive attached below is a ready-to-install mod: the full `deploy/` tree plus the
freshly cross-built `FalloutAnomalyRadiation.dll`.

## Highlights

- **Self-contained storm dome.** The storm-dome lighting no longer depends on
  `KORsAnomaly Stripped.esp` — the arena LIGH records and their GodRays/LensFlare
  sub-records are now owned by `RadiationOverhaul.esp` (assets shipped with permission).
  No hidden plugin requirement.
- **Verified game-main-thread execution.** The radiation engine tick, storm work, HUD,
  gas-mask, and debug paths run through an explicit dispatcher pinned to the Fallout 4
  main thread (`RE::Main::threadID` + a thread-specific `WH_GETMESSAGE` hook), replacing
  the disproven worker-thread/`AddTask` assumptions. The plugin fails closed if the
  dispatcher cannot be established.
- **Native storm effects.** Distant firestorm nuke detonation, decaying + sustained
  lethal-storm camera shake, and the rotating cloud dome are driven natively (no more
  dead Papyrus `SetStage` paths).
- **Stability fixes.** Dispatcher globals made atomic; coalescing flags recover from an
  enqueue exception instead of stranding ticks; storm shake counters are disarmed on
  save-load and debug reset (no phantom rumble carried into a later storm); the storm
  dome clears stale references before re-spawning and only places its support lights
  when the dome itself placed (no per-tick churn).

## Requirements

- Fallout 4 (OG runtime) + [F4SE](https://f4se.silverlock.org/)
- CommonLibF4-based; built for the OG runtime — the plugin fails closed on unsupported runtimes.

## Install

Install the attached `RadiationOverhaul-rad-overhaul-v1.0.0.zip` with your mod manager
(Mod Organizer 2 / Vortex) and enable the ESPs, or extract it into your `Data` folder
manually. The archive lays out `F4SE/`, `Meshes/`, `Textures/`, `Sound/`, `Scripts/`,
`PrismaUI_F4/`, and the ESPs at the root.

## Notes

Corresponds to the fixes tracked in issues #38, #39, #46, #47 (PR #53).
