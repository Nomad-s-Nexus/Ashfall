# Native Needs: read-only reader for the Papyrus pools

## Ownership moved to the quest script (2026-10-26)

`src/NeedsRuntime.cpp:NeedsRuntime::Update` no longer simulates needs. It resolves five records from
`RadSurvival.esp` through `RE::TESDataHandler::LookupForm` - `0x000801`, `0x000802`, `0x000803`
(`RadSurvival_Hunger`, `_Thirst`, `_Sleep`) and `0x000822`, `0x000823` (`RadSurvival_Risk`,
`_Disease`) - reads their `TESGlobal::value`, sanitises them, and hands a snapshot to
`src/RadHudNeeds.cpp:RadHud::PushNeeds`. `src/NeedsRuntime.cpp:ResolveGlobals` caches the pointers and
`NeedsRuntime::Reset` clears them on load, so a pointer never outlives a save. The lookup takes the
*local* form id; the data handler applies the load-order prefix, so the reader is independent of where
`RadSurvival.esp` sits in the order.

The clock, the hourly rates, the `needs.json` settings file, the `NEED` co-save writer and the
candidate-restoring reader are all gone. Two systems advancing the same pools was the original defect:
`RadSurvivalQuest.psc` accrues from game time, applies relief from confirmed inventory-count changes,
and mirrors every change into the globals, while the DLL previously ran a second, independent model
with different rates and its own save record. Now there is one owner and the globals are the contract.

## Legacy saves

`src/RadEngine_Serialization.cpp:RadiationEngine::LoadState` still recognises the `NEED` record by
signature and skips it with an info log, so a save written by the clock build loads instead of
reporting an unknown record. It reads nothing from it: the pools it carried are superseded by the
globals, which Papyrus restores with the quest. `src/NeedsSerializationPolicy.h:kRecord` is kept for
that recognition alone - the older `RecordCandidates`/`Write` API is deleted, so nothing can start
writing the format again by accident.

## Views

`src/RadHudNeeds.cpp:RadHud::PushNeeds` sends `hunger`, `thirst`, `sleepDebt`, `risk` (percent) and
`disease` (packed `DiseaseId * 10 + Stage`) and only when the payload changes. The view keeps its
previous behaviour for positions: `hud_needs.html:setLocalHUDLayout` marks an applied local position
before the delayed default callback can overwrite it, which `tests/status_hud_tests.js` covers by
dispatching those delayed callbacks. `src/RadHud.cpp:ResetDebugMenuForSession` clears and hides the
view until a fresh payload arrives after a session change.

## Tests

`tests/verify_needs_globals.py` fails if the C++ stops referencing any of the five form ids or the
plugin name, if `needs.json` returns, or if a native clock/`OpenRecord` for the needs payload
reappears. `tests/needs_policy_tests.cpp` covers only what remains in `src/NeedsPolicy.h`: clamping and
the packed-disease range check.

## Still unverified

Live PrismaUI rendering of the two new chips and the in-game read of the globals are acceptance items;
CI proves the reader's shape, not that a running game shows the numbers.
