# Fallout Anomaly - Radiation Overhaul

A self-contained radiation overhaul for Fallout 4 (OG / 1.10.163): F4SE DLL +
`RadiationOverhaul.esp` + optional PrismaUI HUD. References only `Fallout4.esm`
(plus `DLCCoast` / `DLCNukaWorld` for merged overrides). No other plugin deps.

Author: NomadsReach

## What it does

- **Twofold radiation.** Particulate rads (vanilla Rads AV, cleared by RadAway) plus a
  persistent **tissue damage** pool that survives saves, builds from sustained exposure,
  and is NOT cleared by RadAway. **Permanent (scarred) tissue** floors how far tissue can
  heal; only chelation or a doctor clears it.
- **10-stage severity** (100 to 1000) drives SPECIAL penalties (END/STR) and Pip-Boy stage perks.
- **STALKER-style storms.** Ash Fallout and Rad Firestorm (lethal, shelter-or-die) plus Toxic
  Sulfur Storm and Acid Rain (ambient hazards), with WARNING / ACTIVE / AFTERMATH phases,
  siren + crowcall audio, and shelter detection (powered shelter or roof overhead).
- **Cures.** RadAway (partial), sleep/wait (time-based heal), doctor visits (clears permanent),
  and a craftable **Radiation Chelation Compound** that clears all permanent tissue.
- **PrismaUI HUD** (optional, degrades to no-op without the framework): configurable tactical
  vitals bars + corner notifications for storms.
- **Sister plugin `RadOverhaul_GearPatch.esp`** buffs the hazmat suit's rad resistance.

See [docs/PROGRESS.md](docs/PROGRESS.md) for the full implemented-systems breakdown and the
DLL-to-ESP FormID contract.

## Layout

```
Rad Mod/
├── src/              C++ plugin source (edit here)
├── xmake.lua         build script -> build/release/FalloutAnomalyRadiation.dll
├── data/             runtime json the DLL loads
├── deploy/           deployment package (esp/esl, F4SE.zip, HUD, sound)
├── icons/ sound/ sound_xwm/  HUD + audio assets
├── docs/             documentation (see below)
└── tooling/          record/ESP editing (not part of the DLL build)
    ├── plugins/      the ESP variants
    ├── esl_src/ esl_build/   Spriggit source + built output
    ├── tools/        gen_esl.py, merge_overrides.py, GearPatch generator
    └── Research/     reference corpus
```

## Build the DLL

```powershell
cd "E:\F4SE OG\Projects\Rad Mod"
xmake
# output: build\release\FalloutAnomalyRadiation.dll
```

Close Fallout 4 before redeploying the DLL (a loaded DLL is file-locked).

## Build the ESP

See [docs/ESP_BUILD_PIPELINE.md](docs/ESP_BUILD_PIPELINE.md) for the full Spriggit pipeline.
Tooling lives in `tooling/tools/` (`gen_esl.py`, `merge_overrides.py`, `GearPatch/`).

## Docs

- [docs/PROGRESS.md](docs/PROGRESS.md) - implemented systems, pending ideas, FormID contract
- [docs/ESP_BUILD_PIPELINE.md](docs/ESP_BUILD_PIPELINE.md) - Spriggit build/deploy pipeline
- [docs/CHANGELOG.md](docs/CHANGELOG.md) - dated change history
- [docs/ISSUES_FIXED.md](docs/ISSUES_FIXED.md) - storm-system bug root-cause log
