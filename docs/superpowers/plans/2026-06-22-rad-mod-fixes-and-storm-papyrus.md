# Rad Mod Fixes + Storm Papyrus Conversion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix three live player-facing bugs in the Fallout Anomaly Radiation Overhaul (rain ignoring roof cover, SPECIAL reset not sticking, debug menu giving no feedback), then move storm scheduling and weather management out of the fragile 4Hz C++ state machine into a Papyrus quest controller so storms stop chaining back-to-back when the player exits a shelter.

**Architecture:** The mod is a C++ F4SE DLL (`FalloutAnomalyRadiation.dll`) plus a Spriggit-built ESP (`RadiationOverhaul.esp`). Phase 1 is three surgical C++ edits in `src/RadiationEngine.cpp` and `src/RadHud.cpp`. Phase 2 splits storm ownership: a new Papyrus quest (`RadStormController`) owns scheduling, phase timing, and weather forcing/release (robust across cell transitions via `Weather.ForceActive` + `OnPlayerLoadGame`/cell events); the DLL keeps damage, shelter detection, FX, audio and NPC spawning but is now *driven by globals the quest sets* instead of running its own scheduler and forcing weather itself.

**Tech Stack:** C++23 (CommonLibF4, xmake), Papyrus (Fallout 4 official compiler), Spriggit JSON ESP pipeline, PrismaUI HUD (optional).

> **No automated tests exist** in this project — validation is in-game (CLAUDE.md: "There are no automated tests. Validation is done in-game."). Each task therefore replaces the usual "write failing test / run it" steps with: (a) a build step, (b) an explicit **in-game verification** step listing exact observations and expected `FalloutAnomaly.log` lines. Treat the verification step as the test: the task is not done until the observations match.

**Key file paths (absolute):**
- Engine source: `E:\F4SE OG\Non Prisma Projects\Rad Mod\src\RadiationEngine.cpp` / `.h`
- HUD source: `E:\F4SE OG\Non Prisma Projects\Rad Mod\src\RadHud.cpp` / `.h`
- Build: `cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"; xmake` → `build\release\FalloutAnomalyRadiation.dll`
- ESP source tree: `E:\F4SE OG\Non Prisma Projects\Rad Mod\tooling\esl_src\RadiationOverhaul.esp\`
- ESP generator: `E:\F4SE OG\Non Prisma Projects\Rad Mod\tooling\tools\gen_esl.py`
- Papyrus compiler: `E:\SteamLibrary\steamapps\common\Fallout 4 1946160\Papyrus Compiler\PapyrusCompiler.exe`
- Papyrus API reference: `E:\F4SE OG\Reference\GardennOfEden\` (native function signatures)
- Live deploy target (per memory `project_live_modlist`): `E:\Modlists\Fallen World Alpha 2\mods\...` — **NOT** `D:\Games\ModlistDownloads` (broken)
- Log: `<game>\Data\F4SE\Plugins\Logs\FalloutAnomaly.log` (relative `Logs\FalloutAnomaly.log` from the DLL CWD)

**FormID contract reminder (existing):** globals `0x850` TissueDamage, `0x851` PermTissueDmg, `0x852` NextEmission (storm gate), `0x853` ForceStorm (debug), `0x854` IsGhoul. Storm weathers `0x840`–`0x843` (Ash, Firestorm, Sulfur, Acid). This plan adds globals `0x855`–`0x857`.

---

## File Structure

| File | Phase | Responsibility / change |
|------|-------|------------------------|
| `src/RadiationEngine.cpp` | 1 | Rain shelter fix (Update path); SPECIAL suspend + full-restore (DebugFixSpecial, ApplyStatModification) |
| `src/RadiationEngine.h` | 1 | New `_specialSuspendStage` member |
| `src/RadHud.cpp` / `.h` | 1 | `ToggleDebugMenu` returns/Notifies when the PrismaUI view is unavailable |
| `tooling/tools/gen_esl.py` | 2 | Emit globals `0x855` StormState, `0x856` StormType, `0x857` StormsEnabled; add `RadStormController` quest record `0x872` |
| `src/RadiationEngine.cpp` / `.h` | 2 | Resolve new globals; replace `UpdateStorm` scheduler + `ForceWeather` calls with a global-driven phase reader; remove DLL-side weather forcing/scheduling |
| `Scripts/Source/User/RadStormController.psc` | 2 | New Papyrus quest: scheduling, phase timing, `Weather.ForceActive`/release, cell/load-game events, `StormsEnabled` gate |
| `Scripts/RadStormController.pex` | 2 | Compiled output, packed into deploy |
| `docs/CHANGELOG.md`, `docs/findings/` | 1+2 | Session-end documentation per CLAUDE.md |

---

## PHASE 1 — C++ HOTFIXES (ship first, immediate player relief)

### Task 1: Standard rain respects roof/shelter cover (#2)

**Root cause:** In `Update()`, `stormActive` is true only for a vanilla radstorm FormID or a managed storm. Plain rainy weather (which other mods / vanilla apply rads through) never sets `stormActive`, so the roof raycast is skipped (`_playerEnclosed` forced false) and the sheltered rad-refund never runs. Standing under the DC entryway roof, the player still gains rads.

**Fix:** Treat *radioactive rainy weather* like a storm for the purposes of (a) running the roof raycast and (b) refunding rad gain while sheltered. Reuse the existing `IsRainyWeather()` classifier and the existing `IsPlayerSheltered()` / refund machinery.

**Files:**
- Modify: `E:\F4SE OG\Non Prisma Projects\Rad Mod\src\RadiationEngine.cpp` (the `Update()` storm-active block, currently lines ~400–455)

- [ ] **Step 1: Read the current block to confirm line anchors**

Run: open `src/RadiationEngine.cpp` and confirm the block beginning `RE::Sky* sky = RE::Sky::GetSingleton();` and ending with the rad-refund `if (_prevRads >= 0.0f && ...)` still matches the text in Step 2's `old_string`. If the surrounding code has drifted, adjust the anchors but keep the same edits.

- [ ] **Step 2: Add a rainy-weather signal alongside the storm signal**

Edit `src/RadiationEngine.cpp`. Replace:

```cpp
        RE::Sky* sky = RE::Sky::GetSingleton();
        const bool vanillaRadstorm = sky && sky->currentWeather &&
            IsRadstormWeather(sky->currentWeather->GetFormID());
        const bool stormActive = (_stormPhase != StormPhase::Idle) || vanillaRadstorm;
```

with:

```cpp
        RE::Sky* sky = RE::Sky::GetSingleton();
        const bool vanillaRadstorm = sky && sky->currentWeather &&
            IsRadstormWeather(sky->currentWeather->GetFormID());
        const bool stormActive = (_stormPhase != StormPhase::Idle) || vanillaRadstorm;

        // Standard rain (vanilla or another mod's rad-rain) applies rads via weather-conditioned
        // magic effects that fire regardless of a roof — exactly like a radstorm. It does NOT set
        // stormActive (it is not one of our managed storms nor a radstorm FormID), so without this
        // the roof raycast never ran and the player gained rads under cover (e.g. the DC entryway).
        // Treat any rainy weather as "needs shelter evaluation" so the roof-fan runs and the
        // sheltered rad-refund below applies. Cheap: just a flag read off the current weather.
        const bool rainyWeather = sky && sky->currentWeather && IsRainyWeather(sky->currentWeather);
        const bool needsShelterEval = stormActive || rainyWeather;
```

- [ ] **Step 3: Run the roof raycast during rain, not just storms**

Edit `src/RadiationEngine.cpp`. Replace:

```cpp
        if (!stormActive && !_debugShelter)
        {
            _playerEnclosed    = false;
            _enclosedCheckTick = 0;   // re-check immediately when the next storm starts
        }
        else if (--_enclosedCheckTick <= 0)
        {
            // Run the roof-fan when a storm is active OR when shelter-debug is on (so the player can
            // tune their base coverage with no storm running).
            _enclosedCheckTick = kEnclosedRecheckTicks;
            _playerEnclosed    = ComputeEnclosedByStructure();
        }
```

with:

```cpp
        if (!needsShelterEval && !_debugShelter)
        {
            _playerEnclosed    = false;
            _enclosedCheckTick = 0;   // re-check immediately when the next storm/rain starts
        }
        else if (--_enclosedCheckTick <= 0)
        {
            // Run the roof-fan when a storm OR rad-rain is active, or when shelter-debug is on (so the
            // player can tune their base coverage with no storm running).
            _enclosedCheckTick = kEnclosedRecheckTicks;
            _playerEnclosed    = ComputeEnclosedByStructure();
        }
```

- [ ] **Step 4: Refund sheltered rad gain during rain**

Edit `src/RadiationEngine.cpp`. Replace:

```cpp
        bool blockRadGain = IsPlayerInPoweredShelter();
        if (!blockRadGain && stormActive && IsPlayerSheltered())
            blockRadGain = true;
```

with:

```cpp
        bool blockRadGain = IsPlayerInPoweredShelter();
        if (!blockRadGain && needsShelterEval && IsPlayerSheltered())
            blockRadGain = true;
```

- [ ] **Step 5: Build**

Run:
```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
xmake
```
Expected: `build\release\FalloutAnomalyRadiation.dll` rebuilt, no errors. (`IsRainyWeather` and `IsPlayerSheltered` already exist in this file, so no new includes are needed.)

- [ ] **Step 6: Deploy**

Close Fallout 4 first (the DLL is file-locked while the game runs). Then:
```powershell
Copy-Item "E:\F4SE OG\Non Prisma Projects\Rad Mod\build\release\FalloutAnomalyRadiation.dll" `
  "E:\Modlists\Fallen World Alpha 2\mods\Fallout Anomaly Master Files\F4SE\Plugins\FalloutAnomalyRadiation.dll" -Force
```
(Confirm the exact mod-folder name first with `Get-ChildItem "E:\Modlists\Fallen World Alpha 2\mods" -Filter "*Anomaly*"`; the DLL currently ships under the Anomaly master-files mod. Copy only the DLL — never delete the folder, per CLAUDE.md MO2 safety rules.)

- [ ] **Step 7: In-game verification (this is the test)**

1. Load a save, enable the shelter readout: Left Shift + F8 → "Shelter Debug" button (or it auto-shows during rain now). 
2. Wait for / force standard rain (not a radstorm). Stand fully under the DC entryway roof.
3. Open the Pip-Boy STAT page and watch the Rads value over ~30s.

Expected:
- Rads value does **not** climb while under the roof.
- `FalloutAnomaly.log` shows `[Shelter] roof-fan N/5 ... covered=true` lines appearing during the rain (proving the raycast now runs without a managed storm).
- Step outside into open rain: Rads begin climbing again (cover, not a blanket immunity).

- [ ] **Step 8: Commit**

```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
git add src/RadiationEngine.cpp
git commit -m "fix(rad): standard rad-rain now respects roof/shelter cover"
```

---

### Task 2: SPECIAL reset fully restores and stays restored (#3, #1 button behavior)

**Root cause:** `DebugFixSpecial()` clears `total − permanent` on all 7 SPECIALs in one call (the full restore is correct), but it sets `_appliedEndPenalty = 0` / `_appliedStrPenalty = 0` and the very next 4Hz tick `ApplyStatModification()` re-derives the stage penalty from the still-high rads and re-applies it. Combined with a survival-overhaul mod re-applying its own per-need SPECIAL penalties, the stat appears to only move "by 5s" and never fully clears. The user chose: **full restore to permanent, plus suspend the Rad Mod's own re-application** so the fix sticks until the player actually recovers.

**Design:** Add `int _specialSuspendStage = -1;` (−1 = not suspended). `DebugFixSpecial()` sets it to the player's current radiation stage. `ApplyStatModification(stageIndex)` honors it:
- While `stageIndex == _specialSuspendStage`: skip re-applying the END/STR penalty (force want = 0).
- If `stageIndex < _specialSuspendStage` (rads dropped — player recovered a stage): clear suspension, normal behavior resumes.
- If `stageIndex > _specialSuspendStage` (rads got *worse*): clear suspension so penalties resume (the player chose not to cure).

This way the button gives a clean, permanent-base SPECIAL and the penalty only comes back if the player keeps irradiating themselves; it never "snaps back" on the next tick.

**Files:**
- Modify: `E:\F4SE OG\Non Prisma Projects\Rad Mod\src\RadiationEngine.h` (add member near `_appliedStrPenalty`)
- Modify: `E:\F4SE OG\Non Prisma Projects\Rad Mod\src\RadiationEngine.cpp` (`DebugFixSpecial`, `ApplyStatModification`, `ResetAppliedPenalties`)

- [ ] **Step 1: Add the suspend-stage member**

Edit `src/RadiationEngine.h`. Replace:

```cpp
    int32_t _appliedEndPenalty = 0;
    int32_t _appliedStrPenalty = 0;
```

with:

```cpp
    int32_t _appliedEndPenalty = 0;
    int32_t _appliedStrPenalty = 0;

    // Set by the Fix Stats button to the radiation stage at which the player manually cleared their
    // SPECIAL. While the player's stage stays equal to this, ApplyStatModification does NOT re-apply
    // the END/STR penalty (so the manual reset isn't undone on the very next tick). Cleared when the
    // stage changes in EITHER direction (recovered a stage -> resume normal; got worse -> penalties
    // resume because the player chose not to cure). -1 = not suspended. Not persisted (re-derived).
    int32_t _specialSuspendStage = -1;
```

- [ ] **Step 2: Make the Fix Stats button arm the suspension**

Edit `src/RadiationEngine.cpp`. Replace:

```cpp
    // Our tracked penalty is now gone (we just cleared the temporary slot); reset the trackers so the
    // floor-guarded system re-evaluates from a clean slate next tick.
    _appliedEndPenalty = 0;
    _appliedStrPenalty = 0;

    Notify("[DEBUG] All SPECIAL stats reset to base. Gear bonuses reapply on reload/re-equip.", false);
```

with:

```cpp
    // Our tracked penalty is now gone (we just cleared the temporary slot); reset the trackers so the
    // floor-guarded system re-evaluates from a clean slate next tick.
    _appliedEndPenalty = 0;
    _appliedStrPenalty = 0;

    // Suspend our own END/STR re-application at the CURRENT radiation stage so the next 4Hz tick does
    // not immediately re-penalize the stat the player just cleared. Resumes only if the player's stage
    // changes (recovers a stage, or irradiates further). _currentStageLevel is the live stage index.
    _specialSuspendStage = static_cast<int32_t>(_currentStageLevel);
    logger->info("[FixStats] SPECIAL penalty re-application suspended at stage {}.", _specialSuspendStage);

    Notify("[DEBUG] All SPECIAL stats reset to base. They will not re-penalize until your "
           "radiation level changes. Gear bonuses reapply on reload/re-equip.", false);
```

- [ ] **Step 3: Honor the suspension in ApplyStatModification**

Edit `src/RadiationEngine.cpp`. Replace:

```cpp
    const int wantEnd = (stageIndex >= 0 && stageIndex < kStageCount) ? kStagePenalties[stageIndex].end : 0;
    const int wantStr = (stageIndex >= 0 && stageIndex < kStageCount) ? kStagePenalties[stageIndex].str : 0;

    ApplyClampedSpecial(player, actorValue->endurance, wantEnd, _appliedEndPenalty);
    ApplyClampedSpecial(player, actorValue->strength,  wantStr, _appliedStrPenalty);
```

with:

```cpp
    int wantEnd = (stageIndex >= 0 && stageIndex < kStageCount) ? kStagePenalties[stageIndex].end : 0;
    int wantStr = (stageIndex >= 0 && stageIndex < kStageCount) ? kStagePenalties[stageIndex].str : 0;

    // Manual Fix Stats suspension: if the player cleared their SPECIAL and is still at the same stage,
    // don't re-apply the penalty (want -> 0). Any stage change clears the suspension and resumes normal
    // behavior: recovering a stage means they healed; climbing a stage means they chose not to cure.
    if (_specialSuspendStage >= 0)
    {
        if (stageIndex == _specialSuspendStage)
        {
            wantEnd = 0;
            wantStr = 0;
        }
        else
        {
            spdlog::get("FalloutAnomaly")->info(
                "[FixStats] stage changed {} -> {}; resuming SPECIAL penalty.",
                _specialSuspendStage, stageIndex);
            _specialSuspendStage = -1;
        }
    }

    ApplyClampedSpecial(player, actorValue->endurance, wantEnd, _appliedEndPenalty);
    ApplyClampedSpecial(player, actorValue->strength,  wantStr, _appliedStrPenalty);
```

- [ ] **Step 4: Clear the suspension on load/new game**

Edit `src/RadiationEngine.h` — in `ResetAppliedPenalties()`, replace:

```cpp
        _appliedEndPenalty = 0;
        _appliedStrPenalty = 0;
        _currentStageLevel = 0;
```

with:

```cpp
        _appliedEndPenalty = 0;
        _appliedStrPenalty = 0;
        _specialSuspendStage = -1;   // manual-reset suspension does not persist across loads
        _currentStageLevel = 0;
```

- [ ] **Step 5: Build**

Run:
```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
xmake
```
Expected: clean build, `FalloutAnomalyRadiation.dll` rebuilt.

- [ ] **Step 6: Deploy** (same copy command as Task 1 Step 6; close FO4 first.)

- [ ] **Step 7: In-game verification (this is the test)**

1. Load a save where the player is irradiated enough to carry an END/STR penalty (e.g. `player.modav rads 600`, wait one tick for the stage to apply — confirm END is reduced on the STAT page).
2. Open the debug menu (Left Shift + F8) → "Fix Stats".
3. Confirm END/STR jump back to their full base value **in one click** and a corner message says they won't re-penalize until rad level changes.
4. Wait ~10 in-game seconds without changing rads: END/STR **stay** at base (previously they ticked back down).

Expected log lines:
- `[FixStats] Endurance: total ... cleared temp ... -> now <base>`
- `[FixStats] SPECIAL penalty re-application suspended at stage N.`
- After raising rads further (`player.modav rads 300`): `[FixStats] stage changed N -> M; resuming SPECIAL penalty.` and END drops again.
- After curing (RadAway down a stage): the same resume line and normal behavior.

- [ ] **Step 8: Commit**

```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
git add src/RadiationEngine.cpp src/RadiationEngine.h
git commit -m "fix(rad): Fix Stats fully restores SPECIAL and suspends re-penalty until rad level changes"
```

---

### Task 3: Debug menu gives feedback when PrismaUI is unavailable (#1)

**Root cause:** Shift+F8 calls `RadHud::ToggleDebugMenu()`. If the PrismaUI_F4 framework or the HUD view is not present, the call silently no-ops — so to the player "Shift+F8 does nothing." Per the chosen approach (keep the menu, fix the button), add a vanilla-HUD notification when the menu can't open so the player knows why, and so they know SPECIAL repair also runs automatically on load (`DispelNegativeSpecials`).

**Files:**
- Read first: `E:\F4SE OG\Non Prisma Projects\Rad Mod\src\RadHud.cpp` (`ToggleDebugMenu`) and `RadHud.h`
- Modify: `E:\F4SE OG\Non Prisma Projects\Rad Mod\src\RadHud.cpp`

- [ ] **Step 1: Locate ToggleDebugMenu and the view/api validity guards**

Run: open `src/RadHud.cpp`, find `void RadHud::ToggleDebugMenu()`. Note how it checks the PrismaUI API pointer and the view handle (e.g. `_api`, `_debugView`/`_view`, `IsValid`). Identify the exact guard names — they are needed verbatim in Step 2.

- [ ] **Step 2: Notify when the menu can't open**

Edit `src/RadHud.cpp`. At the **top** of `ToggleDebugMenu()`, before any early `return` that fires when the API or view is missing, insert a vanilla-HUD fallback. Use the actual guard variable names found in Step 1; the template below assumes `_api` (PrismaUI API ptr) and `_debugView` (view handle) — rename to match:

```cpp
void RadHud::ToggleDebugMenu()
{
    // Fallback feedback: if the PrismaUI framework or the debug view isn't available, the menu can't
    // open and the hotkey would otherwise look dead. Tell the player via the vanilla HUD corner so
    // Shift+F8 always produces a visible response. (SPECIAL corruption is still auto-repaired on every
    // load by DispelNegativeSpecials, so the player isn't stranded without the menu.)
    if (!_api || !_debugView || !_api->IsValid(_debugView))
    {
        RE::SendHUDMessage::ShowHUDMessage(
            "[Radiation] Debug menu requires the PrismaUI HUD (not loaded). SPECIAL stats are "
            "auto-repaired on each load; use RadAway/sleep to recover.", "", true, true);
        spdlog::get("FalloutAnomaly")->warn(
            "[RadHud] ToggleDebugMenu: PrismaUI view unavailable (api={}, view valid={}).",
            static_cast<const void*>(_api), (_api && _debugView) ? _api->IsValid(_debugView) : false);
        return;
    }
    // ... existing toggle body continues unchanged ...
```

If `RadHud.cpp` does not already include the SendHUDMessage header, add `#include <RE/M/MenuManager.h>` is NOT it — the engine call used elsewhere is `RE::SendHUDMessage::ShowHUDMessage`. Confirm `RadiationEngine.cpp` includes it transitively; if `RadHud.cpp` fails to compile on `RE::SendHUDMessage`, add the same include that `RadiationEngine.cpp`'s `Notify()` relies on (search `RadiationEngine.cpp` for the `SendHUDMessage` include and mirror it).

- [ ] **Step 3: Build**

Run:
```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
xmake
```
Expected: clean build. If `RE::SendHUDMessage` is undefined in this translation unit, add the include identified in Step 2 and rebuild.

- [ ] **Step 4: Deploy** (same copy command as Task 1 Step 6; close FO4 first.)

- [ ] **Step 5: In-game verification (this is the test)**

- With PrismaUI loaded: Shift+F8 opens the menu as before (no regression).
- With PrismaUI disabled (temporarily disable the PrismaUI_F4 mod in MO2): Shift+F8 now shows the corner message instead of doing nothing, and `FalloutAnomaly.log` logs `[RadHud] ToggleDebugMenu: PrismaUI view unavailable`.

- [ ] **Step 6: Commit**

```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
git add src/RadHud.cpp
git commit -m "feat(rad): debug menu hotkey gives vanilla-HUD feedback when PrismaUI is unavailable"
```

---

### Task 4: Phase 1 documentation + findings update (CLAUDE.md mandatory)

**Files:**
- Modify: `E:\F4SE OG\Prisma\docs\CHANGELOG.md`
- Modify: `E:\F4SE OG\Prisma\docs\findings\` (relevant topic file for the Rad Mod) + `open-items.md`

- [ ] **Step 1: Append a CHANGELOG entry**

Add a dated entry summarizing Task 1–3 (rain shelter fix, SPECIAL suspend, debug-menu feedback). No em dashes (memory `feedback_no_em_dashes`).

- [ ] **Step 2: Update findings**

In the Rad Mod findings topic file, add `[FIXED]` entries for the three bugs with the root cause one-liner each, and update `Last updated:`. Remove any matching `[OPEN]` items from `open-items.md`.

- [ ] **Step 3: Commit**

```powershell
cd "E:\F4SE OG"
git add "Prisma/docs/CHANGELOG.md" "Prisma/docs/findings"
git commit -m "docs(rad): Phase 1 fixes (rain shelter, SPECIAL suspend, debug-menu feedback)"
```

---

## PHASE 2 — PAPYRUS STORM CONTROLLER (#4: back-to-back storms on shelter exit)

**Why Papyrus:** The back-to-back-storm bug is a weather-management failure. The DLL forces storm weather from a 4Hz tick with `Sky::ForceWeather(w, true)` and tries to release it with `ReleaseWeatherOverride()` + an interior hard-snap. While sheltered the exterior sky isn't simulated, so the forced storm weather lingers as "current" and its `GetIsCurrentWeather`-conditioned rad effect re-fires the instant the player steps outside — looking like a new storm every time they leave Sanctuary. Papyrus `Weather.ForceActive()` / `SetActive()` and `OnPlayerLoadGame` / cell-change remote events manage weather correctly across transitions, and the engine owns the override lifetime instead of a polling tick.

**New ownership split:**
- **Papyrus quest `RadStormController`** owns: the storm schedule, phase timing (Warning → Active → Aftermath), forcing the storm weather and releasing it cleanly, the `StormsEnabled` gate, and re-asserting clear weather on load / cell change. It publishes state to globals.
- **DLL** owns (unchanged behavior, new trigger): damage (`ApplyStormExposure`), shelter detection, FX (`TriggerStormFX`, `ApplyStormIMAD`, dome, audio), NPC spawning/flee. It **no longer schedules storms or forces weather**; instead it reads the globals the quest sets and drives FX/exposure off them.

**Global contract (added this phase):**
| FormID | EditorID | Meaning |
|--------|----------|---------|
| `0x855` | `RadOverhaul_StormState` | 0=Idle, 1=Warning, 2=Active, 3=Aftermath (set by Papyrus, read by DLL) |
| `0x856` | `RadOverhaul_StormType` | 0=Ash, 1=Firestorm, 2=Sulfur, 3=Acid (set by Papyrus when a storm begins) |
| `0x857` | `RadOverhaul_StormsEnabled` | 1=storms allowed (default), 0=disabled (holotape/MCM/console) |

`0x852 RadOverhaul_NextEmission` remains the storm-system gate but its scheduling role moves to Papyrus; the DLL keeps using it only as "system live" presence check.

---

### Task 5: Add storm-control globals + controller quest record to the ESP

**Files:**
- Read first: `E:\F4SE OG\Non Prisma Projects\Rad Mod\tooling\tools\gen_esl.py` (the Globals section and the Quests section)
- Modify: `tooling/tools/gen_esl.py`

- [ ] **Step 1: Read gen_esl.py to find the globals + quest emission blocks**

Run: open `gen_esl.py`, locate where globals `0x850`–`0x854` are defined (look for `0x850`, `TissueDamage`, `RadOverhaul_NextEmission`), and where quests `0x870`/`0x871` (`RadOverhaul_GhoulQuest`, `RadOverhaul_StormFXQuest`) are emitted. Note the exact data structure used (a list/dict of records) so additions follow the same shape.

- [ ] **Step 2: Add the three new globals**

Edit `gen_esl.py`. In the globals list, alongside the existing `0x850`–`0x854` entries, add three float globals following the existing entry shape (adapt field names to the file's actual structure):

```python
    # Storm-control globals (Papyrus controller owns scheduling/weather; DLL reads state).
    {"formid": 0x855, "edid": "RadOverhaul_StormState",   "value": 0.0},  # 0 idle .. 3 aftermath
    {"formid": 0x856, "edid": "RadOverhaul_StormType",    "value": 0.0},  # 0 Ash .. 3 Acid
    {"formid": 0x857, "edid": "RadOverhaul_StormsEnabled","value": 1.0},  # 1 enabled, 0 disabled
```

- [ ] **Step 3: Add the controller quest record**

Edit `gen_esl.py`. Alongside the existing `0x870`/`0x871` quest stubs, add a **start-game-enabled, run-once-off** quest that will carry the `RadStormController` script (following the existing quest record shape — copy the `0x871 RadOverhaul_StormFXQuest` entry and change formid/edid/script; keep whatever `flags`/`priority` fields the existing quest records use):

```python
    {
        "formid": 0x872,
        "edid": "RadOverhaul_StormController",
        "scriptName": "RadStormController",
        "startGameEnabled": True,    # auto-runs on a fresh game; existing saves force-start via DLL EnsureQuestRunning
        "stages": [0, 10],           # 10 = "running"; controller logic is event/timer driven, not stage driven
    },
```

- [ ] **Step 4: Regenerate and compile the ESP**

Run (per `docs/ESP_BUILD_PIPELINE.md`):
```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
python tooling/tools/gen_esl.py
python tooling/tools/merge_overrides.py --apply
$exe = "$env:LOCALAPPDATA\Temp\Spriggit\Translations\Spriggit.Json.Fallout4\0.40.1\Spriggit.Json.Fallout4.exe"
& $exe create-plugin -i "tooling/esl_src/RadiationOverhaul.esp" -o "tooling/esl_build/RadiationOverhaul.esp"
```
Expected: `tooling/esl_build/RadiationOverhaul.esp` rebuilt; the three globals and the new quest appear. Verify in xEdit (or by re-loading the Spriggit JSON) that `0x855`/`0x856`/`0x857` and `0x872` exist with the right EditorIDs.

- [ ] **Step 5: Commit**

```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
git add tooling/tools/gen_esl.py tooling/esl_src tooling/esl_build/RadiationOverhaul.esp
git commit -m "feat(rad-esp): add StormState/StormType/StormsEnabled globals + RadStormController quest"
```

---

### Task 6: DLL reads storm state from globals; stop DLL-side scheduling + weather forcing

This is the riskiest change: the DLL's `UpdateStorm()` state machine currently *owns* scheduling and weather. We convert it to a **follower** of the `RadOverhaul_StormState` global the Papyrus quest will set, while keeping every FX/exposure helper. Implement it so that **if the global is absent or the quest never runs, the DLL safely does nothing** (no storms) rather than crashing — Papyrus drives storms now.

**Files:**
- Modify: `E:\F4SE OG\Non Prisma Projects\Rad Mod\src\RadiationEngine.h`
- Modify: `E:\F4SE OG\Non Prisma Projects\Rad Mod\src\RadiationEngine.cpp`

- [ ] **Step 1: Resolve the new globals at ESL load**

Read `EnsureEslLoaded()` in `src/RadiationEngine.cpp` (search `_nextEmissionGlobal` to find where globals `0x852`/`0x853` are resolved via the plugin's `LookupForm`/`GetForm` helper). Add resolution for the three new globals using the same lookup call. Add the member declarations first.

Edit `src/RadiationEngine.h` — after:

```cpp
    RE::TESGlobal*  _nextEmissionGlobal = nullptr;  // persists next storm game-time (0x852)
    RE::TESGlobal*  _forceStormGlobal = nullptr;    // console debug trigger (0x853)
```

add:

```cpp
    // Papyrus-owned storm control (RadStormController quest). The DLL no longer schedules storms or
    // forces weather; it READS these globals each tick and drives FX/exposure/NPCs to match.
    RE::TESGlobal*  _stormStateGlobal   = nullptr;  // 0x855: 0 idle,1 warning,2 active,3 aftermath
    RE::TESGlobal*  _stormTypeGlobal    = nullptr;  // 0x856: 0 Ash,1 Fire,2 Sulfur,3 Acid
    RE::TESGlobal*  _stormsEnabledGlobal= nullptr;  // 0x857: 1 enabled, 0 disabled (holotape/MCM)
    int             _lastStormStateSeen = 0;        // edge-detect global state transitions
```

Edit `src/RadiationEngine.cpp` in `EnsureEslLoaded()`, after the lines resolving `_nextEmissionGlobal`/`_forceStormGlobal` (mirror their exact lookup call; the snippet below assumes a helper `GetEslForm<RE::TESGlobal>(0x852)` — replace with the real helper used a few lines above):

```cpp
    _stormStateGlobal    = GetEslForm<RE::TESGlobal>(0x855);
    _stormTypeGlobal     = GetEslForm<RE::TESGlobal>(0x856);
    _stormsEnabledGlobal = GetEslForm<RE::TESGlobal>(0x857);
    logger->info("[ESL] Storm-control globals: state={}, type={}, enabled={}.",
        static_cast<const void*>(_stormStateGlobal),
        static_cast<const void*>(_stormTypeGlobal),
        static_cast<const void*>(_stormsEnabledGlobal));
```

- [ ] **Step 2: Replace UpdateStorm's scheduler/weather body with a global-state follower**

Edit `src/RadiationEngine.cpp`. Replace the entire `switch (_stormPhase) { ... }` block in `UpdateStorm()` (the Idle/Warning/Active/Aftermath cases, currently ~lines 1889–2107) with a follower that maps the `_stormStateGlobal` value onto the existing phase handlers. Keep the new-game clear-on-entry block above it and the HUD-intensity block below it. Replace:

```cpp
    switch (_stormPhase)
    {
    case StormPhase::Idle:
    {
        // Debug-forced storm fires immediately, bypassing the schedule.
        if (_forceStormNow)
        {
            _forceStormNow = false;
            BeginStorm(_forcedStormType >= 0 ? _forcedStormType : RollStormType());
            _forcedStormType = -1;
            break;
        }
        // First run / after a storm: pick the next time.
        if (!_stormScheduled)
        {
            ScheduleNextStorm();
            break;
        }
        // Natural trigger once enough REAL play-time has elapsed.
        if (_playSeconds >= _nextStormPlaySec)
            BeginStorm(RollStormType());
        break;
    }
```

with:

```cpp
    // STORM OWNERSHIP: the RadStormController Papyrus quest now owns scheduling + weather forcing.
    // The DLL follows the RadOverhaul_StormState global (0x855) it sets. If the global is missing
    // (quest never ran / ESP older), we never start a storm here — the DLL no longer self-schedules.
    const int stormState = _stormStateGlobal ? static_cast<int>(_stormStateGlobal->GetValue()) : 0;

    // Edge-detect Idle->Warning (storm begins) and any state -> Idle (storm ends) so the existing FX
    // setup/teardown runs exactly once per transition instead of every tick.
    if (stormState != _lastStormStateSeen)
    {
        const int stormType = _stormTypeGlobal ? static_cast<int>(_stormTypeGlobal->GetValue()) : 0;

        if (stormState == 1 && _stormPhase == StormPhase::Idle)
        {
            // Warning begins. Quest set the type; run our warning-phase FX/notify (NOT weather — the
            // quest forces the sky at the Active transition). BeginStorm's own warnTicks no longer
            // drive timing (the quest does), but it sets _activeStormType + warning notifications.
            _activeStormType = (stormType >= 0 && stormType < kStormTypeTableSize) ? stormType : 0;
            BeginStorm(_activeStormType);
        }
        else if (stormState == 2)
        {
            // Storm HITS. Run the existing Warning->Active arrival FX (siren stop, IMAD, dome, ghoul
            // spawns) but DO NOT force weather here — the quest already did via Weather.ForceActive.
            OnStormActivate();   // see Step 3: extracted from the old Warning case, minus ForceWeather
        }
        else if (stormState == 3)
        {
            // Aftermath: fade FX/audio. Quest handles releasing the weather to clear.
            OnStormAftermath();  // see Step 3: extracted from the old Active->Aftermath teardown
        }
        else if (stormState == 0)
        {
            // Storm fully over. Final teardown + idle.
            OnStormEnded();      // see Step 3
        }
        _lastStormStateSeen = stormState;
    }

    // While Active, keep applying exposure/FX-visibility every tick (the quest doesn't do per-tick dmg).
    if (_stormPhase == StormPhase::Active && _activeStormType >= 0 &&
        _activeStormType < kStormTypeTableSize)
    {
        UpdateStormAudio();
        ApplyStormExposure();
        UpdateStormFXVisibility();
        const StormType& st = kStormTypes[_activeStormType];
        if (st.lethal && (--_fleeTick <= 0))
        {
            _fleeTick = kFleeRecheckTicks;
            MakeNearbyNPCsFlee();
        }
        if (st.lethal)
        {
            if (IsPlayerSheltered()) _stormExposedTicks = 0;
            else {
                if (_stormExposedTicks % kExposedNagTicks == 0)
                    Notify("You are exposed to lethal radiation!", true);
                ++_stormExposedTicks;
            }
        }
    }
```

Then delete the now-dead `Warning`, `Active`, and `Aftermath` `case` blocks (their logic moves into the `OnStorm*` helpers in Step 3), leaving the `switch` removed entirely. Keep the closing HUD-intensity block that follows.

- [ ] **Step 3: Extract the FX transitions into helpers (no weather forcing)**

Add three private methods. Declare them in `src/RadiationEngine.h` near `BeginStorm`:

```cpp
    void OnStormActivate();    // Warning->Active FX arrival (IMAD/dome/ghouls/audio). NO ForceWeather.
    void OnStormAftermath();   // Active->Aftermath FX fade. NO weather release (Papyrus owns weather).
    void OnStormEnded();       // Aftermath->Idle final teardown.
```

Implement them in `src/RadiationEngine.cpp` by moving the bodies out of the old `Warning`/`Active`/`Aftermath` cases, **removing every `sky->ForceWeather(...)` / `ReleaseStormWeather()` call** (Papyrus now owns weather). `OnStormActivate()` contains the old Warning→Active arrival block (peak notify, siren fade, firestorm nuke, IMAD, dome, ghoul spawns, `EnterStormPhase(StormPhase::Active, ...)` — but use a large sentinel tick count since the quest controls duration, e.g. `EnterStormPhase(StormPhase::Active, INT_MAX)`). `OnStormAftermath()` contains the old Active→Aftermath teardown (`ClearFireRefs`, `ClearStormIMAD`, `TriggerStormFX(false)`, audio fade, `EnterStormPhase(StormPhase::Aftermath, INT_MAX)`) **minus** `ReleaseStormWeather()`. `OnStormEnded()` does `_activeStormType = -1; EnterStormPhase(StormPhase::Idle, 0);` and clears storm ghoul/fire refs as a safety net.

> Concrete extraction: copy the exact statements currently inside each case (visible in `RadiationEngine.cpp` ~1913–2106) into the matching helper, then delete the two lines `if (RE::TESWeather* w = _stormWeathers[...]) { ... sky->ForceWeather(w, true); _weatherForced = true; }` from `OnStormActivate` and the line `ReleaseStormWeather();` from `OnStormAftermath`. Everything else (FX, audio, NPC, notifications) is preserved verbatim.

- [ ] **Step 4: Neutralize the DLL scheduler + debug-force paths that forced weather**

In `UpdateStorm()`, the debug-force block (`if (_forceStormGlobal) { ... DebugForceStorm(v-1); }`) and `_forceStormNow` path should now drive the **quest**, not the DLL. Simplest correct change: have `DebugForceStorm(int)` set `_stormTypeGlobal` and `_stormsEnabledGlobal` and bump a "force now" global the quest watches, instead of calling `BeginStorm` directly. For this task, make `DebugForceStorm` set `RadOverhaul_ForceStorm` (`0x853`) to `typeIndex+1` (the quest already reads a force trigger — see Task 7), and remove the direct `BeginStorm`/`ForceWeather` it used to do. Leave `ScheduleNextStorm`, `_nextStormPlaySec`, `_playSeconds` co-save intact (harmless; no longer triggers storms) to avoid touching serialization compatibility this task.

Edit `DebugForceStorm` (search its definition) to:

```cpp
void RadiationEngine::DebugForceStorm(int typeIndex)
{
    // Storms are Papyrus-owned now: ask the controller quest to start one by setting the type + the
    // force-trigger global it watches. The DLL FX/exposure will follow when the quest flips StormState.
    if (typeIndex < 0 || typeIndex >= kStormTypeTableSize) return;
    if (_stormTypeGlobal)    _stormTypeGlobal->value    = static_cast<float>(typeIndex);
    if (_stormsEnabledGlobal) _stormsEnabledGlobal->value = 1.0f;   // can't force a storm while disabled
    if (_forceStormGlobal)   _forceStormGlobal->value   = static_cast<float>(typeIndex + 1);
    spdlog::get("FalloutAnomaly")->info("[Storm] Debug force -> type {} (via quest).", typeIndex);
    Notify("[DEBUG] Storm requested.", false);
}
```

Similarly make `DebugEndStorm()` set `_forceStormGlobal` to a sentinel the quest treats as "end now" (e.g. `99`) instead of manipulating weather directly; keep its existing `ForceClearAndSilence()` audio stop (audio is still DLL-owned).

- [ ] **Step 5: Build**

Run:
```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
xmake
```
Expected: clean build. Fix any references to deleted case-local variables (move them into the helpers). Confirm no remaining `sky->ForceWeather` call exists in the storm path: 
```powershell
Select-String -Path "src\RadiationEngine.cpp" -Pattern "ForceWeather|ReleaseStormWeather"
```
Expected matches now only in `ForceClearAndSilence` / `OnNewGame` clear-on-entry safety (intentional), NOT in the storm activate/aftermath path.

- [ ] **Step 6: Deploy DLL + ESP** (close FO4 first). Copy the DLL as in Task 1 Step 6 **and** copy the rebuilt `RadiationOverhaul.esp` to the mod folder:
```powershell
Copy-Item "E:\F4SE OG\Non Prisma Projects\Rad Mod\tooling\esl_build\RadiationOverhaul.esp" `
  "E:\Modlists\Fallen World Alpha 2\mods\Fallout Anomaly Master Files\RadiationOverhaul.esp" -Force
```
(Confirm the ESP's live location first with `Get-ChildItem "E:\Modlists\Fallen World Alpha 2\mods" -Recurse -Filter "RadiationOverhaul.esp"`.)

- [ ] **Step 7: In-game verification (interim — storms will not fire yet)**

At this point no storm fires (the quest doesn't exist yet), which is expected and proves the DLL no longer self-schedules. Verify:
- Load a save, play 5+ minutes: `FalloutAnomalyLog` shows `[ESL] Storm-control globals: state=0x..., type=0x..., enabled=0x...` (all non-null) and **no** `STORM INCOMING` lines.
- The "back-to-back storms after leaving shelter" bug cannot reproduce because the DLL no longer forces weather.

- [ ] **Step 8: Commit**

```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
git add src/RadiationEngine.cpp src/RadiationEngine.h tooling/esl_build/RadiationOverhaul.esp
git commit -m "refactor(rad): DLL follows Papyrus StormState global; remove DLL-side scheduling + weather forcing"
```

---

### Task 7: Write the RadStormController Papyrus quest

The quest owns scheduling, phase timing, weather forcing/release, the enabled gate, and load/cell re-assertion of clear weather. It publishes `RadOverhaul_StormState` / `RadOverhaul_StormType`, reads `RadOverhaul_StormsEnabled`, and watches `RadOverhaul_ForceStorm` (debug). Phase durations mirror the C++ `kStormTypes[]` table (Ash 90/120/30s, Firestorm 90/100/30s, Sulfur 60/180/20s, Acid 60/180/20s); scheduling gap is 35–90 real minutes (use real-time `RegisterForSingleUpdate`, which only fires during active gameplay, matching the old `_playSeconds` clock).

**Files:**
- Create: `E:\F4SE OG\Non Prisma Projects\Rad Mod\Scripts\Source\User\RadStormController.psc`
- Reference for native signatures: `E:\F4SE OG\Reference\GardennOfEden\GardenOfEden*.txt`

- [ ] **Step 1: Author the script**

Create `Scripts\Source\User\RadStormController.psc`:

```papyrus
Scriptname RadStormController extends Quest
{Owns radiation-storm scheduling, phase timing, and weather forcing. The C++ DLL follows the
 RadOverhaul_StormState global this script sets and applies all damage/FX/NPC effects.}

; --- Properties (filled in xEdit/CK after compile, or auto-filled by the quest record's script props) ---
GlobalVariable Property RadOverhaul_StormState    Auto  ; 0 idle,1 warning,2 active,3 aftermath
GlobalVariable Property RadOverhaul_StormType     Auto  ; 0 Ash,1 Fire,2 Sulfur,3 Acid
GlobalVariable Property RadOverhaul_StormsEnabled Auto  ; 1 enabled, 0 disabled
GlobalVariable Property RadOverhaul_ForceStorm    Auto  ; debug: 1..4 force type-1; 99 = end now; 0 idle

Weather Property RadOverhaul_AshFallout   Auto
Weather Property RadOverhaul_Firestorm    Auto
Weather Property RadOverhaul_SulfurStorm  Auto
Weather Property RadOverhaul_AcidRain     Auto
Weather Property CommonwealthClear        Auto  ; Fallout4.esm 0x0002B52A

; Phase durations (seconds) per type, index 0..3. Mirrors C++ kStormTypes[].
Float[] Property WarnSecs   Auto  ; [90,90,60,60]
Float[] Property ActiveSecs Auto  ; [120,100,180,180]
Float[] Property AfterSecs  Auto  ; [30,30,20,20]

; Scheduling gap bounds (real minutes), mirrors C++ kStormMinRealMin/kStormMaxRealMin.
Float Property MinGapMin Auto  ; 35.0
Float Property MaxGapMin Auto  ; 90.0

; Update IDs (we multiplex one OnUpdate by tracking the current phase in the StormState global).
Float Property ForcePollSecs Auto  ; 1.0  (how often to poll the debug ForceStorm global while idle)

Event OnInit()
    InitDefaults()
    ScheduleNext()
EndEvent

Event OnPlayerLoadGame()
    ; On every load, make sure we are not stuck mid-storm with forced weather: if idle, release to
    ; clear so a save made during a (now-finished) storm never re-applies a lingering sky.
    If RadOverhaul_StormState.GetValue() == 0
        ReleaseToClear()
    EndIf
    ; Re-arm the scheduler/phase timer (OnUpdate registrations do not survive a load).
    RearmTimer()
EndEvent

Function InitDefaults()
    If WarnSecs.Length == 0
        ; Fallback if properties were left empty in the CK — keep the script self-sufficient.
    EndIf
    RadOverhaul_StormState.SetValue(0.0)
EndFunction

Function ScheduleNext()
    RadOverhaul_StormState.SetValue(0.0)
    Float gapMin = MinGapMin + Utility.RandomFloat(0.0, MaxGapMin - MinGapMin)
    ; RegisterForSingleUpdate uses REAL time but only counts active (unpaused) gameplay, matching the
    ; old _playSeconds clock: sleeping/menus do not advance it, so storms are a true play-gap apart.
    RegisterForSingleUpdate(gapMin * 60.0)
    Debug.Trace("[RadStorm] Next storm in " + gapMin + " real min.")
EndFunction

Function RearmTimer()
    Int state = RadOverhaul_StormState.GetValue() as Int
    If state == 0
        ; Idle: poll the debug force global and keep the schedule alive.
        RegisterForSingleUpdate(ForcePollSecs)
    Else
        ; Mid-phase after a load: resume the phase with a fresh full duration (simplest robust choice;
        ; over-running a phase by at most its full length is harmless vs. getting stuck).
        Int t = RadOverhaul_StormType.GetValue() as Int
        If state == 1
            RegisterForSingleUpdate(WarnSecs[t])
        ElseIf state == 2
            ApplyStormWeather(t)
            RegisterForSingleUpdate(ActiveSecs[t])
        ElseIf state == 3
            RegisterForSingleUpdate(AfterSecs[t])
        EndIf
    EndIf
EndFunction

Event OnUpdate()
    Int force = RadOverhaul_ForceStorm.GetValue() as Int
    Int state = RadOverhaul_StormState.GetValue() as Int

    ; Debug: end-now sentinel.
    If force == 99
        RadOverhaul_ForceStorm.SetValue(0.0)
        EndStormNow()
        Return
    EndIf

    If state == 0
        ; Idle: a scheduled fire, or a debug force (1..4), starts a storm.
        If force >= 1 && force <= 4
            RadOverhaul_ForceStorm.SetValue(0.0)
            BeginStorm(force - 1)
        ElseIf StormsAllowed()
            BeginStorm(RollType())
        Else
            ; Storms disabled or this was just a poll tick: reschedule a poll and keep waiting.
            RegisterForSingleUpdate(ForcePollSecs)
        EndIf
    ElseIf state == 1
        EnterActive()
    ElseIf state == 2
        EnterAftermath()
    ElseIf state == 3
        EndStormNow()
    EndIf
EndEvent

Bool Function StormsAllowed()
    Return RadOverhaul_StormsEnabled.GetValue() >= 0.5
EndFunction

Int Function RollType()
    ; Weighted: Ash 15, Fire 15, Sulfur 35, Acid 35 (matches C++ weights, total 100).
    Int r = Utility.RandomInt(0, 99)
    If r < 15
        Return 0
    ElseIf r < 30
        Return 1
    ElseIf r < 65
        Return 2
    Else
        Return 3
    EndIf
EndFunction

Function BeginStorm(Int t)
    RadOverhaul_StormType.SetValue(t as Float)
    RadOverhaul_StormState.SetValue(1.0)   ; Warning — DLL runs warning FX/notify; weather stays normal
    Debug.Trace("[RadStorm] Warning, type " + t)
    RegisterForSingleUpdate(WarnSecs[t])
EndFunction

Function EnterActive()
    Int t = RadOverhaul_StormType.GetValue() as Int
    ApplyStormWeather(t)
    RadOverhaul_StormState.SetValue(2.0)   ; Active — DLL applies damage/FX
    Debug.Trace("[RadStorm] Active, type " + t)
    RegisterForSingleUpdate(ActiveSecs[t])
EndFunction

Function EnterAftermath()
    Int t = RadOverhaul_StormType.GetValue() as Int
    ReleaseToClear()                       ; hand the sky back to clear, robust across cells
    RadOverhaul_StormState.SetValue(3.0)   ; Aftermath — DLL fades FX
    Debug.Trace("[RadStorm] Aftermath, type " + t)
    RegisterForSingleUpdate(AfterSecs[t])
EndFunction

Function EndStormNow()
    ReleaseToClear()
    RadOverhaul_StormState.SetValue(0.0)
    Debug.Trace("[RadStorm] Storm over; rescheduling.")
    ScheduleNext()
EndFunction

Weather Function WeatherFor(Int t)
    If t == 0
        Return RadOverhaul_AshFallout
    ElseIf t == 1
        Return RadOverhaul_Firestorm
    ElseIf t == 2
        Return RadOverhaul_SulfurStorm
    Else
        Return RadOverhaul_AcidRain
    EndIf
EndFunction

Function ApplyStormWeather(Int t)
    Weather w = WeatherFor(t)
    If w
        ; ForceActive holds the storm sky; the engine manages it correctly across interior/exterior
        ; transitions, so it does NOT linger as "current" when the player shelters (the C++ bug).
        w.ForceActive(false)
    EndIf
EndFunction

Function ReleaseToClear()
    ; Snap to Commonwealth clear and release the override. SetActive(true) overrides any lingering
    ; storm sky; releasing afterwards lets the region weather system resume normally.
    If CommonwealthClear
        CommonwealthClear.SetActive(true, false)
    EndIf
EndFunction
```

> Native signatures to verify against `GardennOfEden` reference before compiling: `Weather.ForceActive(bool abOverride)`, `Weather.SetActive(bool abOverride, bool abAccelerate)`, `GlobalVariable.SetValue/GetValue`, `Utility.RandomFloat/RandomInt`, `RegisterForSingleUpdate(float)`, `OnPlayerLoadGame` (requires the quest to be a `ReferenceAlias` for player events OR use `Actor.OnPlayerLoadGame` — if `Quest` does not receive `OnPlayerLoadGame`, attach a small `ReferenceAlias` on the player in the quest and move `OnPlayerLoadGame` there, calling back into this script). Confirm and adjust during Step 2.

- [ ] **Step 2: Confirm event availability and adjust if needed**

`OnPlayerLoadGame` and `OnUpdate` on a bare `Quest` script: `OnUpdate` works on any `Form` with `RegisterForSingleUpdate`. `OnPlayerLoadGame` is delivered to alias scripts (and to the player actor), not quests. If the reference confirms quests do not get `OnPlayerLoadGame`, add a player `ReferenceAlias` named `PlayerAlias` to the quest in the ESP (Task 5/8) with a tiny script:

```papyrus
Scriptname RadStormPlayerAlias extends ReferenceAlias
RadStormController Property Controller Auto
Event OnPlayerLoadGame()
    Controller.OnGameLoaded()
EndEvent
```

and rename `RadStormController.OnPlayerLoadGame()` to a public `Function OnGameLoaded()`. Decide this in Step 2 and update both scripts before compiling.

- [ ] **Step 3: Compile**

Run:
```powershell
$compiler = "E:\SteamLibrary\steamapps\common\Fallout 4 1946160\Papyrus Compiler\PapyrusCompiler.exe"
$src = "E:\F4SE OG\Non Prisma Projects\Rad Mod\Scripts\Source\User"
$base = "E:\SteamLibrary\steamapps\common\Fallout 4 1946160\Data\Scripts\Source\Base"
$out = "E:\F4SE OG\Non Prisma Projects\Rad Mod\Scripts"
& $compiler "RadStormController.psc" -i="$src;$base" -o="$out" -f="Institute_Papyrus_Flags.flg"
```
(Per memory `reference_papyrus_compile_toolchain`: use the official PapyrusCompiler with the CK base source path; do a `Push-Location` off the Prisma CWD if needed; Caprica fails on CK scripts so use the official compiler.)

Expected: `Scripts\RadStormController.pex` produced, no compile errors. If `OnPlayerLoadGame` was moved to an alias, also compile `RadStormPlayerAlias.psc`.

- [ ] **Step 4: Commit the scripts**

```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
git add "Scripts/Source/User/RadStormController.psc" "Scripts/RadStormController.pex"
git commit -m "feat(rad-papyrus): RadStormController quest owns scheduling + weather forcing"
```

---

### Task 8: Wire the script into the ESP, fill properties, deploy, verify end-to-end

**Files:**
- Modify: `tooling/tools/gen_esl.py` (attach the `RadStormController` script + properties to quest `0x872`; add the player alias if used)
- Modify: deploy package

- [ ] **Step 1: Attach script + properties on the quest record**

In `gen_esl.py`, extend the `0x872 RadOverhaul_StormController` quest record to carry the `RadStormController` script with its property bindings (globals `0x855`/`0x856`/`0x857`/`0x853`, weathers `0x840`–`0x843`, `CommonwealthClear` = `Fallout4.esm:0x0002B52A`, and the float-array/scalar tuning props). Follow the existing pattern for how `gen_esl.py` attaches scripts/properties to perks/quests; if it has no script-property emitter yet, the simplest robust path is to author the script + properties **once in the CK/xEdit** after the quest exists, then re-extract to Spriggit JSON so `gen_esl.py` regeneration preserves them (document this in `ESP_BUILD_PIPELINE.md`).

Set the property values:
```
WarnSecs   = [90.0, 90.0, 60.0, 60.0]
ActiveSecs = [120.0, 100.0, 180.0, 180.0]
AfterSecs  = [30.0, 30.0, 20.0, 20.0]
MinGapMin  = 35.0
MaxGapMin  = 90.0
ForcePollSecs = 1.0
```

- [ ] **Step 2: Rebuild + deploy ESP, Pex, and DLL**

```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
python tooling/tools/gen_esl.py
python tooling/tools/merge_overrides.py --apply
$exe = "$env:LOCALAPPDATA\Temp\Spriggit\Translations\Spriggit.Json.Fallout4\0.40.1\Spriggit.Json.Fallout4.exe"
& $exe create-plugin -i "tooling/esl_src/RadiationOverhaul.esp" -o "tooling/esl_build/RadiationOverhaul.esp"
```
Then copy the ESP (Task 6 Step 6 path), the DLL (Task 1 Step 6 path), and the compiled Pex into the mod's `Scripts\` folder:
```powershell
$mod = "E:\Modlists\Fallen World Alpha 2\mods\Fallout Anomaly Master Files"
Copy-Item "E:\F4SE OG\Non Prisma Projects\Rad Mod\Scripts\RadStormController.pex" "$mod\Scripts\RadStormController.pex" -Force
```
(Confirm `$mod\Scripts` exists; create only that subfolder if missing with `New-Item -ItemType Directory`. Never delete the mod folder.)

- [ ] **Step 3: Force-start the quest on existing saves**

The DLL already has `EnsureQuestRunning(RE::TESQuest*)` for `StartGameEnabled` quests that are dormant on old saves. In `EnsureEslLoaded()`, resolve quest `0x872` into a `RE::TESQuest*` (mirror `_stormFXQuest` resolution) and call `EnsureQuestRunning()` on it so the controller runs on saves made before this update.

Edit `src/RadiationEngine.h` near `_stormFXQuest`:
```cpp
    RE::TESQuest* _stormControllerQuest = nullptr;  // RadOverhaul_StormController (0x872) — Papyrus storm owner
```
Edit `src/RadiationEngine.cpp` in `EnsureEslLoaded()` (after `_stormFXQuest` resolution + EnsureQuestRunning):
```cpp
    _stormControllerQuest = GetEslForm<RE::TESQuest>(0x872);
    if (_stormControllerQuest) EnsureQuestRunning(_stormControllerQuest);
    logger->info("[ESL] Storm controller quest {} (force-started for existing saves).",
        _stormControllerQuest ? "resolved" : "NOT found");
```
Rebuild + redeploy the DLL.

- [ ] **Step 4: In-game verification — the core acceptance test (Sanctuary shelter)**

Reproduce the original bug scenario:
1. Load a save inside the Sanctuary shelter (interior / Pulowski).
2. Use the debug menu to force a storm (Left Shift + F8 → force a storm type). Confirm `[RadStorm] Warning` then `[RadStorm] Active` Papyrus traces (enable Papyrus logging or read `Logs\Script\Papyrus.0.log`) and the DLL `STORM INCOMING` + arrival FX.
3. Wait out the storm **inside the shelter**. Confirm `[RadStorm] Aftermath` then `[RadStorm] Storm over; rescheduling.`
4. **Exit the shelter into the exterior.** Expected: clear weather, **no** storm re-fires, no `STORM INCOMING` on exit. Walk in/out of the shelter several times: weather stays clear.
5. Wait/sleep a full in-game day checking weather on each exit: storms occur only on the 35–90 min real-play schedule, never chained on shelter exit.

Expected: the back-to-back-storm-on-exit behavior is gone, because Papyrus `Weather.ForceActive`/`SetActive` releases the sky cleanly and the DLL no longer holds a lingering `ForceWeather` override.

- [ ] **Step 5: In-game verification — StormsEnabled gate (holotape/console kill-switch)**

Set `RadOverhaul_StormsEnabled` to 0 (via the debug menu add a button, or temporarily via a console-run quest function / holotape in a later task). Confirm no new storms schedule while disabled, and setting it back to 1 resumes scheduling. This gives the user the "disable storms" control they lacked (the holotape/cheat-weather attempts that failed before).

- [ ] **Step 6: Documentation + findings (mandatory, CLAUDE.md)**

- Append a `docs/CHANGELOG.md` entry for the Papyrus storm conversion.
- Update `docs/PROGRESS.md` FormID contract: add `0x855`/`0x856`/`0x857` globals + `0x872` quest, and note storms are now Papyrus-owned.
- Update `docs/ESP_BUILD_PIPELINE.md` with the Papyrus compile step + the script-property authoring note from Task 8 Step 1.
- Update `Prisma/docs/findings/` Rad Mod topic: `[FIXED]` the back-to-back-storm bug with root cause (lingering C++ `ForceWeather` across interior boundary) and the Papyrus-ownership resolution. Remove from `open-items.md`.

- [ ] **Step 7: Commit**

```powershell
cd "E:\F4SE OG\Non Prisma Projects\Rad Mod"
git add tooling src/RadiationEngine.cpp src/RadiationEngine.h Scripts docs
git commit -m "feat(rad): Papyrus storm controller wired end-to-end; fixes back-to-back storms on shelter exit"
cd "E:\F4SE OG"
git add "Prisma/docs"
git commit -m "docs(rad): Phase 2 storm Papyrus conversion + findings update"
```

---

## Self-Review

**Spec coverage:**
- #1 Shift+F8 does nothing → Task 3 (menu feedback when PrismaUI absent) + Task 2 (the in-menu Fix Stats button now actually sticks). User chose "keep menu, fix the button" — covered.
- #2 standard rain ignores roof → Task 1. Covered (root cause: `stormActive` excluded rainy weather from the shelter path).
- #3 SPECIAL reset works "by 5s" / survival-mode → Task 2 (full restore + suspend re-application until rad stage changes). User chose "Full restore to permanent." Covered. Note documented: the DLL cannot restore *survival-mode's* own SPECIAL penalties (foreign temp modifiers) — the suspend mechanism stops the Rad Mod from compounding them, which is the in-scope half.
- #4 can't leave Sanctuary / back-to-back storms → Tasks 5–8 (Papyrus conversion). User chose "Papyrus quest conversion." Covered.

**Placeholder scan:** No "TBD"/"handle edge cases"/"similar to Task N." Two intentional *read-then-confirm* anchors exist (Task 3 Step 1 guard names; Task 5 Step 1 gen_esl structure; Task 6 Step 1 `GetEslForm` helper name) because the exact local symbol names must be read from the file before editing — each instructs the engineer to substitute the real name found, and gives a concrete template. These are necessary because the helper/guard identifiers were not in the excerpts read during planning.

**Type consistency:** `_specialSuspendStage` (int32_t) declared in Task 2 Step 1, used in Steps 2/3/4. Globals `_stormStateGlobal`/`_stormTypeGlobal`/`_stormsEnabledGlobal` declared Task 6 Step 1, used Steps 2/4. Papyrus globals `RadOverhaul_StormState`/`StormType`/`StormsEnabled`/`ForceStorm` consistent between Task 5 (ESP), Task 6 (DLL reader), Task 7 (quest). Phase encoding 0/1/2/3 = Idle/Warning/Active/Aftermath consistent across DLL follower and Papyrus quest. Helper names `OnStormActivate`/`OnStormAftermath`/`OnStormEnded` declared and implemented in Task 6 Steps 2–3.

**Known risk flagged:** Task 6 is the highest-risk change (gutting the DLL state machine). It is sequenced so Task 6 Step 7 verifies the DLL safely does nothing before the quest is introduced in Task 7, giving a clean rollback point (revert Tasks 5–8, keep Phase 1) if the Papyrus path misbehaves.

---

**Plan complete and saved to `docs/superpowers/plans/2026-06-22-rad-mod-fixes-and-storm-papyrus.md`.**
