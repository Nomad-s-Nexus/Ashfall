# Cold Survival Core

## Purpose

Radiation Overhaul is expanding into a broader environmental-survival system. The cold-survival work lives inside the existing `Rad-Overhaul-Fallout-4` DLL so storms, shelter, HUD ownership, serialization, and player exposure can share one lifecycle.

The implementation is proof-first. Deterministic temperature, wetness, ambient mapping, insulation, persistence validation, heat falloff, and hypothermia behavior are separated from Fallout 4 acquisition and gameplay mutation.

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

`ClimateStagePolicy.h` adds recovery hysteresis so a player hovering around a boundary does not flip effects on and off at 4 Hz.

Stage effects are returned as desired multipliers/deltas rather than accumulated mutations. Future gameplay integration must apply the current desired stage idempotently and remove the old stage cleanly when state changes.

## Wetness model

Swimming saturates wetness quickly. Rain increases wetness while exposed. Shelter suppresses most precipitation gain. Drying accelerates indoors, near heat, and with activity.

Wetness increases cooling instead of directly damaging the player.

## Ambient and wind mapping

`src/ClimateEnvironmentPolicy.h` owns the deterministic environment mapping used by the runtime adapter:

- interiors use a 20 C baseline
- exterior baseline is 10 C
- a 3 C day/night curve peaks around 14:00 and bottoms around 02:00
- rainy weather subtracts 4 C
- invalid game-time input resolves to a finite safe result
- the authored weather `kWindSpeed` byte is converted to a bounded 0-20 wind proxy

The weather byte is deliberately treated as a relative proxy, not claimed to be authored physical meters per second.

## Armor insulation strategy

Climate protection does not require FormLists and does not modify any ESP/ESL.

The default path is automatic:

1. inspect the player's already-loaded biped equipment
2. deduplicate armor records by runtime FormID
3. read each armor record's authored biped-slot mask
4. translate head/torso/arm/leg coverage into the tested insulation policy
5. combine multiple pieces with diminishing returns
6. cache the result and rescan approximately once per second

This means ordinary modded armor participates without a compatibility patch or hand-maintained list.

### Why JSON overrides instead of FormLists

A FormList would require plugin-record maintenance and normally represents only membership. Climate needs a numeric insulation strength and the user does not want climate compatibility to require plugin edits.

`F4SE/Plugins/FalloutAnomaly/climate_protection.json` therefore acts only as an exception layer. The automatic result remains authoritative unless a specific armor has an override.

Overrides are identified by plugin filename plus local FormID. They are resolved once through `TESDataHandler` into runtime FormIDs, so load-order changes do not change the configuration target and the 4 Hz path performs no JSON string lookup.

Example:

```json
{
  "version": 1,
  "armorOverrides": [
    {
      "plugin": "ExampleArmor.esp",
      "form": "0x00001234",
      "insulation": 80
    }
  ]
}
```

`insulation` is an exact 0-100 piece insulation override. `form` may be a decimal JSON integer or a decimal/hex string. Invalid individual entries are rejected without discarding valid entries. A missing file is valid and simply leaves automatic insulation in control.

The shipped file is intentionally empty. It is not a database of every supported armor.

Keyword/category rules may be added later if real compatibility data proves they are useful, but exact item overrides are the smallest deterministic exception mechanism.

## Power Armor

`ClimatePolicy.h` already defines a strong Power Armor insulation floor. The existing Rad shelter subsystem owns the authoritative complete-suit test: helmet plus all six distinct ArmorTypePower pieces.

Climate will reuse that result rather than create a different definition of a sealed Power Armor suit. The bridge is still pending.

## Heat policy

`ClimateHeatPolicy.h` defines bounded source falloff and diminishing multi-source combination before any world-reference scan is added.

The runtime world scan is still pending. It must be bounded and must not enumerate an unbounded world/reference set every climate tick.

## Serialization contract

`src/ClimateSerializationPolicy.h` reserves version 1 of a `CLIM` co-save record containing exactly:

- core temperature
- wetness

Only finite values inside the policy bounds are accepted. Invalid records restore to a clean normal/dry state. Hypothermia stage is derived from restored core temperature rather than serialized independently.

The record contract is tested, but the existing Rad F4SE serialization loop has not yet been changed to write/read `CLIM`. Climate must integrate into that existing record loop rather than create a competing reader for the same F4SE record stream.

## Live observation runtime

`src/ClimateRuntime.cpp` reads only interfaces already trusted by the Rad plugin or pinned `commonlib_NonVR`:

- `PlayerCharacter::GetSingleton()` and the player's parent cell
- `TESObjectCELL::IsInterior()`
- `ActorState::IsSwimming()`
- `Sky::GetSingleton()` and the current weather
- weather `kRainy` classification
- weather `kWindSpeed` byte through the bounded wind policy
- `Calendar::gameDaysPassed`
- cached worn-armor protection from `ClimateProtectionRuntime`

It prepares environment/protection samples, runs the tested climate policy, and emits stage/status diagnostics including insulation.

The runtime is invoked immediately after `RadiationEngine::Update()` inside the existing marshaled game-main 4 Hz task. It is not called directly from the worker-thread `PlayerCharacter::Update` producer.

Climate state and the protection/config cache are reset on:

- F4SE serialization revert
- post-load main-thread lifecycle work
- new-game main-thread lifecycle work

This prevents one character's transient body temperature or equipment cache from leaking into another save while persistent `CLIM` record wiring is still pending.

The observation runtime does not modify actor values, health, inventory, weather, references, position, forms, FormLists, or plugin records.

## Verification

The Rad correctness tests and dedicated climate policy tests cover:

- exact hypothermia boundaries and recovery hysteresis
- invalid-state sanitization
- rain wetness
- swimming saturation
- drying indoors and near heat
- wet/windy cooling
- insulation effectiveness and exact overrides
- Power Armor insulation floor policy
- heat recovery and heat-source falloff
- critical exposure
- comfortable recovery toward 37 C
- day/night ambient mapping
- rainy ambient penalty
- weather wind-byte conversion
- climate serialization validation and safe restore
- weighted equipment coverage and diminishing insulation combination

Source-contract guards additionally enforce:

- verified cell/weather/calendar/swimming/wind acquisition
- game-main climate execution rather than worker-thread engine reads
- reset on revert/post-load/new-game
- no gameplay mutation in the observation slice
- bounded cached worn-equipment scanning
- deduplication by armor FormID
- authored biped-mask coverage
- JSON override resolution to runtime armor FormIDs
- no FormList/plugin mutation APIs in the protection configuration path

## Still required before gameplay penalties

1. write/read the versioned `CLIM` record through the existing Rad serialization loop
2. share exterior roof/shelter coverage with a climate-safe cadence
3. feed the existing complete Power Armor result into climate protection
4. discover nearby heat sources safely with bounded work
5. evaluate whether automatic material/resistance signals need more evidence or whether coverage plus overrides is sufficient
6. apply/remove hypothermia effects idempotently
7. add PrismaUI temperature/wetness/insulation HUD and debug controls

No guessed relocation or unverified setter should be added to complete those steps.

## Observation runtime acceptance

Before gameplay penalties are enabled, an in-game session must prove:

- `[Climate]` status lines appear at the expected cadence
- exterior ambient changes by time of day
- rainy weather lowers ambient and raises wetness
- weather wind changes the reported bounded wind proxy
- swimming rapidly raises wetness
- entering an interior reports 20 C and accelerates drying
- changing worn armor changes reported insulation after the bounded refresh interval
- a JSON override changes only the intended armor's insulation
- loading another save/new game starts from clean transient climate state while persistence is absent
- the new observation work does not disturb the existing Rad update cadence or produce queue growth

Later acceptance must additionally prove Power Armor, exterior shelter, heat sources, persistent climate state, hypothermia effects, and HUD behavior.
