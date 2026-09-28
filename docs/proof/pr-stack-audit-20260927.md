# PR stack audit, 2026-09-27

## TLDR

Repository: https://git.nomadicinteractive.dev/PRISMA/LargePluginMods

| PR | Audited final head | Windows CI |
|---|---|---|
| 66 | 450017c2334e695350aaf83a77ec617ee51d7248 | https://git.nomadicinteractive.dev/PRISMA/LargePluginMods/actions/runs/12 PASS |
| 68 | e7cf602563db3cdca917d167c24196b0d7db0a6c | https://git.nomadicinteractive.dev/PRISMA/LargePluginMods/actions/runs/7 PASS |
| 70 | c07a37fef71ffc6d40534427f9f04d6bf4e63749 | https://git.nomadicinteractive.dev/PRISMA/LargePluginMods/actions/runs/8 PASS |
| 71 | 9d1886f6ff228731a4b449e0b4d92dace4176413 | https://git.nomadicinteractive.dev/PRISMA/LargePluginMods/actions/runs/2 PASS |

Source, tests, assets and documentation were compared by Git blob hash to the complete, untruncated remote trees. Final PR66 matches its local worktree completely. For 68/70/71 only build-workflow blob hashes differ locally; production, tests and assets match. Local worktrees: `git/rad-pr66-20260927` at e1644f8; `git/rad-pr68-20260927` at e45be21; `git/rad-pr70-20260927` at 62db6a5; `git/rad-pr-stack-20260927` at 70982ec.

Seven portable C++ policy executables, both Node suites, and eight focused Python contracts passed again in the PR71 worktree. Windows MSVC DLL linking and artifact upload were checked in the current-head CI jobs, including final PR66 job 1346. `git diff --check` passed for all four worktrees; the original current remote diffs contained no added trailing whitespace. No gameplay acceptance is inferred from source contracts.

No PR was merged, closed, retargeted or taken out of WIP. No CommonLib file was changed. Audit remains limited by the RE and live-evidence gaps below.

## Verified defects

### PR66 CI copied the DLL into the live modlist: fixed

- File/function: `Rad-Overhaul-Fallout-4/xmake.lua:FalloutAnomalyRadiation.after_build`.
- Failure path: the Actions build executed an unconditional deployment callback; an existing destination folder allowed `os.trycp` to overwrite the installed DLL.
- Proof: job 1292 at da33711fbfa0012c9dc6141914bfccd3f1659f60 logged `[deploy] FalloutAnomalyRadiation.dll -> E:/Modlists/Fallen World Alpha 2/mods/RadiationOverhaul/F4SE/Plugins` at 2026-09-28 00:02:12 UTC.
- Fix: use the existing PR68 `GITHUB_ACTIONS` early-return guard; add a behavioral regression and its contract-step invocation. Local reviewed commit e1644f8; remote fix commit 858c8c4dacf62263d58c08c5932b43745d99bb2d; final head is in the table.
- Regression: `tests/verify_ci_deploy.py` executes the actual Lua callback with fake environment/filesystem operations. Before: FAIL, two CI copies. After: PASS, zero CI copies and two local copies. Job 1346 ran it successfully and its complete log has no `[deploy]` line.
- Residual impact: `E:/Modlists/Fallen World Alpha 2/mods/RadiationOverhaul/F4SE/Plugins/FalloutAnomalyRadiation.dll` still has SHA256 `0C631EE4160ADEA06CA33B78F72F246C8493437B72F59ADFF5829F851A9DEF39`, size 1035264 and write time 2026-09-28 00:02:12 UTC. These stayed unchanged through the fixed run. No adjacent backup was found; prior bytes were not restored.

No other defect was promoted from an unverified engine assumption. Independent read-only review found no portable Needs defect.

Historical PR68 comment 509: `src/ClimateRuntime.cpp:Update` now uses `ElapsedGameSeconds` in both normal and jump paths, supports bounded substeps, and supplies zero elapsed time for ordinary paused/no-time samples. `GameMonth` now falls back to 8, matching the inspected CommonLib declaration. Portable tests cover conversion, zero-step pause, delayed steps and simulated jumps. Actual menu and wait/sleep/fast-travel behavior is still a runtime gate.

## RE gaps/blockers

ReverseEng was checked first. Primary inspected repository: https://git.nomadicinteractive.dev/ReverseEng/commonlib_NonVR at dbaeca7486300f4f1cc15e4607f8208f0df93013.

| File/function | OG 1.10.163 | AE 1.11.240 | Scope |
|---|---|---|---|
| `src/RE/C/Calendar.cpp:GetMonth`, `include/RE/C/Calendar.h` | Declaration inspected | Declaration inspected | Fallback and enum agree; this does not independently verify executable layout or runtime clock behavior |
| `include/RE/A/Actor.h:DrinkPotion`, `RE-Work/ghidra-dump/needs-consumption-bindings-2026-09-09/INDEX.md` | Declaration/alias inspected | Declaration/alias inspected | Current private raw vtable/body proof could not be reopened |
| `RE-Work/ghidra-dump/needs-consumption-bindings-2026-09-09/get-item-icon-og.txt`, `get-item-icon-ae.txt`: GetItemIconID | Stamped static record inspected | Stamped static record inspected | Classification evidence does not establish consumption/cardinality |
| `RE-Work/Runtime/04-TASK-SYSTEM-OG.md`, `05-TASK-SYSTEM-AE-1.11.240.md`: queue/main-loop records | Static records present | Static records inspected | Live F4SE serialization callback ordering/thread affinity remains unproven |
| `include/RE/P/PlayerCamera.h:ShakeCamera`, `RE-Work/docs/rad-overhaul-natives-2026-08.md` | Existing stamped record | Unresolved and guarded | Do not claim AE camera-shake parity |
| `include/RE/T/TESDataHandler.h:CreateExplosionAtLocation` | Existing stamped record | Authority revision disagreement | Forgejo main is OG-only; the read-only build pin below includes an AE mapping and stamped note |

Read-only build authority: https://github.com/PRISMA-USER-INTERFACE-FRAMEWORK/commonlib_NonVR at 6ab58418bdb639936265db713baf064b36560484 (68/70/71), and 961358dd72838417579fc6186a5a0da3b3242d37 (66). Both tracked checkouts remained clean. Forgejo ReverseEng could not resolve the 6ab5841 SHA. Its current native wrapper differs from the build authority. Reconcile repository revision/provenance before certifying the AE integration; do not modify CommonLib to hide this gap.

BLOCKED: `docs/research/needs-consumption-trace.md` cites `E:/Projects/ToolsReverse Engineering/ReverseEngineering/Research/Findings/NeedsConsumption-2026-09-09`. That directory is absent on this PC. The CSV manifest is insufficient to revalidate the missing DrinkPotion body, callback ABI, queue path and inventory-cardinality evidence. An established CommonLibF4 fallback was searched; the historical LucaDotGit pin could not be retrieved, and no replacement OG+AE success proof was established. Stop at this proof boundary; add no consumption adapter.

`src/NeedsConsumptionTrace.cpp:ActivateOnGameThread` currently records four event streams plus player DrinkPotion entry/return. Success callback, RemoveItem, inventory-list, perk, animation and sleep/wait streams required by the wider research matrix are not all instrumented. The callback signature remains a specific RE gap.

## Runtime-only gates

- 66/68 inherited storm gates: smooth weather return, clear/endstorm/release controls, save/load mid-handoff, same-world streamed-cell and interior teardown, bounded queues/hard-freeze reproduction, native dome ownership, ghoul-pack pending activation/persistence/cleanup, NPC temporary mask removal and displaced-equipment restoration.
- 68 Firestorm: no dome; farther detonation visible within retries; exposed damage after blast; siren and supported shake behavior. Climate: paused menus, delayed dispatcher, wait/sleep/fast-travel, tuning and 24-hour jump cost; non-default CLIM save/exit/relaunch/load and cross-save/new-game isolation.
- 68 old checklist limits still present: universal 45% automatic piece warmth, selected thermal biped regions, movement/cell-cached roof probes, heat sources passed as active after discovery, movement/load-event heat invalidation, small built-in heat catalog, discrete weather sampling, uniform interior baseline. These require equipment, workshop-roof, source on/off, weather-transition and dense/large-radius runtime evidence; no engine failure is claimed from these limits alone.
- 70: all 17 consumption rows remain NOT CAPTURED for both runtimes. Capture food/drink inputs and special ingestibles, direct calls and failed use, with drop/store/give/sell/buy/crafting controls and correlated ordering/cardinality. No live consumption or sleep relief exists.
- 71: Calendar progression, native NEED read/write dispatch and read-only HUD exist. Portable tests prove policy and mocked record behavior, not live callbacks. Prove non-default save/restart/load, absent/invalid/conflicting records, revert/new-game/load isolation, paused and jumped time, and native percentages/layout with and without PrismaHUD on both runtimes. Prove Survival enabled/disabled compatibility before effects. Consumption, sleep recovery, disease/DISE and companion work remain outside the implemented observation slice.

## Exact stack cleanup steps

1. Keep 66 open: its new `verify_ci_deploy.py`, workflow invocation and anchored CI proof note are not in 68. Carry those narrow additions into `feat/rad-cold-survival-core`, preserving 68's existing deployment guard, climate/storm work and 6ab5841 dependency pin. A mechanical merge of local 66/68 equivalents reports conflicts in the build workflow and code note; do not use wholesale ours/theirs resolution.
2. Preserve the inherited 66 runtime checklist when closure becomes eligible. Its old source head 51b1bb154b6286a46139988768c7a76cfe789676 is an ancestor of 68's pre-CI head 221ec5a57921b8ae8a48a4ce4565c354606f9c2e; weather-handoff and actor modules were retained. Later dome and Firestorm changes are intentional. Current 66 also has the new regression delta, so complete supersession is not yet true.
3. Refresh `feat/rad-native-survival-needs` (#70) on finalized #68. It contains 221ec5a but not #68's current CI commits. Preserve the Needs policy and observation-only trace; deduplicate equivalent workflow changes.
4. Refresh `nomad/rad-recovered-20260927` (#71) on finalized #70. It contains the pre-CI #70 head 0c17d55950544343d4631f78c383ee87bad6ebac, not the current #70 head. Its source additions preserve the previous policy/trace and add Calendar/NEED/HUD. Local equivalent CI merges for 68->70 and 70->71 were conflict-free; this does not prove rewritten remote heads before they exist.
5. Verify exact ancestry, focused contracts, MSVC builds and clean diffs on every resulting head. Retain bases 68->70->71 and WIP/runtime gates. Update stale PR validation text only when explicitly authorized; perform no force-push, closure or merge from this audit.
6. Before gameplay acceptance, reconcile the installed modlist DLL with the intended validation artifact; the accidental old PR66 copy is not evidence for 68/70/71 runtime acceptance.
