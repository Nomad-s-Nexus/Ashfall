# Rad Mod - Needs Your Input / Deferred (updated 2026-07-12)

---

## 00. FIRESTORM NUKE — SAFE BLAST + QUEST REPAIR DEPLOYED; NEEDS GAME TEST (2026-07-12)

Decision made: **no missile yet**. A safe native distant `FatManExplosion` plus a quest stage-table
repair candidate are deployed. The quest's five empty stage shells now each contain an inert log-entry
anchor, matching a working vanilla quest's runtime-materializable structure. The DLL also emits a
one-shot `[StormFX/StageDiag]` dump before calling `SetStage`.

Test outdoors with forced Firestorm. Expected:
1. `[StormFX/StageDiag]` reports `stages.size=5`, non-null stage pointers, and populated table entries.
2. `[Storm/Nuke] Safe distant SLBM-scale explosion spawn succeeded ...` and a giant 360-degree
   cinematic mushroom cloud appears in the distance. The custom form is 0x885 and carries zero
   direct damage/radiation hazard; storm exposure still owns gameplay damage.
3. `[StormFX] SetStage(10) result=true ...` and camera shake begins.
4. After ~12 seconds the existing blast-wave sound, fire ring, fog and IMAD roll in.

If the table is populated but `SetStage` remains false, the dump contains the exact mask/indices needed
for the next decompile comparison. Do not add native projectile launch yet.

### Previous investigation / options (retained for history)

The firestorm's nuke has **never once fired**. Root cause found: the whole `RadStormFX` Papyrus quest
(`0x871`) is dead — `SetStage(N)` always returns **false**, so the nuke, cloud dome AND camera shake
have all been silently doing nothing. (The fog/IMAD/weather DO work; those are native in the DLL.)
Not the "dormant quest" enabled-bit theory (that was proven wrong — the bit was already set); it's the
engine's runtime **stage-table lookup** failing. See CHANGELOG 2026-07-12 pt3 for the decompile.

**What you want** (ref `Yangtze Submarine Tactical Nuke Redux - True SLBM`): missile flies over your
head, nuke goes off in the distance. Researched — it's all **vanilla**: projectile
`MS02NukeMissileProjectileFar` (`Fallout4.esm 0x065DF4`, Warhead01.nif) detonating `FatManExplosion`
(`0x1A7FF2`). Yangtze only overrides the projectile with long-flight tuning, so launching the vanilla
form inherits it for free while Yangtze is installed.

**Pick one (this is the only thing blocking the nuke):**
1. **Native `Projectile::Launch`** — full flyover + distant nuke. Real CTD risk: no Address Library id
   found (needs a hardcoded OG offset `0x140FCA260` + version guard) and an untested hidden-return-pointer
   ABI. Off-thread looks safe (the fire ring already does native ref-spawns from the same 4Hz hook).
2. **Distant nuke blast, no flyover** — proven-safe primitives only (`CreateReferenceAtLocation` +
   `ApplyArtObject`). Low risk, confident first try, but no missile arcing overhead.
3. **Keep debugging the quest** — diag-dump the quest's stage table to find why the lookup fails; if
   fixed, Papyrus fires the missile on the main thread (safest), and the dome + shake come back too.

---

## (previous list, updated 2026-07-03)

Everything that is NOT already fixed. Full issue lists: docs/CODE_REVIEW_2026-06-30.md and
docs/CODE_REVIEW_2026-07-03.md (the 07-03 pass fixed its whole list same-day; see its OUTCOME
section). This file is just the "waiting on you" shortlist.

**2026-07-03 updates to this list:** item 2 (radhud.js dead-code excision) is DONE - the vitals
JS/CSS/C++/INI paths are fully removed, resolving M8 with it. Items 1, 3, 4 below remain.
**Log path changed:** the DLL log is now at `Documents\My Games\Fallout4\F4SE\FalloutAnomaly.log`
(no longer MO2 overwrite\Root\Logs\) - use that path for the freeze test below.

Current build: clean, DLL + views + stripped ESP deployed to the live modlist. The deployed DLL
still carries the freeze DIAG probes, so the test below and the fixes ride in one run.

---

## 0. Gas-mask foley/equip — FIXED + CONFIRMED IN-GAME (2026-07-06)

**Equipping a gas mask works now (no CTD)** — confirmed in-game after: wav->xwm foley, the
worker-thread Papyrus CTD fix (native ActorEquipManager equip + Papyrus fade removed). Remaining
sub-items below are still worth a look (choke gates via [ChokeDiag], fog via filter:dead, the new
storm/ambient audio), but the crash + silent-foley blockers are cleared.

### Original retest notes (still useful for the remaining items):

The mask foley/VO were silent because loose stereo `.wav` doesn't play through the engine's
file-based audio (xwm does). All 12 converted to `.xwm`, DLL rebuilt + deployed (21:03). Retest:

1. **Foley:** equip, then unequip, then re-equip your gas mask (the FIRST equip after a load syncs
   silently by design — you need a real on->off or off->on transition). You should now hear the
   draw/withdraw take. If still silent, the log will have a `[Sound] GetSoundHandleByFile failed`
   line naming the file.
2. **Mask detection:** in the debug menu run **`mask:scan`** (or check the log for `[MaskScan]`).
   It lists every worn armor + biped slot. Detection keys ONLY on **slot 44 (kMouth)** — if your
   mask doesn't fill kMouth it is invisible to the whole system (no filter widget, no fog, no foley).
3. **Choke gates:** turn on the **`shelter`** debug toggle, then force a storm (`weather:1`) and
   stand outside unmasked. Watch the log for `[ChokeDiag]` (1/sec): it prints the current-weather
   FormID, whether it's in the rad-weather FLST, realMasked, and the shelter/PA flags — so we can
   see exactly which gate is blocking the choke (esp. whether `ForceWeather` actually flips
   `sky->currentWeather` into the FLST).
4. **Fog:** while masked, run `filter:dead` (sets charge 0) — visor fog should appear immediately.
   It never showed before only because the filter takes ~5 min to drain and test sessions were short.

Note: the managed storms (Firestorm/Sulfur/Acid/Dust) never auto-fire — the storm clock never
reaches `next` (~90 min of outdoor play; sessions were ~40-85s). Use the debug menu to force them.

---

## 0b. Storm dome — investigated 2026-07-06: essentially unbuilt (goes in a separate plugin)

Why it never appears, from a read-only load-order investigation:
- `RadOverhaul_StormDome` [000872:RadiationOverhaul.esp] is a plain **STAT** -> `KORsMeshes\Anomaly\
  CloudArena01.nif`. **Nothing references it — it is not placed in ANY cell/worldspace.** No world
  ref = nothing to enable = never shows. The "2-3 stacked activators" do not exist (zero ACTI in the ESP).
- STAT is the wrong type anyway: "Random Animation Start" + animated clouds/lightning need **ACTI or
  Movable Static (MSTT)**, not STAT.
- Assets DO exist (Anomaly mod): `CloudArena01-04.nif` (+LODs) and an arc/lightning FX set
  (`ArcBase`, `ArcIdleFX`, `ArcBlastFX`, `BurnerFX`, `Artifact01`) under
  `mods\Anomaly\meshes\KORsMeshes\Anomaly\`. Plus 14 `blowout_lightning_*.xwm` for the audio.
- Decision (confirmed): build the dome FRESH in its own F4SE plugin under a Fallen World master.
  Coupling to Rad Mod TBD after inspecting what (if anything) the Anomaly mod already wires with
  these NIFs. Do NOT fold dome logic back into Rad Mod.

## 1. Dust Storm freeze - [FIXED 2026-07-07] engine div-by-zero on ThunderFreq=0

**ROOT CAUSE FOUND + FIXED.** The `[HIT] N/4` probes showed all four hit-steps completing
(`1/4 ForceWeather done` / `2/4 TriggerStormFX done` (SetStage returned false - quest inert) /
`3/4 ApplyStormIMAD done`), then a CTD one second later. The crash log
(`crash-2026-07-07-18-21-18`) was decisive:

```
EXCEPTION_INT_DIVIDE_BY_ZERO at Sky::UpdateSound (Fallout4.exe+0x650917, div r8d)
R9: (TESWeather*) 0xAC000844 "RadiationOverhaul.esp"   ← our Dust Storm weather
Thread: BSJobs::JobThread
```

Decompiled `Sky::UpdateSound` (RVA 0x650510): the thunder branch (`SkySound.soundID == 3`) does
`BSRandom() % ((uint)weather->weatherData[0xA] * SkySound::ucThunderCount)`. Offset 0xA76 =
`TESWeather::weatherData[0xA]` = **kThunderFreq** (CommonLibF4 TESWeather.h). Our generated storm
weathers carry a **Thunder-type WeatherSound but ThunderFreq == 0**, so the modulo divides by zero
the instant the storm sky becomes current, on the audio worker thread. The "pink/black-and-white
frozen frame" was just the IMAD grade on the last rendered frame before the crash - NOT a texture
bug. (The old CHANGELOG "astral emoji" root-cause was wrong; disproven - banner is clean BMP.)

**Fix (RadiationEngine.cpp EnsureEslLoaded storm-weather loop):** the DLL already patches these
weathers' `fogData` at runtime, so it now also clamps `weatherData[kThunderFreq]` from 0 -> 100 for
every storm weather. Divisor is safe; a high freq makes real thunder ~never fire. No ESP surgery
(avoids the weather-record spriggit-roundtrip + master-order hazards). All 5 storm weathers carried
the same bad byte, so this fixes ash/sulfur/acid/firestorm too (they were equally at risk).
Deployed; probes removed. **Needs a confirming in-game click** (should no longer CTD).

---

## 2. Full radhud.js dead-code excision - gated on item 1

The vitals HUD moved to PrismaHUD_F4. I already null-guarded the leftover `#hud` references (the
bug). There are still ~200 lines of inert vitals/drag/prefs JS plus the matching C++ paths
(`RadHud::Push`/`updateRad`, `LoadHudPrefsToView`/`SaveHudPrefs`, `ToggleHud`, the toggle-key INI)
that can be removed. Also resolves M8 (INI writes on the JS thread).

**Why deferred:** it is a coordinated C++/JS pass. Doing it now, mid-freeze-diagnosis, risks the
live HUD. Safer right after item 1 confirms the guards behave.

**Needs from you:** nothing - just do item 1 first, then I proceed. Say "go" and I do it.

---

## 3. M3 - storm screen FX (dome/shake) dead on pre-existing saves

On saves made BEFORE the storm-FX quest was added, the quest stays dormant and `SetStage` silently
does nothing, so shake/dome never fire. New games are fine.

**Why deferred:** the obvious fix (VM `Quest.Start()`) is documented to CTD (null callback). Needs a
safe force-start (proper stack-callback, or writing the quest running flag directly).

**Decision I need:** is this worth fixing, or is "new games only" acceptable? If worth it, I will
research a safe start path (Ghidra / CommonLib) before touching it.

---

## 4. M5 - RadAway tissue heal fires on any big rad drop

Any rads drop > 15 in one tick heals reducible tissue as if RadAway was used (not RadAway
specifically). Low stakes (regular tissue re-accrues); chelation already uses the reliable
inventory-delta method.

**Decision I need:** switch RadAway to the same reliable inventory-count-delta detection (cleaner,
slightly changes feel), or leave as-is? Your call - it is a design/feel choice, not a bug.

---

## 5. L8 - filter/condition drain ignores time-jumps (minor, optional)

Tissue healing accounts for sleep/wait/fast-travel time; gas-mask filter/condition drain does not
(it just pauses). Low impact. Left for a future tuning pass unless you want it.

---

## Not action items (recorded so they are not re-litigated)

These were flagged by review but verified correct after reading the code - no change needed:
- **H5** LoadState record skip: F4SE auto-skips unread bytes; tolerated.
- **M4** `_lastGameTime` not persisted: persisting it would wrongly heal across different saves.
- **M6** `ApplyClampedSpecial` floor: rounding would breach the "never below 1" invariant.

Nothing is committed. Good night.
