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

- `src/RadEngine_Storm_Weather.cpp:RadiationEngine::SpawnDistantNuke`: each eligible attempt uses the player heading plus a uniform ±25° yaw jitter and a uniformly selected 3,500–5,000 unit distance, then resolves ground height and submits a native explosion at `groundZ + kNukeGroundClearance`. The earlier note here described a no-jitter 10,000–22,000 unit placement, which the shipped code never used; the placement maths is `src/RadCorrectnessPolicy.h:DistantNukePlacementXY`, the two constant ranges sit in `src/RadEngine_Storm_Weather.cpp:SpawnDistantNuke`, and `tests/verify_hud_fallback.py` only asserts the helper is called instead of reimplemented inline.
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

Reference: `deploy/Scripts/Source/User/RadSurvivalQuest.psc`, `deploy/Scripts/RadSurvivalQuest.pex`, `deploy/RadSurvival.esp`, `tests/verify_survival.py`, `tests/verify_survival_consumption.py`.

**Decision.** Hunger, thirst and sleep debt are a vanilla Papyrus quest script. No script extender is used and there is no native needs clock, but **the F4SE plugin (RadiationOverhaul) must be loaded**: the quest calls its native `GetReliefFlags` relief bridge for the `[Relief]` INI extras, and without the DLL loaded those extras stay off and each ingestible equip logs one unbound-native error (the vanilla-keyword pools still work).

**Why Papyrus, not native.** The native needs runtime could not prove two signals: exactly-once food/drink consumption, and real sleep versus wait with an interrupted flag. PR #70 shipped with "no sleep recovery hook until sleep is proven" for that reason. The shipped Creation Kit base sources expose both to Papyrus, and vanilla Survival itself consumes them: `HC_ManagerScript.psc:50,1167` registers and handles `Actor.OnItemEquipped` for food and drink; `:2618-2671` handles wait and sleep with `abInterrupted`; `:15` gates on `Game.GetDifficulty() == 6`. Rebuilding that natively would be an inference from `SleepWaitMenu`, bed furniture and calendar delta, not proof.

**Why not Hydra (the removed script extender).** It was removed for four reported reasons: a 1.0.2 runtime; an out-of-bounds read in its string handling (missing null terminator); fatal-load and `REL::ID` failures on current runtimes; and an open "Var[] corruption" report in the same variant subsystem the needs state would have been stored in. These are upstream reports, not re-verified in this repository - TODO-verify each against the runtime issue tracker before citing any of them as confirmed. Any one of them is a crash or data-loss risk on the storage path itself.

**Why not Garden of Eden.** `DrinkPotion` is an action that deliberately does not equip, and `OnEquipUnequip` duplicates vanilla `OnItemEquipped`. It adds a dependency without a new signal.

**Event wiring.** `OnInit` arms `RegisterForPlayerSleep()`/`RegisterForPlayerWait()` (unregister first, so a surviving instance cannot double-deliver), registers the remote `OnItemEquipped` and `OnItemRemoved` events with an inventory filter, anchors the clock and starts the game-time timer. `OnPlayerLoadGame` re-anchors, clears the consumption candidates and the sleep/wait trackers, re-registers the remote events, and restarts the timer.

**Consumption (2026-10-28 rewrite).** The previous design armed a candidate on `Actor.OnItemEquipped` with `GetItemCount` and confirmed on a later tick by watching that count fall. That is timing-dependent: if the engine consumes an aid item before the equip event is delivered to Papyrus, the armed count is already the post-consumption count and the drop is never observed. The tester report ("food/drink never relieves hunger/thirst") is consistent with that, and the previous note's claim that the shipped base sources declare no `OnItemAdded`/`OnItemRemoved` was wrong - `ObjectReference.psc:1047` declares `Event OnItemRemoved(Form, int, ObjectReference, ObjectReference)`. The rewrite applies relief on the engine's own removal instead: `OnItemRemoved` is registered remotely and relief runs only when `akDestContainer == None` (not a transfer into a container) and an `OnItemEquipped` for the same form arrived within `PairWindowSeconds` (2.0 s real). Whichever of the two arrives second credits exactly one use, so the equip/removal ordering cannot lose it, a failed equip cannot credit it, and a drop followed by eating the same item cannot credit the dropped stack size. The `None` destination does not prove a consumption - a world drop also reports a null destination - only the equip pairing separates a drop from a use, which stays an in-game check. The filter is `AddInventoryEventFilter(None)` on this script, because `ScriptObject.psc:39` states that objects without a filter receive no inventory events; vanilla uses the same quest-script pattern for remote inventory events (`MQ206Script.psc:590` registers `OnItemAdded` the same way). Pairing state is saved script state but is cleared in `OnPlayerLoadGame`, and the 2 s window is the only timing state left. `Debug.Trace` markers `consume-arm equip`, `consume-confirm remove` and `consume-apply` are emitted on every step, so a Papyrus.0.log proves the path next time. `tests/verify_survival_consumption.py` pins the handler signatures, the filter and remote registration, the pairing helpers, the absence of the count-drop path, the load-time candidate clear and the trace markers; `tests/verify_survival.py` keeps only the one-line pairing and no-`GetForm` cross-checks to stay inside the 750-line source limit.

**Food/drink classification coverage.** `IsFoodOrDrink` classifies by the four vanilla ObjectType keywords - `ObjectTypeFood` 0x00055ECC, `ObjectTypeNukaCola` 0x00249F30, `ObjectTypeDrink` 0x000F4AEC, `ObjectTypeWater` 0x000F4AED - and `ObjectTypeAlcohol` 0x0010C416 scales hydration; all five EditorIDs were verified with FO4IDE. The keywords are cached after the first look-up, so per-event classification no longer calls `GetFormFromFile`. Against the tester's `FWHardcore_Food.esp` (read with `tests/plugin_reader.py`): 113 of its 123 ALCH records carry at least one of the four keywords (109 ObjectTypeFood, 4 ObjectTypeWater, 1 ObjectTypeDrink); one record carries only ObjectTypeChem, a chem and correctly not food. Remaining gaps: `RefreshingBeverage` (0x00058AB8) and `MS17DeezerLemonade` (0x000E9284) carry only vanilla `HC_SustenanceType_QuenchesThirst` (0x000884), so vanilla Survival would quench thirst and this script does not; `MoldyFood01` (0x001A6397) plus the seven `*Witherd` crop records (0x01000800-0x01000813) carry only `HC_DiseaseRisk_FoodHigh` (0x00089A), which vanilla also uses for disease risk and not for hunger relief, so no hunger credit is expected there. Reading the `HC_SustenanceType_*` keywords would close the two drinks, but the script constraint forbids reading any `HC_*` form; that gap stays a decision for the owner.

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

**HUD.** Every pool change is mirrored into the matching `GLOB` through `GlobalVariable.SetValue` in `RadSurvivalQuest.psc:MirrorToGlobal`, and `src/NeedsRuntime.cpp:Update` reads them back: `RadSurvival_Hunger`, `_Thirst` and `_Sleep` for the three pool chips, `RadSurvival_Risk` and `RadSurvival_Disease` for the disease row. The native side therefore holds no needs state of its own - no clock, no hourly rates, no co-save record - and the globals are the single contract between the two halves.

**Tuning.** `RadSurvivalQuest.psc` owns the rates outright (hunger 0.05, thirst 0.10, sleep debt 0.025 per game hour). The former `needs.json` fed the native clock and was deleted with it; `tests/verify_survival.py` pins the three constants and `tests/verify_needs_globals.py:check` fails if the native reader stops looking up the globals.

**Settings table (live tuning).** `src/RadNeedsTuningPolicy.h:kSpecs` is the single table behind every slider and toggle in the SETTINGS tab; the `Key` enum order is the table order. The first `kPapyrusKeys` (16) entries are read by Papyrus through `RadNeedsTuning.cpp:PapyrusTuningOverride`, bound as `TuningOverride` on both `RadSurvivalQuest` and `RadSurvivalDiseases`; the native returns `value + 1.0` for an override and `0.0` for unset, so an unbound native (plugin not loaded) decodes to unset and the script defaults apply. `RadSurvivalDiseases.psc:DiseasesOn` is the only disease gate (the script property AND the tuned toggle); the per-source risks are read through `Tuned()` at each use. Keys from `kFilterEnabled` on are C++ only: `RadEngine_Mask.cpp:ApplyTuning` overlays them on the engine members after `LoadFilterIni`, and the `tune:` branch of `RadEngine_Debug.cpp` calls it after every change (`LoadFilterIni` on reset, which re-reads the INI defaults). Persistence is the `[Tuning]` INI section; an empty key means unset.

**Climate scaling.** `ClimatePolicy.h:Tuning` carries `coolingScale`, `wetnessGainScale` and `wetnessDryScale` (all 1 by default, which reproduces the previous maths exactly). `ClimateRuntime.cpp` builds it each tick from `TemperatureEnabled`/`ColdIntensity`, `WetnessEnabled`/`WetnessSoakRate` and `WetnessDryRate`; a disabled feature sets its scale to 0 rather than skipping the step, so the HUD state stays consistent.

**Always-show needs.** `RadHudNeeds.cpp:PushNeeds` adds `"always"` from `kAlwaysShowNeeds`; `hud_needs.html:renderNeeds` then shows all three chips at stage 0. The flag is part of the deduped payload, so toggling it re-pushes.

**Settlers.** There is no settler disease or needs mechanic in this mod (the needs and disease systems act on the player only), so a "can affect settlers" toggle has nothing to gate; it needs a new settler-side mechanic first. Ammo weight is not tuned here by request.

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

**HUD contract.** `RadSurvival_Risk` mirrors the pool directly, and `RadSurvival_Disease` packs the state as `DiseaseId * 10 + DiseaseStage` (fever incubating 10, fever stage 3 13, wasting stage 2 22, healthy 0). `src/NeedsRuntime.cpp:Update` decodes both into the needs payload as `risk` (percent) and `disease` (packed), so the exposure and illness chips read the two globals the Papyrus layer writes, still without a Papyrus-to-native bridge.

**Untested in game.** The magnitudes, the risk pace and the 4h/10h/24h ladder are conservative starting values, not measured ones. Nothing in this repository can execute Papyrus, so the contracts cover record shape, mirror wiring, gating and the presence of every rule; how it feels is an in-game acceptance item.

## Session review: the load handler, and HUD chips that stay quiet (2026-10-26)

Reference: `RadSurvivalQuest.psc:OnInit`, `RadSurvivalQuest.psc:OnPlayerLoadGame`, `hud_needs.html:renderNeeds`, `hud_filter.html:renderFilter`, `hud_climate.html:renderClimate`, `hud_status.css`.

**The load handler had never run.** Four consecutive sessions in `Papyrus.0.log` show two `[RadSurvivalQuest] ready at game day` traces each and not one `post-load anchor` line. The handler was present and correctly shaped, but `OnPlayerLoadGame` is a remote event declared on `Actor.psc:1000`, not a quest-native event, so a script that implements `Event Actor.OnPlayerLoadGame(Actor akSender)` compiles to `remote_Actor_OnPlayerLoadGame` and only fires if something registered for it. `OnInit` registered `OnItemEquipped` and nothing else, so the branch was dead code that still looked correct in review. Vanilla does the same thing in `Hardcore/HC_ManagerScript.psc`, which is the pattern now copied: register and unregister the event in `OnInit`, and re-arm it inside the handler itself. The practical cost of the gap was the state the handler owns and `OnInit` does not: `ClearPending` and the sleep/wait anchor reset. A consumption candidate armed before a save kept its `PendingForm` and `PendingCount` across the load, so the next tick could compare an inventory count taken on the far side of a save and credit relief for something the player never ate. `tests/verify_survival.py` now asserts the registration in both bodies, which is the assertion that would have caught this originally - checking that a handler exists is not checking that it is wired.

**Chip visibility has to be a view decision.** `RadHudNeeds.cpp:PushNeeds` calls `api->Show(_needsView)` on every changed payload, and the filter and climate views are shown the same way, so the DLL always has those views displayed. Making them appear only when a stat needs attention therefore cannot live in C++ without a rebuild and a new artifact; it lives in the views, which are the only part that can be shipped as files. `hud_needs.html:renderNeeds` hides a chip below stage 1 and reveals it at the same 34/67/90 boundaries `RadSurvivalQuest.psc:StageFor` uses, so what appears on screen is exactly the pool that already has a penalty applied - `warn` while the first-stage ability is on the actor, `danger` from stage 2. `hud_filter.html:renderFilter` shows the row only when the air is contaminated, the mask has failed, or charge/condition sits below 100, which is what stops a permanently-worn mask from pinning three chips to the screen forever. `hud_climate.html:renderClimate` shows the cluster only when the body is chilled or worse, wetness reaches 50, ambient is at or below 0, or wind reaches 14, and dims the chips inside the cluster that are fine so the offending one is the only bright thing in it. With nothing to report the root carries no visible class and the transparent view holds no pixels. `tests/status_hud_tests.js` pins the transitions in both directions: below the first boundary every chip is hidden and the root is not visible, a single chip past 34 reveals the row, clean air with a fresh mask keeps the filter row off, and a nominal climate leaves the root class empty.

**Icons.** The hand-drawn 24-grid paths are replaced with Tabler outline art (MIT), credited in `THIRD_PARTY_NOTICES.md`, drawn at one stroke weight by `hud_status.css` so the chips read as a set instead of six unrelated glyphs. Two chips had also been sharing a glyph: `WARM` and `TEMP` in `hud_climate.html` both used the thermometer, and core warmth now has its own mark. Neither change needs the DLL rebuilt: the views and their stylesheet are plain files in the mod folder.

## Exposure on hit and in water (2026-10-26)

Reference: `RadSurvivalQuest.psc:OnHit`, `RadSurvivalQuest.psc:OnPlayerSwimming`, `RadSurvivalQuest.psc:ArmPlayerEvents`, `RadSurvivalDiseases.psc:QualifiesAsWound`, `RadSurvivalDiseases.psc:AddRisk`, `RadSurvivalDiseases.psc:ExposureSwim`, `tools/rad_diseases.json`.

**Both events are vanilla, and both have a trap.** `RegisterForHitEvent` is consumed by the delivery that answers it: `ScriptObject.psc` states that objects without registrations cannot receive hit events, and vanilla `Hardcore/HC_ManagerScript.psc` re-arms its own registration at the end of its `OnHit` handler. Our handler therefore re-registers before it filters anything, or the layer would go deaf after the first hit the player ever takes - the same shape of bug as the unregistered `OnPlayerLoadGame` found in the logs earlier the same day. `OnPlayerSwimming` is the opposite problem: it repeats for as long as the player is in water, so vanilla throttles it on game time, and `ExposureSwim` does the same with `SwimIntervalGameDays`. The throttle is saved state (`NextSwimAllowed`) in game days, so quitting and reloading mid-swim cannot be used to re-trigger it.

**Filtering lives in the script, because it cannot live in the registration.** Vanilla filters its hit event with `DiseaseRiskCombatantFactions`, and this mod has no `FLST` filled for it - the record tool can now add form-list entries, but vanilla food carries no raw or tainted keyword to key a list off, so a curated list of vanilla food forms stays a separate decision. So `QualifiesAsWound` filters on what it can see: the target must be the player, the aggressor must be an actor other than the player, and a spell source is skipped. That keeps radiation cloaks, self-inflicted explosions and falling damage out, while unarmed creature melee still counts, which a naive "source must be a Weapon" test would have excluded. Blocked and power attacks are not distinguished, matching vanilla.

**Amounts.** A qualifying hit is worth `RiskPerHit` (0.01) behind a two-second real-time cooldown; an automatic weapon can otherwise deliver dozens of hits in a second and would fill the pool instantly. Vanilla charges 0.05 per hit with no cooldown, against a roll threshold of 0.25 - the per-hit figure here is deliberately smaller because this pool triggers outright at 1.0 instead of rolling against it. Swimming is worth `RiskPerSwim` (0.03) per 0.1 game days, which is vanilla's own swimming pair copied unchanged. Both numbers are conservative starting values and are in `tools/rad_diseases.json`, where `tests/verify_survival.py` checks them against the script.

**One rule, three sources.** Both events and the ambient tick answer to `AddRisk`, which applies the same gates everywhere: the layer must be active, `DiseasesEnabled` on, no illness already live, and an immunity window absorbs exposure instead of pausing it. That is what stops a hit taken while the layer is suspended at difficulty 6 from starting an illness, and it is why the exposure path could be added without touching the escalation or cure logic - a full pool still enters the same `StartDisease` and the same 4h/10h/24h ladder.

## Weather exposure (2026-10-26)

Reference: `RadSurvivalDiseases.psc:ExposureWeather`, `RadSurvivalDiseases.psc:IsRadWeather`, `RadSurvivalDiseases.psc:ResolveRadWeather`, `RadSurvivalQuest.psc:TickDiseases`, `tools/rad_diseases.json`.

**The sky is sampled on the pool tick, not on an event.** There is no weather-change event in vanilla Papyrus, so `ExposureWeather` runs from `RadSurvivalQuest.psc:TickDiseases` with the same elapsed interval the pools just consumed. That interval is the whole exposure: a player who stands in a rad storm for 0.25 game hours is charged `RiskPerRadHour * 0.25`, and one who slept through it is charged for the sleep as well, which matches how hunger and thirst already behave across a wait. An interval of zero adds nothing, so the first tick after a load cannot double-charge.

**Two sky sources, one amount each.** A rad weather is worth `RiskPerRadHour` (0.08/h), which fills the pool from empty in about twelve game hours - a deliberate hard ceiling on loitering in a storm. Any other weather that reports classification `RainClassification` (2, rain or snow) is worth `RiskPerRainHour` (0.02/h), a quarter of the storm rate, so the Commonwealth being wet is a background risk rather than a second storm. `IsInInterior()` gates both: a player under a roof, in a cave or in a vault takes nothing from the sky, which is the only shelter mechanic available without a native line-of-sight test.

**Why the rad set is resolved by file name.** The storms that matter are the five weathers `RadiationOverhaul.esp` forces from `src/RadEngine_Internal.h:kStormTypes` (0x840-0x844), plus vanilla `CommonwealthGSRadstorm` (0x1C3D5E). None of them can be a VMAD property or a form-list entry: `RadSurvival.esp` keeps `Fallout4.esm` as its only master, and adding a second would move every Rad-owned form from master index 1 to 2, breaking the DLL's 0x010008xx HUD contract. `ResolveRadWeather` therefore uses `Game.GetFormFromFile` with the plugin name and caches the result in `RadWeatherForms`; `Resync` drops that cache on load so a changed load order is picked up, and an empty resolve is not cached, so a missing plugin is retried rather than remembered as "no storms ever".

**It is the same rule, not a fourth one.** Both branches call `AddRisk`, so the active-layer gate, `DiseasesEnabled`, the live-illness check and the immunity window apply exactly as they do to hits and swimming, and a full pool enters the same `StartDisease` ladder. `tests/verify_survival.py` pins the two branches (two `AddRisk` calls, no private pool), the interior guard, the classification threshold, the cached set and the manifest parity for the four ids and two rates.

## Taint shortlists and the carried cure (2026-10-26)

Reference: `RadSurvivalDiseases.psc:ConsumeItem`, `RadSurvivalDiseases.psc:ExposureCategory`, `RadSurvivalDiseases.psc:ExposureConsume`, `RadSurvivalDiseases.psc:IsCureItem`, `RadSurvivalDiseases.psc:TraceTaintLists`, `RadSurvivalQuest.psc:ApplyRelief`, `RadSurvivalQuest.psc:OnItemEquipped`, `deploy/RadSurvival.esp`, `tools/rad_diseases.json`, `tests/verify_survival.py`, `tests/verify_survival_diseases.py`.

**This section supersedes "Why ambient and not per-item" and the "Resolution" paragraph above.** Both were written when FO4IDE could not author the records: the ambient-only route and the rest-only cure were the reduced design. The build of 2026-10-26 14:48 can mint `KYWD`, fill an `FLST` and create an `ALCH`, so the item-driven route the earlier note called "layered on top later" is now the shipped one. The ambient, hit, swim and weather paths are unchanged; taint consumption is a fifth caller of the same `AddRisk` rule, and the rest cure is still there beside the item.

**Record table.** All nine records are Rad-owned in `RadSurvival.esp`; no vanilla record is overridden and `Fallout4.esm` stays the only master.

| Record | Form-ID | Contents |
| --- | --- | --- |
| `KYWD RadSurvival_Keyword_TaintedFood` | `0x01000824` | category 1, raw food |
| `KYWD RadSurvival_Keyword_TaintedWater` | `0x01000825` | category 2, tainted water |
| `KYWD RadSurvival_Keyword_Contaminated` | `0x01000826` | category 3, contaminated source |
| `KYWD RadSurvival_Keyword_CureItem` | `0x01000827` | opt-in cure marker |
| `FLST RadSurvival_TaintedFood` | `0x01000828` | 8 entries: Molerat, Radroach and Mirelurk meat, Brahmin and Radstag meat, Mutfruit, Tato, Dogfood |
| `FLST RadSurvival_TaintedWater` | `0x01000829` | 3 entries: Dirty Water, Drugged Water, Dirty Wastelander |
| `FLST RadSurvival_Contaminated` | `0x0100082A` | 5 entries: Glowing Fungus, Brain Fungus, Glowing One Blood, Glowing Blood Pack, Moldy Food |
| `MGEF RadSurvival_CureEffect` | `0x0100082B` | Script archetype, flags 2562, silent: the ingestible's required effect entry |
| `ALCH RadSurvival_CureItem` | `0x0100082C` | one effect (the MGEF above), value 125, weight 0.5, Medicine flag |

**Why three lists and not one.** A `FormList` carries no per-entry data, so a single list cannot say *which* category an item belongs to, and the items are vanilla, so the category cannot be a keyword on them without overriding food records. One list per category is the only shape where the record says both "these are tainted" and "this is how tainted". The keywords carry the same three categories for a different purpose: an item that is not in any list opts into one by carrying the keyword, which is the extension point an add-on or a later Rad-authored ingestible uses, and it keeps the category vocabulary a record rather than a string in the script.

**Consumption is confirmed by the removal event, not by a count drop (2026-10-28 rewrite).** `OnItemEquipped` cannot tell a use from an equip, so the quest pairs it with the engine's own `ObjectReference.OnItemRemoved` event (`RadSurvivalQuest.psc:PairEquip`, `:PairRemove`, `:PruneConsumptionCandidates`): a removal counts only when `akDestContainer == None`, the item is a consumable by the cached keyword rule or the INI bridge, and an equip for the same form arrived inside `PairWindowSeconds` (2 s). Whichever event arrives second credits exactly one use (`ApplyRelief(akItem, 1)`), so the equip/removal order cannot lose it, a container transfer cannot credit it, a failed equip cannot credit it, and a drop followed by eating the same item cannot credit the dropped stack size. The old count-drop design armed `GetItemCount` in the equip event and confirmed a later drop, which is timing-dependent and was replaced. The candidate arrays are initialised in `OnInit` via `ClearConsumptionCandidates()` before `ArmPlayerEvents`, and `PruneConsumptionCandidates` re-initialises them when the ready flag is false or either array is None (e.g. a mid-save install), because uninitialised dynamic arrays are None and every pair would silently no-op. The `[Relief]` bridge is one native, `GetReliefFlags` (bit 1 = food, bit 2 = thirst), called only for `Potion` items, so a not-loaded DLL costs one unbound-native error per ingestible equip instead of two per equip event; the INI lists are resolved once per session on the game thread, with malformed and missing entries logged by raw text (a truncated 4 KB list warns as well), so a `[Relief]` edit needs a game restart to take effect. `ApplyRelief` still forwards a confirmed item to `RadSurvivalDiseases.psc:ConsumeItem`, so the cure/taint routing is unchanged: the cure item clears the same state a long rest clears, and anything else charges `RiskPerTaintedFood`/`RiskPerTaintedWater`/`RiskPerContaminated` through `AddRisk`. Proven from the shipped sources: `Actor.OnItemEquipped` fires for ingestibles (vanilla `HC_ManagerScript.psc:1167` relies on it). NOT proven from the Papyrus sources: that a consumed item's removal delivers `akDestContainer == None`; that remains an in-game check (`docs/IN_GAME_TEST_CHECKLIST.md`). A world drop is rejected in practice only through the equip pairing, not through `akItemReference`, so the source comment does not claim otherwise.

**Why the item is resolved by form id.** Writing the `FULL` name and the `KWDA` list was retried against this build and the refusals are now recorded, not assumed. `list_records_summary` for `ALCH` exposes `EDID`, the typed `ALCH\*` columns and the raw `Authoring\*` columns, and no name column at all - so the documented `EYES\Full` route has no `ALCH` counterpart here; `set_field` refuses `ALCH\Full`, `ALCH\FULL`, `ALCH\Name`, `FULL` and `Authoring\FULL` with `FieldWriteUnsupported: Native Radicle editing supports EDID plus typed EYES and LVSP fields only`, and `create_record` silently ignores `name`/`fullName` for `ALCH`. The keyword list is a form-link list: `ALCH\Keywords[0]` is recognised and refused with `FormLinkListIndexOutOfRange` because the list holds no elements, `ALCH\Keywords` on the whole list is refused as unsupported, and no tool appends a keyword - so `KWDA` can be edited on a record that already has keywords but never grown from none. The shipped ingestible therefore has no display name and carries no keyword and still passes `--check`; `IsCureItem` resolves it with `Game.GetFormFromFile(2092, "RadSurvival.esp")` and accepts `KeywordCureItem` from an item the Creation Kit authors later. In game it appears as an unnamed Aid entry, and the notification path is the feedback: `CureDisease` announces the recovery, and a cure consumed while healthy now answers "nothing to cure" instead of doing nothing at all. This is an upstream FO4IDE gap, filed with the `HEDR` `numRecords` quirk and the recorded-source-path limitation above.

**No HC_* form is referenced, including by list membership.** `HC_SippableDirtyWater` was the one `HC_*` form the water shortlist carried - a read-only reference, but the constraint says never touch an `HC_*` form, and a reference is a dependency. It was removed through `remove_form_list_entry` (index 1), leaving Dirty Water, Drugged Water and Dirty Wastelander; Dirty Water alone already covers the sippable case the vanilla list existed for, and `tools/rad_diseases.json` carries a `constraint` note so a future entry cannot reintroduce one.

**The cure test is gated before it pays anything.** `IsCureItem` now casts to `Potion` first, so every weapon, armour and junk equip - which `OnItemEquipped` sees all day - returns before the `GetFormFromFile` lookup; only an ingestible reaches the form-id and keyword tests. `tests/verify_survival.py` pins the cast, the form id, the keyword and the "nothing to cure" notification.

**Fallback when a list is missing.** `ListHas` treats an unbound or empty list as "not tainted" rather than failing, so a plugin whose VMAD lost a list charges nothing for that category instead of charging the wrong one, and `TraceTaintLists` names the gap once on every resync. The risk pool still fills from the ambient, hit, swim and weather paths, so the layer degrades instead of going silent. The list ids, entries, keyword ids, per-category amounts, cure item ids, value, weight, effect archetype and flags all live in `tools/rad_diseases.json`. Two contracts read that manifest, split because `tests/verify_survival.py` sits at the repository's 750-line cap: `tests/verify_survival_diseases.py` parses the shipped plugin with `tests/plugin_reader.py` and checks each `KYWD` editor id, each `FLST`'s `LNAM` list against the manifest order, the `ALCH` effect/value/weight, the effect's flags and archetype at the documented `DATA` offsets, and the seven VMAD bindings on the disease script; `tests/verify_survival.py` checks the script's use of the lists and the cure keyword, the three per-category amounts and `CureItemFormId`. Both run in the build and release workflows. The VMAD is decoded, not grepped: `tests/plugin_reader.py:vmad_properties` walks the v6 table (script name, flags, property count, then per property name, type and value) and returns `{script: {property: form id}}`, so a binding is compared as a property-to-value pair and a form id that merely appears elsewhere in the block cannot satisfy a check. That is also what lets `tests/verify_survival.py` assert the cross-script link the cure depends on - `RadSurvivalQuest.Diseases == 0x01000800` - which a byte search could not tell apart from the quest form id appearing anywhere else in the record.

**Authoring note for the next record.** FO4IDE's MCP host drops the open plugin after a slow call: `create_record` with form-list entries or a magic-effect entry sometimes times out and still lands the record, and the next call answers `PluginNotLoaded`. The safe loop is create, then `save_plugin`, then re-`open_plugin`, and never to retry a timed-out write before checking `list_record_types`.

## Shipped-plugin validation findings (2026-10-26)

Reference: `deploy/RadSurvival.esp`, `deploy/RadiationOverhaul.esp`, `deploy/RadOverhaulSurPatch.esp`, `tests/verify_av_ownership.py`, `docs/ESP_BUILD_PIPELINE.md`.

FO4IDE's contract checker (`FO4RecordEditor.exe --check <plugin> --scripts <dir> --json`) is run on the three shipped plugins. Two report findings and one is clean; the split below is the decision for each, with the command that reproduces it.

| Plugin | Findings | Verdict | Action |
| --- | --- | --- | --- |
| `RadSurvival.esp` | none (one `MASTER_CHECK_SKIPPED` info: the CLI has no active profile) | clean | none |
| `RadiationOverhaul.esp` | 10 x `EFFECT_LIST_EMPTY` on `RadOverhaul_Stage100`-`Stage1000`; 1 x `HEDR_RECORD_COUNT_MISMATCH` | first is real and deliberate; second is a check-context artifact | keep, documented below |
| `RadOverhaulSurPatch.esp` | 82 x `SCRIPT_MISSING` over two `SMO:` scripts; 1 x `HEDR_RECORD_COUNT_MISMATCH` | both are check-context artifacts | keep, documented below |

**`RadiationOverhaul.esp` empty abilities are the AV ownership placeholder.** The ten `RadOverhaul_Stage*` spells carry a `SPIT` and no `EFID`, which is exactly what `EFFECT_LIST_EMPTY` reports: granted by `RadOverhaul_Perk100`-`Perk1000`, they modify nothing. Populating them would name an actor value, and that value then has a second Rad owner next to the survival layer - the decision `tests/verify_av_ownership.py` exists to intercept (it fails the moment one of them gains an effect). They stay empty until that decision is made; the check finding is the cost of shipping the placeholder rather than deleting the perks.

**`RadOverhaulSurPatch.esp` script findings are the absent dependency.** All 82 are the namespaced scripts `SMO:HC_SMO_Effect_DiseaseHandler` (81) and `SMO:DiseaseEffects:HC_SMO_VoicesEnd` (1). The `SMO:` namespace belongs to `Survival Mode Overhaul`, which is this plugin's second master (`Fallout4.esm`, `Survival Mode Overhaul.esp`) and is not installed in TMR, so the compiled `.pex` files are not on any script path the checker is given - pointing `--scripts` anywhere in this repo cannot satisfy them. The plugin therefore ships disabled; enabling it without that master crashes the load, and that is why `plugins.txt` leaves it out.

**`HEDR_RECORD_COUNT_MISMATCH` is the known counting quirk.** `--check` compares `HEDR::numRecords` against its own records-plus-groups total: it reports 238 for a total of 237 in `RadiationOverhaul.esp`, and 116 against 117 in `RadOverhaulSurPatch.esp`. `RadSurvival.esp`, which FO4IDE itself saved, is clean, so the writer and the checker agree on their own model and the two older plugins disagree with it by one either way. The field is ignored by the engine and rewritten by xEdit on load, so it is reported upstream with the other FO4IDE quirks rather than hand-patched.

Reproduce:

```
FO4RecordEditor.exe --check deploy\RadSurvival.esp --scripts deploy\Scripts --json
FO4RecordEditor.exe --check deploy\RadiationOverhaul.esp --scripts deploy\Scripts --json
FO4RecordEditor.exe --check deploy\RadOverhaulSurPatch.esp --scripts deploy\Scripts --json
```

## Third-party gas masks: the `[Masks]` contract (2026-10-26)

Reference: `src/RadEngine_Mask.cpp:IsRecognizedGasMask`, `src/RadEngine_Mask.cpp:LoadMaskConfig`, `src/RadEngine_Mask.cpp:ResolveMaskEntry`, `src/RadCorrectnessPolicy.h:IsConfiguredMask`, `src/RadiationEngine.h:_extraMaskKeywords`, `deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini`, `tests/verify_mask_extras.py`, `tests/correctness_policy_tests.cpp`.

**The problem is a keyword we do not own.** Every protective path in the engine - the mask screen overlay, the filter drain, the condition/repair system, the choking damage and the rad-exposure resistance scale - hangs off `IsRecognizedGasMask`, and that rule is "fills the MOUTH biped slot AND carries `FARO_GasMask` (`RadiationOverhaul.esp` `0x809`)". That keyword is ours, and it is applied only to the eight vanilla armors we patch. A mask added by another mod therefore equips and renders but protects nothing: `Gasmask Overhaul` (Nexus 108845) adds `Armor_Metro_PPM-88_Gas_Mask` (`0x01011004` in `Gasmask Overhaul.esp`, mouth+eye slots, 50 rad resist) with its own `ma_armor_Metro_PPM-88_Gas_Mask` keyword (`0x01011001`) and no `FARO_GasMask`. Copying their record to add the keyword is not an option: we do not override another author's gear, the record has to be copied whole to stay faithful, and this FO4IDE build cannot grow a `KWDA` list on a record that has one (see the taint section).

**The fix is declaration, not authoring.** `deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini` gains a `[Masks]` section with two comma-separated keys, each entry written `Plugin.esp:0xLOCALID` - the form id as the plugin stores it, not the load-order id:

- `ExtraKeywords` adds a mask *keyword*. An armor that fills the MOUTH slot and carries that keyword is a mask.
- `ExtraMasks` adds an armor *record*. A listed armor is a mask whatever biped slot it fills, which is what covers an eye-slot layout such as Gasmask Overhaul's optional Overlay Framework patch.

`RadEngine_Mask.cpp:LoadMaskConfig` runs next to `LoadFilterIni` at engine init, resolves every entry through `TESDataHandler::LookupForm` (the same helper the HUD globals use), and type-checks it: a keyword entry must be a `KYWD`, an armor entry an `ARMO`. An absent plugin, a missing form or a wrong type is logged once and skipped, so a load order without the mod - or with the mod disabled - leaves the vanilla rule untouched instead of failing. The rule split lives in `RadCorrectnessPolicy.h:IsConfiguredMask(listed, mouth, keyword)`: `listed || IsGasMask(mouth, keyword)`. Keeping it in the policy header is what lets `tests/correctness_policy_tests.cpp` pin the truth table (listed-and-slotless is protective, mouth-without-keyword is not) without an engine.

**Defaults shipped.** The INI declares both PPM-88 entries - `Gasmask Overhaul.esp:0x011001` as a keyword and `Gasmask Overhaul.esp:0x011004` as an armor - so the mask is protected under either slot layout and nothing has to be edited when the optional patch is added. Both entries are inert when the plugin is missing.

**What this does not cover.** The INI is read at engine init, so editing it needs a game restart, and a load-order change is picked up on the next launch rather than mid-save. Gasmask Overhaul ships its own F4SE plugin (`FO2287LiteWipe.dll`) for equip/remove animations, visor glass and droplets; that is independent of the protection state, but the two systems both react to a mask being equipped, so in-game acceptance has to watch for doubled equip foley (ours plays `Sound\FalloutAnomaly\mask\*`) and for their `G` equip/remove hotkey fighting our auto-equip. `tests/verify_mask_extras.py` pins the INI entries, the native parsing, the type checks and the policy split; CI runs it with the other mask contracts.

## Gap closure: obtainable cure, retunable rates, inferred masks (2026-10-26)

Reference: `RadSurvivalDiseases.psc:IsCureItem`, `RadSurvivalDiseases.psc:RadAwayFormId`, `src/RadEngine_Mask.cpp:LoadMaskConfig`, `src/RadEngine_Mask.cpp:HasRadResistance`, `src/RadCorrectnessPolicy.h:IsRadShieldedMask`, `deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini`, `tests/verify_survival.py`, `tests/verify_mask_extras.py`.

**The cure is obtainable now, and it is a vanilla item.** The authored `ALCH` cannot be named or keyworded by this FO4IDE build (see the taint section), so it can never be crafted, sold or picked up - the recovery path was the console. `RadSurvivalDiseases.psc:IsCureItem` therefore also accepts **vanilla RadAway** (`0x023742:Fallout4.esm`, resolved through `RadAwayFormId`): a named item that is already craftable at a chemistry station and carried by doctors and vendors. Both items route through the same `CureDisease` path and the same 24-hour immunity; the authored item stays as a second path for a future tool that can name it. The manifest records the extra cure id under `taint.cure.extraCureFormIds`, and the disease contract asserts both the constant and the `radAway` lookup.

**Retuning no longer needs a Papyrus compile.** All 28 tuning values - 12 on the quest script (pool rates, food/drink relief, tick length, hysteresis) and 16 on the disease script (risk rates, timing, immunity, per-category taint risk) - moved from `Auto Const` properties to plain `Auto` properties and are now authored as **float properties in the quest VMAD** (`RadSurvivalQuest` indices 4-15, `RadSurvivalDiseases` indices 9-24). The `.pex` keeps the same initialisers, so behaviour is identical; the difference is that the VMAD value wins and is editable in FO4IDE or xEdit with no compiler involved. `tests/verify_survival.py` now reads the values out of the VMAD and requires each declaration to stay non-const, so a future edit cannot silently bake the number back in.

**A mask mod needs no INI entry any more.** `[Masks]` gained `InferFromRadResist=1` and `MinRadResist=10`: `src/RadEngine_Mask.cpp:HasRadResistance` reads the armor's radiation damage-type resistance (`0x00060A85` in `armorData.damageTypes`) and `RadCorrectnessPolicy.h:IsRadShieldedMask` accepts any MOUTH-slot armor above the threshold as a gas mask. The explicit list and keyword paths still win, so the declared masks and a keyworded add-on keep working; the inference is what makes an arbitrary mask mod protective without a record edit or a config line. Setting `InferFromRadResist=0` restores the strict list-only behaviour.

**A spent or broken mask is recoverable out of the box.** The shipped defaults were hostile: a 300-second filter with `ReplaceKey=0` and `RepairKey=0` meant a mask could die with no way back except editing the INI or the debug menu. Defaults are now `DurationSeconds=1200` (20 minutes of wear), `ReplaceKey=117` (F6) and `RepairKey=118` (F7); the mask contract fails if any of them regresses to 0. F8 stays reserved for the debug menu.

**Correction: permanent tissue damage IS curable.** The earlier note that it had no cure was wrong. `RadOverhaul_ChelationCompound` (`RadiationOverhaul.esp` `0x860`) runs the verified chelation signal (`RadCorrectnessPolicy.h:CuresPermanentTissue`, `RadEngine_Tissue.cpp:HealPermanentTissue`), and `deploy/F4SE/Plugins/RobCo_Patcher/leveledList/RadOverhaul_ChelationCompound_Loot.ini` distributes it when RobCo Patcher is installed. RadAway still heals only reducible tissue, and no doctor path exists - that part is by design, because a doctor cure would have to touch the vanilla `HC_*` disease state this layer is forbidden to own.

**Survival Mode Overhaul is gone from the Fallen World list.** Its folder moved out to `D:\Games\FallenWorld\_smo_removed_20261026` and its `modlist.txt` entry was deleted (profile backups `*.20261026-smo-removed.bak`). Nothing else had to change: every plugin that declares `Survival Mode Overhaul.esp` as a master sits in an `optional` subfolder and none was ever enabled, so the load order is unaffected. The Rad layer's own Survival gate is untouched - at difficulty 6 it still yields, so Very Hard remains the mode that exercises it.

## Actor-value ownership (2026-10-26)

Reference: `RadSurvivalQuest.psc:ApplyStagePenalty`, `RadSurvivalQuest.psc:UpdatePenaltyStages`, `RadSurvivalDiseases.psc:Reconcile`, `tools/rad_penalty_spells.json`, `tools/rad_diseases.json`, `tests/verify_av_ownership.py`.

Every effect this layer applies is a constant-effect ability carrying one peak-value modifier, and modifiers on the same actor value add together. Two Rad systems on one actor value would therefore stack without a message, without a log line, and with no way to tell from the game which one is responsible. The partition below is the contract: one owner per actor value, and `tests/verify_av_ownership.py` fails if a second one appears.

| Actor value | Sole Rad owner | Carrier | Stage 1 / 2 / 3 |
| --- | --- | --- | --- |
| `CarryWeight` (`0002DC:Fallout4.esm`) | hunger penalty | `RadSurvival_HungerPenaltySpell1`-`3` | -10 / -25 / -45 |
| `ActionPoints` (`0002D5:Fallout4.esm`) | thirst penalty | `RadSurvival_ThirstPenaltySpell1`-`3` | -5 / -15 / -30 |
| `HealRateMult` (`000358:Fallout4.esm`) | exhaustion penalty | `RadSurvival_SleepPenaltySpell1`-`3` | -0.15 / -0.30 / -0.50 |
| `SpeedMult` (`0002DA:Fallout4.esm`) | wasting | `RadSurvival_WastingSpell1`-`3` | -5 / -10 / -15 |
| `DamageResist` (`0002E3:Fallout4.esm`) | fever | `RadSurvival_FeverSpell1`-`3` | -10 / -25 / -40 |

**Vanilla Survival is the other owner, and it gets the whole set back.** The five actor values above are also what `HC_ManagerScript.psc` moves, so the layer yields all of them together rather than trying to share one. `IsActive()` false - the enable switch, or difficulty 6 - reaches `RadSurvivalQuest.psc:UpdatePenaltyStages`, which calls `ClearAllPenalties`, and the same tick calls `ReconcileDiseases`, which passes `abActive` into `RadSurvivalDiseases.psc:Reconcile` and so removes the disease ability too. Both sets therefore leave the actor on the first tick after the gate closes (0.25 game hours), not at the next stage boundary, and the pools stop advancing so nothing rebuilds behind the gate. Nothing in either layer writes an actor value directly - `ModValue` and friends appear nowhere in `src/` or in the scripts - which is what makes the removal exact: the spell is the only carrier, and removing it removes the modifier.

**The base plugin's radiation stages own nothing today.** `RadiationOverhaul.esp` ships ten perks (`RadOverhaul_Perk100`-`RadOverhaul_Perk1000`) that grant ten ability spells (`RadOverhaul_Stage100`-`RadOverhaul_Stage1000`), and every one of those spells has a `SPIT` but no `EFID`: they are empty abilities, so they modify no actor value and cannot stack with anything. If they are ever populated - the radiation-sickness debuffs they were clearly meant to carry - they become a second Rad owner of whichever actor value they name, and the ownership table is the place to decide whether that value moves to them. `tests/verify_av_ownership.py` fails the moment one of those spells gains an effect, so the decision cannot be skipped by accident.

## Papyrus build gate: the stamp instead of a compile (2026-10-26)

Reference: `tools/stamp_papyrus.py`, `tests/verify_papyrus_pex.py`, `deploy/Scripts/papyrus-build.json`, `RadSurvivalQuest.psc:OnInit`, `.github/workflows/rad-overhaul-build.yml`.

**Why CI cannot just compile.** The survival layer ships as committed bytecode, and the compiler that produced it is FO4IDE's built-in Papyrus engine inside `FO4RecordEditor.exe` - a Windows binary the MSVC build runner does not have. So the workflows cannot run the tool the artifacts actually need. Running nothing was the previous state, which is how a `.psc` edit could sit in the tree with bytecode sixteen commits old and still be green.

**What the gate does instead.** `tools/stamp_papyrus.py` runs immediately after a compile and writes `deploy/Scripts/papyrus-build.json`: for each script the source hash, the bytecode hash and the source string the bytecode records. `tests/verify_papyrus_pex.py` recomputes the hashes, requires every `.psc` in `deploy/Scripts/Source/User` to have a stamp entry and every `.pex` to have one too, and refuses bytecode that names a source other than its own - the shape a stale or crossed build shows. Source hashes normalise line endings, because the checkout is CRLF on Windows and LF in CI and a byte-for-byte hash of the working copy would otherwise differ by platform. The engine is deterministic, so the bytecode hash is meaningful: recompiling an unchanged source reproduced both survival `.pex` files byte for byte.

**The recorded source path, which could not be removed.** The built-in engine writes the source path it was given into the `.pex` header at offset 16 - the leading string of the string table, before the script's own name. Compiling with the documented absolute source argument therefore produces `E:\Projects\Forgejo\...\deploy\Scripts\Source\User\<Script>.psc` inside the shipped bytecode. Three attempts to avoid it: a repo-relative source argument is canonicalised back to an absolute path; compiling from a staging folder with `cwd` set there records the staging folder instead, so the absolute path merely changes address; compiling a folder records the folder. There is no flag on `compile_papyrus` for it, and editing the string is exactly the binary patcher that was retired. The stamp therefore records the string each `.pex` carries, so a build made on another machine or with a toolchain that stops writing it fails the check rather than drifting, and the limitation is stated in `docs/ESP_BUILD_PIPELINE.md`. This is an upstream FO4IDE item, filed with the `HEDR` `numRecords` quirk.

**Wiring.** Both Rad workflows now run the four survival contracts (`verify_survival.py`, `verify_needs_globals.py`, `verify_av_ownership.py`, `verify_papyrus_pex.py`); the build workflow already ran `verify_survival.py` and the release workflow ran it too, so the additions sit next to them rather than in a new lane.
## Wave 3 module notes moved out of the source (2026-10-26)

Reference: `src/RadEngine_ESL.cpp:RadiationEngine::EnsureEslLoaded`, `src/RadEngine_Session.cpp:RadiationEngine::OnPostLoad`, `src/RadEngine_Session.cpp:RadiationEngine::RevertState`.

The split of `src/RadiationEngine.cpp` moved these two blocks into their own translation units.
Repository policy keeps explanation out of source, so the per-site rationale that travelled with
them is recorded here instead, in the order it appeared. Nothing below is a new claim; it is the
same text, relocated so the source carries only constraints.

### `RadEngine_ESL.cpp`

- Report the plugin's load-order slot. By design (see gen_esl.py) the plugin
- ships as a REGULAR full ESP — not ESL/light — so it loads LAST and wins
- conflicts over the rest of the load order. LookupForm() below resolves forms
- correctly regardless of slot type, so a full plugin is the expected case.
- Check the light index first: an ESL plugin reports a real light index AND the
- shared full index 0xFE, so testing the full index first would misclassify it.

### `RadEngine_ESL.cpp`

- (The radiation-sickness screen tint is an HTML overlay now; the engine IMAD 0x830 is no
- longer resolved or triggered — the record remains in the ESP, unused.)

### `RadEngine_ESL.cpp`

- Resolve the rad-weather FormList (optional). Drives the auto-mask-equip feature: when the
- current weather is in this list, a worn mask is auto-donned. Data-driven so the rad-weather
- set is editable in xEdit without a rebuild.

### `RadEngine_ESL.cpp`

- Resolve the gas-mask filter consumable (optional). Enables the filter system: the mask drains
- this while worn, and replacing it consumes one from the player's inventory.

### `RadEngine_ESL.cpp`

- and auto-equip fail closed instead of treating bandanas or cosmetic mouth armor as respirators.

### `RadEngine_ESL.cpp`

- Resolve the gas-mask repair kit consumable (optional). Repairs the mask's condition.

### `RadEngine_ESL.cpp`

- Resolve the persistent tissue-damage global (Pillar I). Seed our cached value
- from it so accumulated tissue damage survives save/load.

### `RadEngine_ESL.cpp`

- Permanent (scarred) tissue global — no doctor or item cure path is active until a verified
- source-identifying adapter is implemented.

### `RadEngine_ESL.cpp`

- Storm FX quest — Papyrus script drives lethal-storm camera/controller shake (stage 10/20).

### `RadEngine_ESL.cpp`

- NOTE: EnsureQuestRunning is currently a no-op (the VM Quest.Start() path crashed — see
- its body). StartGameEnabled starts these quests on a NEW GAME automatically; on saves
- made before the quests existed they stay dormant until we add a crash-safe start path.

### `RadEngine_ESL.cpp`

- Fire hazard object for Firestorm ring spawn (from base game — always present).
- DN121FireHazard: a large fire hazard that spawns fire particles + light + damage.

### `RadEngine_ESL.cpp`

- Storm dome layers: our scriptless FARO_StormDome01..04 ACTI copies (0x805-0x808), meshes bundled
- (KORs_Anomaly_CloudArena01, which carries KORsCloudArenaActScript + defaultdisablehavokonload)
- natively from the 4Hz worker-thread hook crashed the game in BSScript VM ObjectBindPolicy +
- Havok behavior-graph binding (same worker-thread VM hazard that killed SetStage). The clouds are
- lit by a separately-spawned scriptless arena LIGHT instead (see _stormDomeLight below) — no VM.

### `RadEngine_ESL.cpp`

- Arena lights for the effect-lit cloud shapes (scriptless LIGH). These were historically resolved
- from "KORsAnomaly Stripped.esp" (ArenaLight01 0x025334, ArenaLight02 0x02626D), which made KORs a
- hidden runtime requirement. The two LIGH records (plus their GodRays/LensFlare sub-records) are now
- owned by RadiationOverhaul.esp itself — copied in with permission — so KORs is no longer required.
- BOTH lights together make the glowing vortex (broad illumination + bright lens-flare eye).

### `RadEngine_ESL.cpp`

- Storm weathers (forced sky per type) — optional; storms still run with just the
- screen FX if a weather record is missing.

### `RadEngine_ESL.cpp`

- Per-storm fog, written to the record (the engine re-reads fogData live each frame in
- Sky::UpdateFog, so this fully controls the on-screen fog). Distance fog (near/far/power)
- sets the visibility wall; height fog (mid/range/HighDensityScale) gives real ground-fog
- depth instead of the flat "painted" tint the generated records shipped (all height fields
- were 0). The Dust Storm gets the CommonwealthDusty full-height-wall profile; the roll-in
- (UpdateDustFogRoll) animates it inward from clear at Active-entry.

### `RadEngine_ESL.cpp`

- Dust Storm: attach the vanilla camera-dust particle VisualEffect (the screen-filling
- blowing dust that actually reads as "blinding" — fog alone is only a haze). This is the
- weather's referenceEffect (record field "VisualEffect", TESWeather+0xFC0); vanilla
- CommonwealthDusty uses this exact effect. Set at runtime (no ESP/master-order surgery) so
- the engine attaches it when the dust sky goes current. FXCameraAttachExplosionDustVE
- (Fallout4.esm 0x1EB2D6), flags AttachToCamera + InheritRotation.

### `RadEngine_ESL.cpp`

- Heavy camera-dust ArtObject for the WALL-ARRIVAL BURST: a short (~4s) heavier dust
- gust fired on the player (attach-to-camera) the moment the roll-in wall reaches full,
- layered over the steady dust VE. Nuka World's DLC04CameraAttachDustHeavyAO; fall back
- to the vanilla FXCameraAttachExplosionDust art if Nuka World isn't present.

### `RadEngine_ESL.cpp`

- PURPLE-SKY FIX: the generated dust weather inherited the CommonwealthGSRadstorm base
- template's per-time IMAGESPACES (0x224586-0x224593 — blue/purple rad-storm colour
- grades). An imagespace tints the WHOLE scene, so it rendered purple regardless of the
- brown sky colours, and disabling our IMAD never touched it. Repoint all 8 time-of-day
- slots to vanilla CommonwealthDusty's brown dust grades (CW_Dusty*). imageSpace[] order
- = {Sunrise, Day, Sunset, Night, EarlySunrise, LateSunrise, EarlySunset, LateSunset}.

### `RadEngine_ESL.cpp`

- AURORA CLEAR — ALL STORMS (2026-07-12): every generated storm inherited the radstorm
- template's AURORA, Sky\RadStormSkyEffect.nif (TESWeather+0xF78) — a full-sky-dome shimmer
- effect that renders OVER the sky colours and doesn't fit any of these storms (fire/ash/
- sulfur/acid/dust). On dust it read as a muddy dome (the "purple"); on the others it makes
- the transition look less clean than dust. Clear it so each storm's sky is just its (correct)
- colours + fog + IMAD. Dust proved this reads clean.

### `RadEngine_ESL.cpp`

- ENGINE CRASH GUARD (confirmed via crash-2026-07-07-18-21-18): Sky::UpdateSound
- carries a Thunder-type sound. Our generated storm weathers ship ThunderFreq (weatherData
- index 0xA) == 0, so that integer modulo divides by zero on the BSJobs audio worker thread
- -> EXCEPTION_INT_DIVIDE_BY_ZERO the moment the storm's sky becomes current. The dust storm
- hit it first; ash/sulfur/acid/firestorm carry the same bad byte and are equally at risk.
- Clamp any zero to a high non-zero value: the divisor is now safe, and a high frequency
- makes real thunder ~never fire (these are dust/ash/chem storms, not thunderstorms).

### `RadEngine_ESL.cpp`

- TRANSITION SPEED (weatherData[kTransDelta], TESWeather+0xA6F). Decompile-verified in
- `weatherData[3]/255 * (transMax - transMin) + transMin` in-game hours — i.e. this byte is a
- normalized 0-255 knob for how LONG our BeginSmoothWeather cross-fade takes. The ESL
- generator left it at whatever it happened to be, so the five storms rolled in at random,
- inconsistent speeds. Author it deterministically so every storm's sky rolls in at the same
- deliberate pace, and so it matches the ~8 s dome/siren onset sequence. Small byte = fast.
- (Exact transMin/transMax engine constants aren't named yet, so this is tuned by feel, not
- converted from a target duration — but it is now CONSISTENT and controllable, which it
- was not before.) See ReverseEngineering\Organized\1.10.163\README-Weather.md.

### `RadEngine_ESL.cpp`

- Four weatherData[] bytes drive the global shader wind, all normalized 0-255 (GetWeatherData
- rule) and all weather-transition-blended like fog:
- [0]    kWindSpeed      -> BSShaderManager::fWindMagnitude (cloud scroll + shader wind)
- [0x13] kWindTurbulence -> fWindFrequency / fWindMinSpeed / fWindMaxSpeed (gust freq + variance)
- [0x11] kWindDir        -> fWindAngle (base direction)
- [0x12] kWindDirRange   -> direction jitter range
- Our generated storm records left these unauthored, so the storms were DEAD CALM — clouds
- didn't scroll and there was no shader wind, which reads badly for what are literally
- wind-driven dust / ash / chem storms. Author them by theme: the dust storm HOWLS, the
- others scale down. weatherData is int8_t but the engine reads each byte UNSIGNED, so store
- the byte pattern of the 0-255 value. Indices are decompile-verified; the magnitudes are a
- design knob (transMin/transMax aren't named, so tuned by feel — but consistent now).

### `RadEngine_ESL.cpp`

- README-Weather.md). The weather's camera-attached VisualEffect (our Dust Storm's
- referenceEffect = FXCameraAttachExplosionDustVE, the blowing-dust motes) does NOT spawn at
- transition start: the engine waits until `currentWeatherPct >= weatherData[kBeginEffect]/255`
- to instantiate it, and tears the outgoing one down at `>= weatherData[kEndEffect]/255`. Both
- are per-record CK bytes our generator left unauthored — so the dust VE appeared at a random
- point in the roll-in (or, if the byte was high, barely at all). Author them so the camera
- dust rolls in EARLY with the storm and clears promptly when it ends. Harmless on storms
- with no referenceEffect (nothing to gate). Indices are CommonLibF4-named + decompile-confirmed.

### `RadEngine_ESL.cpp`

- Commonwealth clear weather (vanilla) — forced once on a new game's first exterior so the
- player never spawns into a rad storm (vault exit / alt-start). Matches console `fw 0002b52a`.

### `RadEngine_ESL.cpp`

- Gas Masks of the Wasteland dust/sand storms (+ vanilla CommonwealthDusty). Not radioactive, but
- they look like a storm and the region can roll one at spawn. We resolve their runtime FormIDs so
- the new-game grace / clear-on-entry safety treat them as "bad weather to suppress at startup".
- Absent GMW, the lookups just fail and the set stays empty (no effect).

### `RadEngine_ESL.cpp`

- Next-storm global — used only as an "is the storm system installed" gate; the actual
- schedule (next-storm time + the _playSeconds play-clock) persists across saves via the F4SE
- after a load is exactly what starved the mod's own storms (they never reached their trigger).

### `RadEngine_ESL.cpp`

- Console debug-trigger global (optional). set RadOverhaul_ForceStorm to 1-4 in-game.

### `RadEngine_ESL.cpp`

- Storm atmosphere IMADs (0x880-0x884), one per storm type, persistent color grading.

### `RadEngine_ESL.cpp`

- Feral ghoul leveled lists from Fallout4.esm — spawned during lethal storms.

### `RadEngine_ESL.cpp`

- ActorTypeGhoul keyword — used to spare/heal ghoul NPCs during storms (they thrive in rads).

### `RadEngine_ESL.cpp`

- ArmorTypePower keyword — marks PA armor pieces. A full PA suit fully blocks rads + storm HP loss.

### `RadEngine_ESL.cpp`

- WorkshopNPC faction — settlers; spared from the lethal-storm peak burst.

### `RadEngine_Session.cpp`

- ---------------------------------------------------------------------------
- STORM SYSTEM — our self-contained versions of NAC's rad storms.
- Lethal blowouts (Ash Fallout, Firestorm) + ambient hazards (Sulfur, Acid Rain).
- ---------------------------------------------------------------------------

### `RadEngine_Session.cpp`

- GameDaysPassed global is the reliable game-time source. (Calendar::rawDaysPassed
- reads 0 in practice on this runtime, which broke the old scheduler.)

### `RadEngine_Session.cpp`

- Runs on new game and before each load: clean defaults so a fresh game / a save with no
- co-save data starts the clock at 0 rather than inheriting the previous game's state.

### `RadEngine_Session.cpp`

- Vanilla HUD corner message — works with or without PrismaUI. throttle=true so a
- repeated message doesn't spam the queue.

### `RadEngine_Session.cpp`

- Arm the one-shot clear-weather safety; fired by UpdateStorm on the first exterior tick.

### `RadEngine_Session.cpp`

- Re-seed tissue fields from globals on every load so switching saves doesn't
- carry the previous run's in-memory values into a fresh character's session.

### `RadEngine_Session.cpp`

- Reset the delta baseline and any verified RadAway window so a cross-save rads/HP change
- cannot receive a stale cure signal.
- -1 sentinel: also skips the sheltered rad-gain refund on that first tick, which otherwise
- read the entire saved rad pool as "gain since last tick" and erased it whenever the player
- loaded in power armor / a powered shelter / sheltered rad weather.

### `RadEngine_Session.cpp`

- Re-sync the gas-mask fade tracker so an already-worn mask on load syncs silently (no blink),
- and cancel any pending fade-back from the previous session.

### `RadEngine_Session.cpp`

- Choking trackers are session-local: reset so a different save doesn't inherit the previous
- one's grace/hysteresis state (e.g. skipping the 2s grace on its first exposure).

### `RadEngine_Session.cpp`

- Re-sync the rad-weather auto-equip tracker so loading into rad weather syncs silently (no
- auto-equip on load); we only act on weather transitions during play. Clear the NPC mask set
- too — actor pointers/handles do not carry across loads, and the re-scan will repopulate it.

### `RadEngine_Session.cpp`

- The engine clears kTemporary AV modifiers on load, but our trackers persist in the DLL singleton.
- Reset them so the radiation SPECIAL penalty re-applies fresh next tick (ApplyClampedSpecial) — a
- stale non-zero tracker would otherwise match `desired` and silently skip re-applying the penalty.

### `RadEngine_Session.cpp`

- Superseded for the tissue-wasting penalties (carry/heal/speed/AP) by the "Carry-weight leak" section at the end of this file: they are now persisted and restored, and `RadiationEngine::RepairLeakedCarryWeight` drops them again if the game shows no matching carry modifier. END/STR trackers are still zeroed on load.

### `RadEngine_Session.cpp`

- Force-heal any SPECIAL stat that loaded in below the game floor of 1 (corruption / foreign
- negative modifiers — the reported "-10"). Safe: only ever touches a genuinely broken stat.

### `RadEngine_Session.cpp`

- NOTE: quest force-start is done in EnsureEslLoaded (after the quests resolve). OnPostLoad
- runs BEFORE that lazy ESL load on first load, so the quests are still null here.

### `RadEngine_Session.cpp`

- Storm: a forced weather override never survives a load, so abandon any ACTIVE storm event
- (reset the phase to Idle, clear its FX/refs). We deliberately do NOT touch _stormScheduled /
- _nextStormPlaySec on a normal mid-gap load — the schedule and the _playSeconds play-clock
- persist via the F4SE co-save, so the next storm keeps its place across reloads instead of
- being pushed a fresh 35-90 min into the future every load (which previously meant the mod's
- storms never fired).
- EXCEPTION: if the persisted schedule is already PAST DUE (the save was made during/after the
- storm it triggered), leaving it be would instantly fire a brand-new, differently-typed storm
- on the very first Idle tick after EVERY reload of that save.
- This used to re-arm a deliberately SHORT 4-8 min window ("the abandoned storm returns soon").
- That was the #1 cause of the "storms are back-to-back" reports: storms are lethal, so players
- die -> reload -> the save is past-due -> a storm hits 4-8 min later -> they die -> reload...
- a storm death-spiral. Any save made during/after a storm reproduces it on EVERY load.
- Re-arm the FULL configured gap instead ([Storm] MinMinutes/MaxMinutes) so a reload can never
- produce a short-gap storm.

### `RadEngine_Session.cpp`

- Native dome refs are temporary (not force-persisted), so a load leaves stale handles in our
- vector pointing at refs the engine already dropped — reset the tracker so the next storm starts
- clean. (handle.get() on a dead ref returns null, so this is safe even if the refs are gone.)

### `RadEngine_Session.cpp`

- On the session's FIRST load the quest/IMAD forms are still null (EnsureEslLoaded runs on the
- first Update tick), so the two calls above no-op — latch a re-fire for after form resolution,
- otherwise a save made mid-storm resumes its persisted Papyrus rumble loop with no stopper.

### `RadEngine_Session.cpp`

- NOTE: _stormScheduled / _nextStormPlaySec intentionally preserved (persisted schedule, see above).

### `RadEngine_Session.cpp`

- Sleep / wait healing: measure elapsed game time since the last save loaded.

### `RadEngine_Session.cpp`

- Sanity bounds: ignore sub-30-min loads (fast travel, quick saves)
- and implausible jumps >48 h that suggest a time-skip or first load.

### `RadEngine_Session.cpp`

- Same per-game-hour rate as live rest, gated on current rads.

### RadStormFX Papyrus shake burst

Reference: `deploy/Scripts/Source/User/RadStormFX.psc:_StartShake` and `RadStormFX.psc:DebugNotify`.

- The burst loop was `While (_shaking)` and only `_StopShake` (stage 20) cleared the flag, so `t = (BurstSteps - I) / BurstSteps` went negative after `BurstSteps` iterations and `ShakeCamera`/`ShakeController` were fed negative magnitudes while the rumble timer never started. The loop is now bounded by `I < BurstSteps`, which is what lets the existing `If (_shaking) StartTimer(RumbleInterval, _kRumbleTimer)` run. `DebugNotify` now defaults to false so the on-screen `[StormFX]` notifications are opt-in.
- This repairs the script's own logic only. The Papyrus shake has never run on any save: `SetStage` returned false on all 35 logged attempts (`docs/ISSUES_FIXED.md`), and the native `NativeShakeCamera` path does the shake that actually plays. Do not read this as a live-bug fix.

### Water bottles at water sources

Reference: `src/RadWaterFillPolicy.h:RadWaterFillPolicy::Decide`, `src/RadWaterFillPolicy.h:RadWaterFillPolicy::IsWaterSource`, `src/RadWaterFillPolicy.h:RadWaterFillPolicy::IsMaintainedVaultCell`, `src/RadWaterFillPolicy.h:RadWaterFillPolicy::FilledWaterFor`, `src/WaterBottleFill.cpp:WaterBottleFill::ActivateOnGameThread`, `src/WaterBottleFill.cpp:ActivateSink::ProcessEvent`, `src/WaterBottleFill.cpp:ResolveForms`, `src/WaterBottleFill.cpp:FillOneBottle`.

- Vanilla Survival fills a bottle from the `Activate` entry point of `HC_FillWaterBottlePerk` (Fallout4.esm 0x249F55), whose Papyrus fragment calls `HC_Manager.FillWaterBottle`. This plugin reproduces the decoded part of that interaction natively through a `RE::TESActivateEvent` sink and does not read or write any `HC_*` form. Two of the five perk conditions are undecoded, so this is not an exact vanilla match.
- A water source is a base object in `RadWaterFillPolicy::kWaterSources` — the ten vanilla/DLC activators whose VMAD carries `drinkFromFountainScript`, the same script vanilla casts the activated ref to — or any ACTI/FURN carrying the `AnimFurnWaterPump` keyword, which also covers mod-added pumps.
- The empty-bottle list is the 26 items of vanilla `HC_FillableWaterBottlesList` (0x00249F56) captured into `kEmptyBottles`; the form list record itself is never referenced.
- Output rule: Dirty Water (0x000366BF) everywhere, Purified Water (0x000366C0) only when the player's current parent cell is one of the maintained Vault 81 interiors in `kMaintainedVaultCells`. Only `Vault81` (0x02A728) and `Vault81Entry` (0x0A9B62) give clean water; everything else is dirty. The check keys on the parent cell's form ID, never on the location, because `Vault81Ext` (0x00E4D0, Commonwealth grid -10/-6) shares `Vault81Location` (0x02BE8D, parent 0x002CF0) with the interior — a location check would hand out clean water in the wasteland outside the vault. `Vault81Ext`, `Vault81HoldingCell` (0x1126B0, location `Vault81HoldingLocation` 0x225F8A) and `Vault81Secret` (0x05226E, FULL "Secret Vault 81") are not maintained cells and stay dirty. An unresolved cell form, a null parent cell or an exterior parent cell all read as "not Vault 81" -> dirty; the code never fails open to clean.
- The activated source's `spellOnDrink` is never consulted. Vanilla's clean/dirty rule compared it against the `CleanDrinkingFountainSpells` property array on `HC_WaterBottleScript`; the parent cell is the only input now.
- `[Water] RequireModifier=1` (default) requires the player to be sneaking. `ActivateSink::ProcessEvent` reads `Actor::IsSneaking()` and `Decide` returns `kNoModifier` before any item removal, so a normal activation only drinks and never consumes a bottle. This deliberately differs from vanilla Survival: `HC_FillWaterBottlePerk` fills a bottle on a plain activation with no sneak. `RequireModifier=0` restores fill-on-every-activation (not recommended: it takes a listed bottle even when the player only wanted to drink).
- Every fill shows a vanilla HUD message ("Filled a bottle with dirty water." / "Filled a bottle with purified water.") through `RE::SendHUDMessage::ShowHUDMessage`, so a consumed bottle is never silent.
- `Decide` guard order, cheapest first: `kDisabled` (cached INI) -> `kNotWaterSource` (base-form compare) -> `kVanillaSurvival` (difficulty 6, yields to the untouched perk) -> `kNoModifier` (sneak state) -> `kNoEmptyBottle` (inventory count) -> `kFill`. The 26-form inventory count only runs for an activation that already matched a listed source; the item is removed only when `Decide` returns `kFill`.
- `FillOneBottle` re-checks the bottle count after `RemoveItem` and skips the water add when the engine refused the removal, so a refused bottle cannot hand out free water.
- Exactly one bottle is consumed per activation with a 400 ms same-target debounce, which is only a guard against a duplicated activate event. The debounce is stamped in `WaterBottleFill.cpp:FillOneBottle` only after the bottle removal and water add both succeed, so a refused removal does not arm it. That is a deliberate simplification of vanilla: the pump furniture fills repeatedly while the player is held in it, and the native path does not.
- The INI is read once per load (`WaterBottleFill.cpp:LoadConfig`) and the form-resolution summary is logged once (`WaterBottleFill.cpp:ResolveForms`), so activation retries do not hit the disk or repeat the log. `[Water] Enabled=0` turns the feature off before the sink is registered. If a critical form stays unresolved the sink is never registered (single warning); a missing Purified Water or maintained-cell form degrades the output to dirty water only.
- Build-verified only — the hook has not been played in game. On this install play is Very Hard, where the vanilla perk does not fill, so the native path is the only bottle filler; every check below assumes Very Hard. Untested in game: whether `TESActivateEvent` fires once or twice when entering/exiting pump furniture (the debounce is the only guard), whether sneak state reads correctly when activating a pump furniture, whether a quest-flagged empty bottle (e.g. `DN050BabyBottle`) is ever refused by `RemoveItem` (the count re-check then suppresses the water add, but that refusal path itself is unexercised), and whether sneak-activating a drinking fountain also runs the vanilla drink (`spellOnDrink`) in the same press. The last one cannot be cancelled from a `TESActivateEvent` sink: the sink is a post-activation observer and `BSEventNotifyControl` only stops later sinks, not the activation itself, so a sneak-activation may legitimately both apply the drink's rads and fill a bottle. That is an open in-game test item, not a fix. The reporters' Papyrus script errors were also not reproduced locally.


## Gas-mask overlay, auto-don gate and mask hotkeys (2026-10-28)

Reference: `src/RadiationEngine.cpp:RadiationEngine::Update`, `src/RadEngine_Mask.cpp:RadiationEngine::PlayerWearsPowerArmorHelmet`, `src/RadEngine_Mask.cpp:RadiationEngine::ReplaceFilter`, `src/RadEngine_Mask.cpp:RadiationEngine::RepairMask`, `src/RadEngine_Mask.cpp:RadiationEngine::ReloadHotkeysFromIni`, `src/RadCorrectnessPolicy.h:RadCorrectnessPolicy::MaskOverlayVisible`, `src/RadCorrectnessPolicy.h:RadCorrectnessPolicy::RadWeatherExposed`, `src/RadCorrectnessPolicy.h:RadCorrectnessPolicy::IsUsableHotkey`, `tests/correctness_policy_tests.cpp`.

- Overlay: `RadiationEngine::Update` now asks `RadCorrectnessPolicy::MaskOverlayVisible(_maskOverride, realMasked, PlayerWearsPowerArmorHelmet())` before `RadHud::PushMask`. A worn power-armor helmet (`PlayerWearsPowerArmorHelmet`, any ArmorTypePower piece that fills the helmet slot 30, frame or full suit) hides the visor overlay because the PA HUD replaces it; the `mask:on` debug override still forces it, and `realMasked` remains the real worn state for foley/filter/choking.
- CSS: `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css` masks `#maskOverlay` and `#visorFog` with a centre-clear radial gradient so the Scaleform HUD reads through them. Assumption (not guessed coordinates): the vanilla HUD owns the top-left notification area, the bottom-centre XP bar and the centre/lower-centre loot and interaction-prompt readouts, so the middle 55% of the visor is fully transparent and only the edge art is drawn. `tests/status_hud_tests.js` asserts both rules keep a transparent centre. Not verified on a running screen.
- Auto-don: the player's mask is no longer donned on the weather transition alone. `RadiationEngine::Update` computes exposure as rad weather AND not interior AND not sheltered AND not a powered Pulowski shelter AND not a full power-armor suit (`RadWeatherExposed`), then acts only on the exposure transition (`ShouldAutoEquipMask` / `ShouldAutoRemoveMask`). Entering cover removes only the mask we auto-equipped, a manual unequip while exposed is respected, and leaving cover into rad weather re-dons. `_wasRadExposed` starts at -1 after a load so the first tick syncs silently; NPC masking still rides the weather transition.
- Hotkeys: the two binds stay in the INI (`[Filter] ReplaceKey`, `[Condition] RepairKey`). `RadCorrectnessPolicy::IsUsableHotkey` rejects F8 (the debug menu key) and a duplicate of the other bind, and `ReloadHotkeysFromIni` re-reads and revalidates them when the Shift+F8 debug menu is opened, so an INI edit applies without a restart. There is no in-game rebinding screen yet - the Prisma settings view is not a small change, so that stays a follow-up.
- Foreign masks: recognition already accepts INI `[Masks] ExtraKeywords`/`ExtraMasks` plus the mouth-slot-plus-rad-resist inference, and the shipped INI lists Gasmask Overhaul's PPM-88 keyword and armor; the tester log confirms `[Masks] 1 extra keyword(s), 1 listed armor(s), inferFromRadResist=true (min 10 rad resist)`. The hotkeys are not broken by that: they consume only OUR `Gas Mask Filter` / `Gas Mask Repair Kit` records, and another mask mod's filters or repair items are never read, so with Gasmask Overhaul's own mask and filters a press can only report "No spare filters" (or now "No recognised gas mask equipped" when nothing recognised is worn). Gasmask Overhaul's own hotkeys are its own system and stay untouched. `src/RadEngine_Mask.cpp:RadiationEngine::ReplaceFilter` and `:RadiationEngine::RepairMask` now log the refusal reason (no recognised mask worn, or no filter/kit in the inventory), so the next F4SE log shows exactly why a press did nothing.

## Penalty audit: hunger/thirst/sleep vs radiation stages (2026-10-28)

Reference: `tools/rad_penalty_spells.json`, `deploy/RadSurvival.esp`, `src/RadiationEngine.cpp:RadiationEngine::ApplyDebuffs`, `deploy/Scripts/Source/User/RadSurvivalQuest.psc:ApplyStagePenalty`.

- Verified with FO4IDE: the nine penalty magic effects target exactly three actor values - hunger -> AVIF `CarryWeight` 0x0002DC, thirst -> AVIF `ActionPoints` 0x0002D5, sleep -> AVIF `HealRateMult` 0x000358 (stage-1 forms 0x000804 / 0x000807 / 0x00080A inspected: one effect each, magnitudes -10 / -5 / -0.15). `RadSurvival.esp` contains only ALCH, FLST, GLOB, KYWD, MGEF, PERK, QUST and SPEL records, and its six PERKs are Fever/Wasting (DamageResist/SpeedMult), so nothing in the plugin or the disease layer changes a SPECIAL stat.
- The tester's SPECIAL drops are our radiation stage perks, not hunger/thirst: `FalloutAnomaly.log` prints "Applied Penalties: END -1 / STR -0 / STR -1" whenever `RadiationEngine::ApplyDebuffs` swaps the radiation stage perk, and `[SPECIAL] penalty 0 -> 1` comes from the same stage swap. Fallen World's `FWHardcore_Difficulty.esp` may add its own penalties; that is not ours and was not inspected further.
- Leaving vanilla Survival: `RadSurvivalQuest.UpdatePenaltyStages` calls `ClearAllPenalties()` whenever `IsActive()` is false, and `verify_survival.py` pins that the reconcile runs after the active/suspended branch, so our spells neither linger nor double up. Vanilla `HC_*` hunger/thirst spells left on a save moved off Survival are vanilla's own state; this plugin reads and writes no `HC_*` form and cannot clear them.

## Tester items that are not ours (2026-10-28)

Reference: `deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini` [Masks]/[Water] sections, `src/RadEngine_Debug.cpp` (the only kAMMO reference), `src/RadEngine_Tissue.cpp:ApplyTissueEffects` (the only other CarryWeight writer), `f4se.log` load order.

- Bullets having no weight "for Ashfall": no Ashfall plugin is in the tester load order (`f4se.log`) and no source file changes ammo weight - the only `kAMMO` reference is a debug form-type print in `src/RadEngine_Debug.cpp`. `FWHardcore_Weapons.esp` / `FWHardcore_CarryWeight.esp` own that change.
- Power-armor carry weight ignoring harness/vest gear: `FWHardcore_Difficulty.esp` authors the `EnchBackpack_250/1000/2500` enchantments. Our only CarryWeight writes are additive `kTemporary` actor-value modifiers (the hunger penalty spells and the radiation tissue penalty in `src/RadEngine_Tissue.cpp`), which cannot cancel another effect's bonus, and `RadiationEngine::IsPlayerInPowerArmor` only gates rad/storm protection - no power-armor path touches carry weight.

## Interior rain is by design (2026-10-28)

Reference: `src/ClimateRuntime.cpp:ClimateRuntime::Update`, `src/ClimatePolicy.h`, `tests/climate_policy_tests.cpp`.

- `ClimateRuntime::Update` sets `environment.precipitation01 = 0` for every interior cell and `ClimatePolicy` then treats indoors as fully rain-sheltered, so rain never wets the player in an interior, including visually roofless ones such as the Museum of Freedom. The tester log agrees with the design: 1208 interior `[Climate]` lines and zero of them with `rainy=true`, so the interior rule is working as written rather than a broken wetness computation. A data-driven whitelist of roofless interiors (a `climate_weather.json` cell list plus a precipitation factor) is implemented for the one approved cell; see the "Roofless-interior rain whitelist: implemented" section below.


## Needs state is per-save; nothing persists across characters (2026-10-28)

Reference: `deploy/RadSurvival.esp`, `src/RadEngine_Serialization.cpp:RadiationEngine::LoadState`, `src/NeedsSerializationPolicy.h`, `deploy/Scripts/Source/User/RadSurvivalQuest.psc:OnInit`.

- Verified with FO4IDE: the `RadSurvival_Hunger` / `_Thirst` / `_Sleep` GLOBs (local forms 0x000801-0x000803) all store FLTV 0, so a fresh character starts at zero. `HungerDebt`/`ThirstDebt`/`SleepDebt` are ordinary quest-script variables saved with the quest instance, and a new game instantiates a new quest, so nothing carries between characters.
- The native plugin writes no needs payload: `RadiationEngine::LoadState` recognises the legacy `NEED` co-save record and drops it with an info log, and `NeedsSerializationPolicy` keeps only the recognition constant - there is no writer left. `Actor.OnPlayerLoadGame` logs the restored anchors ("post-load anchor H/T/S="), which is the in-game check to run next time.
- So "the needs persist and never lower" is the missing relief from the consumption item, not stale saved state: the pools only ever rose because no food/drink relief was ever credited.

## Gas mask in power armor: what we found (2026-10-28)

Reference: `src/RadEngine_Mask.cpp:RadiationEngine::SetActorMaskEquipped`, `src/RadEngine_Mask.cpp:CaptureDisplacedGear`, `src/Hooks_Input.cpp:RadInputHandler`, `src/RadiationEngine.cpp:RadiationEngine::Update`.

- The plugin's only equip/unequip path for masks is `SetActorMaskEquipped`, which claims ownership in `RadMaskOwnership` before `EquipObject`/`UnequipObject` and releases it on failure. `CaptureDisplacedGear` is used only for the temporary masks handed to NPCs by `EquipNearbyNpcMasks`, so it never moves the player's own gear.
- The player mask fills the MOUTH biped slot while power-armor helmets fill slot 30 (`kHairTop`), so the two do not share a slot. The cases that looked wrong are now gated: the overlay is hidden by a PA helmet, the auto-don skips interiors/shelter/PA, and `Hooks_Input.cpp` only handles Shift+F8 - it contains no mask equip hook at all. Nothing in our code swaps a mask when entering or leaving power armor; Gasmask Overhaul's own scripts and hotkeys are separate and untouched.


## Review round 2: consumption arrays, relief INI bridge, overlay alpha, exposure hysteresis, hotkeys (2026-10-28)

Reference: `deploy/Scripts/Source/User/RadSurvivalQuest.psc:OnInit`, `:OnItemRemoved`, `:PairEquip`, `:PairRemove`,
`:ResolveKeywords`, `src/RadReliefConfig.cpp`, `src/RadReliefPolicy.h`, `src/RadCorrectnessPolicy.h:ParseHotkey`,
`:ValidateHotkeyPair`, `:StepAutoMask`, `src/RadiationEngine.cpp:RadiationEngine::PollInput`,
`src/RadEngine_Mask.cpp:RadiationEngine::ReloadHotkeysFromIni`, `src/RadEngine_Session.cpp:RadiationEngine::ResetSession`,
`tools/bake_visor_clear_zones.py`, `tests/status_hud_tests.js`, `tests/verify_survival_consumption.py`.

- Candidate arrays: declared without initialisers, so `OnInit` calls `ClearConsumptionCandidates()` before
  `ArmPlayerEvents`; `PruneConsumptionCandidates` calls it again when `ConsumptionArraysReady` is false, so a mid-save
  install cannot leave relief dead until a reload. The same guard covers `PairEquip`/`PairRemove`, which call prune
  first.
- Drop vs use: the removal handler rejects `akDestContainer != None`, ammo (every shot fires ammo removal events) and
  non-consumables before `Game.GetPlayer()`/`IsActive()`; only the equip pairing separates a drop from a use, because
  `akItemReference` is not a reliable drop indicator and is not claimed to be one.
- Relief INI bridge: `[Relief] ExtraFoodKeywords`, `ExtraThirstKeywords`, `ExtraFoodItems`, `ExtraThirstItems` are read
  by `RadReliefConfig` (plugin + local id, `RadReliefPolicy::ParseList`), resolved through `TESDataHandler::LookupForm`
  once on the game thread, and exposed to Papyrus as one native, `GetReliefFlags` (bit 1 = food, bit 2 = thirst)
  (`RadReliefConfig::InstallPapyrusBridge`, registered from `F4SEPlugin_Load`). `RadSurvivalQuest.psc` calls it only
  for `Potion` items from `IsConsumable` and `ApplyRelief`; no `HC_*` form is read. The shipped `ExtraThirstItems` entry carries
  `Fallout4.esm:0x058AB8` (RefreshingBeverage) and `Fallout4.esm:0x0E9284` (MS17DeezerLemonade), both vanilla ALCH
  drinks that only carry `HC_SustenanceType_QuenchesThirst` 0x000884 (verified with FO4IDE) and would otherwise get no
  thirst relief. Spoiled food (`MoldyFood01` 0x001A6397, the seven `*Witherd` records 0x01000800-0x01000813) is
  deliberately not listed: vanilla gives those disease risk only, never hunger relief.
- Overlay: `#maskOverlay` and `#visorFog` no longer use `mask-image` (Ultralight support unproven). The HUD clear zones
  are baked into the PNG alpha by `tools/bake_visor_clear_zones.py` (overlay and `visor_fog.png`, referenced from
  `radhud.css`). Review round 3: the bake now reads the pristine `art-src/visor/gasmask_overlay_pristine.png` (from
  `4e4f81b`) and writes the deployed overlay, so it is idempotent; it feathers only the translucent glass and tint
  (alpha below `FRAME_ALPHA`) over the top-left notification area, the centre loot/prompt readout and the lower-centre
  glass, and leaves every opaque frame pixel untouched, so the visor frame is no longer cut open. RGB is zeroed where
  alpha is 0, which shrank the PNG from 2.7 MB to 1.5 MB. Trade-off: the XP bar at the very bottom sits behind the
  opaque bottom-centre frame bump; the 3D camera visor effect removes that overlap. `tests/status_hud_tests.js`
  decodes both PNGs and asserts clear glass points plus opaque frame points.
- Ownership: `RadMaskOwnership.cpp:EquipSink::ProcessEvent` no longer erases ownership on the unequip event itself. It
  stamps `g_pendingUnequip`, a re-equip of the same mask clears it, and `Owns`/`Lookup`/`Snapshot` forfeit the entry
  only after `kUnequipGraceMs` (2.5 s). This is what keeps an auto-donned mask ours when Gasmask Overhaul unequips and
  re-equips it around its draw animation; a manual removal still forfeits ownership after the window.
- Choke grace: `RadCorrectnessPolicy.h:kChokeGraceTicks` is `kAutoMaskStableTicks + 1`, used by
  `RadiationEngine.cpp:RadiationEngine::Update` for the first choke warning, because the old 8-tick grace expired
  before the 12-tick auto-don and produced a gasp on every storm entry. `tests/correctness_policy_tests.cpp` has a
  `static_assert` tying the two.
- Auto-don: `RadCorrectnessPolicy::StepAutoMask` requires `kAutoMaskStableTicks` (12 ticks = 3 s at the 4 Hz update) of
  a stable exposure sample before donning or removing, which stops doorway/roof-edge flapping; a null parent cell
  (load/fast travel) skips the step entirely instead of reading as exterior; `RadEngine_Session.cpp` sets
  `_wasRadExposed = 1` when the ownership table says we auto-donned the player's mask, so it is still ours to remove
  after a load.
- Hotkeys: the INI values are read as strings and parsed by `RadCorrectnessPolicy::ParseHotkey` (decimal or 0x-hex;
  negatives and junk are logged and disable that action). `ValidateHotkeyPair` rejects mouse buttons 1-6, Shift, Esc
  and F8, and disables BOTH keys when they duplicate, so INI order cannot decide. `PollInput` only acts while our game
  window is the foreground window (`GetForegroundWindow` process id) and no blocking menu is on the UI stack
  (`RE::UI::menuStack`, `IMenu::OnStack`, `kPausesGame`/`kUsesCursor`/`kModal`/`kFreezeFramePause`) - proven from
  `RE/U/UI.h:UI::GetMenuOpen`, `RE/I/IMenu.h:IMenu::OnStack` and `UI_MENU_FLAGS`. An in-game rebinding screen is still a
  follow-up; INI edits apply when the debug menu is next opened. Review round 3: `RadiationEngine.cpp:PollInput` keeps
  the `_filterReplaceWasDown`/`_conditionRepairWasDown` trackers on the real key state while gated, so a key held across
  un-gating is not a phantom press; it returns early when both keys are 0; Shift+F8 also needs
  `GameWindowForeground()`; `BlockingMenuOpen` takes `BSAutoReadLock{ UI::GetMenuMapRWLock() }` like
  `ClimateRuntime.cpp`. `ParseHotkey` ignores a trailing `; comment`, and `IsUsableHotkey` rejects LSHIFT 0xA0 and
  RSHIFT 0xA1.

## Roofless-interior rain whitelist: implemented for ConcordMuseum01 (2026-10-29)

Reference: `deploy/F4SE/Plugins/FalloutAnomaly/climate_weather.json`, `src/ClimateWeatherConfig.cpp:ClimateWeatherConfig::Load`, `src/ClimateWeatherConfig.h:ClimateWeatherConfig::IsRooflessInterior`, `src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::SampleRooflessInterior`, `src/ClimatePolicy.h:RadClimatePolicy::PrecipitationFor`, `src/ClimateRuntime.cpp:ClimateRuntime::Update`, `tests/climate_policy_tests.cpp`, `tests/verify_climate_roofless.py`.

- The tester's example is `ConcordMuseum01` (CELL 0x017DF7, EditorID `ConcordMuseum01`, `CELL\Flags` 0x1181 -> interior
  bit set, no `GridX`/`GridY`, location 0x01D807, exterior LOD worldspace 0x00003C Commonwealth; verified with FO4IDE).
  Whether its playable space is genuinely open to the sky is a *visual* property that no record exposes, so it is not
  proven here.

| Candidate | Form ID | Evidence | Status |
|---|---|---|---|
| ConcordMuseum01 (Museum of Freedom) | Fallout4.esm 0x017DF7 | interior cell, no grid, location 0x01D807 | reported by tester; rooflessness needs in-game confirmation |
| MuseumOfWitchcraft01 | Fallout4.esm 0x15A1E3 | interior cell (EditorID), roof status unknown | not proposed until someone confirms a sky opening |

- Implemented with the approved narrow table: `climate_weather.json` carries `rooflessInteriorCells` with exactly
  `Fallout4.esm:0x017DF7`; `ClimateWeatherConfig::Load` resolves it through `TESDataHandler::LookupForm` once with the
  weather overrides, rejects non-`CELL` and non-interior entries, and logs resolved/rejected counts.
  `ClimateShelterRuntime::SampleRooflessInterior` runs the existing five-ray roof probe for a listed interior
  (`RadClimateShelterPolicy::FromRooflessInterior` keeps indoor thermal shelter but takes rain cover from the raycast).
  `ClimateRuntime::Update` then computes `RadClimatePolicy::PrecipitationFor(interior, listedRoofless, roofed,
  exterior01)`: an exterior keeps its precipitation, a listed interior open to the sky uses the exterior value, a listed
  interior with a roof gives 0, and every unlisted or unresolved interior keeps the old zero rule.
  `RadClimatePolicy::NextWetness` treats a listed roofless interior as rain-exposed, so wetness can actually rise in the
  open part, while a roofed result stays dry. `tests/climate_policy_tests.cpp` pins all five outcomes plus the wetness
  rule, and `tests/verify_climate_roofless.py` pins the JSON shape (one entry), the loader/plumbing and the absence of a
  hard-coded cell id in C++. Not played in game: whether the ConcordMuseum01 play space actually has open sky above the
  player is a visual property no record exposes, so that stays on `docs/IN_GAME_TEST_CHECKLIST.md`.

## Phosphor theme for the status HUD (2026-10-29)

Reference: `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_status.css` (the `body.theme-phosphor` block), `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_needs.html:setTheme`, `hud_needs.html:renderNeeds`, `hud_needs.html:ring`, `hud_toxic.html:setTheme`, `hud_toxic.html:renderToxic`, `hud_toxic.html:ring`, `hud_filter.html:setTheme`, `hud_filter.html:renderFilter`, `hud_filter.html:setKeys`, `hud_filter.html:ring`, `hud_climate.html:setTheme`, `radhud.js:setTheme`, `radhud.css` (the `body.theme-phosphor #emissionBanner` rules), and `tests/status_hud_tests.js`.

- The theme is selected by `setTheme('phosphor'|'classic')`, which toggles `theme-phosphor` on `<body>`. Classic is the default, so an unset theme renders the existing design. Every Phosphor rule in `hud_status.css` sits under that class. The only classic-side changes are hiding the new ring and caption elements and adding `position:relative` to `.status`.
- `hud_needs.html:renderNeeds` shows fullness as the ring fill (1 minus debt). The payload and `updateNeeds` are unchanged. Colour tiers come from fullness: below 0.25 is rad, 0.25 to 0.5 amber, above 0.5 pip. Caption words switch at fullness 0.66, 0.5 and 0.25. Chip visibility stays on the debt stage boundaries (34/67/90) in both themes.
- `hud_toxic.html:renderToxic` hides the health, AP and rads fallback chips under Phosphor. The tissue ring is tissue/1000 and the scarring arc is permanentTissue/1000 on a second outer circle. The amber (150) and rad (600) thresholds match classic.
- `hud_filter.html:renderFilter` draws rings for filter and mask. The 25 and 60 thresholds match classic.
- `hud_filter.html:setKeys` accepts a JSON string or an object. `replace` fills the filter hint (`[F6]`) and `repair` fills the mask hint (`[F7]`). An empty key hides its hint. This applies in both themes, and the hints stay hidden until native code supplies keys.
- `hud_climate.html:setTheme` and `radhud.js:setTheme` only toggle the class. Climate chips and the storm banner are styled by CSS. The visor overlay and fog rules are not touched.
- Each of the three ring-drawing views carries its own one-line `ring` helper. The views load only inline scripts, so a shared file would need a new loader path.
- Verified by the node harness only. No browser or in-game render was run, so the ring geometry and the Ultralight rendering are unconfirmed.

## Phosphor companion mode: detection, theme push, duplicate-vitals drop (2026-10-29)

Reference: `src/RadHudCompanion.cpp:RadHud::IsCompanionMode`, `src/RadHudCompanion.cpp:ResolveCompanionMode`, `src/RadHudCompanion.cpp:RadHud::PushTheme`, `src/RadHudCompanion.cpp:RadHud::PushFilterKeys`, `src/RadHudCompanion.cpp:VkLabel`, `src/RadHud.cpp:RadHud::PushToxic`, `src/RadHud.cpp:RadHud::PushFilter`, `src/RadHud.cpp:RadHud::CreateView`, `src/RadHudLifecycle.cpp:SuppressVanillaMetersOnPrismaThread`, `src/RadHudLifecycle.cpp:CreateRadHudViewOnPrismaThread`, `src/RadHudNeeds.cpp:RadHud::PushNeeds`, `src/RadHudNeeds.cpp:SurvivalLayerActive`, `src/RadHudClimate.cpp:RadHud::EnsureClimateView`, `src/RadiationEngine.h:RadiationEngine::FilterReplaceKey`, `src/RadiationEngine.h:RadiationEngine::ConditionRepairKey`, `deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini:[Hud] Mode`, `tests/verify_hud_fallback.py:check_source`, `tests/verify_local_hud_editor.py`.

- Mode source is `[Hud] Mode=auto|phosphor|classic` (default auto). `RadHudCompanion.cpp:ResolveCompanionMode` reads it with `GetPrivateProfileStringA` on the same INI path the other readers use. `phosphor` and `classic` force the mode; `auto` is companion when `GetModuleHandleW(L"PhosphorHUD.dll")` is non-null or `Data/F4SE/Plugins/PhosphorHUD.dll` or `PhosphorHUD.ini` exists. Detection is by file or module name only, so OG and AE builds behave the same. An unknown value logs a warning and resolves as auto.
- `RadHud::IsCompanionMode` resolves once and caches in `_companionMode` (-1 unresolved). It is deliberately not gated on `PlayerInWorld()`: the first caller is `RequestAPI`'s view-creation path, which runs at game time after F4SE has loaded every plugin DLL, so the module check is already final. A world gate would cache classic from the pre-world `SuppressVanillaMetersOnPrismaThread` call and leave the vanilla meters suppressed after PhosphorHUD is in charge.
- `RadHud::PushToxic` in companion mode sends `fallback:false` and skips the health/AP/rads reads. The payload keeps its shape with zeros, so the debug menu's `updateVitalsInfo` copy of the same payload shows zero health/AP/rads while companion mode is on.
- `SuppressVanillaMetersOnPrismaThread` and `CreateRadHudViewOnPrismaThread` make no `SuppressHUDWidget` calls for `HUDPlayerHealthMeter` or `HUDActionPointMeter` in companion mode. Phosphor's `bHideVanillaMeters` owns that. Classic mode is unchanged.
- Theme: each view's DOM-ready callback (`RadHud::CreateView` for radhud, hud_toxic and hud_filter; `RadHud::PushNeeds` for hud_needs; `RadHud::EnsureClimateView` for hud_climate) calls `RadHud::PushTheme`, which sends `setTheme("phosphor"|"classic")`. The mode is cached, so no later mode change needs a second push. A view reload re-runs its callback and re-sends the theme.
- Keys: `RadHud::PushFilter` calls `RadHud::PushFilterKeys` on the filter view. The payload is `{"replace","repair"}` built from `RadiationEngine::FilterReplaceKey` and `ConditionRepairKey` through `VkLabel` (F1..F12 from 0x70..0x7B, letters and digits as themselves, other codes as `#<hex>`, 0 as empty). It is change-detected against `_keysPayload`, so an INI reload through `ReloadHotkeysFromIni` reaches the view on the next tick. `CreateView` clears `_keysPayload` when it recreates the filter view.
- Needs chips: in companion mode they still show, because Phosphor cannot read the GLOB-based needs outside Survival. `RadHudNeeds.cpp:SurvivalLayerActive` is the layer gate. It is false on difficulty 6 (`kTrueSurvival`, the same value `WaterBottleFill.cpp` uses for vanilla Survival), so `PushNeeds` hides the chips once and stops pushing while stale globals would otherwise show. Limits: `RadSurvivalQuest.psc:IsActive` also checks the `Enabled` and `SkipWhenVanillaSurvival` properties, which are `Auto Const` ESP values that C++ cannot read cheaply, so only the difficulty half is mirrored. The debug menu's SURVIVAL tab (`RadHud::PushSurvival`) still receives the stale snapshot on difficulty 6; it was left alone because the brief scoped the fix to the chips.
- `RadHudCompanion.cpp` is a separate module because `RadHud.cpp` was at the 750-line cap. Build sources come from `xmake.lua` `add_files("src/**.cpp")`.


## Visor overlay is first-person only; companion layout and rings (2026-10-29)

- `src/RadCorrectnessPolicy.h:MaskOverlayVisible` takes a fourth argument, `a_firstPerson`. The flat PNG visor (`gasmask_overlay.png`, `visor_fog.png`) is the wearer's own view, so it is hidden in third person. The debug override still wins. `src/RadEngine_Shelter.cpp:RadiationEngine::PlayerInFirstPersonView` reads `RE::PlayerCamera::QCameraEquals` for `kFirstPerson` and `kIronSights` and returns true when the camera singleton is missing, so a load screen cannot flash the visor off. `src/RadiationEngine.cpp` passes it at the single `MaskOverlayVisible` call. The helper sits in `RadEngine_Shelter.cpp` because `RadEngine_Mask.cpp` is at the 750-line cap.
- `src/RadHudCompanion.cpp:CompanionWidgetDefault` is the one table of companion-mode default positions (1920x1080 pixels, stored as fractions): needs (290,962), filter (478,962), climate (400,892), tissue (652,892). They sit right of Phosphor's HP/AP/RAD rings (x 56..272) and below its XP/location text (x < 390, y 820..915), and stay under x 664 so the compass row (x >= 660, y > 970) is clear. `CompanionPlaced` feeds the same table to every PrismaHUD `RegisterWidget` call, and `src/RadHudLayout.cpp:EnsureLoaded` applies it to `g_layout` before `hud_layout.json` is read, so a saved layout still wins. Classic mode is untouched: both functions return the descriptor or default unchanged.
- `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_climate.html`: each of the four chips (warm, wet, air, wind) now has a Phosphor-style `svg.ring` (r=20 on a 48 box, same markup as `hud_needs.html`). `ringTo` sets the dash offset: warmth/100, wetness/100, (ambient+10)/40 for temperature, wind/20. The `data-ring` attribute marks chips that get the panel-less 44px ring look in `hud_status.css` (`body.theme-phosphor .status[data-ring]`); it is an attribute because the scripts overwrite `className` on every render. In classic mode the ring is `display:none`, as before.


## M50 first-person mask view model (2026-10-29)

- `art-src/mask-view/m50/build_m50_mask.py:main` rebuilds `RadMaskView.nif` from scratch in Blender 5.2 with PyNifly (`io_scene_nifly`). Run `blender -b --python build_m50_mask.py -- <scaffold.nif> <out_raw.nif|-> [<out.blend>]`. The scaffold is any earlier RadMaskView NIF; only its nodes and transforms are kept. Copy `art-src/mask-view/m50/RadMaskView_raw.nif` somewhere else before using it as the scaffold, because the build overwrites that file. `main` sets the scale of the mesh-parent empties to 1 because PyNifly drops empty scale. The pipeline after that is `art-src/overlay-nif/patch_overlay_nif.py` (adds AttachT=NamedNode&Camera and zeroes the effect-shader flags), then `art-src/mask-view/build_mask_view.py:main` (repoints strings and writes the Mask textures into `deploy/`).
- Shapes under the `Mask` node (`art-src/mask-view/m50/build_m50_mask.py:build_parts`): `Mask:0` is the shell and strap pads. The rest are `MaskNoseCup`, `MaskValve`, `MaskDrinkTube`, `MaskChinCup`, `MaskLining` (fabric panels), `MaskHardware` (buckles), `MaskGlass` and `MaskRef`. `build_m50_mask.py:clone` copies `Mask:0` for any shape the scaffold lacks and sets `pynNodeName`. `build_m50_mask.py:main` gives `MaskValve` and `MaskHardware` their own material copy pointing at `materials\RadiationOverhaul\Mask\RadMaskPlastic.bgsm`; everything else uses `RadMask.bgsm`. `art-src/mask-view/make_plastic_bgsm.py` derives the plastic .bgsm from RadMask.bgsm (own diffuse path, specular multiplier 1.5, smoothness stays 1.0); `art-src/mask-view/build_mask_view.py:textures` writes `RadMaskPlastic_d/_s.dds` (lighter, glossier). Whether the game honours the file's values over its template is unverified.
- Frame: world x is lateral, y is up and z points toward the viewer. The eye sits at the origin looking down -Z. `build_m50_mask.py:put` moves geometry into each shape's local space and sets `hasFullPrecision`, because half-float positions collapse small parts to zero-area tris. `clean` BEAUTY-triangulates so the exporter does not fan 16-gon caps into slivers.
- `build_m50_mask.py:build_lens` is a wraparound lens on a cylinder (LA=5.4, LB=4.1, PHM=1.24 rad). From the eye it spans about ±75 degrees horizontally at eye level (±42 degrees at x=±3.05) and +31.5/-29.8 degrees vertically. The old flat panel was ±42/±28. The lens sides are off-screen at 90 and 116 degree FOV, so only the top lip and the nose notch overlap the PNG frame. `outline_py` makes the nose notch narrow at the top with steeper flanks (NW=0.6, NH=1.5, rounded-V profile). `recalc_face_normals` keeps the centre fan and the edge band wound the same way as the rings.
- `build_m50_mask.py:shell_rows` sweeps the shell profile: inner lip (scaled 0.8 to 1.25 toward the bottom, so the frame is not uniformly thick), Bezier inner wall, seal roll, outer skin and clamp band. `detail_inner` adds creases and the parting line.
- `build_m50_mask.py:build_cup` follows photos 13 and 15 of a real M50 (wearer side). `CUP_HALF` gives a tall narrow round-topped bridge rising into the lens notch (apex at `CUP_TOP` = -1.35), flaring into broad wings and a nearly flat lower edge. A narrow dent (`cr`) creases the bridge, and two small stacked bars at `cup_point(g, 0, 3)` stand for the stepped numbered tab. Walls are thin (`CUP_T` = 0.11) with a rolled 0.12 bead. The front edge is raycast onto the shell (scale 0.74 against the 1.18 rim, so the flank reads as a broad band) and embedded 0.1. The old cup-side valve seats, cheek ports and voicemitter module were removed because the photos show none of them inside.
- `build_m50_mask.py:valve_assembly` sets the axis to (0,0.25,1) and places the housing from nine shell raycasts centred at y=-3.65 so it sits inside the cup opening. It builds a collar flange, a housing with a chamfered rim, 10 radial ribs, a concentric ring, a domed hub with a pin and `arc_flap`, a hinged shield over the upper left (95 to 185 degrees). It returns the frame to `build_drink_tube`, which makes the ribbed rubber stub with an open bore up-left of the valve (`MaskDrinkTube`). `build_chin_cup` (`MaskChinCup`) raycasts a lipped rubber pocket onto the chin interior (`chin_hit`, `CHIN_S`/`CHIN_H`) with a ring for the drain hole; it is faint in renders.
- `build_m50_mask.py:build_lining` places two black fabric panels on the cheek walls between the lens lip and the cheek ports. They are sampled from the shell rows with `shell_at`, offset 0.06 toward the face, with sinusoidal folds, a turned-down hem and two snap rivets each. The reference photo does not show them clearly, so their exact layout is a guess.
- `build_m50_mask.py:strap_tab` keeps the moulded pad in the shell and puts a square-section buckle frame, a centre bar and a pad rivet into MaskHardware.
- Known limits: nothing is verified in game. The drain hole is a ring, not a cut hole. Refinement: the pointed chin in `Mask:0` came from `art-src/mask-view/m50/build_m50_mask.py:SEAL_R` putting the bottom centre at z=-0.9 against z=-0.2 beside it, plus the `shell_rows` bulge z of -1.9 at the bottom; both are now near-flat (z -0.1 to 0 and -0.9), so the lower edge is a wide U. `build_chin_cup` spans y -4.85 to -7.0. `strap_tab` buckles are solid rail frames with an extra hook bar on the top clips. The lens footprint is still wider than the old panel, and the PNG frame may need retuning. Rubber texture is not tileable. Sources: reference photos 13 to 16 supplied by the user; photo 16 (exterior) was only used to judge buckles.

## M50 hand rebuild from Meshy trace (2026-10-29)

The user rejected the automated split and decimation of the Meshy scan. Everything in `art-src/mask-view/m50/meshy_trace/rebuild/` is procedural geometry of our own. The Meshy mesh is only measured, never copied. Frame: Meshy's, Z up, mask facing -Y. The NIF frame later is (-x, z, y).

- `rebuild/measure.py` runs in Blender with `--disable-autoexec` on the Meshy blend. It ray-casts the scan into six axis depth maps plus a spherical grid from `C` = (0, 0.22, 0.42), with theta 0 to 170 and phi every 2 degrees, keeping up to four hits per ray. Output: `meshy_rays.npz`.
- `rebuild/trace.py:main` (system Python, `-I`) reduces the rays to `m50_trace.json`.
  - Rim per phi column: the walk stops at a miss, at z > 0.71, at y > 0.21, or at an r jump over 0.12 past 62 degrees. The rim curve is then median-filtered (15 samples), Gaussian-smoothed and resampled by `resample_quadrants`.
  - Anchors in `resample_quadrants`: top = argmin(9|x| - z), bottom = argmin(9|x| + z). A mid-height test fails on the lens, whose nose-notch bottom sits above mid-height.
  - Radius grid: 4 rounds of outlier-masked `nan_gauss` (|r - base| < 0.04). This drops the canister-port rays that made the star/fan artefacts.
  - `fill_cols` interpolates NaN gaps in the radius and thickness grids. A NaN there becomes a vertex at the origin after `validate()` and `remove_doubles`, and that is what made the facepiece non-manifold.
  - Facepiece rows lerp ray directions from the visor-frame outline (`lens_y` + 0.065) to the rim.
  - `LENS_CP`/`FRAME_CP`/`LENS_C` are measured by hand from the front depth map. `LENS_C` is a 9-term lens surface fit.
  - The canister outline is a polar r around the port, taken from the canister-face height map.
- `rebuild/build_m50.py:main` (`blender -b --factory-startup`) builds ten objects with the `rebuild/m50lib.py` helpers (`grid`, `sweep`, `lathe`, `rounded_box`, `catmull`). It assigns 5 materials (MaskRubber, MaskVisor, MaskCanister, MaskHarness, MaskMetal), smart-project UVs at 60 degrees, prints `M50REPORT`, and saves `OurM50_handbuilt.blend`/`.glb`.
  - `build_m50.py:facepiece`: torus-topology shell (inner rows, rim lip, outer offset by the traced thickness, front lip).
  - `build_m50.py:lens`, `visor_frame`: column-grid lens on the fitted surface, and a frame swept by `FRAME_PROFILE`.
  - `build_m50.py:canister`: lathe-like rings on the traced outline, with a side clip. The left canister is `mirror_x`.
  - `build_m50.py:valve`, `shield`: shield-shaped grille housing, vertical slats, side coupler and latch. `shield` normals point outward (`-tan.z, tan.x`).
  - `build_m50.py:drink_tube`, `chin_tab`, `harness`, `buckles`: paths measured from the depth maps.
- Refinement pass (2026-10-29):
  - `build_m50.py:valve_front` is domed: -0.878 + 2.6x² + 1.3(z+0.28)². The grille recess and the slat backs sit 0.07 behind that surface instead of at a fixed y = -0.70, which the stronger curve would have pushed in front of the bottom corners.
  - `build_m50.py:harness`: each top strap continues past the back of the head to the opposite side band, so the two straps cross in an X. The mirrored strap is pushed 0.016 toward the head near x = 0 so the two do not overlap in the same plane. A domed rivet marks the crossing, and the old joint plate is gone.
  - `build_m50.py:nose_cup` (new part `M50_NoseCup`, rubber):
    - `CUP_HALF` is a teardrop outline, kept under `FRAME_CP` so it does not run into the lens nose notch.
    - Each outline point is raycast (`BVHTree`) onto the `inner_surface` rows, then swept toward the face by `CUP_PROFILE` into a rolled lip 0.168 deep.
    - `taper` shrinks the inward lip offset near the narrow apex so the wall does not fold over itself.
    - The function asserts if any ray misses the wall.
- Result: every part is manifold with no loose or zero-area faces, 22,256 tris in total. The facepiece is 7,680 tris, the nose cup 1,152, each canister 2,152 and the lens 2,152.
  - `rebuild/render_compare.py` renders the same orthographic cameras as the Meshy reference.
  - Silhouettes match the scan within a few pixels in the front, left, three-quarter and back views. The interior (back view) is smooth.
- Not done: the nose cup has no inhalation valve discs, and the result has not been converted to the NIF frame or exported to `RadMaskView.nif`.

### Geometry correction pass against real M50 photos (2026-10-29)

`rebuild/DEFECTS_2026-10-29.md` is the record of this pass. It lists each defect, its reference photo, the fix and the proof, plus what is still wrong. The originals are in `rebuild/baseline_2026-10-29/`. The build is now split into modules:

- **`shape.py`:** the traced shell, the lens surface and `Rays`.
- **`interior.py`:** the nose cup, the inner valve, the drink pin and the face seal.
- **`hardware.py`:** the canister and grille, the harness and the head pad.
- **`build_m50.py`:** the lens, the visor frame, the valve, the drink tube, the chin tab, the materials and `main`.

The notes below explain the non-obvious choices.

- **`m50lib.py:make_object`:**
  - After `recalc_face_normals`, it reverses all faces whenever the signed volume is negative.
  - Blender's recalculation alone left the double-walled nose cup inside out, and reversing the input winding has no effect because the recalculation overrides it.
- **`shape.py:shell`:** the front lip is pushed 0.045 past the mid-wall along the row direction. A smaller push left see-through slits under the visor frame at x≈±0.25.
- **`shape.py:Rays.surf`:** returns a hit point offset along the hit normal, with the normal flipped to face the ray. `build_m50.py:drink_tube` uses it to keep the tube at a constant distance off the shell.
  - A fixed -y gap put the tube inside the canister and the chin.
- **`interior.py:nose_cup`:** the thickness direction is chosen by how the normal lines up with "away from the valve axis, toward -y".
  - Using the sign of the normal's y alone flipped columns along the near-horizontal bottom edge, and the cup folded through itself there.
- **`interior.py:inner_valve`, `drink_pin`, `face_seal`:** the valve recess, the spoke grille, the pin on the wearer's left and the in-turned seal are laid out from ref 13 and ref 15. The seal is 0.08 wide, narrower than the real one.
- **`hardware.py:canister`:**
  - **Can:** a superellipse can (A 0.29, B 0.28, depth 0.19). Exponent 2.4 makes it nearly round, as in ref 16; 3.2 read as a rounded square.
  - **Face:** the face edge is chamfered over 0.035, and a shallow groove runs at -0.11.
  - **Placement:** it sits on the shell at `CAN_ANCHOR` along the traced canister normal. Its stem ends at `-CAN_D - 0.012`, so it does not reach the cup.
- **`hardware.py:grille`:**
  - **Layout:** a round boss (r 0.175) carries a dark floor disc (r 0.15), returned separately so it takes the dark grille material. It also carries six concentric `GRILLE_RIBS` (`m50lib.ring`) and four radial bridges in each gap between ribs. Successive gaps rotate the bridges by 45°, which gives the broken-ring look of ref 16.
  - **Centring:** the grille is offset (+0.02 u, -0.02 v) from the can centre toward the front and bottom, as in the photo.
  - **Not modelled:** bayonet lugs.
  - **Audit counts:** the audit's large `Canister` self-intersection count comes from ribs and bridges seated into the boss as separate islands. `probe_islands` finds no hits inside any single island.
- **`m50lib.py:ring`:** moved here from interior.py so the grille and the inner valve share it.
- **`hardware.py:head_pad`, `harness`:**
  - **Head and pad:** an ellipsoid head (`HEAD`, `HEAD_R`) carries both the pad and the strap paths.
  - **Pad shape:** a padded band from crown to nape. `PAD_T` (-40° to 78°) sets its length. `PAD_HW` 15° sets its half-width: 0.32 across, against 0.53 for the previous oval shell. A ^8 end falloff gives it strap-like rounded ends. The dome across the width (0.01 + 0.018) gives the padded section.
  - **Strap ends:** the temple and lower straps end at phi 13°, just inside the pad edge. The crown straps end on top of the pad at (16, 72).
  - **Strap offset:** strap points sit 0.04 off the head so they clear the pad's 0.028 dome.
  - **Lead-out:** each strap's second point leaves the buckle at x unchanged, heading halfway between straight back and its first mid-point. A pure +y lead-out kinked into the lower strap's climb, and the strap folded through its own width (3 self-hits). Aiming fully at the mid-point bent it into the shell's rear rim.
  - **Lower straps:** these end at theta -28, and the pad reaches down to -40 at the centre, so the straps run back level instead of climbing.
  - **Crown straps:** these pass over the head through head_dir (150, 52) and (70, 82). A two-point crown path cut a straight line above the head, and an uneven point spacing made the catmull curve loop on itself at the buckle.
- **`build_m50.py:valve`:** the cover body ends on a ring raycast into the shell wall (`front_of`, -0.012). The old skirt cap sat inside the mask and hid the inner valve.
- **`review_views.py`:** renders with a camera-aligned sun so the interior shots are readable, and hides the harness for the rear-interior and cup shots. Two views show the harness from behind: `q_rear` (three-quarter) and `rear` (straight). The straight view is the one that shows pad width; the three-quarter view mostly shows the crown-to-nape arc.
- **`audit.py`:** prints `AUDIT` JSON with each part's size, triangle count, closed state, signed volume and self-intersections, plus pairwise BVH overlaps. Overlaps from parts seated into each other on purpose are listed in DEFECTS.

## Downloaded M50: interior rebuild (2026-10-29)

`art-src/mask-view/m50/downloaded/fix_interior.py` rebuilds the interior of the downloaded M50 model. It reads `backup_2026-10-29_downloaded_before_interior.blend` and writes `downloaded/RadMaskView_m50_downloaded_interior.blend`. It never saves over the live `RadMaskView_m50.blend`. `downloaded/DEFECTS_2026-10-29.md` lists each defect, its fix, the proof and what is still open.

- **`fix_interior.py` (old cup):**
  - The original cup was two thin funnel islands: plastic, 1088 verts, and sealings, 384 verts.
  - `find_island` deletes exactly those two islands by vertex count and centroid box, and fails loudly if it finds no match or more than one.
- **`fix_interior.py:port_frames`, `interior_ports.py:port_frame`:**
  - Each filter mount (plastic islands of 3281 verts) is fitted on its own by PCA, so the left and right ports are not assumed to be mirror images.
- **`interior_ports.py:port_collar`:** a molded rubber collar that covers the filter rim, which used to poke through the facepiece inside.
  - **Top surface:** rides `lift` above the tallest of wall, filter and mount, smoothed by `spread`.
  - **Outer band:** sinks `sink` into the wall.
  - **Underside:** follows the wall at `-under`. A deeper underside showed through on the exterior renders (max pixel diff 83); 0.002 brings the diff back to 1.
- **`interior_ports.py:spread`:** a max followed by a mean, so the smoothed field never drops below its input. A collar or cup pushed by it cannot sink back into what it clears.
- **`fix_interior.py:port_push`:** clears the cup from the ports.
  - **Order:** the ports are built before the cup.
  - **Clearance:** the cup's inner surface lifts along each port axis until it sits `margin` above the port's top surface (seat, flapper, collar, mount).
  - **Smoothing:** the lift field goes through `spread(lift, 4)`. A per-vertex lift made the cup lip wavy.
  - **Why not -y:** a clearance only along -y could not fix the sideways penetration into the flappers.
- **`interior_cup.py:inner_surface`, `nose_cup`:** every row is clamped to `CLEAR + T0` off the wall, and the last row also gets `BEAD_MAX`, so the rolled lip bead does not cut into the facepiece.
- **`interior_valve.py:housing`, `grille`, `drink_pin`:** these are separate objects, so they stay editable. The housing sits on `flange_levels` of the exterior valve, so the valve is recessed rather than a tunnel.
- **`fix_interior.py:seen_faces`, `cabin_faces`:** colour the cabin as black molded rubber without touching the exterior.
  - **Why:** the facepiece's inner rubber shared the stony atlas texture. These faces get a second material slot, `M50_InnerRubber`, instead.
  - **Seeds:** faces first hit from interior eye points.
  - **Flood fill:** spreads across edges whose normals turn less than 60°, while the face still faces the head point.
  - **Exterior exclusion:** faces first hit from exterior cameras (front, sides, quarter views, top and bottom) are excluded, then that set is grown by 2 vertex rings. The grow is needed because the 160×160 ray grid misses one-face-wide slivers on the visor frame edge, which changed exterior pixels (max diff 5–12).
  - **No rear cameras:** rear cameras would look into the cabin through the back opening and exclude the whole interior.
- **`fix_interior.py` (visor):** `lens` gets its own `M50_Visor` material: alpha 0.1, transmission 1, roughness 0.12, specular IOR level 0.15. Before this it shared the opaque atlas. This is the one intentional exterior change.

### Pass 2: matching ref 15 (2026-10-29)

`downloaded/DEFECTS_pass2_2026-10-29.md` lists the eight defects, the fix for each and the proof renders in `downloaded/review_pass2_2026-10-29/`. The pass-1 blend and scripts are kept in `downloaded/pass1_2026-10-29/`.

- **`interior_cup.py:RIM`, `SAG`, `nose_cup` (bead):** the rim is a short rounded nose hump (top at z ≈ 0.232) that falls into concave shoulders. The lower lip crosses just below the valve centre, as in ref 15. The bead tapers to 0.004 at the bottom of the rim, so the lower lip reads as thin.
- **`fix_interior.py:wall_pull`:** pulls cup points toward a head point until they sit `margin` in front of the first wall hit on the ray from that head point. The walls are the rubber, lens, sealings and both port collars.
  - **Why a ray from the head:** the wider shoulders reach the cheek walls, lens frame and collars at angles where a pull along -y or along the port axis leaves the cup inside them.
  - **Why `spread` twice:** a single `spread(mag, 2)` left a fold by the right collar (adjacent cup faces with normal dot −0.83). A single `spread(mag, 4)` cleared the fold but left a pinch in the wings. `spread(spread(mag, 4), 4)` clears both.
  - **Order:** applied after `port_push`, inside the same callback that `nose_cup` calls per row.
- **`interior_valve.py:grille`:** 12 thin spokes, a hex hub and an outer ring, from ref 15.
- **`interior_valve.py:housing` (flange kept):** the visible disc is the retaining collar (r 0.138), not the flange. The grille ring at 0.091 is 0.66 of the collar, against about 0.64 in ref 15, so the flange radius was not changed.
- **`interior_valve.py:drink_pin`:** a boss, three rounded barbs and an open tip, leaning toward the wearer and up. The old five tall ribs read as a spring.
- **`interior_ports.py:port_seat`:** the inner ring at r 0.127 was removed. No photo shows it, and with it the port read as concentric rings.
- **`interior_ports.py:port_collar`:** the top uses `spread(..., 4)` instead of 2, which removes the wavy ridges that followed the filter rim.
- **Chin fitting (plastic island 4, not changed):** this is the exterior drink-hose end cap. Its side faces pass through the chin rubber, so its cabin-side U outline cannot be hidden without moving faces that the exterior cameras see.
  - A tuck along the view ray moved 42 verts and changed 274 front-view pixels.
  - Limiting it to verts within 3 mm of the rubber still left a sliver at the cap's lower edge in the 3/4 view.
  - Excluding verts on exterior-visible faces left nothing to move.
  - It is left as an open choice for Nomad: accept a small exterior change to the cap, or keep the outline.

## Carry-weight leak: persisting the applied tissue penalties (2026-10-29)

Tester report: carry weight keeps dropping, now -92, while the radiation stage has not advanced.

- **Cause (`src/RadEngine_Session.cpp:RadiationEngine::OnPostLoad`, before this change):** it zeroed `_appliedCarryPen/_appliedHealPen/_appliedSpeedPen/_appliedApPen` on every load. `src/RadEngine_Tissue.cpp:ApplyTrackedAV` applies only the delta between the wanted penalty and that counter, so after a reload it applied the full penalty again on top of a `kTemporary` modifier the game had already saved. Each reload stacked another penalty.
  - **Unverified, and contradicted by an older note:** whether the game persists `kTemporary` modifiers in saves. The older `RadEngine_Session.cpp` note said the engine drops them on load, which is the reason the counters used to be zeroed. The -92 symptom fits stacking, which needs them to persist, but that is circumstantial. The guard below makes the fix safe under either premise.
- **Persist (`src/RadEngine_Serialization.cpp:RadiationEngine::SaveState`, `RadiationEngine::LoadState`):** a new `PENA` v1 record holds the four counters (16 bytes). Load validates with `src/SerializationPolicy.h:IsValidPenaltyState` (finite, 0..`kMaxPersistedPenalty`) and restores only when `PenaltyCandidatesAgree` (non-empty and all candidates identical), the same duplicate-record rule as the filter and storm-clock records. A save with no `PENA` record, for example one made before this change, starts at 0 and then goes through the old-save inference below.
- **Ordering (`src/RadEngine_Session.cpp:RadiationEngine::RevertState`, `OnPostLoad`):** `RevertState` now zeroes the four counters. F4SE runs `RevertState`, then `LoadState`, then kPostLoadGame (`OnPostLoad`), so `OnPostLoad` must not zero them or the restore is lost.
- **Guard (`src/RadEngine_Tissue.cpp:RadiationEngine::RepairLeakedCarryWeight`, `src/SerializationPolicy.h:RestoredCarryPenaltyHeld`):** if a restored carry penalty is above 0 but `permanent - total` is under half of it, the game did not keep the modifier, so all four restored counters are zeroed and the next tick re-applies from tissue. Without this, a game that drops modifiers would get a bonus when tissue heals. Heal/speed/AP are judged by the carry check since the four share one lifecycle. Limit: a large carry bonus from gear or chems can hide a real penalty and trigger a false drop, which stacks one extra penalty on that load if modifiers do persist.
- **Old saves (`src/RadEngine_Tissue.cpp:RadiationEngine::RepairLeakedCarryWeight`, `WantedTissuePenalties`, `RadiationEngine::_penaltiesFromSave`):** a save with no usable `PENA` record leaves `_penaltiesFromSave` false. The load then infers the four counters from the current tissue damage, but only when the wanted carry penalty is at least 0.5 and the FULL wanted penalty is visible (`permanent - total >= wanted`). This stops the first load of an old save stacking one more penalty if modifiers persist. Limit: another negative temporary carry modifier (hunger/thirst/sleep up to 45/25/10) can satisfy the test when the game dropped ours, which gives a small bonus when tissue later heals. The full-penalty threshold is stricter than the half-penalty one used for restored counters because here the evidence is the only thing deciding. `ApplyTissueEffects` uses the same `WantedTissuePenalties` so the two cannot drift apart.
- **Repair (`src/RadEngine_Tissue.cpp:RadiationEngine::RepairLeakedCarryWeight`, `src/SerializationPolicy.h:CarryLeakRepairDelta`):** one-time fix for saves that already leaked. If the total carry weight is below 0 and the permanent base is above 0, it raises the total to the permanent base via a `kTemporary` mod and posts a HUD note. It runs from `OnPostLoad` after `DispelNegativeSpecials` and zeroes `_appliedCarryPen`, because the raise wipes the whole tissue modifier and the next tick must re-derive it. `src/RadEngine_Session.cpp:OnNewGame` also zeroes the four counters so a new game does not depend on the revert callback.
  - **Limits:** it only fires on a negative total. A leak that still leaves a positive total is not detected. Raising to the permanent base can overshoot by up to the active hunger/thirst/sleep modifiers (45/25/10) once those expire.
- **Test:** `tests/serialization_policy_tests.cpp` covers the validity bounds, candidate agreement, the guard and the repair delta (-92 against base 250 gives 342). The save/load wiring and the repair call are not covered by the standalone tests; they need an in-game check for `[Load] Tissue penalties restored` and `[RepairCarry]` in f4se.log.

## M50 build scripts (art-src/mask-view) - moved comments (2026-10-29)

Paths are relative to `art-src/mask-view/`. Where a script's removed text is identical to an earlier entry, each bullet points back to that entry. No script reads its own `__doc__`, so the removed docstrings were documentation only. `make_plastic_bgsm.py`, `m50/meshy_trace/rebuild/measure.py`, `m50/meshy_trace/rebuild/baseline_2026-10-29/measure.py` and `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/measure.py` had no comments or docstrings.

- `build_mask_view.py:module`: builds the first-person gas-mask view assets. It repoints the licensed mask NIF at our own materials and writes the textures. Usage: `build_mask_view.py <source.nif> <deploy_dir>`.
- `m50/build_m50_mask.py:module`: builds the M50 interior view (Mask:0 shell, MaskNoseCup, MaskValve, MaskDrinkTube, MaskChinCup, MaskLining, MaskHardware, MaskGlass, MaskRef) into a RadMaskView scaffold NIF and exports it. Usage: `blender -b --python build_m50_mask.py -- <scaffold.nif> <out_raw.nif|-> [<out.blend>]`.
- `m50/build_m50_mask.py:clean`: the BEAUTY ngon triangulation stops the exporter fanning 16-gon caps into slivers. Kept in source as a one-line `must` constraint.
- `m50/build_m50_mask.py:put`: `hasFullPrecision` must stay 1, because half-float positions collapse small parts to zero-area tris. Kept in source as a one-line `must` constraint.
- `m50/downloaded/fix_interior.py:module`: rebuilds the downloaded M50's nose cup, inner valve and inner side ports. Run: `blender -b backup_2026-10-29_downloaded_before_interior.blend --python fix_interior.py -- <out.blend>`.
- `m50/downloaded/fix_interior.py:y_hits`: (y, normal y) of the highest hit among bvhs looking -Y from start_y at (x, z), or (-inf, 1).
- `m50/downloaded/fix_interior.py:find_island`: the single vertex island of exactly nverts verts whose centroid lies in the box lo..hi; fails loudly otherwise.
- `m50/downloaded/fix_interior.py:axial`: height along ax of the topmost surface among bvhs under point p, or None.
- `m50/downloaded/fix_interior.py:port_push`: moves points within r_max of a port axis up along it until they sit margin above that port's top surface; the lift field is smoothed.
- `m50/downloaded/fix_interior.py:wall_pull`: pulls points toward head until they sit margin in front of the first wall hit on the ray from head; the pull field is smoothed.
- `m50/downloaded/fix_interior.py:seen_faces`: faces of ob first hit by rays from each eye through a square grid facing it at centre.
- `m50/downloaded/fix_interior.py:cabin_faces`: faces of ob seen from eyes inside the mask, flood-filled across smooth edges while they face head and stay hidden from outside eyes.
- `m50/downloaded/interior_chin.py:module`: lower facepiece rubber of the downloaded M50: rises in front of the nose cup's lower half and forms the chin pocket (ref 15).
- `m50/downloaded/interior_chin.py:filt`: grid filter (max: dilate, mean: box blur) of radius r cells.
- `m50/downloaded/interior_chin.py:mold`: smooth A without dropping below it anywhere: dilate by 2r, then two box blurs of r.
- `m50/downloaded/interior_chin.py:half_width`: wide enough at the lip to take the cup's lower flanks, then pinched inside the port openings' inner edge.
- `m50/downloaded/interior_chin.py:target`: front surface depth: crowned across, a recessed chin pocket and a raised lower contour.
- `m50/downloaded/interior_chin.py:chin_seal`: wall(x, z) and part(x, z) give (y, normal y) of the first facepiece/collar/plastic and interior-part hit looking -y. The front follows target() and falls onto whatever is behind it at the side and bottom edges. Below the lip roll the back fills down to the facepiece (less SINK) or onto the cup and valve (plus CLR), so the rubber is solid.
- `m50/downloaded/interior_cup.py:module`: molded nose cup for the downloaded M50 (refs 13, 15): rounded nose hump, concave shoulders, narrow smile lip under the valve.
- `m50/downloaded/interior_cup.py:rim_loop`: closed rim as (x, z, y) resampled evenly by arc length, starting at the nose bridge going down the +x side.
- `m50/downloaded/interior_cup.py:nose_cup`: closed shell: inner (face-side) surface, rolled lip, outer surface back to the valve opening. push(P, margin) moves inner-surface points clear of side-port parts; margin is per v-row.
- `m50/downloaded/interior_ports.py:module`: inner side ports for the downloaded M50: mounting ring and spoked seat over each filter mount, rubber flapper.
- `m50/downloaded/interior_ports.py:port_frame`: centre and inward axis of a filter-mount disc from its own vertices (each side fitted separately).
- `m50/downloaded/interior_ports.py:port_seat`: mounting ring lapping the facepiece lip, spokes and hub, sitting just inside the filter mount.
- `m50/downloaded/interior_ports.py:port_flapper`: thin rubber disc held by the hub on the wearer side of the seat.
- `m50/downloaded/interior_ports.py:spread`: max then mean over a (2k+1) x 3 window, wrapping in angle; the mean of the max never drops below the input.
- `m50/downloaded/interior_ports.py:port_collar`: molded rubber collar round the inner port: top rides lift above the filter rim and facepiece, outer band sinks into the wall.
- `m50/downloaded/interior_valve.py:module`: inner exhalation valve for the downloaded M50: recessed housing on the facepiece, radial grille, drink pin.
- `m50/downloaded/interior_valve.py:revolve`: rows: (radius, y or y per angle) around world Y through HOLE_C; radius-0 end rows close the solid.
- `m50/downloaded/interior_valve.py:flange_levels`: per-angle facepiece height under the flange edge, and the constant flange top above it.
- `m50/downloaded/interior_valve.py:housing`: flange seated on the facepiece, sleeve the cup opening sits on, retaining collar, recessed bore.
- `m50/downloaded/interior_valve.py:grille`: hex hub, thin spokes and outer ring, seated just above the housing floor (ref 15).
- `m50/downloaded/interior_valve.py:drink_pin`: short tube on a boss with rounded barbs and an open tip, standing off the cup floor toward the wearer and up (ref 15).
- `m50/downloaded/m50lib.py:module`: mesh helpers for the hand-built M50. A part is (verts ndarray[N,3], faces list).
- `m50/downloaded/m50lib.py:grid`: P[u, v, 3]; u is the ring direction. Open v ends get ngon caps.
- `m50/downloaded/m50lib.py:frames`: per-point (side, up, tangent) along a polyline; up_hint is a vector or per-point array.
- `m50/downloaded/m50lib.py:sweep`: profile[M, 2] in (side, up); returns grid part with rings along the profile.
- `m50/downloaded/m50lib.py:lathe`: profile[M, 2] as (radius, height along axis); ends capped with ngons.
- `m50/downloaded/m50lib.py:ring`: closed annulus of rectangular section around axis, between heights h0 and h1.
- `m50/downloaded/m50lib.py:rounded_box`: basis columns are the local x, y, z axes.
- `m50/downloaded/pass1_2026-10-29/fix_interior.py:module`, `:find_island`, `:axial`, `:port_push`, `:seen_faces`, `:cabin_faces`: same text as `m50/downloaded/fix_interior.py` for each symbol. This copy has no `y_hits` or `wall_pull` docstring.
- `m50/downloaded/pass1_2026-10-29/interior_cup.py:module`: molded nose cup for the downloaded M50 (refs 13, 15): bell rim wide at the chin, shallow bowl, rolled lip, valve opening.
- `m50/downloaded/pass1_2026-10-29/interior_cup.py:rim_loop`: same text as `m50/downloaded/interior_cup.py:rim_loop`.
- `m50/downloaded/pass1_2026-10-29/interior_cup.py:nose_cup`: same text as `m50/downloaded/interior_cup.py:nose_cup`.
- `m50/downloaded/pass1_2026-10-29/interior_ports.py:module`: same text as `m50/downloaded/interior_ports.py:module`.
- `m50/downloaded/pass1_2026-10-29/interior_ports.py:port_frame`: same text as `m50/downloaded/interior_ports.py:port_frame`.
- `m50/downloaded/pass1_2026-10-29/interior_ports.py:port_seat`: same text as `m50/downloaded/interior_ports.py:port_seat`.
- `m50/downloaded/pass1_2026-10-29/interior_ports.py:port_flapper`: same text as `m50/downloaded/interior_ports.py:port_flapper`.
- `m50/downloaded/pass1_2026-10-29/interior_ports.py:spread`: same text as `m50/downloaded/interior_ports.py:spread`.
- `m50/downloaded/pass1_2026-10-29/interior_ports.py:port_collar`: same text as `m50/downloaded/interior_ports.py:port_collar`.
- `m50/downloaded/pass1_2026-10-29/interior_valve.py:module`: same text as `m50/downloaded/interior_valve.py:module`.
- `m50/downloaded/pass1_2026-10-29/interior_valve.py:revolve`: same text as `m50/downloaded/interior_valve.py:revolve`.
- `m50/downloaded/pass1_2026-10-29/interior_valve.py:flange_levels`: same text as `m50/downloaded/interior_valve.py:flange_levels`.
- `m50/downloaded/pass1_2026-10-29/interior_valve.py:housing`: same text as `m50/downloaded/interior_valve.py:housing`.
- `m50/downloaded/pass1_2026-10-29/interior_valve.py:grille`: hub, spokes and outer ring, seated just above the housing floor.
- `m50/downloaded/pass1_2026-10-29/interior_valve.py:drink_pin`: ribbed cylinder on a boss, standing off the cup floor toward the wearer and up.
- `m50/downloaded/pass1_2026-10-29/m50lib.py:module`: same text as `m50/downloaded/m50lib.py:module`.
- `m50/downloaded/pass1_2026-10-29/m50lib.py:grid`: same text as `m50/downloaded/m50lib.py:grid`.
- `m50/downloaded/pass1_2026-10-29/m50lib.py:frames`: same text as `m50/downloaded/m50lib.py:frames`.
- `m50/downloaded/pass1_2026-10-29/m50lib.py:sweep`: same text as `m50/downloaded/m50lib.py:sweep`.
- `m50/downloaded/pass1_2026-10-29/m50lib.py:lathe`: same text as `m50/downloaded/m50lib.py:lathe`.
- `m50/downloaded/pass1_2026-10-29/m50lib.py:ring`: same text as `m50/downloaded/m50lib.py:ring`.
- `m50/downloaded/pass1_2026-10-29/m50lib.py:rounded_box`: same text as `m50/downloaded/m50lib.py:rounded_box`.
- `m50/downloaded/pass2_2026-10-29/fix_interior.py:module`, `:find_island`, `:axial`, `:port_push`, `:wall_pull`, `:seen_faces`, `:cabin_faces`: same text as `m50/downloaded/fix_interior.py` for each symbol. This copy has no `y_hits` docstring.
- `m50/downloaded/pass2_2026-10-29/interior_cup.py:module`: same text as `m50/downloaded/interior_cup.py:module`.
- `m50/downloaded/pass2_2026-10-29/interior_cup.py:rim_loop`: same text as `m50/downloaded/interior_cup.py:rim_loop`.
- `m50/downloaded/pass2_2026-10-29/interior_cup.py:nose_cup`: same text as `m50/downloaded/interior_cup.py:nose_cup`.
- `m50/downloaded/pass2_2026-10-29/interior_ports.py:module`: same text as `m50/downloaded/interior_ports.py:module`.
- `m50/downloaded/pass2_2026-10-29/interior_ports.py:port_frame`: same text as `m50/downloaded/interior_ports.py:port_frame`.
- `m50/downloaded/pass2_2026-10-29/interior_ports.py:port_seat`: same text as `m50/downloaded/interior_ports.py:port_seat`.
- `m50/downloaded/pass2_2026-10-29/interior_ports.py:port_flapper`: same text as `m50/downloaded/interior_ports.py:port_flapper`.
- `m50/downloaded/pass2_2026-10-29/interior_ports.py:spread`: same text as `m50/downloaded/interior_ports.py:spread`.
- `m50/downloaded/pass2_2026-10-29/interior_ports.py:port_collar`: same text as `m50/downloaded/interior_ports.py:port_collar`.
- `m50/downloaded/pass2_2026-10-29/interior_valve.py:module`: same text as `m50/downloaded/interior_valve.py:module`.
- `m50/downloaded/pass2_2026-10-29/interior_valve.py:revolve`: same text as `m50/downloaded/interior_valve.py:revolve`.
- `m50/downloaded/pass2_2026-10-29/interior_valve.py:flange_levels`: same text as `m50/downloaded/interior_valve.py:flange_levels`.
- `m50/downloaded/pass2_2026-10-29/interior_valve.py:housing`: same text as `m50/downloaded/interior_valve.py:housing`.
- `m50/downloaded/pass2_2026-10-29/interior_valve.py:grille`: same text as `m50/downloaded/interior_valve.py:grille`.
- `m50/downloaded/pass2_2026-10-29/interior_valve.py:drink_pin`: same text as `m50/downloaded/interior_valve.py:drink_pin`.
- `m50/downloaded/pass2_2026-10-29/m50lib.py:module`: same text as `m50/downloaded/m50lib.py:module`.
- `m50/downloaded/pass2_2026-10-29/m50lib.py:grid`: same text as `m50/downloaded/m50lib.py:grid`.
- `m50/downloaded/pass2_2026-10-29/m50lib.py:frames`: same text as `m50/downloaded/m50lib.py:frames`.
- `m50/downloaded/pass2_2026-10-29/m50lib.py:sweep`: same text as `m50/downloaded/m50lib.py:sweep`.
- `m50/downloaded/pass2_2026-10-29/m50lib.py:lathe`: same text as `m50/downloaded/m50lib.py:lathe`.
- `m50/downloaded/pass2_2026-10-29/m50lib.py:ring`: same text as `m50/downloaded/m50lib.py:ring`.
- `m50/downloaded/pass2_2026-10-29/m50lib.py:rounded_box`: same text as `m50/downloaded/m50lib.py:rounded_box`.
- `m50/meshy_trace/rebuild/audit.py:module`: mesh audit of the open blend: size, topology, normals orientation, self and pairwise intersections. Run: `blender -b <OurM50_handbuilt.blend> --python audit.py`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/audit.py:module`: same text as `m50/meshy_trace/rebuild/audit.py:module`.
- `m50/meshy_trace/rebuild/build_m50.py:module`: hand-built multi-part Avon M50 from the Meshy trace (Meshy frame: Z up, mask faces -Y). Run: `blender -b --factory-startup --python build_m50.py -- <m50_trace.json> <out dir>`.
- `m50/meshy_trace/rebuild/build_m50.py:shield`: valve cover outline in (x, z): round top, straight sides, rounded bottom corners; top-centre start, clockwise.
- `m50/meshy_trace/rebuild/build_m50.py:valve`: front module: tight bezel, narrow vertical slots, side drink coupler and lower latch.
- `m50/meshy_trace/rebuild/build_m50.py:drink_tube`: short stowed tube from the module's side coupler down to the chin stowage coupler.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/build_m50.py:module`: same text as `m50/meshy_trace/rebuild/build_m50.py:module`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/build_m50.py:shield`: same text as `m50/meshy_trace/rebuild/build_m50.py:shield`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/build_m50.py:valve`: same text as `m50/meshy_trace/rebuild/build_m50.py:valve`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/build_m50.py:drink_tube`: same text as `m50/meshy_trace/rebuild/build_m50.py:drink_tube`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/build_m50.py:module`: same text as `m50/meshy_trace/rebuild/build_m50.py:module`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/build_m50.py:shield`: same text as `m50/meshy_trace/rebuild/build_m50.py:shield`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/build_m50.py:nose_cup`: oronasal cup: outline raycast onto the facepiece inner wall, wall swept toward the face (+y).
- `m50/meshy_trace/rebuild/hardware.py:module`: M50 exterior hardware: M61 filter canister with grille, head harness (pad, straps) and plastic buckles (ref: image 16).
- `m50/meshy_trace/rebuild/hardware.py:grille`: round boss with a dark floor, hub, concentric ribs and staggered radial bridges between them (ref 16).
- `m50/meshy_trace/rebuild/hardware.py:head_pad`: padded band down the back of the head from crown to nape, domed across its width (ref 16).
- `m50/meshy_trace/rebuild/hardware.py:harness`: temple and lower straps per side from the shell buckles to the rear pad, plus two crown straps.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/hardware.py:module`: same text as `m50/meshy_trace/rebuild/hardware.py:module`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/hardware.py:grille`: same text as `m50/meshy_trace/rebuild/hardware.py:grille`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/hardware.py:head_pad`: same text as `m50/meshy_trace/rebuild/hardware.py:head_pad`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/hardware.py:harness`: same text as `m50/meshy_trace/rebuild/hardware.py:harness`.
- `m50/meshy_trace/rebuild/interior.py:module`: M50 interior: oronasal cup, inner valve assembly, drink pin and in-turned face seal (refs: images 13, 14, 15).
- `m50/meshy_trace/rebuild/interior.py:nose_cup`: funnel shell from the face-side lip down to a ring that clamps over the valve housing rim.
- `m50/meshy_trace/rebuild/interior.py:inner_valve`: recessed housing, radial spoke grille, hub and the outer grille ring, axis toward the face (+y).
- `m50/meshy_trace/rebuild/interior.py:drink_pin`: ribbed drink-tube pin left of the valve (wearer's left, +x), base sunk into the cup wall.
- `m50/meshy_trace/rebuild/interior.py:face_seal`: in-turned flange along the facepiece rim (image 15).
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/interior.py:module`: same text as `m50/meshy_trace/rebuild/interior.py:module`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/interior.py:nose_cup`: same text as `m50/meshy_trace/rebuild/interior.py:nose_cup`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/interior.py:inner_valve`: same text as `m50/meshy_trace/rebuild/interior.py:inner_valve`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/interior.py:drink_pin`: same text as `m50/meshy_trace/rebuild/interior.py:drink_pin`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/interior.py:face_seal`: same text as `m50/meshy_trace/rebuild/interior.py:face_seal`.
- `m50/meshy_trace/rebuild/m50lib.py:module`: same text as `m50/downloaded/m50lib.py:module`.
- `m50/meshy_trace/rebuild/m50lib.py:grid`: same text as `m50/downloaded/m50lib.py:grid`.
- `m50/meshy_trace/rebuild/m50lib.py:frames`: same text as `m50/downloaded/m50lib.py:frames`.
- `m50/meshy_trace/rebuild/m50lib.py:sweep`: same text as `m50/downloaded/m50lib.py:sweep`.
- `m50/meshy_trace/rebuild/m50lib.py:lathe`: same text as `m50/downloaded/m50lib.py:lathe`.
- `m50/meshy_trace/rebuild/m50lib.py:ring`: same text as `m50/downloaded/m50lib.py:ring`.
- `m50/meshy_trace/rebuild/m50lib.py:rounded_box`: same text as `m50/downloaded/m50lib.py:rounded_box`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/m50lib.py:module`: same text as `m50/downloaded/m50lib.py:module`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/m50lib.py:grid`: same text as `m50/downloaded/m50lib.py:grid`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/m50lib.py:frames`: same text as `m50/downloaded/m50lib.py:frames`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/m50lib.py:sweep`: same text as `m50/downloaded/m50lib.py:sweep`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/m50lib.py:lathe`: same text as `m50/downloaded/m50lib.py:lathe`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/m50lib.py:ring`: same text as `m50/downloaded/m50lib.py:ring`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/m50lib.py:rounded_box`: same text as `m50/downloaded/m50lib.py:rounded_box`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/m50lib.py:module`: same text as `m50/downloaded/m50lib.py:module`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/m50lib.py:grid`: same text as `m50/downloaded/m50lib.py:grid`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/m50lib.py:frames`: same text as `m50/downloaded/m50lib.py:frames`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/m50lib.py:sweep`: same text as `m50/downloaded/m50lib.py:sweep`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/m50lib.py:lathe`: same text as `m50/downloaded/m50lib.py:lathe`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/m50lib.py:rounded_box`: same text as `m50/downloaded/m50lib.py:rounded_box`. This copy has no `ring` docstring.
- `m50/meshy_trace/rebuild/shape.py:module`: shared M50 shape data: the traced shell, lens surface and ray helpers used by every part module.
- `m50/meshy_trace/rebuild/shape.py:shell`: inner grid, outer grid and the closed facepiece part.
- `m50/meshy_trace/rebuild/shape.py:Rays`: ray casts against a part; hit() returns the first hit point or None.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/shape.py:module`: same text as `m50/meshy_trace/rebuild/shape.py:module`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/shape.py:shell`: same text as `m50/meshy_trace/rebuild/shape.py:shell`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/shape.py:Rays`: same text as `m50/meshy_trace/rebuild/shape.py:Rays`.
- `m50/meshy_trace/rebuild/render_compare.py:module`: render the open blend with the Meshy reference cameras (meshy_views.py) for side-by-side checks. Run: `blender -b <OurM50_handbuilt.blend> --python render_compare.py -- <out dir> [grey|material]`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/render_compare.py:module`: same text as `m50/meshy_trace/rebuild/render_compare.py:module`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/render_compare.py:module`: same text as `m50/meshy_trace/rebuild/render_compare.py:module`.
- `m50/meshy_trace/rebuild/review_views.py:module`: render the review set (exterior, interior, close-ups) of the open blend with EEVEE and the real materials. Run: `blender -b <OurM50_handbuilt.blend> --python review_views.py -- <out dir> <prefix>`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/review_views.py:module`: same text as `m50/meshy_trace/rebuild/review_views.py:module`.
- `m50/meshy_trace/rebuild/trace.py:module`: reduces the Meshy ray casts to the curves and grids the hand builder needs. Run: `python -I trace.py <meshy_rays.npz> <can_hmax.npy> <out m50_trace.json>`.
- `m50/meshy_trace/rebuild/trace.py:resample_quadrants`: closed polyline -> k points, k/4 per quadrant between top, +x, bottom and -x anchors.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/trace.py:module`: same text as `m50/meshy_trace/rebuild/trace.py:module`.
- `m50/meshy_trace/rebuild/backup_pass3_2026-10-29/trace.py:resample_quadrants`: same text as `m50/meshy_trace/rebuild/trace.py:resample_quadrants`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/trace.py:module`: same text as `m50/meshy_trace/rebuild/trace.py:module`.
- `m50/meshy_trace/rebuild/baseline_2026-10-29/trace.py:resample_quadrants`: same text as `m50/meshy_trace/rebuild/trace.py:resample_quadrants`.
- `art-src/overlay-nif/patch_overlay_nif.py:module` post-processes the PyNifly export of RadMaskOverlay.nif, because PyNifly cannot write NiStringsExtraData or zero the effect-shader flags. Usage: `patch_overlay_nif.py <in.nif> <out.nif>`.
- `art-src/overlay-textures/generate_overlay_textures.py:module` generates the RadMask overlay textures procedurally (original art). Usage: `generate_overlay_textures.py <png_dir> <dds_dir>`.
- `src/RadiationEngine.cpp:RadiationEngine::Update` (auto-don block): `AutoEquipMask` off, or `AutoEquipInCombat` off while `IsInCombat()`, blocks the automatic mask-up. A blocked `step.equip` resets `_wasRadExposed = 0` because `StepAutoMask` reports `equip` once per transition; re-arming lets it fire after combat ends or the toggle returns, so a mask is never donned mid-fight where a hit would wear it down. Auto-removal is unaffected (it only fires for a mask the mod put on).
- `src/RadEngine_Audio.cpp:RadiationEngine::EnsureBed`: the `applied` reference tracks the attenuation last set on the playing bed, so a volume slider retunes the loop immediately instead of at the next loop restart. `0xFFFF` means no bed is playing.
- `src/RadEngine_Storm.cpp:RadiationEngine::EffectiveStormGap`: applies the Min/Max storm sliders on read over the INI values and keeps Max at or above Min. The INI-loaded `_stormMinRealMin/_stormMaxRealMin` are never written back, so lowering Min again restores the INI Max.
- `src/RadEngine_Mask.cpp:RadiationEngine::ApplyTuning`: when the effective storm gap differs from the one last applied it clears `_stormScheduled`, so the next storm is redrawn from the new gap; the first call keeps a schedule loaded from the save.
- `src/RadEngine_Mask.cpp:RadiationEngine::ApplyTuning` and `src/RadNeedsTuning.cpp:SetLive`: `ApplyTuning` publishes the value in force for every C++-side key, and `Json` shows override, else live value, else the table reference. A hand-edited INI value (for example `[Filter] DurationSeconds=600`) therefore shows as 600 instead of the table default.
- `src/WaterBottleFill.cpp:WaterBottleFill::ActivateOnGameThread`: the sneak requirement reads `RadNeedsTuning::On(kWaterRequireSneak, g_config.requireModifier)` on every activation, so the panel toggle (`WaterRequireSneak`) overrides `[Water] RequireModifier` live and an unset toggle falls back to the INI. With sneak off a plain activation both fills a bottle and runs the vanilla drink; whether the drink's rads also apply is the open question already in the checklist. `LoadConfig` publishes the INI value through `SetLive` so the panel shows the real value.
- `src/RadiationEngine.cpp:RadiationEngine::Update` (air payload): `autoMask` is 0 when `AutoEquipMask` is off, 2 when it is armed but `AutoEquipInCombat` is off and the player is in combat, else 1. `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_filter.html:renderFilter` shows it as the AUTO chip (OFF warns, HELD, ON) and hides the chip when the field is absent, so an older engine build still renders.

## NPC outbreak milestone 1

- `deploy/Scripts/Source/User/RadOutbreakEffect.psc:RadOutbreakEffect` is an ActiveMagicEffect on an ability spell
  (`deploy/RadOutbreak.esp` 000801 -> MGEF 000800, Script archetype). State (stage, start time) lives on the effect, so it is
  saved with the actor and cleared by Dispel.
- `RadOutbreakEffect::EnsureInBed` finds beds with the vanilla `IsSleepFurniture` keyword (021B18) and calls `Bed.Activate(Patient)`
  instead of authoring an AI package. Unproven in game: a workshop package may override it. The `Probe` trace lines are the evidence.
- `RadOutbreakEffect::OnEffectStart` skips the player, the dead and essential actors only. Quest-marked exclusion, companion
  "ill but never dead" handling and stage-3 death are not built yet.
- Sound properties are None-safe until the ElevenLabs clips are exported into the repo.

### Empty bottle returned after drinking filled water

- `deploy/Scripts/Source/User/RadSurvivalQuest.psc:RadSurvivalQuest::ReturnEmptyBottle` runs from `RadSurvivalQuest::ApplyRelief`, so it fires only on a confirmed consume (equip and removal pair) and only while `IsActive()` is true (so not under vanilla Survival, which `src/WaterBottleFill.cpp:ActivateSink::ProcessEvent` also yields to).
- It matches the two items `WaterBottleFill.cpp:FillOneBottle` hands out, WaterDirty (0x366BF) and WaterPurified (0x366C0), and gives back one `BottleEmpty01` (0xDF221) per drink. The original bottle type is not recorded on the water item, so the returned bottle is always that one.
- Any WaterDirty or WaterPurified drunk returns a bottle, including looted, bought or purifier-made water, because the filled item carries no origin tag. Pairing is by form within `PairWindowSeconds`, so a drop and a drink of the same water inside that window can credit one bottle.

### M50 first-person mask view deployed, PNG visor disabled

- `deploy/Meshes/RadiationOverhaul/Overlay/RadMaskView.nif` is now build g23 of the M50 interior (`art-src/mask-view/m50`), 23 shapes, 278788 tris. It references five new `RadMaskM50*.bgsm` under `deploy/materials/RadiationOverhaul/Mask` and their `RadMaskM50*` textures; the glass and refraction `.BGEM` files and the vanilla cubemaps are unchanged. The old `RadMask*.bgsm` and `RadMask_*.dds` remain in `deploy/` but the NIF no longer uses them.
- `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:setMask` is gated by `MASK_PNG_ENABLED = false`, so `gasmask_overlay.png` never shows while the 3D view is the visor. Flip it to true to restore the PNG. `tests/status_hud_tests.js` pins the flag.
- Not run in game: NIF load, material paths and draw cost of 278k tris are unverified.
