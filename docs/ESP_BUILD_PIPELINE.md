# RadiationOverhaul.esp — Data → Build → Compile Pipeline

## Verified recovery and audit (2026-09-26)

The old generator/source directory documented below was absent on this host. The deployed ESP at source head `49b9ba9a3de50951f2446b08586d9d008386babd` was instead audited through a task-local copy of the pinned official [Spriggit 0.40.1 release](https://github.com/Mutagen-Modding/Spriggit/releases/tag/0.40.1). This recovers the serializer, not the missing generator. No generator or compiled deploy file was changed.

Run from `Rad-Overhaul-Fallout-4` in the task worktree, with new output directories:

```powershell
dotnet tool install Spriggit.Json.Fallout4 --version 0.40.1 --tool-path ./build-msvc/spriggit-0.40.1 --add-source https://api.nuget.org/v3/index.json
$serializer = './build-msvc/spriggit-0.40.1/Spriggit.Json.Fallout4.exe'
& $serializer serialize -i ./deploy/RadiationOverhaul.esp -o ./build-msvc/esp-audit/baseline/RadiationOverhaul.esp -g Fallout4 -p Spriggit.Json.Fallout4 -v 0.40.1 -u true
New-Item -ItemType Directory -Path ./build-msvc/esp-audit/roundtrip -Force
& $serializer deserialize -i ./build-msvc/esp-audit/baseline/RadiationOverhaul.esp -o ./build-msvc/esp-audit/roundtrip/RadiationOverhaul.esp
& $serializer serialize -i ./build-msvc/esp-audit/roundtrip/RadiationOverhaul.esp -o ./build-msvc/esp-audit/roundtrip-json/RadiationOverhaul.esp -g Fallout4 -p Spriggit.Json.Fallout4 -v 0.40.1 -u true
```

The output directory must exist before deserialization. Both serializations fail on unknown fields. All 215 JSON files were structurally identical after the round trip; master order and record types/FormKeys also matched. Binary hashes differ, so the original deploy ESP was retained. Evidence: [audit report](proof/esp-audit-20260926.json).

Issue #67's self-owned `000854:RadiationOverhaul.esp` and `000870:RadiationOverhaul.esp` are already absent, as is the obsolete quest EditorID. The intentional `RadOverhaul_Stage1000`, `RadOverhaul_Perk1000` and `RadOverhaul_StormFXQuest` remain. References to `000854:Fallout4.esm` in vanilla ingestible overrides are a different FormKey and were preserved. `tests/verify_ghoul_contract.py` now checks the compiled ESP as well as the native source to prevent the obsolete records from returning.

The remaining historical pipeline below records the former generator workflow and paths; its unavailable generator cannot be certified by this round trip.

**Last updated:** 2026-07-18 (path corrections only — see note below; the pipeline mechanics
below are unchanged from 2026-06-06 and were not re-verified end-to-end this pass)
**Tool:** Spriggit 0.40.1 (`Spriggit.Json.Fallout4` serializer)
**Output:** `RadiationOverhaul.esp` (plain ESP, ~94 KB)

> **2026-07-18 correction:** this doc previously said to `cd` into the Rad Mod project folder
> itself and run `gen_esl.py`/`merge_overrides.py` there, with the source tree at
> `esl_src\`/`esl_build\` right underneath it. **That's no longer where the scripts live.**
> `Projects\Rad Mod\tooling\` (which held them) doesn't exist anymore — they now live at
> **`Tools\PluginEditTool\Patched Data\Source\`**, alongside their own `esl_src\`/`esl_build\`
> (same subfolder names, just relocated up a level to the shared plugin-edit toolchain). Every
> `cd`/path below has been updated to match. The workspace itself was also reorganized
> 2026-06-30 — the project root is `Projects\Rad Mod\`, not the `Non Prisma Projects\Rad Mod`
> this doc originally used.

This document describes the complete round-trip for producing the companion plugin:
**parse** existing plugins into JSON → **build** our JSON record tree from scripts →
**read/merge** vanilla overrides → **compile** the JSON tree back into a binary `.esp`.

The plugin is the *content* half of the mod (records: globals, perks, spells, magic
effects, an ingestible, IMAD, GMST/spell overrides). The *behavior* half is the
`FalloutAnomalyRadiation.dll` F4SE plugin, which reads these records by FormID at
runtime. This doc only covers the ESP. See `ARCHITECTURE.md` for the DLL.

---

## 0. Mental model — why JSON at all

Bethesda `.esp/.esm/.esl` files are a packed binary record format. They are painful
to author or diff by hand. **Spriggit** (built on Mutagen) converts losslessly
between the binary plugin and a folder of human-readable JSON — one file per record,
grouped into folders by record type (`Globals/`, `Perks/`, `Spells/`, …).

```
   binary .esp  ⇄  Spriggit  ⇄  folder of per-record JSON
   (game eats)      (lossless)    (we author / script / git-diff)
```

We work entirely in the JSON representation, generate it programmatically with
Python, and compile it back to binary only at the very end.

---

## 1. PARSE — serialize existing plugins to JSON (research input)

To copy a vanilla/DLC record exactly (e.g. RadAway, the Glowing Sea radstorm spell,
`RadImod` IMAD), and to mine other radiation mods for override records, we first
serialize those plugins into JSON.

```powershell
# Serialize ANY plugin (Fallout4.esm, a DLC, or a research mod) to a JSON folder.
spriggit serialize `
  -i "path\to\SomeMod.esp" `
  -o "Research\Outpuit\SomeMod.esp" `
  -p Spriggit.Json.Fallout4 `
  -v 0.40.1
```

Output lives under `Research\Outpuit\<ModName>\<RecordType>\<EditorID> - <FormID>_<Master>.json`.
The full Fallout4.esm parse (every record type — `Ingestibles/`, `Spells/`,
`ImageSpaceAdapters/`, `GameSettings/`, …) is the source we copy verbatim records from.

Research mods serialized for override-mining live alongside it (Radiation Rebalance,
ImmersiveRadiation, whisperRadiationStays, etc. — see `merge_overrides.py:SOURCE_MODS`).

> **Note on the filename convention.** Spriggit names each record file
> `<EditorID> - <FormID>_<Master>.json`. The `_<Master>` suffix tells Spriggit which
> master owns the FormKey. Our generators reproduce this exactly so the records
> deserialize cleanly.

---

## 2. BUILD — generate our JSON record tree (`gen_esl.py`)

`gen_esl.py` is the primary generator. It **wipes and rebuilds**
`esl_src\RadiationOverhaul.esp\` from scratch, then writes:

| Records | FormID range | Purpose |
|---|---|---|
| 2 Magic Effects | `0x800`, `0x801` | `PeakValueMod` on Endurance / Strength (no `Recover` → permanent while ability active) |
| 10 Spells (abilities) | `0x810`–`0x819` | one per radiation stage, stage-specific END/STR magnitudes |
| 10 Perks | `0x820`–`0x829` | one per stage; each carries its ability via `PerkAbilityEffect` (shows in PERKS + STATUS tabs) |
| IMAD | `0x830` | green "radiation sickness" screen tint (re-keyed copy of vanilla `RadImod`) |
| Global `RadOverhaul_TissueDamage` | `0x850` | persistent particulate/tissue pool (survives saves) |
| Global `RadOverhaul_PermTissueDmg` | `0x851` | scarred tissue; no doctor or item cure path is active |
| Ingestible `RadOverhaul_ChelationCompound` | `0x860` | planned permanent-tissue cure item; no verified DLL item-use adapter exists |
| Spell override `GlowingSeaPlayerRadiationRadStorm` | `23EC91:Fallout4.esm` | retuned Glowing Sea radstorm magnitude (8.0) |
| GMST override `fRadsDamageFactor` | `0769CD:Fallout4.esm` | master radiation multiplier 0.60 (≈ +300% vs vanilla 0.15) |
| Ingestible override `RadAway` | `023742:Fallout4.esm` | guaranteed-working cure (wins load-order nerfs) |

### Key design decisions baked into the generator

- **Plain ESP, not ESL.** The header `Flags: []` (no `Light` flag) makes it a *full*
  ESP that loads **last** and **wins** conflicts over 500+ mods. An ESL is force-loaded
  early and would lose. Despite folder/var names containing "esl", the output is a
  regular ESP. (`gen_esl.py:14-17`)
- **Custom FormIDs use the `0x800+` low range**; override records keep their original
  `XXXXXX:Fallout4.esm` FormKey so our copy *replaces* the vanilla record at load.
- **Masters declared explicitly** in `RecordData.json`: Fallout4.esm + DLCCoast +
  DLCNukaWorld (DLC masters are needed because merged overrides touch DLC forms).
- **Vanilla copies are re-keyed, not hand-built.** IMAD and RadAway are loaded from
  the `Research\Outpuit\Fallout4.esm\...` parse, mutated minimally, and rewritten —
  this guarantees field-structure correctness.

Run it:

```powershell
cd "E:\F4SE OG\Tools\PluginEditTool\Patched Data\Source"
python gen_esl.py
```

To retune, edit the constants at the top of the relevant section and re-run:
`ENV_GLOWINGSEA_MAG`, `RADS_DAMAGE_FACTOR`, `RADAWAY_REMOVE`, `STAGES[]`.

---

## 3. READ / MERGE — fold in vanilla overrides (`merge_overrides.py`)

`merge_overrides.py` mines the serialized research mods (step 1) and copies *only
safe vanilla/DLC override records* into our JSON tree, layering on top of `gen_esl.py`.

**Safety filters (all must pass):**
- Only **overrides of vanilla/DLC records** — never a mod's custom-owned FormKey
  (those carry meshes/scripts/dependencies we can't ship). (`record_ok`)
- Every referenced FormKey must point to an **allowed master** (Fallout4 + DLCs only).
- **Radiation-themed only** — EditorID must contain `rad/nuk/glow/atom/geiger/hazmat/isotope`
  (theme mods like `whisperRadiationStays` bypass this since all their content is on-theme).
- **Excluded:** PIRAD entirely; leveled lists (broad LOOT side effects); deleted records;
  records with non-ASCII/mojibake strings; and a hard `EXCLUDE_FORMKEYS` set
  (RadAway, Rad-X, `fRadiationHealthCurve` — we keep those vanilla or author our own).
- **Precedence:** `SOURCE_MODS` order — first mod to claim a FormKey wins.

```powershell
# Dry run first — prints the merge plan, required masters, and overlaps:
python merge_overrides.py

# Then apply into esl_src:
python merge_overrides.py --apply
```

> **Order matters:** run `gen_esl.py` **before** `merge_overrides.py --apply`.
> `gen_esl.py` wipes `esl_src\`; the merge then adds override files on top.

After both scripts, `esl_src\RadiationOverhaul.esp\` is the complete Spriggit source tree.

---

## 4. COMPILE — JSON tree back to binary `.esp`

This is the step with a **known gotcha**.

### ⚠️ The `spriggit` CLI wrapper is broken for this source tree

The top-level `spriggit` global tool fails with:

```
System.Data.DataException: Could not locate source info from ...\esl_src\RadiationOverhaul.esp
   at Spriggit.Engine.Services.Singletons.GetMetaToUse.Get(...)
```

This happens **even though** `RecordData.json` contains a valid `SpriggitSource`
(`{PackageName: "Spriggit.Json.Fallout4", Version: "0.40.1"}`) and `-p`/`-v` are passed
explicitly. The wrapper's meta-resolution layer can't locate the serializer package
metadata. `--Debug` (NuGet cache reset), absolute paths, alternate output dirs, and
adding a `spriggit.json` to the source folder **do not fix it.**

### ✅ The working bypass — call the serializer exe directly

The `spriggit` CLI is only a thin launcher around per-game **translation packages**,
which it caches under `%LOCALAPPDATA%\Temp\Spriggit\Translations\`. That cached exe
has its own `create-plugin` command and works perfectly:

```powershell
$exe = "$env:LOCALAPPDATA\Temp\Spriggit\Translations\Spriggit.Json.Fallout4\0.40.1\Spriggit.Json.Fallout4.exe"

& $exe create-plugin `
  -i "E:\F4SE OG\Tools\PluginEditTool\Patched Data\Source\esl_src\RadiationOverhaul.esp" `
  -o "E:\F4SE OG\Tools\PluginEditTool\Patched Data\Source\esl_build\RadiationOverhaul.esp"
```

Expected output:

```
[INF] Starting deserialization from ...\esl_src\... to ...\esl_build\RadiationOverhaul.esp
[INF] Finished deserializing
```

> **If the cached exe doesn't exist yet:** run any `spriggit serialize ...` once
> (step 1). The CLI downloads and caches the `Spriggit.Json.Fallout4` translation
> package on first use, after which the exe is present at the path above.
>
> **Terminology:** in Spriggit, "deserialize" = JSON→plugin (compile), "serialize" =
> plugin→JSON (parse). `create-plugin` is an alias for deserialize.

Verify the result — a complete build is **~94 KB**; a near-empty/failed one is < 10 KB:

```powershell
(Get-Item "E:\F4SE OG\Tools\PluginEditTool\Patched Data\Source\esl_build\RadiationOverhaul.esp").Length
```

---

## 5. DEPLOY — copy to MO2

**2026-07-18 correction:** the mod-folder name and DLL build-output path below were stale
(pre-2026-06-30-reorg). Per `brain/PROJECTS.md` (generated from the live `xmake.lua`, confirmed
2026-07-18), the DLL's actual deploy target is
`E:\Modlists\Fallen World Alpha 2\mods\Fallen World Rad Overhaul\F4SE\Plugins`, and the build
output is `Projects\Rad Mod\build\windows\x64\release\FalloutAnomalyRadiation.dll` (xmake's real
output path, not `build\release\`). The ESP compile-output path in the source tree (step 4) is
unaffected by the reorg. **Always targeted copies — never delete the mod folder wholesale**, per
the workspace's standing MO2-safety rule (mod folders can hold untracked runtime files not
recoverable from git).

```powershell
$mo2 = "E:\Modlists\Fallen World Alpha 2\mods\Fallen World Rad Overhaul"
$scripts = "E:\F4SE OG\Tools\PluginEditTool\Patched Data\Source"
$rad = "E:\F4SE OG\Projects\Rad Mod"

# 1. ESP (compiled this pipeline)
Copy-Item "$scripts\esl_build\RadiationOverhaul.esp" "$mo2\RadiationOverhaul.esp" -Force

# 2. F4SE DLL (built separately with xmake — see ARCHITECTURE.md; cmake is retired workspace-wide)
Copy-Item "$rad\build\windows\x64\release\FalloutAnomalyRadiation.dll" "$mo2\F4SE\Plugins\FalloutAnomalyRadiation.dll" -Force

# 3. PrismaUI HUD view
Copy-Item "$rad\...\views\Interface\FalloutAnomaly\radhud.html" `
          "$mo2\PrismaUI_F4\views\Interface\FalloutAnomaly\radhud.html" -Force
# ^ exact source path for step 3 not re-verified this pass (out of scope for a path-only
#   correction) — confirm the live view's location under Projects\Rad Mod before relying on this line.
```

---

## 6. End-to-end quick reference

```powershell
cd "E:\F4SE OG\Tools\PluginEditTool\Patched Data\Source"

# (1) PARSE — only when adding a new vanilla/research source (one-time per source)
# spriggit serialize -i "SomeMod.esp" -o "Research\Outpuit\SomeMod.esp" -p Spriggit.Json.Fallout4 -v 0.40.1

# (2) BUILD our records
python gen_esl.py

# (3) MERGE vanilla overrides (dry run, then apply)
python merge_overrides.py
python merge_overrides.py --apply

# (4) COMPILE to binary (use the cached translation exe — NOT the spriggit wrapper)
$exe = "$env:LOCALAPPDATA\Temp\Spriggit\Translations\Spriggit.Json.Fallout4\0.40.1\Spriggit.Json.Fallout4.exe"
& $exe create-plugin -i "esl_src\RadiationOverhaul.esp" -o "esl_build\RadiationOverhaul.esp"

# (5) DEPLOY (see section 5)
```

---

## 7. FormID map (authoritative)

| FormID | Type | EditorID |
|---|---|---|
| `0x800` | MagicEffect | `RadOverhaul_ReduceEND` |
| `0x801` | MagicEffect | `RadOverhaul_ReduceSTR` |
| `0x810`–`0x819` | Spell (ability) | `RadOverhaul_Stage100` … `Stage1000` |
| `0x820`–`0x829` | Perk | `RadOverhaul_Perk100` … `Perk1000` |
| `0x830` | IMAD | `RadOverhaul_SicknessIMAD` (green sickness tint) |
| `0x831` | IMAD | `RadOverhaul_BlinkIMAD` (pain blink fade-to-black) |
| `0x840` | Weather | `RadOverhaul_AshFallout` (lethal blowout — gray fallout) |
| `0x841` | Weather | `RadOverhaul_Firestorm` (lethal blowout — burning sky) |
| `0x842` | Weather | `RadOverhaul_SulfurStorm` (ambient hazard — toxic haze) |
| `0x843` | Weather | `RadOverhaul_AcidRain` (ambient hazard — green rain) |
| `0x850` | GlobalFloat | `RadOverhaul_TissueDamage` |
| `0x851` | GlobalFloat | `RadOverhaul_PermTissueDmg` |
| `0x852` | GlobalFloat | `RadOverhaul_NextEmission` (storm system gate) |
| `0x853` | GlobalFloat | `RadOverhaul_ForceStorm` (script debug: force a storm 1-4) |
| `0x854` | *(RESERVED)* | was `RadOverhaul_IsGhoul` — ghoulification fully removed 2026-07-03; kept RESERVED, never reused, so old saves don't misresolve it. See `PROGRESS.md`. |
| `0x860` | Ingestible | `RadOverhaul_ChelationCompound` (Medicine-flagged aid item) |
| `0x861` | ConstructibleObject | `co_chem_RadOverhaul_Chelation` (Chem Station recipe, Medicine tab) |
| `23EC91:Fallout4.esm` | Spell (override) | `GlowingSeaPlayerRadiationRadStorm` |
| `0769CD:Fallout4.esm` | GMST (override) | `fRadsDamageFactor` |
| `023742:Fallout4.esm` | Ingestible (override) | `RadAway` |

These FormIDs are the contract between the ESP and the DLL — the DLL hardcodes them
(e.g. `kPermTissueGlobalID = 0x851`, `kTissueGlobalID = 0x850`,
`kNextEmissionGlobalID = 0x852`, and the storm weather IDs 0x840–0x843 in the DLL
`kStormTypes[]` table). **Changing a FormID in `gen_esl.py` requires a matching change
in the DLL constants.**

### `RadSurvival.esp` (survival quest records)

Hand-authored with FO4IDE rather than through this Spriggit pipeline, and shipped alongside
`RadiationOverhaul.esp`. It overrides no vanilla record and declares `Fallout4.esm` as its single
master, so every record below is a new form at master index 1 and the full id is `0x010008xx`.

| Form-ID | Type | EditorID | Notes |
|---|---|---|---|
| `01000800` | QUST | `RadSurvivalQuest` | Flags `0x09` (Start Game Enabled + Starts Enabled), priority 0, two VMAD scripts |
| `01000801`-`01000803` | GLOB (float) | `RadSurvival_Hunger`, `_Thirst`, `_Sleep` | 0..1 pools, mirrored for the native HUD |
| `01000804`-`01000806` | MGEF | `RadSurvival_HungerPenalty1-3` | Peak Value Modifier on CarryWeight: -10 / -25 / -45 |
| `01000807`-`01000809` | MGEF | `RadSurvival_ThirstPenalty1-3` | Peak Value Modifier on ActionPoints: -5 / -15 / -30 |
| `0100080A`-`0100080C` | MGEF | `RadSurvival_SleepPenalty1-3` | Peak Value Modifier on HealRateMult: -0.15 / -0.3 / -0.5 |
| `0100080D`-`01000815` | SPEL (Ability) | `RadSurvival_HungerPenaltySpell1-3`, `_ThirstPenaltySpell1-3`, `_SleepPenaltySpell1-3` | constant effect, self, one effect each |
| `01000816`-`01000818` | MGEF | `RadSurvival_Fever1-3` | Peak Value Modifier on DamageResist: -10 / -25 / -40 |
| `01000819`-`0100081B` | MGEF | `RadSurvival_Wasting1-3` | Peak Value Modifier on SpeedMult: -5 / -10 / -15 |
| `0100081C`-`0100081E` | SPEL (Ability) | `RadSurvival_FeverSpell1-3` | constant effect, self |
| `0100081F`-`01000821` | SPEL (Ability) | `RadSurvival_WastingSpell1-3` | constant effect, self |
| `01000822` | GLOB (float) | `RadSurvival_Risk` | 0..1 disease exposure pool |
| `01000823` | GLOB (float) | `RadSurvival_Disease` | `DiseaseId * 10 + DiseaseStage`; 0 while healthy, stage 0 while incubating |

The quest VMAD holds two scripts, and each owns its own properties. `RadSurvivalQuest` carries the
object properties `RadSurvival_Hunger`, `RadSurvival_Thirst`, `RadSurvival_Sleep`,
`RadSurvival_Risk` and `RadSurvival_Disease`, each bound to the `GLOB` of the same name, plus a
`Diseases` property bound to the quest form itself. `RadSurvivalDiseases` holds the disease state and
registers no events. `tests/verify_survival.py` asserts the record set, the spell effect lists, the
VMAD ownership and binding, the QUST flags, the magnitudes, and that the shipped plugin still matches
the tuning manifests in `tools/rad_penalty_spells.json` and `tools/rad_diseases.json`.

**Hand-authored, not Spriggit.** There is no YAML serialization of this plugin and no Spriggit
round-trip in CI. FO4IDE over MCP is the only authoring path, `tests/plugin_reader.py` is the
read-only binary checker used by the contract test, and a record change means re-authoring through
the tool, re-saving the plugin into `deploy/`, and re-running `tests/verify_survival.py`.

### Papyrus artifacts (`deploy/Scripts`)

The survival layer is vanilla Papyrus: `RadSurvivalQuest.psc` owns the pools, the events and the
stage abilities, `RadSurvivalDiseases.psc` owns the disease state, and `RadStormFX.psc` is the storm
visual effect. `RadSurvivalQuest.psc` now needs the F4SE plugin (RadiationOverhaul) loaded: its
`RadNatives.GetReliefFlags` global native is the `[Relief]` INI bridge, so without the DLL those extras stay off and an
ingestible equip logs an unbound-native error. Every script ships as committed bytecode next to its
source, so the Papyrus layer needs no compiler at build time - but a source edit that never reached
the `.pex` would ship stale behaviour.

**Compile.** FO4IDE's built-in engine, no Creation Kit:

```
compile_papyrus source=<repo>/Rad-Overhaul-Fallout-4/deploy/Scripts/Source/User/<Script>.psc
                output=<repo>/Rad-Overhaul-Fallout-4/deploy/Scripts
                imports=<vanilla script sources, e.g. git/papyrus-base-20261025>
                flags=Institute_Papyrus_Flags.flg
                debug_info=false
                engine=builtin
```

`debug_info` defaults to `true` in the tool, which writes a different (larger) header and debug
player, so it is not the shape the committed bytecode has: a compile without it fails the byte
comparison even when the source is unchanged. Every shipped `.pex` here was built with
`debug_info=false`, and `tools/stamp_papyrus.py` records that invocation.

The engine is deterministic: recompiling an unchanged source reproduces the committed `.pex` byte for
byte, which is what makes a hash gate meaningful.

**Stamp.** `tools/stamp_papyrus.py` writes `deploy/Scripts/papyrus-build.json` immediately after a
compile, recording each source and bytecode hash. Commit the `.psc`, the `.pex` and the stamp
together. `tests/verify_papyrus_pex.py` recomputes both hashes (normalising line endings, so a CRLF
checkout and an LF one agree), refuses bytecode that names a source other than its own, and requires
every `.pex` in the folder to be stamped. That check runs in both Rad workflows, so a source edit
without a recompile fails CI even though CI cannot compile Papyrus itself.

**Known limitation - the recorded source path.** The built-in compiler writes the source *path* into
the `.pex` header, and there is no flag to suppress it. It resolves whatever it is given, so a
relative source argument is recorded as an absolute one: compiling
`deploy/Scripts/Source/User/RadSurvivalQuest.psc` (FO4IDE `1ee68cf38`, checked 2026-10-26) still wrote
`E:\Projects\...\deploy\Scripts\Source\User\RadSurvivalQuest.psc` into the header. Compiling from a
staging folder only swaps that path for another absolute one, so the string cannot be removed through
the compiler interface, and rewriting the bytes by hand is the retired binary-patcher route.
`papyrus-build.json` records the exact string each `.pex` carries, so a toolchain that stops writing
it, or a build made somewhere else, shows up as a stamp mismatch instead of drifting silently. Reported upstream with the `HEDR`
`numRecords` quirk; a FO4IDE build that omits the path needs a recompile and a re-stamp, not a code
change.

**CI cannot compile Papyrus.** `FO4RecordEditor.exe` is a Windows binary and the Rad build job runs on
the MSVC Windows runner without FO4IDE installed, so the workflows run the stamp check rather than
the compiler. If FO4IDE is ever provisioned on that runner, `compare`-style compilation can be added
in front of the stamp step.
