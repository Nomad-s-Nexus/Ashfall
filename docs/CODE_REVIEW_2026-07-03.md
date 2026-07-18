# Rad Mod - Code Review (2026-07-03)

Full review of FalloutAnomalyRadiation: bugs, incomplete features, and a dedicated audit that the
player ghoul-race (ghoulification) code is fully removed. Two parallel review passes (engine core;
HUD/lifecycle/scripts/data) plus a hand-verified ghoul remnant sweep across src, deploy, the ESP
generator, the Spriggit JSON source, and the live modlist. Every finding below was verified against
the real code/files before being recorded. Nothing here duplicates items already tracked in
CODE_REVIEW_2026-06-30.md unless its status changed.

**Status legend:** `OPEN` not fixed - `DECISION` needs a user call before it is safe to touch.

---

## OUTCOME - fix pass completed same day (2026-07-03, "fix all issues")

Everything below was FIXED, built (clean), and deployed to the live modlist the same day, except
the items listed as still-open. Details per item in the sections that follow.

**Fixed:** B1 (rad wipe on load - `_prevRads`/`_prevHP` -1 sentinels), B2 (`data\thresholds.json`
re-synced from the canonical deploy copy; CLAUDE.md corrected), B3 (deploy package now reproduces
the live mod: RadStormFX.pex + recovered .psc source, both patch ESPs, RobCo loot ini, full storm
sound library; stale .esl + orphan siren wav removed), B4 (post-load FX stop latched via
`_pendingLoadFXCleanup`, fired after `EnsureEslLoaded`), B5 (past-due storm schedule re-armed 4-8
min on load instead of instant refire), B6 (missing thresholds.json now logs a loud error), B7
(logger writes to Documents\My Games\Fallout4\F4SE, game-dir Logs\ fallback), B8 (NPC unmask walks
the tracked set, not the high-process list), B9 (DebugForceStorm tears down previous storm FX),
B10 (null stage config clears perk/penalties), B11 (Hide-after-CreateView guarded on DOM-ready),
B12 (`_domReady`/`_toxicDomReady` are std::atomic), B13 (INI rewritten: Shift+F8 documented, dead
[HUD] section removed, [Filter]/[Condition] documented), B15 (wait thread 10-min hard cap), B16
(all stale comments/log strings), F2 (vitals pipeline fully excised: C++ Push/updateRad,
prefs/ToggleHud/PollInput toggle, ~250 lines of JS, CSS vitals block, orphan PNGs; also resolves
M8), F3 (siren plumbing removed), F4 (`_ghoulPackCellID` removed), F5 (dead config fields removed;
stage banner logs the real kStagePenalties), F6 (dead IMAD tint path removed).

**Ghoul removal COMPLETED:** gen_esl.py no longer generates 0x854/0x870 (FormIDs reserved),
esl_src records deleted, QUST 0x870 + GLOB 0x854 stripped from the shipped ESP (fo4editor,
backup at `deploy\RadiationOverhaul.esp.20260703_170854.bak`, check_plugin clean, deployed),
stub `RadGhoulQuest.pex` + `optional\RadOverhaul_GhoulRace.esp` + `tooling\plugins\
RadOverhaul_GhoulRace.esp` deleted. Only the stage-10 "Ghoulification" severity LABEL remains
(intentional).

**F1 resolved as superseded:** the FW_GasMaskFade Papyrus script was an old approach - the C++
engine already does the equip fade natively (`PlayMaskFade`/`CallFadeOutGame`). The orphan
.pex/.psc were removed from `deploy\Scripts\` instead of being wired up.

**Still open (unchanged, by design):** the Dust Storm freeze (needs the one in-game DIAG test -
NEEDS_INPUT.md item 1), M3 (storm FX on pre-existing saves - needs a safe quest force-start), M5
(RadAway heuristic - behavioral decision), L8 (filter time-jump drain - minor), B14 (vtable index
0x0CF unverified on NG/VR - needs per-runtime RE; the shipped modlist is OG-only so low risk).

**NOTE - log path changed:** the DLL log now lives at
`Documents\My Games\Fallout4\F4SE\FalloutAnomaly.log` (no longer MO2 `overwrite\Root\Logs\`).

---

## GHOUL-RACE REMOVAL AUDIT

**Verdict: functionally removed - no ghoulification can occur in-game - but remnants remain and the
ESP generator would recreate the records on any regeneration.**

Verified clean (no live ghoul-race path anywhere):
- `src/` has zero race-switch logic: no `SetStage` on quest 0x870, no writes to global 0x854, no
  race swap, no `RevertGhoulIfDisabled` (migration retired 2026-06-30). Only explanatory comments
  remain (`RadiationEngine.cpp:104`, `:3339`).
- The shipped `RadGhoulQuest.pex` (live modlist `Scripts/`) is an EMPTY STUB - decompiled it:
  `OnStageSet` has no body. Even though quest 0x870 is StartGameEnabled and running in every save,
  its stages fire nothing and nothing ever sets them.
- `RadOverhaul_IsGhoul` (GLOB 0x854) defaults 0.0 and is never written by anything.
- `optional/RadOverhaul_GhoulRace.esp` in the live mod folder is NOT in plugins.txt (inactive).
- INI, HUD views (html/js/css), FW_GasMaskFade: zero ghoul references.
- All remaining "ghoul" symbols in src are the separate, intentional feral-ghoul storm-spawn
  NPC feature (`IsGhoulActor`, `SpawnStormGhouls`, storm-peak ghoul healing). Not player race code.

Remnants (OPEN/DECISION - see bd issue):
1. `tooling/tools/gen_esl.py:488-492` and `:549-592` still GENERATE the ghoul global 0x854 and the
   StartGameEnabled RadOverhaul_GhoulQuest 0x870 (comment even says "ghoulification via Papyrus
   SetRace"). Any `python gen_esl.py` regeneration re-emits them. This is the one remnant that can
   silently resurrect the records.
2. Shipped `RadiationOverhaul.esp` (deploy == live, same hash) still contains QUST 0x870 (running
   stub quest in every save) and GLOB 0x854. Inert, but permanent save-file residents.
3. `tooling/esl_src/` (Spriggit source of truth) still carries both records, plus the
   Stage1000/Perk1000 "Ghoulification" severity records (those are the intentional 10th severity
   stage debuff - KEEP - only the quest+global are race-system remnants).
4. Live modlist still ships the stub `Scripts/RadGhoulQuest.pex`; `optional/RadOverhaul_GhoulRace.esp`
   and `tooling/plugins/RadOverhaul_GhoulRace.esp` still exist on disk.
5. Doc drift: `docs/Rad Mod/PROGRESS.md` still says `RevertGhoulIfDisabled()` "runs on every load"
   (it was deleted 2026-06-30); workspace CLAUDE.md still advertises a "ghoulification capstone".

Removal decision needed: stripping QUST 0x870 + GLOB 0x854 from the ESP mid-playthrough leaves an
orphaned already-started quest in existing saves (generally tolerated by the engine, but it is a
save-format change). The zero-risk subset is: delete the generator blocks in gen_esl.py, delete the
two esl_src records, delete the stub pex + optional/tooling GhoulRace ESPs, and fix the docs -
deferring the binary-ESP record strip to the next natural ESP rebuild.

---

## BUGS (new this review)

### HIGH

**B1. Sheltered rad-gain refund wipes ALL rads on the first tick after save-load.**
`RadiationEngine.cpp:532` guards the refund with `_prevRads >= 0.0f`, but `OnPostLoad`
(`:2792`) and `ResetAppliedPenalties` (`RadiationEngine.h:72`) reset `_prevRads = 0.0f`, so the
guard is always true. First tick after a load with `blockRadGain` true (power armor, powered
shelter, or sheltered during storm/rad weather) treats the player's entire saved rad pool as
"gain since last tick" and refunds it: save at 350 rads in power armor, load, rads silently gone.
Violates the code's own invariant comment at `:2363`. Fix: sentinel `_prevRads = -1.0f` on load
(and align the `CheckActiveCures` first-tick skip at `:2768` with the new sentinel).

**B2. Repo `data/thresholds.json` is the pre-rebalance Jun 5 design; deploy + live carry the
rebalanced Jun 6 file.** Stages/names/penalties all differ (old: 200/400 steps, END penalty up to
10; new: 100..1000, END 1..7). CLAUDE.md wrongly calls `data/` "runtime json the DLL loads" (DLL
reads `Data/F4SE/Plugins/FalloutAnomaly/`, `RadiationEngine.cpp:387`). Any future re-sync of deploy
from `data/` regresses the whole SPECIAL-penalty balance.

**B3. `deploy/` package cannot reproduce the live mod.** Missing: `Scripts/RadStormFX.pex` (quest
0x871's script - and its .psc source exists NOWHERE in the repo, only decompilable from the live
pex), `RadOverhaul_GearPatch.esp`, `RadOverhaulSurPatch.esp`, the RobCo chelation loot ini, the
whole blowout sound library. Stale extras: `deploy/RadiationOverhaul.esl` (Jun 6, superseded by the
.esp, referenced by nothing), orphan `radstorm_warning.wav`, unwired `FW_GasMaskFade.pex`. A fresh
install from deploy/ = no storm FX script, no gear patches, no storm audio.

### MED

**B4. Dangling storm-FX cleanup no-ops on the session's first load.** `OnPostLoad:2828-2833` calls
`TriggerStormFX(false)`/`ClearStormIMAD()`, but `_stormFXQuest`/IMADs are null until
`EnsureEslLoaded()` runs on the first Update tick. A save made mid-lethal-storm, loaded first thing
after game launch, resumes the persisted Papyrus rumble loop (RadStormFX `_shaking` + self timer
persist in the save) with C++ at Idle; nothing sends SetStage(20) until the next storm ends. Fix:
latch a needs-FX-stop flag in OnPostLoad, fire it after EnsureEslLoaded in Update.

**B5. Loading any mid-storm save instantly fires a brand-new storm.** Co-save persists
`_stormScheduled=true` + past-due `_nextStormPlaySec` while `OnPostLoad` resets phase to Idle, so
the first Idle tick re-triggers `BeginStorm(RollStormType())` - a different random type with a
fresh warning, every reload. Either persist `_activeStormType` and resume, or reschedule
`_nextStormPlaySec` when abandoning.

**B6. Missing `thresholds.json` is silently tolerated.** `RadiationConfig.cpp:15-36`: unopenable
file skips the parse but still returns true; log claims success; every stage debuff/perk silently
dead with zero diagnostic.

**B7. Logger writes to relative `Logs\FalloutAnomaly.log` (game-dir CWD), not the standard
Documents F4SE path** (`main.cpp:65-69`). On permission-restricted installs the sink throws and the
catch swaps in a null sink: all logging silently disappears exactly where diagnostics matter.

### LOW

**B8. NPC auto-mask leak:** `EquipNearbyNpcMasks(false)` (`RadiationEngine.cpp:1687`) only unmasks
NPCs still in the high-process list, then clears `_autoMaskedNpcs`; NPCs that unloaded mid-storm
keep the gas mask permanently.
**B9. `DebugForceStorm` mid-firestorm leaks the fire ring/dome into the new storm** (`:2879-2890`,
no `ClearFireRefs`/`ClearStormIMAD`/`TriggerStormFX(false)` unlike DebugEndStorm). Debug-only.
**B10. `ApplyDebuffs` early-returns on null stage config** (`:793-797`) leaving the last stage perk
+ penalties applied forever if the "0" stage is user-removed from thresholds.json.
**B11. HUD DOM-ready callback calls `Show` while post-CreateView code unconditionally `Hide`s**
(`RadHud.cpp:193-204`, `:214-222`): correct only while DOM-ready is always async; a synchronous
callback would leave the overlay hidden all session.
**B12. `_domReady`/`_toxicDomReady` are plain bools** written on the JS thread, read on the game
thread (`RadHud.h:82,86`) - make them `std::atomic<bool>` like `g_inWorld`.
**B13. INI header says the debug menu is Shift+F10; the real key is Shift+F8** (`kDebugMenuKey =
VK_F8`, `RadiationEngine.cpp:201`) and the same comment suggests `F8=119` as a ToggleKey example,
which would collide. Both deploy and live INIs.
**B14. `Hooks.cpp:102` hardcodes vfunc index 0x0CF for PlayerCharacter::Update across OG+NG+VR**
- the index inside the vtable is not guaranteed identical per runtime; verify per-runtime via
Ghidra/address bins before trusting the multi-runtime claim.
**B15. Detached `WaitThenCreateRadHud` thread has no shutdown cancellation** (`main.cpp:108-147`);
can enqueue an F4SE task during teardown (rare crash-on-exit).
**B16. Stale comments/log strings** the 06-30 pass (L3) missed: `RadiationEngine.cpp:276`
(0x840..0x843 - now ..0x844), `:1084` ("ForceStorm 1-4" - code accepts 1-5), `:2977`
("storm:0..3"), `:277-278` (ambient "apply always (halved when sheltered)" - since the dust
commit, `ApplyStormExposure:2259` returns for ALL types when sheltered). Also deploy INI missing
the `[Filter]`/`[Condition]` sections + Color keys the live INI has (works today only because
defaults match).

---

## INCOMPLETE / HALF-WIRED FEATURES

**F1. FW_GasMaskFade is fully unwired.** Source + pex exist in `deploy/Scripts/`, but no ESP
references the script (binary-grepped: 0 hits - the start-game-enabled quest + player alias were
never authored) and the pex is not even deployed to the live modlist. The equip fade-to-black
feature cannot ever run.
**F2. Dead vitals-HUD pipeline (H1 follow-up from 06-30 - still not excised).** ~200 lines of
inert JS (`radhud.js:196-411`), dead CSS (`radhud.css:110-165`), C++ `RadHud::Push`/`updateRad`,
`SaveHudPrefs`/`LoadHudPrefsToView`/`ToggleHud`, INI ToggleKey/pos/scale/color keys, and orphan
PNGs (heart-rate/running-man/radiation). ToggleKey is a user-visible no-op. M8 (sync INI writes on
JS thread) rides along.
**F3. Dead storm-audio remnants:** `kSndWarningSiren`/`kSndAmbientLoop` never played;
`_stormSirenHandle` has three Stop() sites and no Play. The siren was removed, the plumbing stayed.
**F4. `_ghoulPackCellID`** written/cleared but never read (`RadiationEngine.h:354`) - the
cell-based ghoul-pack cleanup it implies was never finished (worldspace+distance used instead).
**F5. Config fields parsed but driving nothing:** `limbDamagePercent`, `healthDamagePercent`,
`fastTravelDisabled`, `radAwayEffectiveness` (`RadiationConfig.h:8-11`); json `endPenalty`/
`strPenalty` intentionally overridden by hardcoded `kStagePenalties` yet the stage banner
(`:809-812`) still logs the json values as if live - misleads tuning.
**F6. `UpdateScreenEffect`/sickness IMAD 0x830** fully implemented, logs "(green screen tint
enabled)", but the only call site is commented out (`:820-822`) - tint moved to HTML.

---

## STATUS OF PRIOR (06-30) DEFERRED/OPEN ITEMS

- Dust Storm freeze: STILL AWAITING the one in-game DIAG data point (probes intact at `:2881,
  :2953, :2975, :3216`). Top priority in NEEDS_INPUT.md unchanged.
- M3 (storm FX dead on pre-existing saves): unchanged, `EnsureQuestRunning` still a no-op.
- M5 (RadAway cure heuristic): unchanged.
- M8/H1 follow-up (vitals excision): not done (= F2 above).
- L8 (filter/condition time-jump drain): still open.
- H5 (co-save version handling): unchanged, accepted behavior.

## VERIFIED SOLID (new code)

- Dust Storm / per-type fog integration is clean: table row 0x844 / theme 5 / weight consistent,
  static_assert holds, fog indices match Mutagen FNAM order, dust fogNear 960 respects the >= 900
  dome rule, IMAD 0x884 = base+4, theme 5 fully wired in deployed radhud.js/css/html, BMP-only
  symbols (no astral chars), debug storm:4/weather:4 present. No off-by-one found.
- C++/JS contract complete both directions; all 34 debug buttons handled; deploy HUD views ==
  live modlist byte-identical; RadiationOverhaul.esp deploy == live; F4SE lifecycle ordering
  correct; FW_GasMaskFade.psc matches its pex and uses FO4-valid API.
