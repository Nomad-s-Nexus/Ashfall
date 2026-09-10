# Rad Overhaul code notes

## PrismaUI HUD creation handoff

Reference: `src/RadHud.cpp:RadHud::RequestAPI` and `src/RadHud.cpp:RadHud::CreateView`.

PrismaUI 2.1 exposes `IVPrismaUI11::DispatchToGameThread`. HUD creation uses that verified dispatcher because callback-bearing `CreateView` is rejected until PrismaUI's own window-thread dispatcher is ready. The retry is bounded and falls back to the existing update-path recovery.

- PrismaHUD is resolved before asynchronous PrismaUI view creation starts, so a fast DOM-ready callback cannot miss initial toxicity/filter registration.
- Optional PrismaHUD acquisition is retried on later safe lifecycle events when PrismaUI is already available. Already-acquired provider pointers are preserved.
- DOM callback bridge pointers are atomically published because PrismaUI callbacks may run on the web thread while view recovery runs on the verified game thread.
- The fullscreen Rad overlay remains at normal compositor order. Only the toxicity/filter widgets use PrismaHUD's high HUD order.

Reference: `src/RadHud.cpp:RadHud::PushToxic` and `src/main.cpp:WaitThenCreateRadHud`.

View recovery runs before the in-world data gate so a missing Pip-Boy/location signal cannot prevent the views from being created; payload updates remain gated until the player is in the world. If the world-ready waiter reaches its five-second hold but `CreateView` dispatch is transiently rejected, it retains the generation latch and retries rather than silently abandoning HUD creation.

## Engine startup and recovery

Reference: `src/Hooks.cpp`, `src/RadLifecycle.cpp`, and `src/main.cpp:MessageHandler`.

- F4SE lifecycle callbacks never sleep waiting for PrismaUI. If `PrismaUI_F4.dll` is loaded but the V11 dispatcher is not accepting work yet, the activation request is deferred.
- The Win32 `WH_GETMESSAGE` dispatcher is a no-PrismaUI fallback only. Rad does not mix an early Win32 queue with a PrismaUI provider that is still starting.
- `RadLifecycle::EnsureEngineActiveOnGameThread` independently tracks core initialization and the PlayerCharacter update producer. It is safe to retry after a transient dispatch failure, config/init failure, or hook-install failure.
- `kPostLoadGame` and `kNewGame` call the lifecycle gate before any engine mutation. `OnPostLoad`, `OnNewGame`, penalty resets, and HUD waiting are skipped when activation is incomplete.
- The log reports `Radiation Engine ACTIVE` only after the core and update producer are both confirmed live.

## Storm dome streaming ownership

Reference: `src/RadEngine_Storm.cpp:RadiationEngine::UpdateStormDomePosition` and `src/RadCorrectnessPolicy.h:ShouldRecreateDomeForOwner`.

The 2026-09-01 runtime capture under `docs/logs/2026-09-01-storm-jitter/` proved that ordinary exterior movement could produce `cellSame=false, worldSame=true`, followed immediately by `Storm dome cleared` and a full native respawn. Parent-cell changes are normal Fallout 4 exterior streaming and are not a worldspace ownership change.

The ownership policy therefore keeps the current dome references alive across same-world exterior cell changes and lets the existing follow path reposition them. Destructive recreation is reserved for an actual worldspace change. This removes the measured cell-boundary clear/respawn jitter without weakening the worldspace safety gate.

## Regression coverage

`tests/verify_hud_fallback.py` guards compositor ordering, position/resize behavior, V11 non-blocking dispatch, truthful engine activation, retained HUD-wait retries, optional PrismaHUD recovery, and atomic cross-thread callback publication. `tests/correctness_policy_tests.cpp` covers the policy-level lifecycle and load-generation invariants plus same-world dome streaming versus real worldspace ownership changes.

### Armor skin exclusion and diagnostic provenance

- `src/ClimateProtectionRuntime.cpp:ClimateProtectionRuntime::Scan`: the 2026-09-09 live armor log included `Fallout4.esm|0xD64` as torso equipment. Read-only record inspection identified `SkinNaked`, with NakedTorso/NakedHands addons and no display name. Exclude the actor race and NPC skin references before both insulation accumulation and diagnostic records; do not hide arbitrary unnamed equipment. Remaining unnamed equipment includes its runtime form ID.
- `src/ClimateInsulationPolicy.h:PieceWarmth` and `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:window.setArmorInfo`: the shared fallback is 45%, with exact/keyword configuration overrides. The active configuration had no overrides. SOURCE now distinguishes DEFAULT from OVERRIDE; accessories without covered regions show no applicable warmth. These are model estimates, not measured material ratings. The displayed 49% follows from incorrectly layering skin over the 40.5% jumpsuit; excluding skin leaves 40.5%. No material ratings or thermal constants were invented.
- `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css:.armor-records-scroll`: a visible scrollbar, keyboard-focusable region and viewport-relative height keep long equipment lists bounded. Outer panel scrolling remains available on short screens.

### Firestorm dome follow cadence

- `src/Hooks.cpp:QueueInputPoll` and `src/RadEngine_Storm.cpp:UpdateStormDomePosition`: the previous policy moved the dome only every fourth 4 Hz tick, producing discrete one-second position jumps. Follow existing reference positions on the already-dispatched per-frame main-thread task. Keep `UpdateStormDomeOwner` on the existing 4 Hz storm controller for ownership, creation and bounded retry. Skip unresolved or mismatched worldspaces and unchanged positions. No worker-thread engine mutation or new producer is introduced.
- `src/RadEngine_Storm.cpp:UpdateStorm` and `src/RadEngine_Debug.cpp:DebugForceStorm`: the captured 2026-09-09 Firestorm test restarted at 16:47:07.076 and entered Warning at 16:47:17.266. That session's log ended at 16:48:19.713, before its 90-second warning completed. The following load began at 16:59:46.636. This capture does not establish failed Active entry or failed damage. Nuke attempts/wait remain bounded, followed by the existing 12-second roll-in; shelter and complete Power Armor suppress exposure as before. A live test must reach blast-wave arrival and record health/radiation changes to accept that path.
- `tests/verify_hud_fallback.py:main`: regression checks bind dome following to the existing main-thread frame queue before the 4 Hz throttle, preserve ownership work in the controller, and prohibit lifecycle or exposure calls from the frame path. Existing policy checks retain worldspace/cell distinctions.

### Firestorm and minimal survival readouts

- `src/RadEngine_Storm.cpp:SpawnStormDome`, `TriggerStormDome`, `TriggerStormFX`, and `UpdateStormDomeOwner`: Firestorm (theme 1) uses no native cloud dome or Papyrus dome stage. Native detonation, blast-wave arrival, weather, fire and exposure remain intact. The 2026-09-09 live log reached PEAK at 23:27:23.973, nuke success at 23:27:27.350 and blast-wave arrival at 23:27:39.598; the user confirmed damage worked.
- `src/ClimatePolicy.h:BodyWarmth01`: WARM previously displayed effective clothing insulation, which does not rise beside a fire. Body warmth now maps the existing modeled core range 28–37 C to 0–100%. Existing heating, cooling, stage thresholds and heat-source strengths are unchanged. HUD arrows stop at modeled temperature bounds; diagnostics distinguish heat balance from actual temperature change at the cap.
- `src/RadiationEngine.cpp:RadiationEngine::Update`: filter drain lacked the hazardous-weather condition even though damage already honored it. Drain now requires both exposure and recognized contaminated weather. The AIR payload uses the same worn-mask, failure, shelter, Power Armor and configured damage/drain values as runtime breathing logic, including after automatic replacement/repair. Breathing rates exclude separate storm exposure and other radiation sources.
- `src/RadHudClimate.cpp:RadHud::PushClimate`, `src/RadHud.cpp:RadHud::PushAir` and `RadHud::SetDebugMenu`: climate and air payloads push on changes, cache the latest data, and replay on debug opening. Full native model rates, temperature, wetness, insulation, nearby heat, shelter and rain cover are visible in the debug menu. The existing ARMOR payload stays intact.
- `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_status.css` and the three `hud_*.html` renderers: compact labeled icons replace the main HUD bars and raw totals. TEMP/WIND describe thermal exposure; BREATH describes clean/contaminated weather and protection. Icon percentages expose reserves/body warmth; full totals and explanations are in `rad_diagnostics.js`. Existing layout callbacks and no-PrismaHUD fallback remain supported.

### Firestorm nuke placement

- `src/RadEngine_Storm.cpp:RadiationEngine::SpawnDistantNuke`: each eligible attempt uses the current player heading with no yaw jitter and a uniformly selected 10,000–22,000 unit distance, retaining the existing `z + 1500` offset and native explosion submission. This changes placement only; the cinematic explosion record and damage balance are unchanged.
- `src/RadEngine_Storm.cpp:RadiationEngine::UpdateStorm`: retries still sample a new target per attempt. Only a successful return sets the one-shot latch. The existing 40-attempt and 40-tick limits remain; either success or exhaustion starts the existing 12-second atmosphere roll-in. Far-target availability and cinematic visibility within this window still require live Fallout validation. A source check or successful DLL build does not establish runtime acceptance.
- `tests/verify_hud_fallback.py:check_source`: the existing CI check now guards distance bounds, player heading, horizontal axes, height, native submission, success return, and bounded retry/roll-in contracts. It fails against the previous 6,000–7,000 placement.

### Climate time semantics and pause handling

- `src/ClimateRuntime.cpp:ClimateRuntime::Update`: climate simulation now operates in an authoritative time domain tied to game time (`GameDays()`) converted via live `timescale` to real gameplay seconds. Fixed 0.25s stepping is eliminated.
- `src/ClimateRuntime.cpp:IsGameplayPaused`: checks `UI::freezeFramePause` and any open menu on `ui->menuStack` with `UI_MENU_FLAGS::kPausesGame`. While gameplay is paused, climate simulation steps with `dt = 0.0f` so core temperature and wetness remain strictly unchanged while still pushing prospective diagnostic rates to the HUD.
- `src/ClimatePolicy.h:StepTimeJump`: handles wait, sleep, and fast-travel time jumps (> 0.01 game days / ~14.4 game minutes) by sub-stepping `Step` across the elapsed duration (bounded to 24 game hours). Wetness dries under shelter, homeostasis warms the player toward 37°C, and hypothermia stages update smoothly without numerical overshoot or unhandled state jumps.
- `src/ClimateRuntime.cpp:GameMonth` and `src/ClimateEnvironmentPolicy.h:AmbientTemperatureC`: month fallback is corrected from index 9 to index 8 (September) per `commonlib_NonVR` authority.
