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
| `01000804`-`01000806` | MGEF | `RadSurvival_HungerPenalty1`-`3` | Peak Value Modifier on CarryWeight (`0002DC:Fallout4.esm`) |
| `01000807`-`01000809` | MGEF | `RadSurvival_ThirstPenalty1`-`3` | Peak Value Modifier on ActionPoints (`0002D5:Fallout4.esm`) |
| `0100080A`-`0100080C` | MGEF | `RadSurvival_SleepPenalty1`-`3` | Peak Value Modifier on HealRateMult (`000358:Fallout4.esm`) |
| `0100080D`-`0100080F` | SPEL (Ability) | `RadSurvival_HungerPenaltySpell1`-`3` | Constant Effect, self, one effect each, magnitudes -10/-25/-45 |
| `01000810`-`01000812` | SPEL (Ability) | `RadSurvival_ThirstPenaltySpell1`-`3` | Constant Effect, self, magnitudes -5/-15/-30 |
| `01000813`-`01000815` | SPEL (Ability) | `RadSurvival_SleepPenaltySpell1`-`3` | Constant Effect, self, magnitudes -0.15/-0.30/-0.50 |
| `01000816`-`01000818` | MGEF | `RadSurvival_Fever1`-`3` | Peak Value Modifier on DamageResist (`0002E3:Fallout4.esm`) |
| `01000819`-`0100081B` | MGEF | `RadSurvival_Wasting1`-`3` | Peak Value Modifier on SpeedMult (`0002DA:Fallout4.esm`) |
| `0100081C`-`0100081E` | SPEL (Ability) | `RadSurvival_FeverSpell1`-`3` | Constant Effect, self, magnitudes -10/-25/-40 |
| `0100081F`-`01000821` | SPEL (Ability) | `RadSurvival_WastingSpell1`-`3` | Constant Effect, self, magnitudes -5/-10/-15 |
| `01000822` | GLOB (float) | `RadSurvival_Risk` | 0..1 exposure pool, mirrored for the HUD |
| `01000823` | GLOB (float) | `RadSurvival_Disease` | `DiseaseId * 10 + DiseaseStage`, 0 while healthy |

The quest VMAD carries one script, `RadSurvivalQuest`, with object properties `RadSurvival_Hunger`, `RadSurvival_Thirst`, `RadSurvival_Sleep`, `RadSurvival_Risk` and `RadSurvival_Disease`, each bound to its own `GLOB` record (`0x01000801`-`0x01000803`, `0x01000822`, `0x01000823`). The plugin declares `Fallout4.esm` as its single master (index 0) and its own records sit at master index 1, so their full form ids are `0x010008xx`. The record set is authored with FO4IDE: `add_masters` for the master, `create_record` for the records, `add_script`/`add_script_property` for the VMAD structure and `set_script_value` for the bound property values; `deploy/Scripts/Source/User/RadSurvivalQuest.psc` only reads the properties.

**Penalties.** `RadSurvivalQuest.psc:ApplyStagePenalty` resolves one ability spell per pool per stage (`HungerPenaltySpellBase` 2061 = `0x80D`, thirst 2064 = `0x810`, sleep 2067 = `0x813`, +0/+1/+2 for stages 1-3), removes whichever spell `AppliedPenalty` says is on the player, adds the new one and records it. Stage 0 removes without adding, so relief clears the penalty and a stage change can never stack a second ability. `PenaltiesEnabled` (default on) short-circuits the lookup, and `ClearAllPenalties` runs whenever `IsActive` is false, so a suspended layer (disabled, or vanilla Survival taking the player back) cannot leave a Rad penalty behind. `ResyncPenalties` recounts the stages from the pools on `OnInit` and `OnPlayerLoadGame`; because the applied spells are saved state, an unchanged stage does nothing.

**Why spells and not actor values.** `ModValue`/`DamageValue` writes do not survive a load the way a spell does, and they fight vanilla Survival on the same actor values. Ability spells are the engine's own channel: one constant-effect ability carries one Peak Value Modifier, and removing the spell removes the modifier. The visible levers are the ones the pools describe: carry weight for hunger, max Action Points for thirst (the engine has no per-actor AP-regen actor value, so the budget is what shrinks), and the healing-rate multiplier for exhaustion. Magnitudes are deliberately small at stage 1 and stop well short of crippling at stage 3; retune `tools/rad_penalty_spells.json` and apply it with `set_magic_effect` rather than editing records by hand, and `tests/verify_survival.py` will fail if plugin and manifest diverge.

**Reconcile every call (`RadSurvivalQuest.psc:UpdatePenaltyStages`).** Until this change the function only called `ApplyStagePenalty` when the computed stage differed from the stored one, which lost the spell in two ordinary cases: `ClearAllPenalties` removes it while the stage trackers keep their old values, so a suspend and resume at an unchanged stage never re-applied it, and `PenaltiesEnabled = false` did not clear a live penalty until the pools crossed another boundary. `UpdatePenaltyStages` now reconciles all three pools unconditionally; `ApplyStagePenalty` compares the wanted spell against `Applied*Spell` and returns without touching the actor when they match, so an unchanged stage costs no form lookup and no actor traffic at all: `RadSurvivalQuest.psc:PenaltySpellFor` memoises the spell per pool and stage, only a stage change or a load resolves again, and `ClearPenaltyCache` runs on every load so a retuned table is picked up. Without that memo a single tick cost twelve `GetFormFromFile` calls, because `AddDebt` reconciles per pool change and the tick reconciles once more; the failed-resolve branch deliberately does not cache, so a plugin that is missing when the stage is first reached is retried instead of being written off for the session. `OnTimerGameTime` calls it after the active/suspended branch, so a difficulty flip or a flipped switch is noticed on the next tick (0.25 game hours) rather than hours later, and the stage trackers survive only to decide the stage-increase notifications - a re-applied stage stays quiet. `tests/verify_survival.py` pins the shape that makes this safe: exactly one `ApplyStagePenalty` call per pool at function indent, no stage inequality guard in the function, and the reconcile call after the tick's branch.

**Authoring the effect list.** Every record, including each spell's effect entry, is authored in FO4IDE. Build 1ee68cf38 exposes the list: `add_magic_effect`/`set_magic_effect`/`remove_magic_effect`/`get_magic_effects` accept SPEL/ALCH/ENCH/INGR and an MGEF form key, and `save_plugin` preserves `EFID`/`EFIT`. The earlier binary patcher (`tools/apply_spell_effects.py`, which appended the missing 28-byte entry with a script because build `f594af075` had no writable path) is retired. The evidence that the native path is equivalent: after rewriting all nine effect entries through `set_magic_effect` with their existing values, a save produced a byte-identical 3897-byte plugin (`cmp` clean against `cd1eed9`), and a second save that also raised `MGEF\DualCastingScale` to 1.0 changed exactly eighteen bytes, two per magic effect, with no size change.

**Test-side reader.** CI has no FO4IDE, so `tests/plugin_reader.py:parse` is the test tree's own read-only reader (TES4/GRUP/record layout, subrecord lookup, EditorId and form-id helpers). `tests/verify_survival.py` uses it to check every MGEF DATA field (flags, archetype at +64, actor value at +68, casting type at +80, `DualCastingScale` at +112), every ability's `SPIT`, and every `EFID`/`EFIT` pair against `tools/rad_penalty_spells.json`, which stays the tuning and contract source of truth. Nothing in that reader writes; re-authoring happens through FO4IDE.

**Two FO4IDE quirks.** `create_record` leaves `MGEF\DualCastingScale` at 0 while vanilla ability effects use 1.0; the field is now exposed, so all nine are set to 1.0 (harmless for a constant-effect ability, but the shape now matches vanilla). Separately, `HEDR::numRecords` counts groups as well as records (26 for 22 records; it was 6 for 4 before the SPEL/MGEF set). Engine and xEdit ignore it, so it is reported upstream rather than hand-patched; a future FO4IDE build that fixes it needs a re-save, not a rebuild.

**HUD.** Every pool change is mirrored into the matching `GLOB` through `GlobalVariable.SetValue`, so PR #71's native HUD can read the pools as `TESGlobal` with no Papyrus-to-native bridge.

**Tuning.** Code constants mirror `needs.json` `hourlyRates` (hunger 0.05, thirst 0.10, sleep debt 0.025 per game hour); `tests/verify_survival.py` fails if the two diverge.

## Diseases: prerequisite findings before implementation (2026-10-26)

Reference: `RadSurvivalQuest.psc:OnItemEquipped`, `RadSurvivalQuest.psc:OnPlayerWaitStop`, `tests/verify_survival.py`, `tools/rad_penalty_spells.json`.

**The record tool blocks both exposure and cure as specified.** FO4IDE build `1ee68cf38` authors records through `create_record`, whose supported types are ARMA, ARMO, CELL, EXPL, EYES, FACT, FLST, GLOB, HAZD, LVLI, LVLN, LVSP, MESG, MGEF, PROJ, QUST, RACE and SPEL. Three gaps matter here: there is no `KYWD` type, so a Rad keyword set cannot be minted; there is no form-list entry tool, so a `FLST` is created with an EditorID and can never hold items; and `ALCH` is not creatable. `search_records` for `ObjectTypeFood` returns exactly one keyword (`055ECC:Fallout4.esm`) - vanilla has no raw, cooked, tainted or irradiated variant - so the planned classification (vanilla keywords plus a Rad keyword set carried by a Rad FLST) has no authorable basis, and attaching a new keyword to vanilla food would mean overriding those records anyway.

**The cure path collides with HC_*.** The vanilla disease cure is `HC_Antibiotics` (`0008AB:Fallout4.esm`), an `HC_` form this layer is not allowed to read or write, and `RadiationOverhaul.esp` already overrides that same record, so reusing the item as a Rad cure would both break the HC_* rule and fight an existing override. Detecting a doctor visit has the same problem (vanilla Survival owns it), and a cure spell is not an item a player can carry.

**Resolution.** The layer was built on the reduced route instead of waiting on the tool: exposure is ambient risk scaled by how run down the three pools are, and the cure is a long rest. Both need only records the tool can author (MGEF/SPEL/GLOB) and events the script already handles, and neither depends on an item list that could not be authored. The taint shortlist was dropped entirely rather than hardcoded: a list of vanilla form ids would rot with every food mod, and there is no way to verify it in game from here. If FO4IDE later gains `create_record KYWD`, form-list entries and `create_record ALCH`, a tainted-food category and a carried cure item can be layered on top of this state machine without changing it - the risk pool, incubation, stages and immunity stay as they are.

## Disease layer: ambient risk, three stages, rest cure (2026-10-26)

Reference: `RadSurvivalDiseases.psc:AccrueRisk`, `RadSurvivalDiseases.psc:AdvanceDisease`, `RadSurvivalDiseases.psc:CureDisease`, `RadSurvivalQuest.psc:TickDiseases`, `deploy/RadSurvival.esp`, `tools/rad_diseases.json`, `tests/verify_survival.py`.

**Two scripts, one quest.** The layer lives in `RadSurvivalDiseases.psc:Tick` attached to the same QUST record as `RadSurvivalQuest.psc:OnInit`, because this repository caps a source file at 750 lines and the quest script was already the larger half. The split is by responsibility, not by line count: `RadSurvivalQuest` keeps every event registration and the three pools, `RadSurvivalDiseases` keeps the disease state and the rules, and the quest script reaches it through one `Diseases` property bound to its own quest form. The four delegates (`TickDiseases`, `RestCureDiseases`, `ReconcileDiseases`, `ResyncDiseases`) return early when that property is unbound, so a plugin whose VMAD lost the entry degrades to "no diseases" instead of failing on a None call. The disease script registers for nothing - one owner per event - which `tests/verify_survival.py` enforces by asserting its source contains no `RegisterFor`, and the VMAD is checked for script ownership: the two disease mirrors and their form ids must sit after the `RadSurvivalDiseases` entry, not in the quest script's block.

**Model.** Two illnesses, `fever` (`DamageResist`) and `wasting` (`SpeedMult`), each with three stages of one constant-effect ability. `DiseaseStage` 0 means incubating: the illness is live but nothing is applied yet, which is what makes the notice at onset actionable instead of cosmetic. Escalation is a single ladder from onset (`DiseaseProgress` in game hours): stage 1 at 4h, stage 2 at 10h, stage 3 at 24h, one step per elapsed interval.

**Exposure.** `AccrueRisk` adds `HourlyRisk` (0.012) plus `RiskPerWeakStage` (0.008) per combined pool stage of hunger, thirst and sleep debt, so a healthy survivor needs roughly 83 game hours of exposure while a starving, parched, exhausted one needs about 12. Risk does not build while an illness is live, and the pool drains at the base rate during immunity. Every elapsed interval feeds it, because `Accrue` is the single path used by the tick, sleep and wait - which is why `TryRestCure` and `AdvanceDisease` hang off the same function and the script needed no new event plumbing.

**Why ambient and not per-item.** Vanilla exposes exactly one food keyword (`ObjectTypeFood`), the record tool cannot mint a `KYWD` or fill an `FLST`, and overriding vanilla food records to add a taint keyword would collide with every load-order neighbour. Ambient risk is a rule the script can state exactly and a test can pin, rather than a guess about which of a thousand items counts as raw.

**Cure and immunity.** A single uninterrupted sleep of `RestCureBaseHours + DiseaseStage` hours (4h incubating, up to 7h at stage 3) cures, opens a 24-hour immunity window (`ImmunityUntil` in game days) and clears the risk pool. An interrupted night cures nothing, which keeps the mechanism distinct from waiting. `ImmunityUntil` is game time, so it survives a load without a real-time anchor.

**State and versioning.** The quest keeps `DiseaseId`, `DiseaseStage`, `DiseaseCount`, `DiseaseProgress`, `RiskDebt`, `ImmunityUntil` and `AppliedDiseaseSpell` as saved script state. `EnsureStateVersion` migrates a save made by the penalty-only build once (there is no disease state to preserve) and records `CurrentStateVersion = 2`; `ResyncDiseases` clamps the loaded state, then reconciles the ability, so a save that loads mid-illness gets its spell back without re-notifying. A new game is isolated by construction: a fresh quest instance starts at `SavedStateVersion` 0.

**Gating.** `RadSurvivalDiseases.psc:Reconcile` applies the ability only when the caller reports an active layer, `DiseasesEnabled` is on and the stage is past incubation, and the game-time tick calls `ReconcileDiseases` next to the penalty reconcile, so switching to vanilla Survival strips the disease ability at the same tick as the penalties and switching back restores it. `AdvanceDisease` also refuses to run while suspended, so an illness cannot progress during the interval Rad is not in control.

**HUD contract.** `RadSurvival_Risk` mirrors the pool directly. `RadSurvival_Disease` packs the state as `DiseaseId * 10 + DiseaseStage` (fever incubating 10, fever stage 3 13, wasting stage 2 22, healthy 0), so the native HUD can show a name and a severity from one `TESGlobal` without a Papyrus-to-native bridge.

**Untested in game.** The magnitudes, the risk pace and the 4h/10h/24h ladder are conservative starting values, not measured ones. Nothing in this repository can execute Papyrus, so the contracts cover record shape, mirror wiring, gating and the presence of every rule; how it feels is an in-game acceptance item.

## Session review: the load handler, and HUD chips that stay quiet (2026-10-26)

Reference: `RadSurvivalQuest.psc:OnInit`, `RadSurvivalQuest.psc:OnPlayerLoadGame`, `hud_needs.html:renderNeeds`, `hud_filter.html:renderFilter`, `hud_climate.html:renderClimate`, `hud_status.css`.

**The load handler had never run.** Four consecutive sessions in `Papyrus.0.log` show two `[RadSurvivalQuest] ready at game day` traces each and not one `post-load anchor` line. The handler was present and correctly shaped, but `OnPlayerLoadGame` is a remote event declared on `Actor.psc:1000`, not a quest-native event, so a script that implements `Event Actor.OnPlayerLoadGame(Actor akSender)` compiles to `remote_Actor_OnPlayerLoadGame` and only fires if something registered for it. `OnInit` registered `OnItemEquipped` and nothing else, so the branch was dead code that still looked correct in review. Vanilla does the same thing in `Hardcore/HC_ManagerScript.psc`, which is the pattern now copied: register and unregister the event in `OnInit`, and re-arm it inside the handler itself. The practical cost of the gap was the state the handler owns and `OnInit` does not: `ClearPending` and the sleep/wait anchor reset. A consumption candidate armed before a save kept its `PendingForm` and `PendingCount` across the load, so the next tick could compare an inventory count taken on the far side of a save and credit relief for something the player never ate. `tests/verify_survival.py` now asserts the registration in both bodies, which is the assertion that would have caught this originally - checking that a handler exists is not checking that it is wired.

**Chip visibility has to be a view decision.** `RadHudNeeds.cpp:PushNeeds` calls `api->Show(_needsView)` on every changed payload, and the filter and climate views are shown the same way, so the DLL always has those views displayed. Making them appear only when a stat needs attention therefore cannot live in C++ without a rebuild and a new artifact; it lives in the views, which are the only part that can be shipped as files. `hud_needs.html:renderNeeds` hides a chip below stage 1 and reveals it at the same 34/67/90 boundaries `RadSurvivalQuest.psc:StageFor` uses, so what appears on screen is exactly the pool that already has a penalty applied - `warn` while the first-stage ability is on the actor, `danger` from stage 2. `hud_filter.html:renderFilter` shows the row only when the air is contaminated, the mask has failed, or charge/condition sits below 100, which is what stops a permanently-worn mask from pinning three chips to the screen forever. `hud_climate.html:renderClimate` shows the cluster only when the body is chilled or worse, wetness reaches 50, ambient is at or below 0, or wind reaches 14, and dims the chips inside the cluster that are fine so the offending one is the only bright thing in it. With nothing to report the root carries no visible class and the transparent view holds no pixels. `tests/status_hud_tests.js` pins the transitions in both directions: below the first boundary every chip is hidden and the root is not visible, a single chip past 34 reveals the row, clean air with a fresh mask keeps the filter row off, and a nominal climate leaves the root class empty.

**Icons.** The hand-drawn 24-grid paths are replaced with Tabler outline art (MIT), credited in `THIRD_PARTY_NOTICES.md`, drawn at one stroke weight by `hud_status.css` so the chips read as a set instead of six unrelated glyphs. Two chips had also been sharing a glyph: `WARM` and `TEMP` in `hud_climate.html` both used the thermometer, and core warmth now has its own mark. Neither change needs the DLL rebuilt: the views and their stylesheet are plain files in the mod folder.

## Exposure on hit and in water (2026-10-26)

Reference: `RadSurvivalQuest.psc:OnHit`, `RadSurvivalQuest.psc:OnPlayerSwimming`, `RadSurvivalQuest.psc:ArmPlayerEvents`, `RadSurvivalDiseases.psc:QualifiesAsWound`, `RadSurvivalDiseases.psc:AddRisk`, `RadSurvivalDiseases.psc:ExposureSwim`, `tools/rad_diseases.json`.

**Both events are vanilla, and both have a trap.** `RegisterForHitEvent` is consumed by the delivery that answers it: `ScriptObject.psc` states that objects without registrations cannot receive hit events, and vanilla `Hardcore/HC_ManagerScript.psc` re-arms its own registration at the end of its `OnHit` handler. Our handler therefore re-registers before it filters anything, or the layer would go deaf after the first hit the player ever takes - the same shape of bug as the unregistered `OnPlayerLoadGame` found in the logs earlier the same day. `OnPlayerSwimming` is the opposite problem: it repeats for as long as the player is in water, so vanilla throttles it on game time, and `ExposureSwim` does the same with `SwimIntervalGameDays`. The throttle is saved state (`NextSwimAllowed`) in game days, so quitting and reloading mid-swim cannot be used to re-trigger it.

**Filtering lives in the script, because it cannot live in the registration.** Vanilla filters its hit event with `DiseaseRiskCombatantFactions`, and this mod has no `FLST` it can fill - the record tool creates one with an EditorID and no entries. So `QualifiesAsWound` filters on what it can see: the target must be the player, the aggressor must be an actor other than the player, and a spell source is skipped. That keeps radiation cloaks, self-inflicted explosions and falling damage out, while unarmed creature melee still counts, which a naive "source must be a Weapon" test would have excluded. Blocked and power attacks are not distinguished, matching vanilla.

**Amounts.** A qualifying hit is worth `RiskPerHit` (0.01) behind a two-second real-time cooldown; an automatic weapon can otherwise deliver dozens of hits in a second and would fill the pool instantly. Vanilla charges 0.05 per hit with no cooldown, against a roll threshold of 0.25 - the per-hit figure here is deliberately smaller because this pool triggers outright at 1.0 instead of rolling against it. Swimming is worth `RiskPerSwim` (0.03) per 0.1 game days, which is vanilla's own swimming pair copied unchanged. Both numbers are conservative starting values and are in `tools/rad_diseases.json`, where `tests/verify_survival.py` checks them against the script.

**One rule, three sources.** Both events and the ambient tick answer to `AddRisk`, which applies the same gates everywhere: the layer must be active, `DiseasesEnabled` on, no illness already live, and an immunity window absorbs exposure instead of pausing it. That is what stops a hit taken while the layer is suspended at difficulty 6 from starting an illness, and it is why the exposure path could be added without touching the escalation or cure logic - a full pool still enters the same `StartDisease` and the same 4h/10h/24h ladder.