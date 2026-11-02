# Rad Overhaul v1.2.0

Feature and fix release of Rad Overhaul (Fallout Anomaly Radiation) for Fallout 4. The attached
archive is a ready-to-install mod: the complete `deploy/` tree plus a freshly built
`FalloutAnomalyRadiation.dll`.

**Nothing in this release was tested in game by the owner before publishing.** It is validated
by contract tests and a green build only. See Known limits.

## Highlights

- **Baked storm dome.** The storm dome is now a baked NIF storm (`CloudArena01_4.nif` and eight
  `textures/FalloutAnomaly/StormNif` textures) that replaces the old dome. A new animation
  clock drives it: the plugin sets the controller manager's active bit, the portal and core
  nodes spin, the Storm sequence loops so the dome does not go dark after 360 s, the mesh is
  collision-free so players are not blocked at the dome edge, and the follow runs every frame
  and moves all layers together. The storm shake stays outdoors and drops to 35 percent
  indoors.
- **Showcase tab.** Shift+F8 has a SHOWCASE tab with one button per scene (five storm types,
  rain, acid rain, mask and filter, radiation, and needs and disease scenes) and a
  **PLAY FULL TOUR** button. The needs and disease scenes overwrite the real needs and illness
  state of the loaded save: use a disposable save and resume time before saving.
- **Survival relief tuning.** A normal meal or drink now visibly moves hunger and thirst (item
  relief floor 0.8, cap 1.5, scaled by value). Relief is confirmed by pairing the equip and
  item-removal events, so a drop or a failed equip credits nothing. `[Relief]
  ExtraFoodKeywords`, `ExtraThirstKeywords`, `ExtraFoodItems` and `ExtraThirstItems` add other
  mods' food and drink. The SETTINGS tab gained live sliders and toggles for survival rates,
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
  for combat.
- **First-person mask view.** The M50 mask interior (`RadMaskView.nif` with `RadMaskM50*`
  materials and textures, shipped with `RadOverhaul_MaskFX.esp`) is the first-person visor and
  the flat PNG visor is off. The M50 model and textures are the repository owner's own Blender
  work.
- **Phosphor HUD companion mode.** With PhosphorHUD present (`[Hud] Mode=auto`, or force
  `phosphor` or `classic`) the status HUD uses a Phosphor-style theme with rings, drops the
  duplicate vitals and uses companion default positions.
- **Roofless-interior rain.** Rain can reach the player inside the Concord Museum of Freedom,
  the one approved roofless interior, when the roof probe finds open sky.
- **Carry-weight leak fix.** Applied tissue penalties are saved, so reloads no longer stack
  carry-weight loss.
- **Native Papyrus bridge through `RadNatives`.** `RadNatives.TuningOverride` and
  `RadNatives.GetReliefFlags` are global natives on a new `native hidden` script, replacing
  member natives that Bethesda's PapyrusCompiler rejects. A missing DLL keeps the script
  defaults.
- **Runtime hardening.** A watchdog re-queues the engine tick if it has not run for 10 s, the
  player update hook is published and torn down in order, and the needs widget hides when the
  needs state is unavailable.
- **NPC outbreak probe.** `RadOutbreak.esp` adds a milestone 1 probe (an infected settler sent
  to bed). It is off unless you add the spell by hand and is a test harness only.

Earlier work, unchanged and included in this first published archive:

- **Native climate model.** Core temperature, wetness, wind, shelter, nearby heat and
  clothing insulation are modelled each frame and shown on the HUD; the Shift+F8 panel
  lists the full values and explains them. Hypothermia penalties stay disabled in this
  release: the observation model is complete, the punishment layer is not.
- **Native needs foundation.** Hunger, thirst and sleep debt are tracked from game time
  with versioned save/load state, saturation and backwards-time protection.
- **Survival quest and disease layer (Papyrus).** `RadSurvival.esp` adds three pools with
  staged abilities (carry weight, AP, heal rate), sleep/wait/eat relief, and two
  illnesses - fever (damage resist) and wasting (speed) - with incubation, three stages,
  five exposure sources (ambient, wounds, swimming, rad weather, tainted food and water),
  and two cures: an uninterrupted rest, or a cure item. Both grant 24 hours of immunity.
- **Pip-Boy disease perks.** Carrying an illness shows a named perk (`Fever` or `Wasting`,
  stage 1-3) in the PERKS tab, cleared when the illness is cured. The cure item is named
  **Rad Cure Compound**; vanilla **RadAway** also cures.
- **Third-party gas masks.** The `[Masks]` section of `FalloutAnomalyRadiation.ini` takes
  keywords or armor records from other plugins, and by default any mouth-slot armor with
  radiation resistance counts as a mask. Verified against **Gasmask Overhaul** (Nexus
  108845) and its PPM-88 at the contract level only.
- **Readable HUD and a SURVIVAL tab.** Status icons are larger and brighter; the debug
  panel has a SURVIVAL pane showing pools, stages, exposure and illness.
- **Storm weather handoff.** Managed weather returns smoothly without handing control back
  to a captured vanilla radstorm, the storm dome follows the player across cell and
  worldspace transitions, and distant firestorm strikes are placed only in a loaded
  exterior worldspace, on terrain, facing the player.
- **Mask defaults that do not strand you.** Filter life is 20 minutes, **F6** replaces a
  filter and **F7** repairs a broken mask out of the box.

## Requirements

- Fallout 4 OG `1.10.163` or AE `1.11.240`, with [F4SE](https://f4se.silverlock.org/).
- The native plugin fails closed on unsupported runtimes (NG `1.10.980`-`1.10.984`).
- Optional: PrismaUI F4 2.2.0 or newer for the HUD. Without PrismaHUD the mod supplies its
  own health, stamina, tissue and filter widgets.
- Optional: PhosphorHUD for the companion HUD mode.

## Install

Install the attached `RadiationOverhaul-rad-overhaul-v1.2.0.zip` with Mod Organizer 2 or
Vortex and enable the included ESPs, or extract it into the Fallout 4 `Data` folder. The
archive places `F4SE/`, `Meshes/`, `textures/`, `materials/`, `Sound/`, `Scripts/`,
`PrismaUI_F4/` and the ESPs at its root.

`RadOverhaul_MaskFX.esp` carries the first-person mask view and `RadOutbreak.esp` is the
optional outbreak probe. `RadOverhaulSurPatch.esp` is an optional patch for **Survival Mode
Overhaul**: leave it disabled unless that mod is installed and enabled in the same profile.

## Known limits

- **Not tested in game by the owner before publishing.** Treat every item above as unverified
  in game.
- The Papyrus bytecode (`.pex`) was compiled with FO4IDE's built-in engine, not the Creation
  Kit compiler.
- The dome visuals and the `RadNatives` static native binding are unverified in game. The
  dome needs the new animation clock: in `FalloutAnomaly.log` look for
  `[StormFX] dome anim driver`, `dome anim t=` (the time should advance) and `dome spin`.
- In game, `FalloutAnomaly.log` should show
  `[Tuning] Papyrus bridge registered (RadNatives.TuningOverride, global).` and
  `[Relief] Papyrus bridge registered (GetReliefFlags).`, and `Papyrus.0.log` should have no
  `Unbound native function` or `could find no matching static function` lines for
  `RadNatives`.
- The M50 mask view (23 shapes, about 279k triangles) is unverified for NIF load, material
  paths and draw cost.
- Not in this release: the Firestorm art previews and the no-dome alternative.
- Survival difficulty (`iDifficulty=6`) suspends the Papyrus needs layer, because vanilla
  survival owns the pools there. Use Very Hard or below to play the Rad Overhaul layers.
- Hypothermia penalties, companion exhaustion and doctor cures are not implemented.
  Permanent tissue scarring is cured only by the chelation compound.
- Showcase needs and disease scenes reset the touched pools and the illness to zero, not to
  your real values.

## Verification

- The tag workflow runs the Python contract suites (CI deployment isolation, masks, HUD,
  ghoul contract, storm spawn ownership, serialization, stack integration, survival,
  consumption, roofless climate, diseases, menu, needs globals, AV ownership, Papyrus
  bytecode) and the C++ correctness-policy and serialization-policy executables before the
  archive is built.
- `verify_papyrus_pex.py` enforces the compiled Papyrus bytecode shipped in the archive.
- The Windows MSVC build produces the DLL packaged here; the archive is assembled from the
  repository's `deploy/` tree plus that DLL, so source and package cannot drift.
- ESP structural validation (`tests/plugin_reader.py`) covers the survival records and the
  VMAD bindings, including the cross-script `Diseases` link the cure depends on.
