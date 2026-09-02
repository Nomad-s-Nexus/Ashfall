#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>

using namespace RadInternal;

// Storm lifecycle: scheduling, phases, FX (fire ring / ghouls / IMAD / quest stages), NPC effects, exposure.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


void RadiationEngine::ReleaseStormWeather()
{
    _dustBuildupActive = false;
    _dustFogRollTick = 0;
    _warnTicksTotal = 0;
    _dustRollPrevWeather = nullptr;
    _dustRollElapsed = 0;
    _dustRollTotal = 0;
    if (RE::TESWeather* dust = _stormWeathers[kDustStormIndex])
    {
        const StormType& st = kStormTypes[kDustStormIndex];
        ApplyWeatherFog(dust, st.fogNear, st.fogFar, st.fogPower, st.fogHeightMid, st.fogHeightRange, st.fogHiDensity);
    }

    _sirenDelayTicks = 0;
    StopStormCue(kRadstormCueFalloffMs);

    auto* sky = RE::Sky::GetSingleton();
    if (!sky)
    {
        _releaseWeatherOverrideWhenSettled = false;
        _weatherHandoffSettleTicks = 0;
        _weatherForced = false;
        return;
    }

    RE::TESWeather* target = _weatherReturnCaptured ? _weatherReturnTarget : nullptr;
    if (!target)
        target = _clearWeather;

    if (!target)
    {
        sky->ReleaseWeatherOverride();
        _weatherReturnTarget = nullptr;
        _weatherReturnCaptured = false;
        _releaseWeatherOverrideWhenSettled = false;
        _weatherHandoffSettleTicks = 0;
        _weatherForced = false;
        Log()->warn("[StormWeather] No remembered/clear weather available; released override without a managed transition.");
        return;
    }

    if (sky->currentWeather == target)
    {
        sky->overrideWeather = target;
        _weatherReturnTarget = target;
        _weatherReturnCaptured = true;
        _releaseWeatherOverrideWhenSettled = true;
        _weatherForced = true;
        Log()->debug("[StormWeather] Return transition already targets 0x{:06X} at pct {:.2f}; continuing without restart.",
            target->GetFormID(), sky->currentWeatherPct);
        return;
    }

    const std::uint32_t previous = sky->currentWeather ? sky->currentWeather->GetFormID() : 0;
    _weatherReturnTarget = target;
    _weatherReturnCaptured = true;
    _weatherHandoffSettleTicks = 0;
    BeginSmoothWeather(target, /*lockOverride=*/true);
    _releaseWeatherOverrideWhenSettled = true;
    _weatherForced = true;
    Log()->info("[StormWeather] Returning weather 0x{:06X} -> 0x{:06X} smoothly; region control releases only after the blend is stable.",
        previous, target->GetFormID());
}
void RadiationEngine::ForceClearAndSilence()
{
    StopStormCue(kRadstormCueFalloffMs);
    if (_stormSoundHandle.IsValid())
        _stormSoundHandle.FadeOutAndRelease(kStormSoundFadeMs);
    _curStormBed = nullptr;
    _radstormWarned = false;
    ReleaseStormWeather();

    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !_clearWeather) return;
    sky->ReleaseWeatherOverride();
    sky->ForceWeather(_clearWeather, false);
    _weatherReturnTarget = nullptr;
    _weatherReturnCaptured = false;
    _releaseWeatherOverrideWhenSettled = false;
    _weatherHandoffSettleTicks = 0;
    _weatherForced = false;
}
void RadiationEngine::SpawnFireRing()
{
    if (!_fireHazardForm) return;
    if (IsPlayerSheltered()) return;  // no indoor fire ring

    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player) return;

    // SpawnFireRing only called when !IsPlayerSheltered(), so player is in exterior.
    // Use cachedWorldspace for the placement; interior is null for exterior cells.
    auto* world = player->cachedWorldspace;
    if (!world) return;

    auto logger = Log();
    ClearFireRefs();  // clean up any leftover refs from a previous storm

    const RE::NiPoint3 base = player->GetPosition();
    constexpr float  kRingRadius = 350.0f;  // game units (~17 feet) — close enough to be menacing
    constexpr int    kCount      = 8;       // fire pillars in a ring
    constexpr float  kPi         = 3.14159265f;

    int spawned = 0;
    for (int i = 0; i < kCount; ++i)
    {
        const float angle = (static_cast<float>(i) / kCount) * 2.0f * kPi;

        RE::NEW_REFR_DATA data;
        data.object    = static_cast<RE::TESBoundObject*>(_fireHazardForm);
        data.location  = { base.x + kRingRadius * std::cos(angle),
                           base.y + kRingRadius * std::sin(angle),
                           base.z };
        data.direction = { 0.0f, 0.0f, 0.0f };
        data.interior  = nullptr;
        data.world     = world;

        auto handle = dh->CreateReferenceAtLocation(data);
        if (static_cast<bool>(handle))
        {
            _fireRefs.push_back(handle);
            ++spawned;
        }
    }
    logger->info("[Storm/Fire] Spawned {} fire hazard refs around player (radius {:.0f}).",
        spawned, kRingRadius);
}
// One distant, purely cinematic firestorm detonation. The 4 Hz engine tick (RadiationEngine::Update and
// everything it calls, this included) is marshaled to the verified Fallout 4 game-main thread by
// Hooks.cpp's dispatcher — this does NOT run on the worker Update hook. The EXPL record
// FARO_EmissionStrikeAlpha (0x885) still keeps PlacedObject = Null by design: it detonates without
// spawning a placed reference, which is exactly what a pure horizon visual wants.
// Returns true iff the explosion actually detonated (valid handle). false = a TRANSIENT failure the
// caller should retry within its bounded window: unsupported runtime, player 3D/worldspace not up,
// sheltered, or the target cell not yet streamed (engine returns a null handle). The one-shot latch
// (_stormNukeFired) must only be set on a true return, so a streaming stall can't consume the storm's
// single nuke.
bool RadiationEngine::SpawnDistantNuke()
{
    if (!REX::FModule::IsRuntimeOG()) return false;   // authority wrapper is currently OG-only; fail closed elsewhere
    if (!_nukeExplosionForm || IsPlayerSheltered()) return false;

    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player || !player->cachedWorldspace) return false;
    if (!player->Get3D()) return false;               // worldspace must actually be up

    static std::mt19937 rng{ std::random_device{}() };
    constexpr float kPi = 3.14159265f;
    std::uniform_real_distribution<float> yawJitter(-25.0f, 25.0f);
    // Keep the giant visual inside the exterior streaming envelope, but far enough away to
    // read as a horizon-scale detonation instead of a player-side blast.
    std::uniform_real_distribution<float> distanceDist(6000.0f, 7000.0f);

    // Place the single strike along the player's current view vector, never in a ring.
    // Engine angles are radians; the small jitter keeps each storm's composition natural.
    const float yaw = player->data.angle.z + yawJitter(rng) * (kPi / 180.0f);
    const float distance = distanceDist(rng);
    const RE::NiPoint3 base = player->GetPosition();
    const auto nuke = RadCorrectnessPolicy::DistantNukePlacement(
        { base.x, base.y, base.z }, yaw, distance, 1500.0f);
    const RE::NiPoint3 pos{ nuke.strike.x, nuke.strike.y, nuke.strike.z };
    const RE::NiPoint3 rot{ 0.0f, 0.0f, nuke.facingYaw };

    // DETONATE (not place) via the authoritative commonlib_NonVR wrapper. Cell is passed null on
    // purpose: with a worldspace supplied the engine resolves the cell from the position and returns a
    // null handle if it is not loaded, instead of faulting.
    auto handle = dh->CreateExplosionAtLocation(
        static_cast<RE::TESBoundObject*>(_nukeExplosionForm),
        pos, rot, nullptr, player->cachedWorldspace);

    Log()->info("[Storm/Nuke] distant firestorm strike detonated={} distance={:.0f} strike=({:.0f},{:.0f},{:.0f}) facingPlayerYaw={:.2f}.",
        static_cast<bool>(handle), distance, pos.x, pos.y, pos.z, nuke.facingYaw);
    if (!handle)
        Log()->warn("[Storm/Nuke] null handle — target cell not loaded at that position (engine gate), "
                    "or the EXPL form failed its type check. Will retry within the bounded window.");
    return static_cast<bool>(handle);
}
