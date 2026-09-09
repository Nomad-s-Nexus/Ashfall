# Cold Survival Core

## Purpose

Radiation Overhaul is expanding into a broader environmental-survival system. The cold-survival work lives inside the existing `Rad-Overhaul-Fallout-4` plugin rather than as a separate DLL so storms, shelter, HUD ownership, serialization, and player exposure can share one lifecycle.

The first slice is intentionally policy-only. It establishes deterministic temperature, wetness, insulation, heat, and hypothermia behavior before any new engine reads or native mutations are wired into the runtime.

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

## Verification

The existing `rad_correctness_policy_tests` target now covers the climate policy alongside the current Rad correctness policies.

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

## Runtime integration still required

This PR does not claim an active in-game cold system yet.

Before wiring runtime inputs, each source must be verified against the supported Fallout 4/CommonLib contract:

1. current exterior/interior ownership
2. current weather and precipitation classification
3. wind magnitude or a safe derived wind proxy
4. time-of-day temperature offset
5. swimming/submerged state
6. armor-slot inventory and insulation classification
7. Power Armor state
8. nearby heat-source discovery
9. shared shelter result from the existing Rad shelter system
10. serialization records for climate state
11. idempotent application/removal of hypothermia effects
12. PrismaUI temperature/wetness HUD and debug controls

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
