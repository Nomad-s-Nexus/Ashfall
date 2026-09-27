# Native Survival Needs Phase

## Calendar observation and persistence (2026-09-26)

`src/RadHudNeeds.cpp:RadHud::PushNeeds` presents a read-only snapshot from `src/NeedsRuntime.cpp:NeedsRuntime::Update` after its state lock is released. Hunger, thirst and sleep debt are rounded to percentages and sent only when the payload changes. The view's DOM-ready callback publishes only its handle; widget registration, layout and Interop calls run from the existing game-main update and require that ready handle to match the current view. `src/RadHudLayout.cpp` and `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/rad_hud_local_editor.js` include Needs in the existing layout save/reset/preview paths. `hud_needs.html` reuses the status styling and layout protocol, labels values as observation-only, rejects malformed data and restores the latest native values after preview. Browser checks do not establish live PrismaUI rendering or save/load acceptance.

`deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/{hud_needs,hud_climate,hud_filter,hud_toxic}.html:setLocalHUDLayout` marks an applied local position before the delayed default callback can overwrite it. The browser preview exposed the early local layout being replaced after 50 ms; `tests/status_hud_tests.js` now dispatches those delayed callbacks and failed with `22px` instead of the requested `384px` before the fix. This uses the same existing position marker as PrismaHUD positioning. `src/RadHud.cpp:RadHud::ResetDebugMenuForSession` clears and hides Needs until a fresh native payload arrives after a session change.

`src/RadEngine_Serialization.cpp:RadiationEngine::SaveState` and `RadiationEngine::LoadState` were moved together with their private record constants out of the oversized engine file to satisfy the existing source-size guard. The member signatures, co-save formats and iterator ownership are unchanged by the move. The reader collects valid candidates before restoring; field-identical duplicate records are accepted and conflicting valid records fail safe. `src/RadiationEngine.cpp:RadiationEngine::RevertState` retains the new-game/pre-load defaults. Filter state uses its own record independently of the storm clock. The source-contract checks now read the serialization module directly.

`src/NeedsPolicy.h:CalendarState::Observe` advances only from positive elapsed game days, converted to hours. Repeated time samples do nothing; invalid samples clear the baseline, backward time reanchors without progression, and load/reset starts with a fresh baseline. Large jumps saturate the normalized needs through the existing policy. No timescale multiplier is applied to an already elapsed calendar interval.

`src/NeedsRuntime.cpp:NeedsRuntime::Update` samples the same public Calendar surface used by climate, on the existing generation-guarded game-main update in `src/Hooks.cpp:QueueEngineUpdate`. It loads `needs.json` rates once. These are provisional observation rates, not accepted gameplay balance. Missing/invalid settings leave progression disabled with zero rates. State snapshots/reset/restore use a mutex and never read or modify vanilla Survival state. This stage does not apply penalties or infer consumption or sleep relief.

`src/NeedsRuntime.cpp:NeedsRuntime::Update` deliberately retains its time baseline during a temporary missing-player/cell interval: positive game time elapsed during wait/travel still contributes when the player returns. Load, revert and new game explicitly reset that baseline. This is a calendar-time model, not an active-rendering-time model; real seconds without a calendar advance add nothing.

`src/main.cpp:SaveCallback` writes the three normalized needs values as the existing version-1 NEED format. `src/RadEngine_Serialization.cpp:LoadState` remains the only co-save iterator, validates NEED version/size/read length/ranges and defers restoration until the complete scan. `src/NeedsSerializationPolicy.h:RecordCandidates` retains one candidate in constant space, accepts identical duplicates, ignores invalid candidates, and rejects conflicting valid records regardless of order. Resetting before the scan prevents missing/corrupt records from inheriting another save's state. New-game and revert clear state too.

`src/NeedsSerializationPolicy.h:RecordCandidates::Read` leaves unsupported or partial payloads for F4SE to discard. The official open-source [F4SE Serialization.cpp](https://github.com/ianpatt/f4se/blob/master/f4se/Serialization.cpp) was inspected on 2026-09-26: `Serialization::GetNextRecordInfo` calls `FlushReadRecord`, which skips the unread bytes in the current chunk before reading the next header. This matches the existing CLIM reader's skip behavior.

`tests/needs_policy_tests.cpp` exercises calendar progression, repeated/backward/invalid time, large jumps, reset/restore baselines and ambiguous records. The same `src/NeedsSerializationPolicy.h:Write` and `RecordCandidates::Read` used by the plugin round-trip three floats through an in-memory byte stream; unsupported versions/sizes, truncated payloads, invalid values and failed record opening are rejected. This is portable serialization proof, not a live F4SE co-save test. In-game consumption, sleep recovery, persistence callback scheduling, and HUD acceptance remain separate proof gates; the following original phase plan is still incomplete.

Tracking issue: #69

This phase starts the native survival overhaul inside the existing `FalloutAnomalyRadiation.dll` without replacing Bethesda's `HC_Manager` and without modifying ESP/ESL records.

## Scope

Phase 1 is observation-only.

- Hunger state
- Thirst state
- Sleep-debt state
- game-time progression
- prove food/drink consumption acquisition
- prove sleep duration/recovery acquisition
- JSON keyword/category rules plus exact item overrides
- versioned `NEED` persistence boundary
- PrismaHUD needs widget

No gameplay penalties are enabled in this phase.

## State convention

Needs use normalized values:

- `0.0` = fully satisfied/rested
- `1.0` = maximum need/debt

The policy layer does not hard-code final balance. It accepts hourly rates so tuning can remain separate from state correctness.

## Runtime design

Prefer event-driven acquisition.

- Calendar/game time drives elapsed-time progression.
- Consumption must be proven from a native event/hook path before it changes needs.
- Sleep recovery must be proven from Sleep/Wait lifecycle and elapsed game time.
- Event callbacks should capture minimal data or mark state dirty; engine reads and state application remain on the verified game-main path.
- No inventory scans on timers.

## Compatibility

Automatic item classification should use existing authored data/keywords first.

Override precedence should follow the climate protection pattern:

`exact item > first matching keyword rule > automatic classification`

JSON resolves plugin/local FormIDs once and must not be parsed in the hot path.

Do not add compatibility FormLists or plugin records.

## Persistence

The `NEED` policy record is versioned independently from climate/disease state.

`src/RadEngine_Serialization.cpp:RadiationEngine::LoadState` currently dispatches the live `NEED` record together with `CLIM` through its existing sole iterator. Extracting a separately named `SurvivalSerialization` owner is deferred; no second F4SE reader is needed.

## Proof gates

Before enabling penalties:

1. Needs progress only from elapsed game time.
2. Backward/invalid time cannot increase needs.
3. Food/drink relief fires once for a proven consumption event.
4. Dropping/transferring/crafting an item cannot masquerade as consumption.
5. Sleep recovery uses actual elapsed sleep duration.
6. Save/load/new-game state remains isolated.
7. HUD state matches native state.
8. No periodic inventory scans.
9. Exact-head CI and Windows DLL build pass.
10. In-game observation remains stable before any actor-value mutation is introduced.

## Diagnostic consumption trace

`src/NeedsConsumptionTrace.cpp:NeedsConsumptionTrace::ActivateOnGameThread` starts the observation step required by `docs/research/needs-consumption-trace.md`. It is diagnostic only and ships disabled.

- Enable with `[NeedsTrace] Enabled=1` in `Data/F4SE/Plugins/FalloutAnomalyRadiation.ini`. Any other value leaves every sink unregistered and the hook uninstalled, so normal play has no cost.
- Activation runs from `src/RadLifecycle.cpp:RadLifecycle::EnsureEngineActiveOnGameThread` after the game-main thread check, once per session.
- Four existing `commonlib_NonVR` event sources are observed:
  - `ActorEquipManager` equip/unequip for the player: item FormID, form type, stack ID, and `IsFood`/`IsPoison` for ALCH.
  - `BGSInventoryInterface` `CountChangedEvent`: raw owner ID, item ID, old/new count and saturated delta. The owner field is logged unfiltered because its identity (handle or FormID) is not yet proven. This source became reachable when `commonlib_NonVR` PR #102 made the event-source bases public.
  - `TESContainerChangedEvent` when the player is the old or new container, labelled left/entered/within by `src/NeedsTracePolicy.h:ClassifyContainerChange`.
  - `TESMagicEffectApplyEvent` targeting the player: effect and caster FormIDs.
- `PlayerDrinkPotionTrace` is a pass-through `PlayerCharacter` vtable hook at `RE::VTABLE_INDEX::Actor::DrinkPotion`. It logs `drink-enter` (item, stack ID, `IsFood`/`IsPoison`), calls the original with unchanged arguments, then logs `drink-return` with the unmodified result and the entry sequence number. It is installed only when the trace is enabled. It observes calls dispatched through the player's vtable; an explicitly qualified base call bypasses it, as the research notes.
- A `drink-return` result of true is not consumption success. The research shows DrinkPotion returns true for any non-null item and may queue the actual processing, so success has to be judged from the count and container records that follow.
- Every record carries a process-wide monotonic `seq`, the dispatching thread ID and a steady-clock millisecond offset, so queued and immediate paths can be ordered without relying on timestamp proximity.
- Sinks and the hook read and log only. They do not mutate game state, scan inventories, call `DrinkPotion` themselves, or queue work.
- Not covered yet: the DrinkPotion success callback (OG 865490 / AE 2231345), whose signature is not recorded in `commonlib_NonVR`, plus `BGSInventoryListEvent`, `TESPerkEntryRunEvent` and animation events.
- `tests/verify_needs_consumption_trace.py` guards the opt-in default, game-main registration order, the single pass-through hook and the absence of state mutation. `tests/needs_trace_policy_tests.cpp` covers container-direction classification, the enable setting and saturated count deltas.
- CI pins `commonlib_NonVR` main at `6ab5841`, which carries PR #102 and PR #103.

Capture procedure: enable the trace, perform one matrix row at a time from the research document, note the action and its visible outcome, then collect the `[NeedsTrace]` lines from `FalloutAnomaly.log`. Repeat rows to expose duplicates. The matrix stays NOT CAPTURED until those logs exist; this code alone establishes nothing about consumption.
