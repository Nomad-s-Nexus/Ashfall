# Cold Survival Core

## Purpose

Radiation Overhaul is expanding into a broader environmental-survival system. The cold-survival work lives inside the existing `Rad-Overhaul-Fallout-4` plugin rather than as a separate DLL so storms, shelter, HUD ownership, serialization, and player exposure can share one lifecycle.

The implementation is proof-first. Deterministic temperature, wetness, insulation, heat, and hypothermia behavior is separated from Fallout 4 acquisition and mutation. The first CommonLib-facing adapter is read-only and is compiled before it is allowed onto the live 4 Hz runtime path.

## Clean-room boundary

Fallout 2287 - Nuclear Winter is used only as a public gameplay reference for the idea of cold exposure, wetness, insulation, heat sources, hypothermia, and temperature feedback.

No Nuclear Winter source code, Papyrus scripts, plugin records, SWF files, sounds, textures, meshes, BA2 content, configuration files, or other assets are copied into this repository. Runtime behavior is implemented independently from documented Fallout 4/CommonLib interfaces and original policy code in this project.

## Policy state

`src/ClimatePolicy.h` owns deterministic state and transition rules that do not depend on Fallout 4:

- core body temperature, bounded to 28-37 C for the cold-only model
- wetness from 0-100%
- ambient temperature
- wind exposure
- precipitation
- submerged state
- shelter
- nearby heat
- player activity
- armor insulation
- Power Armor insulation floor
- reversible hypothermia stages and stage-effect targets

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

Stage effects are returned as desired multipliers/deltas instead of accumulated mutations. Runtime integration must apply the current desired stage idempotently and remove old-stage effects when the stage changes.

## Wetness model

Submersion saturates wetness quickly. Precipitation increases wetness according to exposure. Shelter suppresses most rain gain. Drying accelerates indoors, near heat, and with activity.

Wetness increases cooling rather than directly damaging the player.

## Thermal model

The policy computes separate cooling and warming rates.

Cooling increases with:

- colder ambient temperature
- exposed wind
- wetness

Cooling decreases with:

- armor insulation
- Power Armor
- shelter

Warming increases with:

- nearby heat
- player activity
- warm ambient temperature
- homeostatic recovery toward 37 C

The rates are deliberately isolated from game-engine acquisition so the model can be tested without Fallout 4.

## Verified runtime adapter

`src/ClimateRuntime.cpp` contains the first CommonLib-facing acquisition layer. It reads only interfaces already used and trusted by the Rad plugin or pinned CommonLib:

- `PlayerCharacter::GetSingleton()` and the player's parent cell
- `TESObjectCELL::IsInterior()`
- `ActorState::IsSwimming()`
- `Sky::GetSingleton()` and the current weather
- the weather record's `kRainy` classification flag
- `Calendar::gameDaysPassed`

The adapter derives a simple original day/night ambient curve and rainy-weather penalty, then prepares a `RadClimatePolicy::EnvironmentSample` and steps the deterministic policy. Swimming feeds the policy's submerged exposure input so it can saturate wetness once the adapter is live.

This adapter does not mutate gameplay. It does not modify actor values, health, inventory, weather, references, position, or forms. `tests/verify_climate_runtime.py` enforces that boundary and is run by the Rad build workflow.

The adapter is not yet invoked from the live 4 Hz engine path. It exists now so the pinned Windows/CommonLib build can prove the acquisition code compiles before lifecycle wiring is added.

## Verification

The existing `rad_correctness_policy_tests` target covers the climate policy alongside the current Rad correctness policies.

The climate checks prove:

- exact stage boundaries
- invalid serialized values sanitize safely
- rain raises wetness
- submersion saturates wetness
- indoor and heat-assisted drying
- wet/windy cold cools faster than dry/calm cold
- insulation reduces cooling
- Power Armor provides a strong insulation floor
- nearby heat can restore a chilled player
- extreme exposure reaches critical hypothermia
- stage penalties are monotonic and normal-state effects return to neutral
- comfortable conditions recover toward, but never exceed, 37 C

The source-contract guard additionally proves that the first CommonLib-facing runtime slice remains observation-only and continues to acquire parent-cell, weather/rain, game-time, and swimming state through the verified interfaces above.

## Runtime integration still required

This PR does not claim an active in-game cold system yet.

The first acquisition adapter exists, but live integration remains pending. Before a gameplay mutation is added, each remaining source or action must be verified against the supported Fallout 4/CommonLib contract:

1. call the climate adapter from the verified 4 Hz game-main path
2. reset climate runtime state across save/load/new-game boundaries
3. persist core temperature and wetness in a versioned co-save record
4. share exterior roof/shelter coverage with the existing Rad shelter system
5. verify a wind magnitude or safe derived wind proxy
6. classify equipped armor insulation
7. integrate the existing full Power Armor result
8. discover nearby heat sources safely and with bounded work
9. apply/remove hypothermia effects idempotently
10. add PrismaUI temperature/wetness HUD and debug controls

No guessed relocation or unverified setter should be added to complete those steps.

## Planned runtime acceptance

When the engine integration lands, runtime acceptance must prove:

- exterior weather changes ambient temperature
- interiors and shelter materially reduce exposure
- rain/swimming change wetness
- wet characters cool faster than dry characters
- insulation and Power Armor materially reduce cooling
- heat sources warm and dry the player
- hypothermia penalties enter and leave cleanly
- save/load restores only the current save's climate state
- new game does not inherit prior climate state
- PrismaUI absence remains fail-safe
- sustained play does not add unbounded per-frame work
