# Cold Survival Core

## Purpose

Radiation Overhaul is expanding into a broader environmental-survival system. The cold-survival work lives inside the existing `Rad-Overhaul-Fallout-4` plugin rather than as a separate DLL so storms, shelter, HUD ownership, serialization, and player exposure can share one lifecycle.

The implementation is proof-first. Deterministic temperature, wetness, ambient mapping, insulation, persistence validation, and hypothermia behavior are separated from Fallout 4 acquisition and gameplay mutation.

## Clean-room boundary

Fallout 2287 - Nuclear Winter is used only as a public gameplay reference for the ideas of cold exposure, wetness, insulation, heat sources, hypothermia, and temperature feedback.

No Nuclear Winter source code, Papyrus scripts, plugin records, SWF files, sounds, textures, meshes, BA2 content, configuration files, or other assets are copied into this repository. Runtime behavior is implemented independently from documented Fallout 4/CommonLib interfaces and original policy code in this project.

## Core policy

`src/ClimatePolicy.h` owns deterministic body-state transitions:

- core body temperature bounded to 28-37 C
- wetness from 0-100%
- ambient temperature input
- wind exposure input
- precipitation input
- swimming/submersion input
- shelter input
- nearby heat input
- player activity input
- armor insulation input
- Power Armor insulation floor
- reversible hypothermia stages and desired effect targets

The cold-only model never raises core temperature above 37 C. Heat-exhaustion mechanics are intentionally outside this slice.

## Hypothermia stages

| Core temperature | Stage |
| --- | --- |
| 36.5 C or warmer | Normal |
| 35.5-36.49 C | Chilled |
| 34.5-35.49 C | Mild |
| 33.0-34.49 C | Moderate |
| 30.0-32.99 C | Severe |
| Below 30.0 C | Critical |

Stage effects are returned as desired multipliers/deltas rather than accumulated mutations. Future gameplay integration must apply the current desired stage idempotently and remove the old stage cleanly when state changes.

## Wetness model

Swimming saturates wetness quickly. Rain increases wetness while exposed. Shelter suppresses most precipitation gain. Drying accelerates indoors, near heat, and with activity.

Wetness increases cooling instead of directly damaging the player.

## Ambient mapping

`src/ClimateEnvironmentPolicy.h` owns the original deterministic ambient mapping used by the runtime adapter:

- interiors use a 20 C baseline
- exterior baseline is 10 C
- a 3 C day/night curve peaks around 14:00 and bottoms around 02:00
- rainy weather subtracts 4 C
- invalid game-time input resolves to a finite safe result

The mapping is tested separately from Fallout 4 acquisition so runtime code cannot silently drift from the verified model.

## Insulation policy

`src/ClimateInsulationPolicy.h` defines the first automatic equipment-side policy without walking Fallout inventory yet.

Coverage is weighted across head, torso, arms, and legs. Radiation and energy resistance are bounded secondary signals rather than hard item lists, allowing modded armor to participate without mandatory patches. Multiple pieces combine with diminishing returns and cap below perfect insulation.

The existing Rad Power Armor implementation remains authoritative for full-suit detection. Climate does not duplicate that six-piece/helmet scan.

## Serialization contract

`src/ClimateSerializationPolicy.h` reserves version 1 of a `CLIM` co-save record containing exactly:

- core temperature
- wetness

Only finite values inside the policy bounds are accepted. Invalid records restore to a clean normal/dry state. Hypothermia stage is derived from restored core temperature rather than serialized independently.

The record contract is tested now, but the existing Rad F4SE serialization loop has not yet been changed to write/read `CLIM`.

## Live observation runtime

`src/ClimateRuntime.cpp` reads only interfaces already trusted by the Rad plugin or pinned `commonlib_NonVR`:

- `PlayerCharacter::GetSingleton()` and the player's parent cell
- `TESObjectCELL::IsInterior()`
- `ActorState::IsSwimming()`
- `Sky::GetSingleton()` and the current weather
- weather `kRainy` classification
- `Calendar::gameDaysPassed`

It prepares an `EnvironmentSample`, runs the tested climate policy, and emits stage/status diagnostics.

The runtime is now invoked immediately after `RadiationEngine::Update()` inside the existing marshaled game-main 4 Hz task. It is not called directly from the worker-thread `PlayerCharacter::Update` producer.

Climate state is reset on:

- F4SE serialization revert
- post-load main-thread lifecycle work
- new-game main-thread lifecycle work

This prevents one character's transient body temperature from leaking into another save while persistent `CLIM` record wiring is still pending.

The observation runtime does not modify actor values, health, inventory, weather, references, position, or forms.

## Verification

The Rad correctness tests and dedicated climate policy tests cover:

- exact hypothermia boundaries
- invalid-state sanitization
- rain wetness
- swimming saturation
- drying indoors and near heat
- wet/windy cooling
- insulation effectiveness
- Power Armor insulation floor
- heat recovery
- critical exposure
- monotonic stage-effect targets
- comfortable recovery toward 37 C
- day/night ambient mapping
- rainy ambient penalty
- climate serialization validation and safe restore
- weighted equipment coverage and diminishing insulation combination

`tests/verify_climate_runtime.py` additionally enforces:

- verified cell/weather/calendar/swimming acquisition only
- use of the tested ambient policy
- climate execution inside the verified game-main queued update
- no climate update in the worker producer
- reset on revert/post-load/new-game
- no gameplay mutation in the observation slice

## Still required before gameplay penalties

1. write/read the versioned `CLIM` record through the existing Rad serialization loop
2. share exterior roof/shelter coverage with the existing Rad shelter result
3. verify a wind source or bounded derived proxy
4. acquire worn armor data and feed the tested insulation policy
5. feed the existing full Power Armor result into climate protection
6. discover nearby heat sources safely with bounded work
7. apply/remove hypothermia effects idempotently
8. add PrismaUI temperature/wetness HUD and debug controls

No guessed relocation or unverified setter should be added to complete those steps.

## Observation runtime acceptance

Before gameplay penalties are enabled, an in-game session must prove:

- `[Climate]` status lines appear at the expected cadence
- exterior ambient changes by time of day
- rainy weather lowers ambient and raises wetness
- swimming rapidly raises wetness
- entering an interior reports 20 C and accelerates drying
- loading another save/new game starts from clean transient climate state while persistence is absent
- the new observation work does not disturb the existing Rad update cadence or produce queue growth

Later acceptance must additionally prove insulation, Power Armor, heat sources, persistent climate state, hypothermia effects, and HUD behavior.
