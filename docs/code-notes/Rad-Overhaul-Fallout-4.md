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

**Decision.** Hunger, thirst and sleep debt are a vanilla Papyrus quest script. No script extender and no C++ for this layer.

**Why Papyrus, not native.** The native needs runtime could not prove two signals: exactly-once food/drink consumption, and real sleep versus wait with an interrupted flag. PR #70 shipped with "no sleep recovery hook until sleep is proven" for that reason. The shipped Creation Kit base sources expose both to Papyrus, and vanilla Survival itself consumes them: `HC_ManagerScript.psc:50,1167` registers and handles `Actor.OnItemEquipped` for food and drink; `:2618-2671` handles wait and sleep with `abInterrupted`; `:15` gates on `Game.GetDifficulty() == 6`. Rebuilding that natively would be an inference from `SleepWaitMenu`, bed furniture and calendar delta, not proof.

**Why not Hydra (the removed script extender).** It was removed for four reported reasons: a 1.0.2 runtime; an out-of-bounds read in its string handling (missing null terminator); fatal-load and `REL::ID` failures on current runtimes; and an open "Var[] corruption" report in the same variant subsystem the needs state would have been stored in. These are upstream reports, not re-verified in this repository - TODO-verify each against the runtime issue tracker before citing any of them as confirmed. Any one of them is a crash or data-loss risk on the storage path itself.

**Why not Garden of Eden.** `DrinkPotion` is an action that deliberately does not equip, and `OnEquipUnequip` duplicates vanilla `OnItemEquipped`. It adds a dependency without a new signal.

**Event wiring.** `OnInit` arms `RegisterForPlayerSleep()`/`RegisterForPlayerWait()` (unregister first, so a surviving instance cannot double-deliver), registers the remote `OnItemEquipped` and `OnItemRemoved` events with an inventory filter, anchors the clock and starts the game-time timer. `OnPlayerLoadGame` re-anchors, clears the consumption candidates and the sleep/wait trackers, re-registers the remote events, and restarts the timer.

**Consumption (2026-10-28 rewrite).** The previous design armed a candidate on `Actor.OnItemEquipped` with `GetItemCount` and confirmed on a later tick by watching that count fall. That is timing-dependent: if the engine consumes an aid item before the equip event is delivered to Papyrus, the armed count is already the post-consumption count and the drop is never observed. The tester report ("food/drink never relieves hunger/thirst") is consistent with that, and the previous note's claim that the shipped base sources declare no `OnItemAdded`/`OnItemRemoved` was wrong - `ObjectReference.psc:1047` declares `Event OnItemRemoved(Form, int, ObjectReference, ObjectReference)`. The rewrite applies relief on the engine's own removal instead: `OnItemRemoved` is registered remotely and relief runs only when `akDestContainer == None` (a consumption, not a transfer into a container or the world) and an `OnItemEquipped` for the same form arrived within `PairWindowSeconds` (2.0 s real). Whichever of the two arrives second applies relief exactly once, so the equip/removal ordering cannot lose it; a drop (removal without equip) or a failed equip (equip without removal) cannot credit it. The filter is `AddInventoryEventFilter(None)` on this script, because `ScriptObject.psc:39` states that objects without a filter receive no inventory events; vanilla uses the same quest-script pattern for remote inventory events (`MQ206Script.psc:590` registers `OnItemAdded` the same way). Pairing state is saved script state but is cleared in `OnPlayerLoadGame`, and the 2 s window is the only timing state left. `Debug.Trace` markers `consume-arm equip`, `consume-confirm remove` and `consume-apply` are emitted on every step, so a Papyrus.0.log proves the path next time. `tests/verify_survival_consumption.py` pins the handler signatures, the filter and remote registration, the pairing helpers, the absence of the count-drop path, the load-time candidate clear and the trace markers; `tests/verify_survival.py` keeps only the one-line pairing and no-`GetForm` cross-checks to stay inside the 750-line source limit.

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

**Consumption is confirmed by the removal event, not by a count drop (2026-10-28 rewrite).** `OnItemEquipped` cannot tell a use from an equip, so the quest pairs it with the engine's own `ObjectReference.OnItemRemoved` event (`RadSurvivalQuest.psc:PairEquip`, `:PairRemove`, `:PruneConsumptionCandidates`): a removal counts only when `akDestContainer == None`, the item is a consumable by the cached keyword rule or the INI bridge, and an equip for the same form arrived inside `PairWindowSeconds` (2 s). Whichever event arrives second applies relief exactly once, so the equip/removal order cannot lose it, a container transfer cannot credit it, and a failed equip cannot credit it. The old count-drop design armed `GetItemCount` in the equip event and confirmed a later drop, which is timing-dependent and was replaced. The candidate arrays are initialised in `OnInit` via `ClearConsumptionCandidates()` before `ArmPlayerEvents` (and `PruneConsumptionCandidates` re-initialises them if an event ever arrives first, e.g. a mid-save install), because uninitialised dynamic arrays are None and every pair would silently no-op. `ApplyRelief` still forwards a confirmed item to `RadSurvivalDiseases.psc:ConsumeItem`, so the cure/taint routing is unchanged: the cure item clears the same state a long rest clears, and anything else charges `RiskPerTaintedFood`/`RiskPerTaintedWater`/`RiskPerContaminated` through `AddRisk`. Proven from the shipped sources: `Actor.OnItemEquipped` fires for ingestibles (vanilla `HC_ManagerScript.psc:1167` relies on it). NOT proven from the Papyrus sources: that a consumed item's removal delivers `akDestContainer == None`; that remains an in-game check (`docs/IN_GAME_TEST_CHECKLIST.md`). A world drop is rejected in practice only through the equip pairing, not through `akItemReference`, so the source comment does not claim otherwise.

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

- Same for the tissue-wasting penalties: the engine drops kTemporary modifiers on load, so a stale
- non-zero tracker would match `desired` and silently skip re-applying the penalty.

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

- `ClimateRuntime::Update` sets `environment.precipitation01 = 0` for every interior cell and `ClimatePolicy` then treats indoors as fully rain-sheltered, so rain never wets the player in an interior, including visually roofless ones such as the Museum of Freedom. The tester log agrees with the design: 1208 interior `[Climate]` lines and zero of them with `rainy=true`, so the interior rule is working as written rather than a broken wetness computation. A data-driven whitelist of roofless interiors (a `climate_weather.json` cell list plus a precipitation factor from report item 10) is not implemented yet: it needs FO4IDE-verified cell form IDs and a loader/policy/test change, so it remains a follow-up rather than a guess.


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
  once on the game thread, and exposed to Papyrus as the native functions `IsExtraFoodItem`/`IsExtraThirstItem`
  (`RadReliefConfig::InstallPapyrusBridge`, registered from `F4SEPlugin_Load`). `RadSurvivalQuest.psc` calls them from
  `IsConsumable` and `ApplyRelief`; no `HC_*` form is read. The shipped `ExtraThirstItems` entry carries
  `Fallout4.esm:0x058AB8` (RefreshingBeverage) and `Fallout4.esm:0x0E9284` (MS17DeezerLemonade), both vanilla ALCH
  drinks that only carry `HC_SustenanceType_QuenchesThirst` 0x000884 (verified with FO4IDE) and would otherwise get no
  thirst relief. Spoiled food (`MoldyFood01` 0x001A6397, the seven `*Witherd` records 0x01000800-0x01000813) is
  deliberately not listed: vanilla gives those disease risk only, never hunger relief.
- Overlay: `#maskOverlay` and `#visorFog` no longer use `mask-image` (Ultralight support unproven). The HUD clear zones
  are baked into the PNG alpha by `tools/bake_visor_clear_zones.py` (overlay in place, new `visor_fog.png`, referenced
  from `radhud.css`), covering the top-left notification area, the centre loot/prompt readout and the bottom-centre XP
  bar while the edges and corners keep the visor look. `tests/status_hud_tests.js` decodes both PNGs and asserts alpha
  at those screen points instead of matching CSS property names.
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
  follow-up; INI edits apply when the debug menu is next opened.

## Roofless-interior rain whitelist: proposal only, not implemented (2026-10-28)

Reference: `src/ClimateRuntime.cpp:ClimateRuntime::Update`, `src/ClimatePolicy.h`, `tests/climate_policy_tests.cpp`.

- The tester's example is `ConcordMuseum01` (CELL 0x017DF7, EditorID `ConcordMuseum01`, `CELL\Flags` 0x1181 -> interior
  bit set, no `GridX`/`GridY`, location 0x01D807, exterior LOD worldspace 0x00003C Commonwealth; verified with FO4IDE).
  Whether its playable space is genuinely open to the sky is a *visual* property that no record exposes, so it is not
  proven here.

| Candidate | Form ID | Evidence | Status |
|---|---|---|---|
| ConcordMuseum01 (Museum of Freedom) | Fallout4.esm 0x017DF7 | interior cell, no grid, location 0x01D807 | reported by tester; rooflessness needs in-game confirmation |
| MuseumOfWitchcraft01 | Fallout4.esm 0x15A1E3 | interior cell (EditorID), roof status unknown | not proposed until someone confirms a sky opening |

- Proposed (not implemented, waiting for the lead's approval of the table): add `rooflessInteriorCells` as
  `Plugin.esp:0xLOCALID` entries to `climate_weather.json`, resolve them once in the same loader, and in
  `ClimateRuntime::Update` treat a listed interior like an exterior for precipitation: `environment.precipitation01 =
  roofed ? 0.0f : configuredFlatFactor` (the existing roof raycast already distinguishes covered from open), while every
  other interior keeps the current zero rule. Policy: a pure `PrecipitationFor(interior, listed, roofed, exterior01)`
  function with tests, plus a `verify_climate_roofless.py` contract test. No change is made until the table is approved,
  because a guessed cell list would wet the player in roofed interiors.
