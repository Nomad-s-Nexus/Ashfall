# Rad Overhaul v1.0.1

Patch release of Rad Overhaul (Fallout Anomaly Radiation) for Fallout 4.
The attached archive is a ready-to-install mod: the complete `deploy/` tree plus a
freshly cross-built `FalloutAnomalyRadiation.dll`.

## Highlights

- **Correct gas-mask recognition.** Eight verified vanilla respiratory headgear records
  are tagged with `FARO_GasMask`. Recognition requires both that keyword and the mouth
  slot, and deliberately excludes the Raider surgical mask.
- **Storm dome transition recovery.** The dome follows the player, recreates its native
  references when the cell or worldspace changes, and retries at a bounded rate if a
  transition temporarily prevents placement.
- **Load and HUD safety.** Post-load, New Game, and HUD-wait work share one generation
  barrier. Starting a New Game invalidates stale work before releasing the HUD waiter
  latch for the new session.
- **Master-safe ESP.** `RadiationOverhaul.esp` keeps its intentional master order:
  `DLCCoast.esm`, `DLCNukaWorld.esm`, then `Fallout4.esm`. The release check resolves
  armor and keyword FormIDs through that table instead of assuming a raw master index.

## Verification

- The release source passed the master-aware ESP catalog check and native correctness
  policy tests.
- The Windows clang-cl/xwin build and link passed before release packaging.
- FO4Editor structural validation found no deleted records, undeclared masters,
  duplicate FormIDs, or broken references.

## Requirements

- Fallout 4 OG runtime + [F4SE](https://f4se.silverlock.org/)
- The native plugin fails closed on unsupported runtimes.

## Install

Install the attached `RadiationOverhaul-rad-overhaul-v1.0.1.zip` with Mod Organizer 2
or Vortex and enable the included ESPs, or extract it into the Fallout 4 `Data` folder.
The archive places `F4SE/`, `Meshes/`, `Textures/`, `Sound/`, `Scripts/`,
`PrismaUI_F4/`, and the ESPs at its root.

## Known Limit

The chelation/RadAway tissue-cure path remains deliberately fail-closed until a verified
main-thread item-use source adapter is available.
