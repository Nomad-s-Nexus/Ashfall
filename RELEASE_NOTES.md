# Rad Overhaul v1.1.0

Feature release of Rad Overhaul (Fallout Anomaly Radiation) for Fallout 4. The attached
archive is a ready-to-install mod: the complete `deploy/` tree plus a freshly built
`FalloutAnomalyRadiation.dll`.

## Highlights

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
- **Pip-Boy disease perks.** Carrying an illness now shows a named perk (`Fever` or
  `Wasting`, stage 1-3) in the PERKS tab, cleared when the illness is cured. The cure item
  is named **Rad Cure Compound**; vanilla **RadAway** also cures.
- **Third-party gas masks.** The `[Masks]` section of `FalloutAnomalyRadiation.ini` takes
  keywords or armor records from other plugins, and by default any mouth-slot armor with
  radiation resistance counts as a mask - so another mask mod needs no configuration. This
  release is verified against **Gasmask Overhaul** (Nexus 108845) and its PPM-88.
- **Readable HUD and a SURVIVAL tab.** Status icons are larger and brighter; the debug
  panel gained a SURVIVAL pane showing pools, stages, exposure and illness, and its detail
  text is scaled up so vitals and armor values read at a glance.
- **Storm weather handoff.** Managed weather returns smoothly without handing control back
  to a captured vanilla radstorm; the storm dome follows the player across cell and
  worldspace transitions; the fallback dispatch queue and dome-follow producer are bounded;
  distant firestorm strikes are placed only in a loaded exterior worldspace, on terrain,
  facing the player.
- **Mask defaults that do not strand you.** Filter life is 20 minutes, **F6** replaces a
  filter and **F7** repairs a broken mask out of the box.

## Requirements

- Fallout 4 OG `1.10.163` or AE `1.11.240`, with [F4SE](https://f4se.silverlock.org/).
- The native plugin fails closed on unsupported runtimes (NG `1.10.980`-`1.10.984`).
- Optional: PrismaUI F4 2.2.0 or newer for the HUD. Without PrismaHUD the mod supplies its
  own health, stamina, tissue and filter widgets.

## Install

Install the attached `RadiationOverhaul-rad-overhaul-v1.1.0.zip` with Mod Organizer 2 or
Vortex and enable the included ESPs, or extract it into the Fallout 4 `Data` folder. The
archive places `F4SE/`, `Meshes/`, `Textures/`, `materials/`, `Sound/`, `Scripts/`,
`PrismaUI_F4/` and the ESPs at its root.

`RadOverhaulSurPatch.esp` is an optional patch for **Survival Mode Overhaul**. Leave it
disabled unless that mod is installed and enabled in the same profile.

## Known limits

- Survival difficulty (`iDifficulty=6`) suspends the Papyrus needs layer, because vanilla
  survival owns the pools there. Use Very Hard or below to play the Rad Overhaul layers.
- Hypothermia penalties, companion exhaustion and doctor cures are not implemented.
  Permanent tissue scarring is cured only by the chelation compound.
- In-game acceptance of the survival and disease layers is advisory for this release: the
  systems are validated by contract tests and a green build, not by a long play session.

## Verification

- Python contract suites (survival, diseases, masks, HUD, serialization, ghoul contract,
  AE runtime support, CI deployment isolation) and the C++ correctness-policy executables
  pass on the release commit.
- `verify_papyrus_pex.py` enforces the compiled Papyrus bytecode shipped in the archive.
- The Windows MSVC build produces the DLL packaged here; the archive is assembled from the
  repository's `deploy/` tree plus that DLL, so source and package cannot drift.
- ESP structural validation (`tests/plugin_reader.py`) covers the survival records and the
  VMAD bindings, including the cross-script `Diseases` link the cure depends on.
