# Rad Overhaul completion review

Date: 2026-09-26. **The full system is not complete.**

## Reviewed baseline

Repository: https://git.nomadicinteractive.dev/PRISMA/LargePluginMods

| PR | Current branch head | Scope reviewed | Result |
|---|---|---|---|
| #66 | `30c264b5a0dc245615548fb66e66f7621182060a` | Weather return and storm teardown at the integrated head | No verified defect in this slice; live traversal remains pending |
| #68 | `3e5e3901e14343167516d95be0117697a7dfe99e` | Climate persistence and load/new-game lifecycle at the integrated head | No verified defect in this slice; live persistence remains pending |
| #70 | `8a0ea418e93f6a515c61bdc3d1786cbea90250c5` | Needs policy, diagnostic trace, build wiring and completion gaps | Foundation only; no connected Needs gameplay system |

Both #66 and #68 are ancestors of #70. Review used the exact #70 head, not the older commits cited in PR descriptions and imported comments. These are bounded reviews, not certification of every change in these large PRs. Unrelated CombatMod and Wasteland Hunter PRs were excluded.

Worktree: `C:/Users/Ricky/Documents/RadRecovery-20260927/git/rad-recovered`.
Branch: `nomad/rad-complete-20260926`. The shared checkout's existing edits were preserved.

## Fixed validation blocker

Commit `bb84c221c66f1adb278f0b94dc121ea51318a7e8` adds MSVC support to `tests/verify_rad_stack_integration.py:check_generation_behavior` and initializes the installed developer shell in `.github/workflows/rad-overhaul-build.yml:Run contract tests`.

Before: the mandatory check failed with `generation gate behavior: no host C++ compiler` in the normal Windows shell, although xmake found MSVC. After: the actual C++ dispatch harness compiles and runs with MSVC. GCC/Clang command support remains. Independent Ponytail review found no unnecessary complexity.

## Local validation

Validated the integration head plus the test/workflow fix and the Needs observation changes described below with:

- Windows MSVC DLL build: PASS. Runtime sources include the Needs observation changes below.
- All seven xmake policy executables: PASS (`rad_correctness_policy_tests`, `climate_policy_tests`, `climate_weather_policy_tests`, `climate_wetness_policy_tests`, `needs_policy_tests`, `needs_trace_policy_tests`, `serialization_policy_tests`).
- All 17 `tests/verify_*.py` scripts: PASS. These include source-contract assertions and the compiled generation behavior harness; they are not all runtime tests.
- `tests/armor_diagnostics_tests.js` and `tests/status_hud_tests.js`: PASS.
- `git diff --check`: PASS.

DLL: `build/release/FalloutAnomalyRadiation.dll`.
SHA-256: `2C208DED73E3B85112933E0B2E5F1FD8C9D5BBD4DEDBB5AE3C0EC85B6E348A44`.

Build used a clean local copy of the workflow-pinned CommonLib commit `6ab58418bdb639936265db713baf064b36560484` and its shared dependency `cb7c610335b7e4b4fc8a451ed11c993e5b1103f9`. `COMMONLIBF4_PATH` selected that copy and `GITHUB_ACTIONS=true` disabled the existing after-build live deployment. No DLL was deployed. Build files remain untracked and are not contribution content.

The supplied RE repositories were read through Forgejo at `commonlib_NonVR` main `a64ea356f02bf2bc3f2821f8997bbb366082858b` and `commonlib-shared` main `4ada850fb3c7ba06c789083d2771868f1ccdb128`; no dependency pin or CommonLib source was changed. Newer repository heads were not substituted into this baseline build.

No new remote CI run or live-game acceptance is claimed. HTTPS Git redirected to Cloudflare Access; the advertised SSH endpoint was unreachable. Forgejo read tools worked. The local fix has not been pushed, and no PR was merged or externally reviewed.

## Needs observation implemented locally

`src/NeedsRuntime.cpp:NeedsRuntime::Update` now advances native hunger, thirst and sleep debt from positive elapsed Calendar days on the generation-guarded game-main update. Versioned `needs.json` supplies provisional hourly rates once; invalid/missing settings disable progression. No consumption inference, sleep relief, vanilla Survival mutation or penalties were added.

`src/main.cpp:SaveCallback` writes `NEED`; the existing sole iterator in `src/RadEngine_Serialization.cpp:LoadState` restores it after validating the stream. Missing, malformed and conflicting records cannot inherit a previous save's needs. Revert/new-game/load reset the calendar baseline. `tests/needs_policy_tests.cpp` proves time and record policies and exercises the production read/write helpers through an in-memory byte stream. These checks do not prove live F4SE callback ordering or game persistence.

The portable Needs test failed before the new clock/record implementation and passed afterward. The full local check set above passed again. Independent read-only Ponytail review found no unnecessary complexity or serious defect in this bounded diff. The existing size guard required moving SaveState/LoadState and their private constants into `src/RadEngine_Serialization.cpp`; a token comparison against the pre-split source confirmed identical code excluding comments and whitespace, the DLL rebuilt, and the affected source-contract checks passed after updating paths. These changes are committed locally as `56be5f3`.

## Needs HUD implemented locally

`src/RadHudNeeds.cpp:RadHud::PushNeeds` sends changed native percentages to a dedicated PrismaHUD widget. Existing local layout controls support its position, size, rotation, visibility, save, reset and preview. Session reset hides the widget until a new native payload arrives. It applies no effects or relief.

`tests/status_hud_tests.js` exercises percentages, malformed inputs, preview restoration and delayed layout callbacks. The browser preview exposed an existing startup issue across the status widgets: their delayed default-position callback overwrote local saved layout. The new callback-dispatch regression failed (`22px` versus requested `384px`) before the one-line position-marker fix in each widget, then passed.

Headless Edge rendered the actual Needs page and retained its position/scale through delayed initialization. Actual DOM clicks in the existing local editor verified Save, Reset and Done for Needs. The rendered screenshot `build-msvc/needs-hud-preview.png` was inspected at three-times display scale. The DLL, seven policy executables, 17 Python checks and both Node suites passed again. Independent read-only Ponytail reviews found no complexity to remove or serious defect in this bounded change. This is browser/local validation; actual PrismaHUD registration and game rendering remain unproven.

## Remaining completion work

The selected scope is issue #69, together with the existing Rad release gates:

1. **Needs acquisition proof:** capture the matrix in `docs/research/needs-consumption-trace.md` using `src/NeedsConsumptionTrace.cpp`. Include food, drink, favorites, chems, failed use, direct calls, and drop/transfer/sell/crafting negative controls. Existing research and PR comments explicitly withhold production consumption integration pending that proof. No new consumption inference was introduced.
2. **Actual sleep recovery:** prove elapsed sleep separately from wait and interrupted sleep before applying relief. No sleep adapter is connected in the current source.
3. **Complete Needs:** Calendar progression, live `NEED` record dispatch and native-state HUD presentation are implemented locally. Acquisition and load-once automatic/keyword/item classification remain. Prove independent operation and actual HUD behavior with vanilla Survival enabled or disabled.
4. **Persistence acceptance:** `src/RadEngine_Serialization.cpp:LoadState` owns the sole record iterator and restores climate and Needs. Portable record-policy tests pass; prove actual save/load/revert/new-game isolation before effects.
5. **Disease and later survival phases:** issue #69's risk pools, disease state machine, JSON definitions, `DISE` persistence, and eventual companion exhaustion are absent from the source. They are not covered by the Needs policy tests.
6. **Gameplay effects:** keep climate/Needs/disease penalties gated until observation acceptance. Then verify idempotent application and removal on load/reset/configuration changes.
7. **Live Rad/climate acceptance:** storms and teardown across cell/interior transitions; firestorm visibility; NPC temporary masks and equipment restoration; climate heat/shelter/equipment behavior; non-default CLIM save/exit/relaunch/load; both supported runtimes where support is claimed. Existing issues #38/#39/#46/#47 and the #68 checklist have not been closed by local build results.
8. **ESP issue #67:** current deploy asset audit PASS. The obsolete self-owned 000854/000870 records and old quest EditorID are already absent. Recovered the pinned serializer task-locally and proved a round trip with all 215 JSON files, master order, record counts/types and FormKeys equal; the original deploy ESP is unchanged. `tests/verify_ghoul_contract.py` now checks the compiled asset and retained severity/storm records. See [pipeline and evidence](ESP_BUILD_PIPELINE.md). The old external generator remains unavailable; its contents and live save compatibility are not certified. The remote issue remains open.

## Review anchors

- Weather: `src/RadEngine_Weather.cpp:CaptureWeatherReturnTarget`, `UpdateWeatherHandoff`, `BeginSmoothWeather`; `src/RadEngine_Storm_Weather.cpp:ReleaseStormWeather`, `ForceClearAndSilence`; `src/RadEngine_Storm_Update.cpp:UpdateStorm`. Existing runtime excerpt: `docs/logs/2026-09-01-firestorm-followup/README.md` (older head, incomplete traversal).
- Climate: `src/main.cpp:SaveCallback`, `RevertCallback`, `MessageHandler`; `src/RadEngine_Serialization.cpp:LoadState`; `src/ClimateRuntime.cpp:Reset`, `RestoreFromSave`, `Update`; `src/Hooks.cpp:QueueEngineUpdate`; `tests/serialization_policy_tests.cpp`. Live F4SE callback-thread evidence is still missing.
- Needs: `src/NeedsPolicy.h:Advance`, `ApplyRelief`; `src/NeedsSerializationPolicy.h:Snapshot`, `Restore`; `src/NeedsConsumptionTrace.cpp:ActivateOnGameThread`, `PlayerDrinkPotionTrace`; `src/RadLifecycle.cpp:EnsureEngineActiveOnGameThread`; `tests/needs_policy_tests.cpp`, `tests/needs_trace_policy_tests.cpp`.
