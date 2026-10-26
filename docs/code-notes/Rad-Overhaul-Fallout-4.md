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

Reference: `src/RadEngine_Storm_Dome.cpp:RadiationEngine::UpdateStormDomeLifecycle`, `src/RadEngine_Storm_Dome.cpp:RadiationEngine::UpdateStormDomeFollow`, `src/Hooks_DomeFollow.cpp:Hooks::QueueDomeFollow`, and `src/RadCorrectnessPolicy.h:ShouldRecreateDomeForOwner`.

The 2026-09-01 runtime capture under `docs/logs/2026-09-01-storm-jitter/` proved that ordinary exterior movement could produce `cellSame=false, worldSame=true`, followed immediately by `Storm dome cleared` and a full native respawn. Parent-cell changes are normal Fallout 4 exterior streaming and are not a worldspace ownership change.

The ownership policy therefore keeps the current dome references alive across same-world exterior cell changes and lets the existing follow path reposition them. Destructive recreation is reserved for an actual worldspace change. This removes the measured cell-boundary clear/respawn jitter without weakening the worldspace safety gate.

Reference: `src/Hooks_DomeFollow.cpp:Hooks::SetDomeFollowActive` and `src/Hooks_DomeFollow.cpp:Hooks::QueueDomeFollow`.

The follow path is inactive outside a live dome, then bounded to approximately 30 Hz with a 16-unit movement threshold and skips refs without loaded 3D. It only updates existing references; spawn, deletion, reparenting, shelter visibility, and worldspace ownership remain on the 4 Hz lifecycle path.

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

- `src/ClimateRuntime.cpp:ClimateRuntime::Update`: climate simulation operates in a pure game-time physiological domain (`gameSeconds = gameDaysElapsed * 86400.0f`). Because ambient temperature and diurnal cycle depend directly on `GameDays()`, dividing by `timescale` previously converted game time into real play seconds, which caused ambient temperature to cycle 20x faster than body physiology and made cold exposure non-invariant across timescale settings. Advancing physiology directly in game-time ensures that 1 game hour of exposure produces identical cooling and physiological progression regardless of timescale setting (e.g. 10, 20, 40).
- `src/ClimateRuntime.cpp:QueryPauseInfo`: queries `UI::freezeFramePause` and any open menu on `ui->menuStack` with `UI_MENU_FLAGS::kPausesGame`. While gameplay is paused without calendar progression, climate simulation steps with `dt = 0.0f` so core temperature and wetness remain strictly unchanged while still pushing prospective diagnostic rates to the HUD.
- `src/ClimateRuntime.cpp:ClimateRuntime::Update` branch ordering: `gameDaysElapsed > kTimeJumpThresholdDays` (0.01 game days / ~14.4 game minutes) takes precedence over `pauseInfo.isPaused`. In Fallout 4, sleep, wait, or fast-travel menus set `kPausesGame` or `freezeFramePause` while advancing calendar time; evaluating menu pause first previously swallowed multi-hour sleep/wait jumps with `dt = 0.0f` and consumed `_prevGameDays`. Giving the jump condition precedence guarantees that all sleep, wait, and fast-travel time jumps are properly simulated.
- `src/ClimateRuntime.cpp:ClimateRuntime::Update` delayed tick preservation: normal gameplay ticks delayed beyond `kMaximumStepSeconds` (5.0s)—such as during engine hitches, cell loading, or delayed tasks—no longer truncate elapsed time. Ticks with `gameSeconds > kMaximumStepSeconds` are sub-stepped via `StepTimeJump` in increments of at most 5.0s, eliminating lost time and preserving backlog invariance.
- `src/ClimateRuntime.cpp:ClimateRuntime::Update` environment sampling approximation during jumps: time jumps sample the environment once at the end of the skipped interval (e.g. arrival cell, destination weather, current shelter/heat) and reuse that sample across all substeps. This is an intentional lightweight approximation avoiding historical reconstruction overhead unless live gameplay traces indicate unacceptable behavior.
- `src/ClimateRuntime.cpp:ClimateRuntime::Update` diagnostic logging: records `prevGameDays`, `nowDays`, `deltaDays`, `freezeFramePause`, `pausedMenu`, `branch`, and `appliedSeconds` via `spdlog` (info level on jumps, debug level periodically, trace level on every tick). Jump records also include calculated `substeps` and wall-clock `wallMs` for measuring large-wait cost.
- `src/ClimatePolicy.h:StepTimeJump`: handles wait, sleep, and fast-travel time jumps (> 0.01 game days / ~14.4 game minutes) as well as sub-stepping delayed ticks by stepping `Step` across the elapsed duration (bounded to 24 game hours = 86,400 game seconds). Wetness dries under shelter, homeostasis warms the player toward 37°C, and hypothermia stages update smoothly without numerical overshoot or unhandled state jumps.
- `src/ClimateRuntime.cpp:GameMonth` and `src/ClimateEnvironmentPolicy.h:AmbientTemperatureC`: month fallback is corrected from index 9 to index 8 (September) per `commonlib_NonVR` authority.
- `tests/climate_policy_tests.cpp`: test suite validates zero-delta pause freeze over 1000 ticks with non-zero prospective HUD rates, small-step integration invariance (4x0.25s vs 1x1.0s), delayed tick preservation (20s delayed equals four 5.0s sub-steps and cools more than a single 5.0s step), pure calendar-to-game-seconds conversion (`ElapsedGameSeconds`) verifying timescale has no participation in physiological duration, single-step clamping to `kMaximumStepSeconds`, and authoritative multi-hour time jumps in pure game seconds (8h sleep, 8h mild recovery, 4h rain wait, 2h fast travel, 24h clamp). All `//` comments are omitted in accordance with repository standards.

## Firestorm weather and nuke follow-up

References: `src/RadEngine_Weather.cpp:RadiationEngine::CaptureWeatherReturnTarget`, `src/RadEngine_Storm_Weather.cpp:RadiationEngine::ForceClearAndSilence`, and `src/RadEngine_Storm_Weather.cpp:RadiationEngine::SpawnDistantNuke`.

The return target excludes vanilla radstorm weather so a managed firestorm cannot hand control back to an ambient radstorm. Debug clear/endstorm commands force the configured clear weather and clear transient handoff state. Distant nuke placement records its strike coordinates and uses a rotation aimed back at the player for runtime diagnosis.
## Storm dome and firestorm nuke presentation

References: `src/RadEngine_Storm_Dome.cpp:RadiationEngine::SpawnStormDome`, `src/RadEngine_Storm_Dome.cpp:RadiationEngine::UpdateStormDomeLifecycle`, `src/RadEngine_Storm_FX.cpp:RadiationEngine::TriggerStormFX`, and `src/RadEngine_Storm_Weather.cpp:RadiationEngine::SpawnDistantNuke`.

The native fog dome is restricted to Ash Fallout. Other storm types retain their weather-specific atmosphere and effects without creating the dome. The firestorm explosion remains a native detonation, placed 3,500–5,000 units from the player at terrain height so the effect remains within the loaded exterior grid.

References: `src/RadEngine_Storm_Weather.cpp:ResolveGroundHeight` and `src/RadEngine_Storm_Weather.cpp:RadiationEngine::SpawnDistantNuke`.

The nuke policy supplies only distant X/Y and facing. The runtime resolves the loaded target cell's collision surface through the verified `TESObjectCELL::Pick` wrapper, reconstructs its height from the probe endpoints and hit fraction, adds 16 units of clearance, and retries when the target cell or probe is unavailable. Nuke logs include player Z, target X/Y, hit fraction, resolved surface Z, final Z, and the resolution result.

## Storm-ghoul controls

References: `src/RadEngine_Debug.cpp:RadiationEngine::DispatchDebugCommand`, `src/RadEngine_Storm_Actors.cpp:RadiationEngine::SpawnStormGhouls`, and `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.html`.

The GHOULS debug tab adjusts the chance used by future managed or accepted vanilla radstorms, enables or disables future packs, and can clear the current pack. `RadiationEngine::PushGhoulSettingsToHud` sends the native enabled/chance state to `RadHud::PushGhoulSettings` when the menu opens and after each command, so the persistent status line does not rely on button-side JavaScript state. These controls are session-local and default to enabled with a 40% chance after restart; the spawn roll uses `>=` so a configured 0% chance cannot spawn a pack.

`src/RadiationEngine.cpp:RadiationEngine::Update` uses the `RadWeatherList` FLST as the ghoul-spawn classification source. A new listed weather gets one roll, including listed managed, vanilla, and third-party weather records; accepted vanilla radstorms still pass their startup, cooldown, and cell-exit vetoes, while forced weather previews can exercise the same list-driven roll.

`src/RadEngine_Storm_Actors.cpp:RadiationEngine::SpawnStormGhouls` creates hunter actors initially disabled, enables each actor after the reference is created, clamps the actor to ground before tracking it, and logs final position, ground-clamp result, parent ownership, and 3D attachment. Unresolved tracked handles defer pack replacement during streaming; non-actor references are deleted instead of being silently lost.

`src/RadEngine_Storm_Actors.cpp:RadiationEngine::RetryPendingStormGhouls` retries activation handles on the main update path, activates actors after streaming resolves them, and promotes them into the live pack. `RadiationEngine::RetryStormGhoulCleanup` owns unresolved deletion handles without blocking new packs, while `RadiationEngine::RetireUnresolvedStormGhouls` bounds unresolved live-pack ownership. `RadiationEngine::ResetStormGhoulStateForLoad` drops all prior-world handle bookkeeping at serialization/load boundaries so stale refs cannot deadlock a new save.

`src/PrismaUI_F4_API.h:PRISMA_UI_API::IVPrismaUI2::InteropCall` accepts its payload as `const char*`. Existing JSON callbacks such as `radhud.js:window.stormPhase`, `radhud.js:window.updateFilter`, and `radhud.js:window.showSpecialInfo` therefore accept both JSON text and object input. `radhud.js:setGhoulSettings` follows the same string-or-object parsing contract so native `InteropCall` state remains authoritative for both enabled state and chance.

## Transition and queue hardening

The visual-follow producer remains coalesced and throttled, while the legacy fallback dispatch queue is capped at 128 tasks. A full fallback queue rejects work so a stalled game thread cannot accumulate unbounded closures.

Reference: `src/RadEngine_Storm_Actors.cpp:RadiationEngine::CheckGhoulPackCleanup` and `src/RadEngine_Storm_Actors.cpp:RadiationEngine::SpawnStormGhouls`.

Dome follow and nuke placement now require an attached exterior player cell whose worldspace matches the cached player worldspace. Dome references are checked for attached exterior ownership before movement and are recreated when their tracked cell becomes stale. Storm-ghoul cleanup defers while player cell/world ownership is temporarily unresolved during exterior streaming. The hunter pack is cleared only after a confirmed interior transition, worldspace change, distance exit, or when a defeated prior pack is explicitly retired before another pack is created.

Weather return handoff state is bounded: a replaced target, non-finite blend, or transition that exceeds the wait window releases the temporary override and clears all handoff bookkeeping. The same transient state is reset on debug force, load, clear, and new return transitions.

## Runtime support boundary

Reference: `src/main.cpp:DetectGameVersion` and `src/main.cpp:F4SEPlugin_Query`.

The plugin accepts OG `1.10.163` and the specifically verified AE runtime `1.11.240`. NG `1.10.980` through `1.10.984`, unknown versions, and other post-NG versions fail closed until their native and hook contracts are independently verified.

## Windows regression compiler

`tests/verify_rad_stack_integration.py:check_generation_behavior` also accepts MSVC and builds its real dispatch harness in the temporary directory. `.github/workflows/rad-overhaul-build.yml:Run contract tests` initializes the installed Visual Studio developer shell before Python runs. The previous script searched only for GCC/Clang, so the MSVC runner could stop at "no host C++ compiler" despite being able to build the DLL with xmake. Compiler failure remains a failed check.

## Climate integration onto storm/runtime hardening

The climate branch was merged onto the storm/runtime hardening branch. Where the two branches implemented the same concern, the hardening implementation is authoritative and the climate additions were grafted onto it. Notes above that predate this merge are superseded where they conflict with this section.

- `src/Hooks.cpp:QueueEngineUpdate`: `ClimateRuntime::Update` runs inside the same `RadLifecycleDispatch::PlayerUpdatesReady(generation)` guard as `RadiationEngine::Update`, so climate simulation stays on the game-main task and a task queued for a previous load generation cannot execute. The worker-thread `PlayerCharacter_Update_Hook` never touches climate state.
- `src/Hooks.cpp:QueueMainThreadTask`: the 128-task bound on the Win32 fallback queue is retained.
- `src/Hooks_DomeFollow.cpp:QueueDomeFollow`: the dedicated, coalesced, generation-guarded ~30 Hz dome-follow producer is retained. The climate branch's per-frame `UpdateStormDomePosition` call inside `QueueInputPoll` was dropped; `UpdateStormDomeFollow` owns dome repositioning.
- `src/RadEngine_Storm_Dome.cpp:SpawnStormDome` and `UpdateStormDomeLifecycle`: the native dome is gated on `RadCorrectnessPolicy::StormUsesDome(themeId)` instead of the Ash-only index, so Firestorm (theme 1) stays no-dome while every other storm keeps a native dome owned by the storm controller.
- `src/RadEngine_Storm_FX.cpp:EnsureQuestRunning` and `TriggerStormFX`: the runtime `EnableCloudDome` Papyrus property mutation is removed, so the bundled script cannot become a second dome owner, and the Firestorm Papyrus route stops at stage 20.
- `src/RadEngine_Storm_Weather.cpp:SpawnDistantNuke`: the hardening branch's ground-resolved placement is retained. Its collision probe only reaches attached grid cells, so the climate branch's 10,000-22,000 unit heading placement cannot be combined with it without making detonation fail every attempt. Revisiting the distance needs a live test of both detonation visibility and ground resolution.
- `src/RadHudLayout.cpp`: the built-in layout editor from the hardening branch now also persists the `rad_climate` widget and the `temperatureUnit` (`unit` action), replacing the climate branch's parallel layout implementation in `RadHud.cpp`. Ghoul controls use the hardening branch's handlers and `PushGhoulSettings(bool, float)`.
- CI keeps the hardening branch's exact-head checkout and `commonlib_NonVR` pin `961358d`, which carries the verified AE `CreateExplosionAtLocation` relocation that the climate branch's pin `db36d54` lacks. That relocation was later merged into `commonlib_NonVR` main (PR #103), and both Rad workflows now pin main at `6ab5841`, which also carries the survival bindings from `db36d54` and the public `BGSInventoryInterface` event sources (PR #102).
- `tests/verify_rad_stack_integration.py`: source and behavior guards for the queue bound, dome-follow producer, generation gating (compiled harness for stale versus current generation callbacks), storm module layout, dome ownership policy and ghoul ownership. It failed 27 checks against the climate head before the merge.
# Compiled player-ghoul record regression (2026-09-26)

`tests/verify_ghoul_contract.py` reuses `tests/verify_mask_catalog.py:records`, `subrecords`, `master_names` and `form_key` to inspect `deploy/RadiationOverhaul.esp`. The pinned Spriggit audit proved that self-owned records 000854/000870 and the old quest EditorID are already absent. The check rejects their return while requiring the intentional severity spell/perk and storm FX quest to remain. Master-aware keys prevent confusing a preserved Fallout4.esm 000854 reference with the obsolete self-owned global. See `docs/ESP_BUILD_PIPELINE.md` and `docs/proof/esp-audit-20260926.json` for artifact provenance. No plugin record was edited.

# CI deployment isolation

`xmake.lua:FalloutAnomalyRadiation.after_build` returns before deployment during Actions builds. PR #66 job 1292 at head da33711fbfa0012c9dc6141914bfccd3f1659f60 logged a copy to the Fallen World Alpha 2 modlist; the old callback lacked the CI guard already present in PR #68. `tests/verify_ci_deploy.py` executes the actual callback body with fake filesystem operations: the old body failed with two CI copies, while the guarded body must make zero CI copies and preserve two local destination copies. `.github/workflows/rad-overhaul-build.yml:Run contract tests` runs this regression before building the DLL. The guard prevents future CI deployment; it does not restore the previously copied modlist file.

## Survival core: vanilla Papyrus quest + Rad-owned records (2026-10-26)

Reference: `deploy/Scripts/Source/User/RadSurvivalQuest.psc`, `deploy/Scripts/RadSurvivalQuest.pex`, `deploy/RadSurvival.esp`, `tests/verify_survival.py`.

**Decision.** Hunger, thirst and sleep debt are a vanilla Papyrus quest script. No script extender and no C++ for this layer.

**Why Papyrus, not native.** The native needs runtime could not prove two signals: exactly-once food/drink consumption, and real sleep versus wait with an interrupted flag. PR #70 shipped with "no sleep recovery hook until sleep is proven" for that reason. The shipped Creation Kit base sources expose both to Papyrus, and vanilla Survival itself consumes them: `HC_ManagerScript.psc:50,1167` registers and handles `Actor.OnItemEquipped` for food and drink; `:2618-2671` handles wait and sleep with `abInterrupted`; `:15` gates on `Game.GetDifficulty() == 6`. Rebuilding that natively would be an inference from `SleepWaitMenu`, bed furniture and calendar delta, not proof.

**Why not Hydra (the removed script extender).** It was removed for four reported reasons: a 1.0.2 runtime; an out-of-bounds read in its string handling (missing null terminator); fatal-load and `REL::ID` failures on current runtimes; and an open "Var[] corruption" report in the same variant subsystem the needs state would have been stored in. These are upstream reports, not re-verified in this repository - TODO-verify each against the runtime issue tracker before citing any of them as confirmed. Any one of them is a crash or data-loss risk on the storage path itself.

**Why not Garden of Eden.** `DrinkPotion` is an action that deliberately does not equip, and `OnEquipUnequip` duplicates vanilla `OnItemEquipped`. It adds a dependency without a new signal.

**Event wiring.** `OnInit` arms `RegisterForPlayerSleep()`/`RegisterForPlayerWait()` (unregister first, so a surviving instance cannot double-deliver), registers the remote `OnItemEquipped`, anchors the clock and starts the game-time timer. `OnPlayerLoadGame` re-anchors, clears the pending candidate and the sleep/wait trackers, re-registers only the remote item event, and restarts the timer.

**Consumption.** `OnItemEquipped` arms a candidate with the live form and the player's inventory count. A later tick re-reads `GetItemCount`; relief is granted exactly once, only when the count dropped, then the candidate clears. A candidate older than two seconds is resolved the same way, so a missed event cannot double-apply relief and an equip that never consumed cannot grant it. The shipped base sources declare no `OnItemAdded`/`OnItemRemoved` event on `ObjectReference`/`Actor`, so a quest cannot register for them remotely; the tick check is the confirmation mechanism, not a fallback.

**Sleep and wait.** The start time is recorded on start; on stop the script computes game hours from `Utility.GetCurrentGameTime()`, applies recovery only for real sleep (`abInterrupted` halves it), accrues that interval once, then restarts the timer. Waiting accrues and never recovers sleep debt.

**Timer.** `StartTimerGameTime` is one-shot (`ScriptObject.psc:177`), so `OnTimerGameTime` re-arms it on every tick; sleep and wait cancel then restart it after accruing their own interval. Progression uses game-time deltas with a 240-hour clamp.

**Gating.** `Enabled` and `SkipWhenVanillaSurvival` (default on) compare against `Game.GetDifficulty() == 6`. While suspended the clock stays anchored, so turning vanilla Survival off mid-save cannot be read as hours of skipped play.

**Record contract (`RadSurvival.esp`).**

| Form-ID | Type | EditorID | Notes |
|---|---|---|---|
| `01000800` | QUST | `RadSurvivalQuest` | Flags 9 (Start Game Enabled + Starts Enabled), priority 0, script `RadSurvivalQuest` |
| `01000801` | GLOB (float) | `RadSurvival_Hunger` | 0..1 pool, mirrored for the HUD |
| `01000802` | GLOB (float) | `RadSurvival_Thirst` | 0..1 pool, mirrored for the HUD |
| `01000803` | GLOB (float) | `RadSurvival_Sleep` | 0..1 sleep debt, mirrored for the HUD |

The quest VMAD carries one script, `RadSurvivalQuest`, with object properties `RadSurvival_Hunger`, `RadSurvival_Thirst` and `RadSurvival_Sleep`, each bound to its own `GLOB` record (`0x01000801`-`0x01000803`). The plugin declares `Fallout4.esm` as its single master (index 0) and its own records sit at master index 1, so their full form ids are `0x010008xx`. The record set is authored with FO4IDE: `add_masters` for the master, `create_record` for the records, `add_script`/`add_script_property` for the VMAD structure and `set_script_value` for the bound property values; `deploy/Scripts/Source/User/RadSurvivalQuest.psc` only reads the properties.

**Penalties.** `ApplyStagePenalty(string asPool, int aiStage)` is the single hook and stays inert until `SPEL` records exist; `Actor.AddSpell`/`RemoveSpell` are the follow-up. No actor-value mutation exists anywhere in the script.

**HUD.** Every pool change is mirrored into the matching `GLOB` through `GlobalVariable.SetValue`, so PR #71's native HUD can read the pools as `TESGlobal` with no Papyrus-to-native bridge.

**Tuning.** Code constants mirror `needs.json` `hourlyRates` (hunger 0.05, thirst 0.10, sleep debt 0.025 per game hour); `tests/verify_survival.py` fails if the two diverge.