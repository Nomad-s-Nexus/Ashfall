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
