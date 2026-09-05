#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/B/bhkPickData.h>
#include <RE/G/GridCellArray.h>
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>
#include <RE/T/TES.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESWorldSpace.h>

using namespace RadInternal;

namespace
{
    constexpr float kGroundProbeTop = 20000.0f;
    constexpr float kGroundProbeBottom = -20000.0f;
    constexpr float kNukeGroundClearance = 16.0f;

    bool ResolveGroundHeight(
        RE::TESWorldSpace* world, const RE::NiPoint3& target, float& groundZ, float& hitFraction)
    {
        hitFraction = -1.0f;
        if (!world || !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z))
            return false;

        auto* tes = RE::TES::GetSingleton();
        if (!tes || tes->worldSpace != world || !tes->gridCells)
            return false;

        const RE::NiPoint3 start{ target.x, target.y, target.z + kGroundProbeTop };
        const RE::NiPoint3 end{ target.x, target.y, target.z + kGroundProbeBottom };
        auto* grid = tes->gridCells;
        float bestFraction = 2.0f;
        bool found = false;

        for (std::uint32_t gy = 0; gy < grid->dimension; ++gy)
        {
            for (std::uint32_t gx = 0; gx < grid->dimension; ++gx)
            {
                auto* cell = grid->GetCell(gx, gy);
                if (!cell || !cell->IsExterior() || !cell->IsAttached() ||
                    cell->worldSpace != world || !cell->GetbhkWorld())
                    continue;

                RE::bhkPickData pick;
                pick.SetStartEnd(start, end);
                (void)cell->Pick(pick);
                if (!pick.HasHit())
                    continue;

                const float fraction = pick.GetHitFraction();
                if (!std::isfinite(fraction) || fraction < 0.0f || fraction > 1.0f || fraction >= bestFraction)
                    continue;

                bestFraction = fraction;
                found = true;
            }
        }

        if (!found)
            return false;

        hitFraction = bestFraction;
        groundZ = start.z + (end.z - start.z) * bestFraction;
        return std::isfinite(groundZ);
    }
}

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
        _weatherReturnTarget = nullptr;
        _weatherReturnCaptured = false;
        _releaseWeatherOverrideWhenSettled = false;
        _weatherHandoffSettleTicks = 0;
        _weatherHandoffWaitTicks = 0;
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
        _weatherHandoffWaitTicks = 0;
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
    _weatherHandoffWaitTicks = 0;
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
    _weatherReturnTarget = nullptr;
    _weatherReturnCaptured = false;
    _releaseWeatherOverrideWhenSettled = false;
    _weatherHandoffSettleTicks = 0;
    _weatherHandoffWaitTicks = 0;
    _weatherForced = false;

    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !_clearWeather) return;
    sky->ReleaseWeatherOverride();
    sky->ForceWeather(_clearWeather, false);
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
// Returns true iff the explosion actually detonated (valid handle). false = a TRANSIENT failure the
// caller should retry within its bounded window: unsupported runtime, player 3D/worldspace not up,
// sheltered, or the target cell not yet streamed (engine returns a null handle). The one-shot latch
// (_stormNukeFired) must only be set on a true return, so a streaming stall can't consume the storm's
// single nuke.
bool RadiationEngine::SpawnDistantNuke()
{
    if (!REX::FModule::IsRuntimeOG() && !REX::FModule::IsRuntimeAE()) return false;
    if (!_nukeExplosionForm || IsPlayerSheltered()) return false;

    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player || !player->cachedWorldspace) return false;
    if (!player->Get3D()) return false;               // worldspace must actually be up
    auto* playerCell = player->GetParentCell();
    if (!playerCell || !playerCell->IsExterior() || !playerCell->IsAttached() ||
        playerCell->worldSpace != player->cachedWorldspace) return false;

    static std::mt19937 rng{ std::random_device{}() };
    constexpr float kPi = 3.14159265f;
    std::uniform_real_distribution<float> yawJitter(-25.0f, 25.0f);
    std::uniform_real_distribution<float> distanceDist(3500.0f, 5000.0f);

    const float yaw = player->data.angle.z + yawJitter(rng) * (kPi / 180.0f);
    const float distance = distanceDist(rng);
    const RE::NiPoint3 base = player->GetPosition();
    if (!std::isfinite(base.x) || !std::isfinite(base.y) || !std::isfinite(base.z) || !std::isfinite(yaw) ||
        !std::isfinite(distance)) return false;
    const auto nuke = RadCorrectnessPolicy::DistantNukePlacementXY(
        { base.x, base.y, base.z }, yaw, distance);
    const RE::NiPoint3 targetXY{ nuke.strike.x, nuke.strike.y, 0.0f };
    float groundZ = 0.0f;
    float hitFraction = -1.0f;
    if (!ResolveGroundHeight(player->cachedWorldspace, targetXY, groundZ, hitFraction))
    {
        Log()->warn("[Storm/Nuke] ground resolution failed playerZ={:.0f} targetXY=({:.0f},{:.0f}) "
                    "hitFraction={:.3f} groundResolved=false; will retry within the bounded window.",
            base.z, targetXY.x, targetXY.y, hitFraction);
        return false;
    }

    const RE::NiPoint3 pos{ targetXY.x, targetXY.y, groundZ + kNukeGroundClearance };
    const RE::NiPoint3 rot{ 0.0f, 0.0f, nuke.facingYaw };

    // DETONATE (not place) via the authoritative commonlib_NonVR wrapper. Cell is passed null on
    // purpose: with a worldspace supplied the engine resolves the cell from the position and returns a
    // null handle if it is not loaded, instead of faulting.
    auto handle = dh->CreateExplosionAtLocation(
        static_cast<RE::TESBoundObject*>(_nukeExplosionForm),
        pos, rot, nullptr, player->cachedWorldspace);

    Log()->info("[Storm/Nuke] distant firestorm strike detonated={} playerZ={:.0f} "
                "targetXY=({:.0f},{:.0f}) groundZ={:.0f} finalZ={:.0f} "
                "distance={:.0f} groundResolved=true hitFraction={:.3f} facingPlayerYaw={:.2f}.",
        static_cast<bool>(handle), base.z, targetXY.x, targetXY.y, groundZ, pos.z, distance, hitFraction, nuke.facingYaw);
    if (!handle)
        Log()->warn("[Storm/Nuke] null handle — target cell not loaded at that position (engine gate), "
                    "or the EXPL form failed its type check. Will retry within the bounded window.");
    return static_cast<bool>(handle);
}
