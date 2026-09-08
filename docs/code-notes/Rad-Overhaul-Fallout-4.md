# Rad Overhaul code notes

## PrismaUI HUD creation handoff

Reference: `src/RadHud.cpp:RadHud::RequestAPI` and `src/RadHud.cpp:RadHud::CreateView`.

PrismaUI 2.1 exposes `IVPrismaUI11::DispatchToGameThread`. HUD creation uses that verified dispatcher because callback-bearing `CreateView` is rejected until PrismaUI's own window-thread dispatcher is ready. The retry is bounded and falls back to the existing update-path recovery.

- PrismaHUD is resolved before asynchronous PrismaUI view creation starts, so a fast DOM-ready callback cannot miss initial toxicity/filter registration.
- Optional PrismaHUD acquisition is retried on later safe lifecycle events when PrismaUI is already available. Already-acquired provider pointers are preserved.
- DOM callback bridge pointers are atomically published because PrismaUI callbacks may run on the web thread while view recovery runs on the verified game thread.
- The fullscreen Rad overlay remains at normal compositor order. Only the toxicity/filter widgets use PrismaHUD's high HUD order.

Reference: `src/RadHud.cpp:RadHud::PushToxic` and `src/main.cpp:WaitThenCreateRadHud`.

View recovery runs before the in-world data gate so a missing Pip-Boy/location signal cannot prevent the views from being created; payload updates remain gated until the player is in the world. If the world-ready waiter reaches its five-second hold but `CreateView` dispatch is transiently rejected, it retains the generation latch and retries rather than silently abandoning HUD creation.

## Engine startup and recovery

Reference: `src/Hooks.cpp`, `src/RadLifecycle.cpp`, and `src/main.cpp:MessageHandler`.

- F4SE lifecycle callbacks never sleep waiting for PrismaUI. If `PrismaUI_F4.dll` is loaded but the V11 dispatcher is not accepting work yet, the activation request is deferred.
- The Win32 `WH_GETMESSAGE` dispatcher is a no-PrismaUI fallback only. Rad does not mix an early Win32 queue with a PrismaUI provider that is still starting.
- `RadLifecycle::EnsureEngineActiveOnGameThread` independently tracks core initialization and the PlayerCharacter update producer. It is safe to retry after a transient dispatch failure, config/init failure, or hook-install failure.
- `kPostLoadGame` and `kNewGame` call the lifecycle gate before any engine mutation. `OnPostLoad`, `OnNewGame`, penalty resets, and HUD waiting are skipped when activation is incomplete.
- The log reports `Radiation Engine ACTIVE` only after the core and update producer are both confirmed live.

## Storm dome streaming ownership

Reference: `src/RadEngine_Storm_Dome.cpp:RadiationEngine::UpdateStormDomeLifecycle`, `src/RadEngine_Storm_Dome.cpp:RadiationEngine::UpdateStormDomeFollow`, `src/Hooks_DomeFollow.cpp:Hooks::QueueDomeFollow`, and `src/RadCorrectnessPolicy.h:ShouldRecreateDomeForOwner`.

The 2026-09-01 runtime capture under `docs/logs/2026-09-01-storm-jitter/` proved that ordinary exterior movement could produce `cellSame=false, worldSame=true`, followed immediately by `Storm dome cleared` and a full native respawn. Parent-cell changes are normal Fallout 4 exterior streaming and are not a worldspace ownership change.

The ownership policy therefore keeps the current dome references alive across same-world exterior cell changes and lets the existing follow path reposition them. Destructive recreation is reserved for an actual worldspace change. This removes the measured cell-boundary clear/respawn jitter without weakening the worldspace safety gate.

Reference: `src/Hooks_DomeFollow.cpp:Hooks::SetDomeFollowActive` and `src/Hooks_DomeFollow.cpp:Hooks::QueueDomeFollow`.

The follow path is inactive outside a live dome, then bounded to approximately 30 Hz with a 16-unit movement threshold and skips refs without loaded 3D. It only updates existing references; spawn, deletion, reparenting, shelter visibility, and worldspace ownership remain on the 4 Hz lifecycle path.

## Regression coverage

`tests/verify_hud_fallback.py` guards compositor ordering, position/resize behavior, V11 non-blocking dispatch, truthful engine activation, retained HUD-wait retries, optional PrismaHUD recovery, and atomic cross-thread callback publication. `tests/correctness_policy_tests.cpp` covers the policy-level lifecycle and load-generation invariants plus same-world dome streaming versus real worldspace ownership changes.

## Firestorm weather and nuke follow-up

References: `src/RadEngine_Weather.cpp:RadiationEngine::CaptureWeatherReturnTarget`, `src/RadEngine_Storm_Weather.cpp:RadiationEngine::ForceClearAndSilence`, and `src/RadEngine_Storm_Weather.cpp:RadiationEngine::SpawnDistantNuke`.

The return target excludes vanilla radstorm weather so a managed firestorm cannot hand control back to an ambient radstorm. Debug clear/endstorm commands force the configured clear weather and clear transient handoff state. Distant nuke placement records its strike coordinates and uses a rotation aimed back at the player for runtime diagnosis.
## Storm dome and firestorm nuke presentation

References: `src/RadEngine_Storm_Dome.cpp:RadiationEngine::SpawnStormDome`, `src/RadEngine_Storm_Dome.cpp:RadiationEngine::UpdateStormDomeLifecycle`, `src/RadEngine_Storm_FX.cpp:RadiationEngine::TriggerStormFX`, and `src/RadEngine_Storm_Weather.cpp:RadiationEngine::SpawnDistantNuke`.

The native fog dome is restricted to Ash Fallout. Other storm types retain their weather-specific atmosphere and effects without creating the dome. The firestorm explosion remains a native detonation, placed 3,500–5,000 units from the player at terrain height so the effect remains within the loaded exterior grid.

References: `src/RadEngine_Storm_Weather.cpp:ResolveGroundHeight` and `src/RadEngine_Storm_Weather.cpp:RadiationEngine::SpawnDistantNuke`.

The nuke policy supplies only distant X/Y and facing. The runtime resolves the loaded target cell's collision surface through the verified `TESObjectCELL::Pick` wrapper, reconstructs its height from the probe endpoints and hit fraction, adds 16 units of clearance, and retries when the target cell or probe is unavailable. Nuke logs include player Z, target X/Y, hit fraction, resolved surface Z, final Z, and the resolution result.

## Storm-ghoul controls

References: `src/RadEngine_Debug.cpp:RadiationEngine::DispatchDebugCommand`, `src/RadEngine_Storm_Actors.cpp:RadiationEngine::SpawnStormGhouls`, and `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.html`.

The GHOULS debug tab adjusts the chance used by future managed or accepted vanilla radstorms, enables or disables future packs, and can clear the current pack. `RadiationEngine::PushGhoulSettingsToHud` sends the native enabled/chance state to `RadHud::PushGhoulSettings` when the menu opens and after each command, so the persistent status line does not rely on button-side JavaScript state. These controls are session-local and default to enabled with a 40% chance after restart; the spawn roll uses `>=` so a configured 0% chance cannot spawn a pack.

`src/PrismaUI_F4_API.h:PRISMA_UI_API::IVPrismaUI2::InteropCall` accepts its payload as `const char*`. Existing JSON callbacks such as `radhud.js:window.stormPhase`, `radhud.js:window.updateFilter`, and `radhud.js:window.showSpecialInfo` therefore accept both JSON text and object input. `radhud.js:setGhoulSettings` follows the same string-or-object parsing contract so native `InteropCall` state remains authoritative for both enabled state and chance.

## Transition and queue hardening

The visual-follow producer remains coalesced and throttled, while the legacy fallback dispatch queue is capped at 128 tasks. A full fallback queue rejects work so a stalled game thread cannot accumulate unbounded closures.

Reference: `src/RadEngine_Storm_Actors.cpp:RadiationEngine::CheckGhoulPackCleanup` and `src/RadEngine_Storm_Actors.cpp:RadiationEngine::SpawnStormGhouls`.

Dome follow and nuke placement now require an attached exterior player cell whose worldspace matches the cached player worldspace. Dome references are checked for attached exterior ownership before movement and are recreated when their tracked cell becomes stale. Storm-ghoul cleanup defers while player cell/world ownership is temporarily unresolved during exterior streaming. The hunter pack is cleared only after a confirmed interior transition, worldspace change, distance exit, or when a defeated prior pack is explicitly retired before another pack is created.

Weather return handoff state is bounded: a replaced target, non-finite blend, or transition that exceeds the wait window releases the temporary override and clears all handoff bookkeeping. The same transient state is reset on debug force, load, clear, and new return transitions.

## Runtime support boundary

Reference: `src/main.cpp:DetectGameVersion` and `src/main.cpp:F4SEPlugin_Query`.

The plugin accepts OG `1.10.163` and the specifically verified AE runtime `1.11.240`. NG `1.10.980` through `1.10.984`, unknown versions, and other post-NG versions fail closed until their native and hook contracts are independently verified.
