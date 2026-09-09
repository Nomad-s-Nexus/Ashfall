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
5. calculate warmth independently for each body region
6. combine overlapping layers on the same region with diminishing returns
7. cache the result and rescan approximately once per second

The region model means one full-body garment and equivalent separate pieces with the same warmth coefficient produce the same whole-body insulation. Splitting an outfit into more records does not change the score by itself.

This lets ordinary modded armor participate without a compatibility patch or hand-maintained list.

### Why JSON overrides instead of FormLists

A FormList would require plugin-record maintenance and normally represents only membership. Climate needs a numeric warmth strength and the user does not want climate compatibility to require plugin edits.

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

`insulation` is a 0-100 regional warmth coefficient for the regions that armor covers. It is not an unconditional whole-body percentage. An 80% torso-only item contributes up to 28% whole-body insulation because torso weight is 35%; an 80% true full-body outfit can contribute 80% before layering/caps.

`form` may be a decimal JSON integer or a decimal/hex string. Invalid individual entries are rejected without discarding valid entries. A missing file is valid and simply leaves automatic insulation in control.

The shipped file is intentionally empty. It is not a database of every supported armor.

Keyword/category rules may be added later if real compatibility data proves they are useful, but exact item overrides are the smallest deterministic exception mechanism.

### Gear audit logging

The worn-equipment scan logs only when the unique worn armor set changes. For file-backed armor it reports:

- plugin filename
- local FormID
- runtime FormID
- automatic and effective regional warmth
- automatic and effective whole-body contribution
- override state
- authored head/torso/arm/leg coverage

The `Plugin.esp|0xLocalFormID` value can be copied directly into an override entry without calculating the load-order-dependent runtime FormID.

## Power Armor

`ClimatePowerArmorPolicy.h` mirrors the existing Rad sealed-suit contract: the vanilla `ArmorTypePower` keyword must identify at least six distinct worn PA armor records and a PA helmet must be present.

The Power Armor keyword is resolved once from `Fallout4.esm`. A complete suit sets `ProtectionSample::powerArmor`, which activates the existing 85% minimum insulation floor in `ClimatePolicy.h`. Missing keyword resolution fails closed and leaves the special PA floor disabled rather than guessing.

The gear-change log and aggregate climate status both expose the detected PA state for runtime acceptance.

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
- cached worn-armor and Power Armor protection from `ClimateProtectionRuntime`

It prepares environment/protection samples, runs the tested climate policy, and emits stage/status diagnostics including insulation and PA state.

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
- region-based insulation and exact overrides
- equality of equivalent full-body versus split-piece coverage
- diminishing returns for overlapping layers
- complete Power Armor requirements and the 85% floor
- heat recovery and heat-source falloff
- critical exposure
- comfortable recovery toward 37 C
- day/night ambient mapping
- rainy ambient penalty
- weather wind-byte conversion
- climate serialization validation and safe restore

Source-contract guards additionally enforce:

- verified cell/weather/calendar/swimming/wind acquisition
- game-main climate execution rather than worker-thread engine reads
- reset on revert/post-load/new-game
- no gameplay mutation in the observation slice
- bounded cached worn-equipment scanning
- deduplication by armor FormID
- authored biped-mask coverage
- JSON override resolution to runtime armor FormIDs
- change-only pasteable gear audit logging
- complete Power Armor detection from the vanilla keyword
- no FormList/plugin mutation APIs in the protection configuration path

## Still required before gameplay penalties

1. write/read the versioned `CLIM` record through the existing Rad serialization loop
2. share exterior roof/shelter coverage with a climate-safe cadence
3. discover nearby heat sources safely with bounded work
4. evaluate whether automatic material/resistance signals need more evidence or whether coverage plus overrides is sufficient
5. apply/remove hypothermia effects idempotently
6. add PrismaUI temperature/wetness/insulation HUD and debug controls

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
- gear-change logs report correct plugin/local FormIDs and authored coverage
- a JSON override changes only the intended armor's regional warmth
- incomplete Power Armor does not set `PA=true`; a full helmet + six-piece suit does
- loading another save/new game starts from clean transient climate state while persistence is absent
- the new observation work does not disturb the existing Rad update cadence or produce queue growth

Later acceptance must additionally prove exterior shelter, heat sources, persistent climate state, hypothermia effects, and HUD behavior.
