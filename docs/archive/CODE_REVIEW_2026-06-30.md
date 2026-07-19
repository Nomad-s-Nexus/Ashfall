# Rad Mod - Code Review & Issue Tracker (2026-06-30)

Full-codebase review of FalloutAnomalyRadiation ("Rad Mod"). Produced by four parallel
review passes (core radiation systems, storm system + dust-freeze, HUD/PrismaUI views,
lifecycle/config/build/docs) cross-checked against the 14,000-line runtime log, then worked
through with each candidate verified against the real code before any change.

**Status legend:** `OPEN` not started - `FIXED` done + built - `DEFERRED` needs a decision or
in-game data before it is safe to touch - `VERIFIED` checked and correct as-is (agent flagged it,
reading the code showed it is not a defect).

Build is clean with **zero warnings** after this pass (C4244 and C4463 both eliminated).

---

## LIVE ISSUE - Dust Storm hard freeze  (STATUS: DEFERRED - needs one in-game data point)

Triggering the Dust Storm (and possibly any storm) from the debug menu hard-freezes the game
(hang, no crash log). Reconciled conclusion from all passes + the runtime log:

- **Not** the ESP, weather/IMAD records, JS unicode, or storm logic. All verified clean; the
  5-type integration is complete and index-consistent (no off-by-one).
- `DebugForceStorm` logs `[DEBUG] Force storm` at warn (flushed) and it is ABSENT in every
  freeze session, so the storm C++ code provably never runs.
- The menu OPEN completes fine (`[HUD] Debug menu opened` logged 25 times). The freeze is at
  button CLICK, inside the PrismaUI JS-to-native bridge (`window.radDebug`), upstream of our code.

The deployed DLL carries DIAG probes. Open the menu, press Dust Storm, let it freeze, kill the
game, then read the log:
- `[DIAG] HandleDebugCommand recv` present  -> click reached C++; freeze is downstream.
- absent -> confirms the JS-to-native bridge hang. Fix: defer `radDebug` via `setTimeout(0)` to
  break click-handler reentrancy, or upgrade the bridge to `IVPrismaUI4::BindUIEvent` (game-thread
  safe; the code currently requests `IVPrismaUI2`).

Note: the `#hud` null-deref (H1) was already fixed this pass, removing it as a confound. It fired at
menu OPEN (which completes), so it was never the freeze, but it was a real bug.

---

## HIGH severity

| ID | Finding | Status |
|----|---------|--------|
| H1 | `radhud.js` half-migrated: `#hud` + vitals elements deleted from HTML, JS still dereferenced them unguarded (`SetDebugMenu`, `applyHudVisibility`, `draggable`), and dead `updateRad` threw every tick. | **FIXED** - all `#hud` access null-guarded; `updateRad` early-returns cheaply. Full dead-vitals-code excision + matching C++ `Push`/prefs removal deferred (see below). |
| H2 | `xmake.lua` had no `after_build`/deploy; DLL never auto-copied to the modlist. | **FIXED** - `after_build` deploys to `XSE_FO4_MODS_PATH` or the live modlist (trycp/isdir, never fails the build). |
| H3 | Per-tick `spdlog::get("FalloutAnomaly")` (global-mutex registry lookup) on the 4Hz hook + most helpers. | **FIXED** - cached once via a file-scope `Log()` helper; ~40 call sites now hit a static pointer. |
| H4 | `FilterItemCount()` walked the inventory every tick for a cosmetic HUD count. | **FIXED** - cached, refreshed at most every 8 ticks and only while masked. |
| H5 | Co-save `LoadState` does not seek past unknown/version-mismatched records. | **VERIFIED** - F4SE `GetNextRecordInfo` auto-skips unread record bytes, so this is tolerated. A storm-clock version bump would still discard (not migrate) the old clock; acceptable, documented. Left as-is to avoid save-format regression risk. |
| H6 | `raceSwitchPending` signed 1-bit field overflow (C4463). | **FIXED** - removed with the dead player-ghoulification migration. |

## MED severity

| ID | Finding | Status |
|----|---------|--------|
| M1 | `items/perks/zones.json` dead at runtime yet shipped, and `Initialize()` logged "Items 10+ / Perks 10+ / Zones 10". | **FIXED** - false log lines corrected; dead copies removed from `deploy/` + live (DLL reads only `thresholds.json`). `data/` + `tooling/` copies kept: `zones.json` feeds the ESP generator. |
| M2 | "10-stage system" is really 6 thresholds; the log claimed 10. | **FIXED** - log line corrected. |
| M3 | Storm screen FX (dome/shake) dead on pre-existing saves: `EnsureQuestRunning` is a no-op; `SetStage` fails on a dormant quest. | **DEFERRED** - the VM `Quest.Start()` path is documented to CTD (null callback). Needs a safe force-start (proper stack-callback or a running-flag write), not a blind fix. |
| M4 | `_lastGameTime` never serialized -> sleep heal-on-load no-ops once per session. | **VERIFIED / by-design** - time only advances during play; persisting it would wrongly heal across different saves/timelines. Current no-heal-on-first-load is correct. |
| M5 | RadAway cure heuristic fires on any >15 rad drop, not RadAway specifically. | **DEFERRED** - behavioral; regular tissue re-accrues so low stakes. A reliable inventory-delta (as chelation uses) is the right change but alters feel - decide first. |
| M6 | `ApplyClampedSpecial` floors `int(perm)` -> penalty vanishes for base-1 stats. | **VERIFIED** - the floor is correct: rounding would breach the "never below 1" invariant (perm 1.9 penalized by 1 -> 0.9, the exact corruption it guards against). |
| M7 | Detached `CreateView` wait-thread could briefly double up across a fast reload. | **FIXED (benign)** - `CreateView` is already idempotent (harmless), and the two spawn sites are deduped into `StartRadHudWaitIfIdle()`. |
| M8 | `SaveHudPrefs` issues ~30 synchronous INI writes on the JS thread. | **DEFERRED** - only reachable from the now-dead vitals drag handlers; resolved when the dead vitals code + C++ prefs path are excised (H1 follow-up). |
| M9 | `MessageHandler` dereferenced the file-scope `logger` unconditionally (null if init threw). | **FIXED** - `InitializeLogger` wrapped in try/catch with a null-sink fallback so `logger` is always valid; `MessageHandler` also guards. |

## LOW / cleanup

| ID | Finding | Status |
|----|---------|--------|
| L1 | Stale pre-reorg `Non Prisma Projects\Rad Mod` paths in `Program.cs` (ESP generator would fail), `convert_sounds.ps1` (doubled `Rad Mod\Rad Mod`), `README.md`. | **FIXED** - all three repointed to `Projects\Rad Mod`. |
| L2 | No guard that `kStormTypeCount` == `kStormTypeTableSize`. | **FIXED** - `static_assert` added in `Initialize()`. |
| L3 | Stale comments (`0x880-0x883`, `DebugForceStorm 0..3`, IMAD index list) predate the 5th storm type. | **FIXED** - comments updated to include Dust / 0x884 / 0..4. |
| L4 | Dust `fogNear = 900` sat on the "camera-centered dome" threshold. | **FIXED** - bumped to 960. |
| L5 | `#include <thread>/<atomic>` mid-file inside the anonymous namespace. | **FIXED** - moved to the top include block. |
| L6 | C4244 `SetStage(int -> uint16_t)` narrowing. | **FIXED** - explicit `static_cast<std::uint16_t>`; warning gone. |
| L7 | Magic numbers: `DebugAddTissue` literal `1000` vs `kTissueMax`; `_playSeconds += 0.25`. | **FIXED (partial)** - `DebugAddTissue` now uses `kTissueMax`. `_playSeconds += 0.25` left as a documented literal (single use; a one-off constant added no clarity). |
| L8 | Filter/condition drain does not account for time-jumps the way tissue does. | **OPEN (minor)** - low impact; left for a future tuning pass. |

## Verified solid (no action)

- Dust Storm 5-type integration complete and index-consistent (arrays sized to 5, indexing by
  storm-index not themeId, 0x844/0x884 resolve at runtime). No off-by-one.
- C++ <-> JS binding contract complete; threading discipline correct (JS thread only enqueues);
  lifecycle ordering correct; blocking-modal handling, view-handle recreation, no astral unicode.
- The four 1-line project docs are legit redirects to the workspace hub (real content exists there).

---

## Fix log

- **2026-06-30 (ghoul)** Removed dead player-ghoulification migration (`RevertGhoulIfDisabled` +
  `_ghoulQuest` + `_ghoulGlobal` + `kHumanRaceFormID` + two call sites). Resolves H6 (C4463). Kept
  the separate live storm-ghoul-spawn feature (`_ghoulKeyword`/`IsGhoulActor`).
- **2026-06-30 (review pass)** Fixed H1 (JS #hud guards), H2 (after_build deploy), H3 (cached
  `Log()`), H4 (filter-count throttle), M1 (dead JSON + false logs), M2 (stage-count log), M7
  (thread dedup), M9 (logger robustness), L1 (stale paths), L2 (static_assert), L3 (stale comments),
  L4 (dust fog 900->960), L6 (SetStage cast), L7 (DebugAddTissue kTissueMax). Build clean, zero
  warnings, auto-deployed. Reclassified H5/M4/M6 as VERIFIED after reading the code.

## Recommended follow-ups (need the in-game test or a decision)

1. **Run the DIAG test** to localize the Dust Storm freeze, then fix the confirmed bridge path.
2. **Full `radhud.js` excision** - remove the ~200 lines of dead vitals/drag/prefs code AND the
   matching C++ (`RadHud::Push`/`updateRad`, `LoadHudPrefsToView`/`SaveHudPrefs`, `ToggleHud`, the
   toggle-key INI). Resolves M8. A coordinated C++/JS pass; do it after the freeze test confirms the
   guards behave, so the live HUD is never at risk mid-diagnosis.
3. **M3** - a safe storm-FX quest force-start for pre-existing saves.
4. **M5** - decide whether RadAway tissue heal should switch to a reliable inventory-delta.
