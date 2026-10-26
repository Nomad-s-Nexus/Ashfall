# Fallout Anomaly - Radiation Overhaul

A radiation and environmental survival overhaul for Fallout 4 supporting OG 1.10.163 and AE 1.11.240; NG 1.10.980-1.10.984 and other unverified runtimes are intentionally unsupported. The package includes an F4SE DLL +
`RadiationOverhaul.esp` + optional PrismaUI HUD. The core ESP references `Fallout4.esm`
(plus `DLCCoast` / `DLCNukaWorld` for merged overrides). The native storm-dome lighting
currently still resolves two LIGH forms from `KORsAnomaly Stripped.esp`; making those records
fully self-contained is tracked in issue #19.

Author: NomadsReach

## What it does

- **Twofold radiation.** Particulate rads (vanilla Rads AV, cleared by RadAway) plus a
  persistent **tissue damage** pool that survives saves, builds from sustained exposure,
  and is NOT cleared by RadAway. **Permanent (scarred) tissue** floors how far tissue can
  heal. Item-triggered tissue cures are currently disabled fail-closed until a
  source-identifying item-use adapter is verified.
- **10-stage severity** (100 to 1000) drives SPECIAL penalties (END/STR) and Pip-Boy stage perks.
- **STALKER-style storms.** Ash Fallout and Rad Firestorm (lethal, shelter-or-die) plus Toxic
  Sulfur Storm, Acid Rain, and Dust Storm, with APPROACH / WARNING / ACTIVE / AFTERMATH phases,
  siren audio, shelter detection, storm atmosphere, and native storm FX.
- **Environmental survival expansion.** The native cold-survival policy core models body
  temperature, wetness, insulation, shelter, heat, activity, and reversible hypothermia stages.
  Runtime acquisition of weather, water exposure, armor insulation, and heat sources is being
  added behind verified engine inputs rather than guessed native calls.
- **Cures.** Sleep/wait/fast-travel can regress reducible tissue over elapsed game time. RadAway
  and the craftable **Radiation Chelation Compound** do not currently change tissue damage: their
  item-triggered effects are disabled fail-closed pending a verified source-identifying item-use adapter.
- **PrismaUI HUD** (optional, degrades to no-op without the framework): gas-mask visor, storm
  banners, debug controls, and Rad-owned toxicity/filter widgets. The Rad debug menu includes a
  temporary built-in layout editor for those widgets. When PrismaHUD_F4 is detected, PrismaHUD
  automatically becomes the layout owner and its shared editor takes over.
- **Needs observation (vanilla Papyrus quest).** Hunger, thirst and sleep debt advance on a
  game-time timer, persist with the quest in the save, and are relieved by real food/drink use and
  real sleep. Consumption is confirmed by an inventory count change, so dropping, storing, selling
  and crafting cannot feed the player. The pools are mirrored into three `GLOB` records so the
  native HUD can read them. `HC_Manager` and every `HC_*` form are untouched, and the layer
  suspends itself while vanilla Survival is active. No script extender is required. Stage penalties
  stay dormant until the `SPEL` records and the `ApplyStagePenalty` hook are wired; see
  [docs/code-notes/Rad-Overhaul-Fallout-4.md](docs/code-notes/Rad-Overhaul-Fallout-4.md).
- **Companion patches.** `RadOverhaul_GearPatch.esp` buffs the hazmat suit's radiation resistance;
  `RadOverhaulSurPatch.esp` is also included in the deployment package.

See [docs/PROGRESS.md](docs/PROGRESS.md) for the implemented-systems breakdown and the
DLL-to-ESP FormID contract.

## Layout

```
Rad-Overhaul-Fallout-4/
├── src/              C++ F4SE plugin source
├── data/             runtime configuration loaded by the DLL
├── deploy/           game-ready package
│   ├── F4SE/         plugin config/runtime files
│   ├── PrismaUI_F4/  HUD views and assets
│   ├── Scripts/      deployed Papyrus sources/compiled scripts
│   ├── Sound/        runtime audio
│   ├── Meshes/ materials/ textures/  runtime visual assets
│   ├── RadiationOverhaul.esp
│   ├── RadOverhaul_GearPatch.esp
│   └── RadOverhaulSurPatch.esp
├── docs/             status, changelog, build notes, and historical investigations
├── NOTICE.md         third-party notices/credits
└── xmake.lua         DLL build definition
```

## Build the DLL

```powershell
cd path\to\Rad-Overhaul-Fallout-4
xmake
# output: build\release\FalloutAnomalyRadiation.dll
```

Close Fallout 4 before redeploying the DLL because a loaded DLL is file-locked.

`xmake.lua` can auto-copy a successful build to a live mod directory when `XSE_FO4_MODS_PATH` is set.
The local development fallback also covers `C:\Modlists\TMR\mods\Fallout Anomaly - Radiation Overhaul`.

## Build the ESP

See [docs/ESP_BUILD_PIPELINE.md](docs/ESP_BUILD_PIPELINE.md) for the current ESP/Spriggit workflow.
The historical `tooling/` tree referenced by older notes is not part of the current repository root.

## Docs

- [docs/STATUS.md](docs/STATUS.md) - current release status, verification gates, and known work
- [docs/PROGRESS.md](docs/PROGRESS.md) - implemented systems, pending ideas, FormID contract
- [docs/code-notes/COLD_SURVIVAL.md](docs/code-notes/COLD_SURVIVAL.md) - clean-room climate model and integration plan
- [docs/ESP_BUILD_PIPELINE.md](docs/ESP_BUILD_PIPELINE.md) - ESP/Spriggit build pipeline
- [docs/CHANGELOG.md](docs/CHANGELOG.md) - dated change history
- [docs/ISSUES_FIXED.md](docs/ISSUES_FIXED.md) - storm-system bug root-cause log
