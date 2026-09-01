# Storm dome movement jitter — 2026-09-01

## Observed issue

During a Rad Firestorm, the storm visuals visibly jar or snap when the player moves. This is a runtime presentation issue in the native storm dome path, separate from the PrismaUI HUD.

## Evidence

The captured session was running the Rad debug command `storm:1`:

- `15:36:25.706`: Rad Firestorm forced; the dome spawned with 4/4 layers.
- `15:36:31.327`: the player moved from cell `0x2129B8F00F0` to `0x2129B8F04B0`; Rad logged `Dome owner changed (cellSame=false, worldSame=true)`, cleared 6 layers, and respawned the dome.
- `15:36:32.612`: the player moved again to cell `0x2129B8F00F0`; the same clear-and-respawn sequence happened again.

The player positions in the corresponding dumps changed from `(-79115,89912,7828)` to `(-79295,90178,7824)` and then `(-79892,90000,7822)`. The repeated destructive recreation while the player is moving matches the reported snapping.

## Root cause

`Rad-Overhaul-Fallout-4/src/RadEngine_Storm.cpp:RadiationEngine::UpdateStormDomePosition` delegates ownership decisions to `RadCorrectnessPolicy::ShouldRecreateDomeForOwner`. The previous policy treated any parent-cell change as ownership loss. Fallout 4 changes the player's exterior parent cell during ordinary streaming, so normal movement triggered a full disable/delete/respawn cycle even though the worldspace was unchanged.

The companion `PrismaUI_F4.log` shows Rad views 11–13 created successfully in the same runtime, so this report is not a HUD view-creation failure.

## Source fix

The ownership policy now preserves the existing dome references when `cellSame=false` but `worldSame=true`. The normal follow path keeps repositioning those references without destructive recreation. A real worldspace change still recreates the dome so references cannot remain owned by the wrong world.

`tests/correctness_policy_tests.cpp` now explicitly covers both cases:

- same worldspace + different exterior cell: do not recreate;
- different worldspace: recreate.

## Reproduction / runtime validation

1. Open the Rad debug menu with Shift+F8.
2. Force `Rad Firestorm` with `storm:1`.
3. Move repeatedly across nearby exterior cell boundaries while the dome is active.
4. Confirm there are no `Dome owner changed`, `Storm dome cleared`, or `Native storm dome spawned` sequences for `cellSame=false, worldSame=true` movement.
5. Confirm the dome continues following the player smoothly.
6. Confirm a real worldspace transition still tears down and recreates the dome when appropriate.

The raw logs and supplied screenshot remain preserved beside this note as pre-fix evidence.
