# Ashfall code notes

## Storm dome: the vortex funnel and portal ring are never drawn

Reference: `src/RadCorrectnessPolicy.h:DomeHiddenShapeMask`, `src/RadCorrectnessPolicy.h:VisibleShapeMask`, `src/RadCorrectnessPolicy.h:kDomeHiddenShapes`, `src/RadEngine_Storm_Dome.cpp:RadiationEngine::ApplyDomeShapeMask`, and `deploy/Meshes/KORsMeshes/Anomaly/CloudArena01_4.nif`.

Symptom: during the Ash Fallout event (a forced `storm:0` in the 2026-11-06 log, "Native storm dome spawned: 4/4 layers", "dome spin: Hole4=found Vortex2=found Vortex3=found") a glassy tube with a flared dark base and a glowing ring inside floats in a flat grey sky, instead of ash filling the air.

Cause, read from the shipped NIF (byte-identical to the file installed in the Fallen World modlist, sha1 77e8a795):

- `StormRoot` scales the whole mesh by 0.1. The `Vortex2` and `Vortex3` nodes sit at (1575, 1725, 3281) world units from the dome origin, which follows the player, so the structure hangs about 4000 units away at roughly 54 degrees elevation. `Hole4` is parented at the same spot.
- `Vortex2` holds `CoreBack` and `Vortex3` holds `CoreFront`. Both are an open-topped cylinder of about 330 units radius that flares to about 440 units over a height of about 450 units. Their vertex alpha is 117 to 255 along the straight wall and fades to 0 only at the flared bottom, so the wall and the open rim read as a hard-edged glass tube. The effect shader flags are double sided, depth tested, no depth write, alpha blended.
- `Hole4` holds `PortalA` and `PortalB` (the `city.dds` portal), `DiskOuter`, `DiskInner`, `DiskHalo` and `PhotonRing`. Together they are the glowing ring inside the tube.
- The baked `Storm` sequence shows these shapes from about sequence second 128 to 320 (`RadDomeVisibilityTable.h`). A natural storm reaches that range before the siren and holds it through Active; a forced storm seeks to about 170 s, so the tube is on screen from the first frame.

Fix: `VisibleShapeMask` clears the bits of the eight shapes in `kDomeHiddenShapes` at every sequence time, including the invalid-time path, so `ApplyDomeShapeMask` calls `SetAppCulled(true)` on them and never un-culls them. The cloud decks (`Plain*`, `Overcast`, `Outer*`, `FogSkirt`, `HorizonBand`), `EyeGlow`, the bolts and the flashes still follow their baked alpha windows, so the overcast sky, lightning and sky fade are unchanged. The baked table, the baker and the NIF are untouched; culled shapes keep animating, which costs nothing visible.

Names are resolved with `DomeShapeIndex`, so a shape missing from the table is skipped and `tests/correctness_policy_tests.cpp` requires exactly eight hidden bits.

Not verified in game. Whether the remaining cloud layers read as ash is a visual judgement that needs a run with the rebuilt DLL.

Related: PR #49 (`feat/rad-storm-rad-weather-no-dome`) removes the dome entirely. It is 85 commits behind `main` and conflicts in 14 files, so this change is separate and smaller; if #49 is rebased and merged later, this change is moot.
