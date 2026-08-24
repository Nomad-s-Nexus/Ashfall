# Fallout Anomaly — Radiation Overhaul · PROGRESS

Living status of what's actually implemented. Newest work at the top.
Self-contained: F4SE DLL + `RadiationOverhaul.esp` + PrismaUI HUD. Only `Fallout4.esm`
(+ DLCCoast/DLCNukaWorld for merged overrides) referenced — no other plugin deps.

**Last updated:** 2026-07-19

> This file lists what IS implemented. For what is **not** finished — blockers, open items,
> decisions awaiting the author — see **`STATUS.md`**, which is the single authoritative list.
> Superseded planning and review docs (the 06-30/07-03 code reviews, the completed HUD migration
> plan, the old NEEDS_INPUT shortlist) are in `archive/`.

---

## ✅ Implemented

### Core radiation (Pillar I — twofold)
- **Particulate rads** = vanilla Rads AV (RadAway clears it).
- **Tissue damage** = persistent pool in global `RadOverhaul_TissueDamage` (0x850), survives saves.
  Builds from sustained exposure (>100 rads), heals slowly; NOT cleared by RadAway.
- **Permanent (scarred) tissue** = global `RadOverhaul_PermTissueDmg` (0x851). Floors how far
  tissue can heal. The chelation item effect is disabled fail-closed pending a verified
  source-identifying item-use adapter.
- 10-stage severity (100→1000) drives SPECIAL penalties (END/STR via C++) + Pip-Boy stage perks.
- GMST `fRadsDamageFactor` = 0.60 (≈ +300% radiation intake vs vanilla 0.15).

### Ghoulification — FULLY REMOVED (2026-06-28; remnants purged 2026-07-03)
Player ghoulification was removed by request. History: the C++ logic went first (2026-06-28), the
one-time `RevertGhoulIfDisabled()` save migration was retired 2026-06-30, and on 2026-07-03 the
remaining remnants were purged end to end: QUST 0x870 `RadOverhaul_GhoulQuest` + GLOB 0x854
`RadOverhaul_IsGhoul` stripped from the shipped ESP (and from `gen_esl.py` + `tooling\esl_src`, so
regeneration cannot resurrect them), the stub `RadGhoulQuest.pex` and the optional
`RadOverhaul_GhoulRace.esp` deleted from the live mod. FormIDs 0x854 / 0x870 stay RESERVED so they
are never reused against old saves. The 10th severity stage keeps its "Ghoulification" LABEL
(perk/spell 0x829/0x819 - a plain END/STR debuff, no race change). The storm feral-ghoul pack
system (`SpawnStormGhouls`) is a separate enemy-NPC feature and is unaffected.

### Cures
- **RadAway** — its item-triggered tissue heal is disabled fail-closed pending a verified
  source-identifying item-use adapter. It still clears vanilla particulate rads.
- **Sleep/wait** — heals tissue on load based on elapsed game-time (via GameDaysPassed).
- **Doctor** — detected by big rads drop + HP gain: clears permanent + 50% tissue.
- **Radiation Chelation Compound** (ingestible 0x860, Medicine-flagged) — the item and recipe
  exist, but its tissue effect is disabled fail-closed pending the same verified adapter.
  - **Craftable at the Chemistry Station** under the Healing/Medicine tab (COBJ 0x861).
  - Also obtainable from a physician (high value, 3500).

### STALKER-style storms (our own NAC-flavored types — self-contained)
One scheduler rolls a weighted type when a storm fires (game-time via GameDaysPassed):
- **Ash Fallout** (0x840, lethal blowout — gray)
- **Rad Firestorm** (0x841, lethal blowout — orange/red)
- **Toxic Sulfur Storm** (0x842, ambient hazard — yellow-green)
- **Acid Rain** (0x843, ambient hazard — green; built on vanilla CommonwealthRain = real rain)
- **Dust Storm** (0x844, ambient hazard — tan/brown GMW-style blowout; densest fog wall + blowing
  dust particles; counts as rad weather so the gas mask auto-equips)

Phases: `WARNING → ACTIVE → AFTERMATH`. Lethal = shelter-or-die (interior cell check), exposed
ramps rads + direct HP damage, one-time exterior-NPC burst at the peak. Ambient = milder always-on
effects (halved when sheltered), no shelter-death. `Sky::ForceWeather` sets the sky per type.

### Audio
- Lethal storm: siren (`radstorm_warning.wav`) on warning, then **cut at ~20 s → `crowcall.wav`** plays.
- `crowcall.mp3` converted to 16-bit PCM WAV (engine doesn't play mp3).

### Feedback / HUD (PrismaUI, optional — degrades to no-op without the framework)
- Sleek tactical vitals (no panel box): rads, tissue, health, AP. Each row = severity-tinted
  mask icon (radioactive / biohazard / heart-rate / running-man PNGs recolour green-to-red with
  condition) + thin bar + value. Permanent tissue shown as red diagonal hatching on the tissue bar.
- Per-bar colour, position, and size are all user-configurable and persist to the INI.
- Auto-fade: show in combat / when hurt / always, with a configurable fade delay (INI + debug menu).
- HUD show/hide keybind (INI `ToggleKey`, polled per-frame so taps never miss).
- **In-game corner notifications** (`SendHUDMessage`) for storm approach / peak / exposed / passing —
  work with or without PrismaUI.

### Debug tools
- **Shift+F8** → PrismaUI debug menu (`radhud.html`, polled per-frame so it opens on first press):
  HUD VISIBILITY toggles (always / in-combat / when-hurt); HUD LAYOUT (drag a bar to move, drag its
  corner box to resize, ALIGN COLUMN tidies them into a column, RESET); BAR COLOURS (click RADS /
  TISSUE / HEALTH / AP to cycle that bar's colour, saved to INI); trigger/end each storm; force each
  storm sky / clear weather (visual only); +rads / full heal; +tissue / +permanent / clear all;
  give Chelation / RadAway / Rad-X. (Scrollable panel.)
- Console: `player.modav Rads 600` to test the HUD. (NOTE: `set RadOverhaul_* to N` does NOT work —
  FO4 strips EditorIDs at runtime; use the hotkeys/menu.)

---

## ⏳ Pending / ideas
- NPC flee-to-shelter AI during storms (needs Papyrus packages).
- Outdoor "under cover" detection via roof raycast (partial shelter).
- Post-storm artifact/loot reward loop.
- Pillars II–IV (diseases, protective-gear durability, source-specific nuclear blasts).
- Geiger-counter sound tied to pre-resistance radiation (needs Ghidra RE of the rad function).
- Optional: siren fade-out instead of hard cut; crows on ambient storms too.

---

## 🔧 Build & deploy (quick ref)
See `ESP_BUILD_PIPELINE.md` for the full pipeline. Short version:
```
python gen_esl.py                 # generate records
python merge_overrides.py --apply # fold in vanilla overrides
# compile (the spriggit CLI is broken — use the cached translation exe):
%LOCALAPPDATA%\Temp\Spriggit\Translations\Spriggit.Json.Fallout4\0.40.1\Spriggit.Json.Fallout4.exe ^
  create-plugin -i esl_src\RadiationOverhaul.esp -o esl_build\RadiationOverhaul.esp
cd "Rad Mod" && xmake     # build the DLL
```
Deploy targets: `D:\Games\ModlistDownloads\mods\Fallen World Rad Overhaul` (primary),
`E:\Modlists\Fallen World Alpha 2\mods\F4SE`. Portable copy: `D:\Games\Patched Data\Source`.
**A DLL loaded by a running game is file-locked — close FO4 before redeploying the DLL.**

## 🗺️ FormID contract (DLL ↔ ESP — keep in sync)
`0x800/801` MGEF · `0x810-819` stage spells · `0x820-829` stage perks · `0x830` sickness IMAD ·
`0x831` blink IMAD · `0x840-844` storm weathers (844 = Dust Storm) · `0x845/0x846` RESERVED (was
dust/ash SPGD — removed, see below) · `0x850` tissue · `0x851` permanent tissue · `0x852` storm gate ·
`0x853` force-storm (script only) · `0x860` chelation item · `0x861` chelation recipe ·
`0x880-884` storm atmosphere IMADs (884 = Dust Storm) · `0x802` rad-weather FormList (auto-mask) ·
`0x803` gas-mask filter · `0x804` gas-mask repair kit.

Storm visuals: the DLL sets per-type fog from `kStormTypes[].fog{Near,Far,Power}` at load (overrides
the ESP record fog), so visibility is tuned in `RadiationEngine.cpp`, not the weather record. Dust =
densest (near 900 / far 2600 / pow 0.55). The Dust Storm blowout is delivered by fog + the brown
darkening IMAD (0x884) — same safe mechanism as the Ash storm.

⚠ Blowing-particle precipitation was attempted (SPGD via the weather `Precipitation` field) and
REMOVED: vanilla has no real dust *precipitation* material — `Effects\CameraAttachWeatherParticlesDusty.BGEM`
is a camera-attach EFFECT material, and feeding it to a precipitation SPGD rendered magenta
(missing-texture "purple sky") and HARD-FROZE the game. Proper blowing dust needs a SHIPPED weather-
particle BGEM+DDS (clean-room), tracked as a follow-up. FormIDs 0x845/0x846 reserved for it.
