# Native Survival Needs Phase

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

The live record must eventually be dispatched through the planned single-owner `SurvivalSerialization` iterator together with `CLIM`, rather than adding another independent F4SE reader.

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
