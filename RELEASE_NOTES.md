# Rad Overhaul v1.2.3

Release of Rad Overhaul (Fallout Anomaly Radiation) for Fallout 4, on top of v1.2.2. The
attached archive is a ready-to-install mod: the complete `deploy/` tree plus a freshly built
`FalloutAnomalyRadiation.dll`.

**Nothing in this release was tested in game by the owner before publishing, and the frame rate
was not measured.** It is validated by contract tests and a green build only. See Known limits.

## What is new in v1.2.3

1. **Every status core is one size larger, and the HUD rows line up.** Each status core is now
   64 px in the same 84 x 98 cell, with 8 px gaps, so the rings, icons and the bigger label
   (11 px) and value (12 px) text fit without crowding. The four HUD rows (toxic, needs, climate
   and filter) share one left edge and an even 108 px pitch. The defaults for those four rows
   moved together in each widget, the layout editor, `RadHudLayout.cpp` and the companion table.
   A saved `hud_layout.json` still wins, so an old saved layout keeps its old positions. With
   PhosphorHUD companion mode the rows moved up and right to x=396, y=538-960, because the old
   bottom band could not hold four rows without overlap. Not verified in game.
2. **The storm portal shows again.** v1.2.2 hid all eight vortex and portal shapes. Now only the
   two tube shapes, `CoreBack` and `CoreFront`, stay hidden. The six portal shapes (`PortalA`,
   `PortalB`, `DiskOuter`, `DiskInner`, `DiskHalo`, `PhotonRing`) follow their baked windows,
   about 128 to 320 s into the storm. The cloud decks, lightning and the sky fade are unchanged.
   Not verified in game.

## Included from v1.2.2

Everything below shipped in v1.2.2 and is still in this archive, except the storm portal, which
v1.2.3 restores (see above).

- **The floating glass tube is gone from storms.** The baked dome drew a glassy tube with a
  glowing ring about 4000 units away in the sky. v1.2.2 stopped drawing the tube and, as a side
  effect, the portal too. The tube (`CoreBack`, `CoreFront`) stays hidden.
- **Every status icon is a Phosphor HUD style core.** Hunger, thirst, fatigue, warmth, wetness,
  temperature, wind, breath, filter, mask, auto mask and tissue draw as a dark disc with a
  segmented ring and the icon inside. When PhosphorHUD is not installed the mod draws its own
  health, AP and rads in the same style. The styling follows Phosphor HUD (MIT licence, credited
  in `THIRD_PARTY_NOTICES.md`).
- **There is no classic HUD look.** `[Hud] Mode` accepts only `auto` or `phosphor` and only
  decides whether the mod drops its own vitals when PhosphorHUD is installed. An old
  `Mode=classic` line logs a warning and is treated as `auto`.
- **Visible HUD changes.** The needs cores show a word (SATED, THIRSTY, EXHAUSTED) with the
  percentage in the tooltip. The permanent-scar arc around the tissue core is gone; the scar
  number is in its tooltip.

## Included from v1.2.1

All six changes concern the baked storm dome (`CloudArena01_4.nif`) added in v1.2.0.

1. **Forced storms start the dome at the natural phase.** The baked dome sequence is nearly
   empty for its first 40 s, and a debug forced storm (Shift+F8, SHOWCASE, a storm button) only
   has a 10 s silent lead, so the dome used to start at sequence second 0 and show almost
   nothing. A forced storm now starts the dome at the phase a natural storm would have reached:
   about 170 s for a 10 s lead. Natural storms are unchanged. (The vortex tube is never drawn;
   the portal follows its baked window, see v1.2.3 above.)
2. **The vanilla cloud fade follows the dome.** The vanilla cloud layers used to fade on a 20 s
   wall-clock ramp. They now follow the dome's own sequence clock: zero until the animation
   runs, a smoothstep up over sequence 20 to 70 s, full through the middle, and down over the
   last 30 s. The plugin logs one line 30 s after the dome spawns with the sky-hook counters:
   `[StormFX] sky cloud hook stats at spawn+30s: passes=N scaled=M blend=.. seq=..`.
3. **Dome shapes are culled while invisible.** Every shape in the dome mesh is invisible by its
   static alpha and appears only through baked alpha keys. The plugin now skips drawing a shape
   while its baked alpha is about 0, using a window table generated from the shipped NIF
   (`tools/bake_dome_visibility.py`, checked by `tests/verify_dome_visibility.py`).
4. **Lower per-frame cost.** Dome layers without a controller manager are skipped, node lookups
   are cached per layer, the animation drive runs at about 30 Hz on the real clock (spin speed
   is unchanged), and the follow only moves the dome when it is more than 64 units off
   (was 0.01). These cuts are reasoned from the mesh and the per-frame code, not measured.
5. **Quieter default log.** The default log level is now info, so the `[Shelter]` roof-fan debug
   line no longer prints. The dome, tuning and relief diagnostics below still do.
6. **Correct dome time after a late start or a reload.** The dome sequence seed offset is now
   exactly the seek (the old value left a permanent lag when a layer was first driven after the
   dome clock had started). A layer whose 3D is recreated, for example by a cell reload during
   a storm, gets its sequences reactivated and reseeded and logs
   `[StormFX] dome layer 3D recreated; sequences reactivated and reseeded.`

Baked timing of the vortex layers: they would appear from about storm timeline 132 s to 314 s
of the 360 s sequence. The tube (`CoreBack`, `CoreFront`) is never drawn; the portal shapes are
drawn only inside their baked window, about 128 to 320 s, as described in v1.2.3.

## Included from v1.2.0

Everything below shipped in v1.2.0 and is still in this archive.

- **Baked storm dome.** `CloudArena01_4.nif` and eight `textures/FalloutAnomaly/StormNif`
  textures replace the old dome. A plugin-driven animation clock sets the controller manager's
  active bit, the portal and core nodes spin, the Storm sequence loops so the dome does not go
  dark after 360 s, the mesh is collision-free so players are not blocked at the dome edge, and
  the follow moves all layers together (now throttled, see above). The storm shake stays
  outdoors and drops to 35 percent indoors.
- **Showcase tab.** Shift+F8 has a SHOWCASE tab with one button per scene (five storm types,
  rain, acid rain, mask and filter, radiation, and needs and disease scenes) and a
  **PLAY FULL TOUR** button. The needs and disease scenes overwrite the real needs and illness
  state of the loaded save: use a disposable save and resume time before saving.
- **Survival relief tuning.** A normal meal or drink visibly moves hunger and thirst (item
  relief floor 0.8, cap 1.5, scaled by value). Relief is confirmed by pairing the equip and
  item-removal events, so a drop or a failed equip credits nothing. `[Relief]
  ExtraFoodKeywords`, `ExtraThirstKeywords`, `ExtraFoodItems` and `ExtraThirstItems` add other
  mods' food and drink. The SETTINGS tab has live sliders and toggles for survival rates,
  relief, diseases, filters, masks, storms, climate, water and volume, backed by the `[Tuning]`
  INI section.
- **Water bottles.** Sneak-activating a water source with an empty bottle fills it natively
  (Dirty Water everywhere, Purified Water only in the maintained Vault 81 cells), and drinking
  it returns an empty bottle. `[Water]` controls the feature, the SETTINGS tab has a
  sneak-to-fill toggle, and vanilla Survival keeps its own filling.
- **Mask handling.** Auto-don fires only when rad-weather exposure starts, needs 3 s of stable
  exposure and removes only a mask the mod equipped. The filter and repair hotkeys
  (`[Filter] ReplaceKey`, `[Condition] RepairKey`) are validated, re-read when the debug menu
  opens, and fire only while the game window is in the foreground with no blocking menu. A
  power-armor helmet hides the visor. An AUTO chip shows whether auto-equip is OFF, ON or HELD
  for combat. Filter life is 20 minutes, **F6** replaces a filter and **F7** repairs a broken
  mask out of the box.
- **First-person mask view.** The M50 mask interior (`RadMaskView.nif` with `RadMaskM50*`
  materials and textures, shipped with `RadOverhaul_MaskFX.esp`) is the first-person visor and
  the flat PNG visor is off. The M50 model and textures are the repository owner's own Blender
  work.
- **Phosphor HUD companion mode.** With PhosphorHUD present (`[Hud] Mode=auto`, or force
  `phosphor`) the status HUD drops the duplicate vitals and uses companion default positions.
  Every status widget uses the Phosphor HUD core look; there is no classic look. Status icons are larger and brighter,
  and the debug panel has a SURVIVAL pane.
- **Roofless-interior rain.** Rain can reach the player inside the Concord Museum of Freedom,
  the one approved roofless interior, when the roof probe finds open sky.
- **Carry-weight leak fix.** Applied tissue penalties are saved, so reloads no longer stack
  carry-weight loss.
- **Native Papyrus bridge through `RadNatives`.** `RadNatives.TuningOverride` and
  `RadNatives.GetReliefFlags` are global natives on a `native hidden` script, replacing member
  natives that Bethesda's PapyrusCompiler rejects. A missing DLL keeps the script defaults.
- **Runtime hardening.** A watchdog re-queues the engine tick if it has not run for 10 s, the
  player update hook is published and torn down in order, and the needs widget hides when the
  needs state is unavailable.
- **NPC outbreak probe.** `RadOutbreak.esp` adds a milestone 1 probe (an infected settler sent
  to bed). It is off unless you add the spell by hand and is a test harness only.
- **Native climate model.** Core temperature, wetness, wind, shelter, nearby heat and clothing
  insulation are modelled each frame and shown on the HUD; the Shift+F8 panel lists the full
  values and explains them. Hypothermia penalties stay disabled: the observation model is
  complete, the punishment layer is not.
- **Native needs foundation.** Hunger, thirst and sleep debt are tracked from game time with
  versioned save/load state, saturation and backwards-time protection.
- **Survival quest and disease layer (Papyrus).** `RadSurvival.esp` adds three pools with
  staged abilities (carry weight, AP, heal rate), sleep/wait/eat relief, and two illnesses:
  fever (damage resist) and wasting (speed). Each has incubation, three stages, five exposure
  sources (ambient, wounds, swimming, rad weather, tainted food and water) and two cures: an
  uninterrupted rest, or a cure item. Both grant 24 hours of immunity.
- **Pip-Boy disease perks.** Carrying an illness shows a named perk (`Fever` or `Wasting`,
  stage 1-3) in the PERKS tab, cleared when the illness is cured. The cure item is named
  **Rad Cure Compound**; vanilla **RadAway** also cures.
- **Third-party gas masks.** The `[Masks]` section of `FalloutAnomalyRadiation.ini` takes
  keywords or armor records from other plugins, and by default any mouth-slot armor with
  radiation resistance counts as a mask. Verified against **Gasmask Overhaul** (Nexus 108845)
  and its PPM-88 at the contract level only.
- **Storm weather handoff.** Managed weather returns smoothly without handing control back to a
  captured vanilla radstorm, the storm dome follows the player across cell and worldspace
  transitions, and distant firestorm strikes are placed only in a loaded exterior worldspace,
  on terrain, facing the player.

## Requirements

- Fallout 4 OG `1.10.163` or AE `1.11.240`, with [F4SE](https://f4se.silverlock.org/).
- The native plugin fails closed on unsupported runtimes (NG `1.10.980`-`1.10.984`).
- Optional: PrismaUI F4 2.2.0 or newer for the HUD. Without PrismaHUD the mod supplies its
  own health, stamina, tissue and filter widgets.
- Optional: PhosphorHUD for the companion HUD mode.

## Install

Install the attached `RadiationOverhaul-rad-overhaul-v1.2.3.zip` with Mod Organizer 2 or
Vortex and enable the included ESPs, or extract it into the Fallout 4 `Data` folder. The
archive places `F4SE/`, `Meshes/`, `textures/`, `materials/`, `Sound/`, `Scripts/`,
`PrismaUI_F4/` and the ESPs at its root.

`RadOverhaul_MaskFX.esp` carries the first-person mask view and `RadOutbreak.esp` is the
optional outbreak probe. `RadOverhaulSurPatch.esp` is an optional patch for **Survival Mode
Overhaul**: leave it disabled unless that mod is installed and enabled in the same profile.

## Known limits

- **Not tested in game by the owner before publishing.** Treat every item above as unverified
  in game.
- **The new HUD cores were not seen in the game.** How the ring paths render in PrismaUI, and
  whether the needs and filter widgets overlap at the PhosphorHUD companion default positions,
  are unchecked. The HUD page test script was not run locally; GitHub CI is the only run.
- **Frame rate was not measured.** The per-frame cost cuts are reasoned from the mesh and the
  per-frame code only.
- Whether lowering the sky property's alpha visibly removes the vanilla cloud layers is
  untested. The hook slot and offsets were verified against the 1.10.163 engine, and the hook
  scaled cloud passes in a 2026-10-31 log, but the visible result was not checked.
- That culling a dome shape keeps its alpha controllers running is engine-checked but has not
  been run.
- The reseed path needs a cell reload during a storm to exercise, and has not been.
- The dome textures were not halved and the arena light was not delayed: neither is in this
  release.
- Not in this release: the Firestorm art previews and the no-dome alternative.
- The Papyrus bytecode (`.pex`) was compiled with FO4IDE's built-in engine, not the Creation
  Kit compiler.
- The `RadNatives` static native binding is unverified in game.
- The M50 mask view (23 shapes, about 279k triangles) is unverified for NIF load, material
  paths and draw cost.
- Survival difficulty (`iDifficulty=6`) suspends the Papyrus needs layer, because vanilla
  survival owns the pools there. Use Very Hard or below to play the Rad Overhaul layers.
- Hypothermia penalties, companion exhaustion and doctor cures are not implemented.
  Permanent tissue scarring is cured only by the chelation compound.
- Showcase needs and disease scenes reset the touched pools and the illness to zero, not to
  your real values.

## In-game checks

In `FalloutAnomaly.log` look for:

- `Dome animation activated ... storm seek 170.x s` in a debug forced storm, or about `0.5 s`
  in a natural storm.
- `[StormFX] dome anim driver`, `dome anim t=` (the time should advance) and
  `dome spin: Hole4=found Vortex2=found Vortex3=found`.
- The `sky cloud hook stats at spawn+30s` line: you want `passes` above 0 and `scaled` above 0.
- `[Tuning] Papyrus bridge registered (RadNatives.TuningOverride, global).` and
  `[Relief] Papyrus bridge registered (GetReliefFlags).`
- No `[Shelter] roof-fan` lines by default.

`Papyrus.0.log` should have no `Unbound native function` or `could find no matching static
function` lines for `RadNatives`.

## Verification

- The tag workflow runs the Python contract suites (CI deployment isolation, masks, HUD, ghoul
  contract, storm spawn ownership, serialization, stack integration, survival, consumption,
  roofless climate, diseases, menu, needs globals, AV ownership, dome visibility, Papyrus
  bytecode) and the C++ correctness-policy and serialization-policy executables before the
  archive is built.
- `verify_papyrus_pex.py` enforces the compiled Papyrus bytecode shipped in the archive.
- `verify_dome_visibility.py` checks the dome visibility table against the shape names in the
  shipped `CloudArena01_4.nif` and against a fresh bake.
- The Windows MSVC build produces the DLL packaged here; the archive is assembled from the
  repository's `deploy/` tree plus that DLL, so source and package cannot drift.
- ESP structural validation (`tests/plugin_reader.py`) covers the survival records and the
  VMAD bindings, including the cross-script `Diseases` link the cure depends on.