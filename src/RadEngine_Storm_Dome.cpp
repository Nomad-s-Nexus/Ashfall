#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include "Hooks.h"
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>

using namespace RadInternal;

// Storm lifecycle: scheduling, phases, FX (fire ring / ghouls / IMAD / quest stages), NPC effects, exposure.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


void RadiationEngine::SpawnStormDome()
{
    if (_activeStormType != kAshStormIndex)
    {
        if (_stormDomeUp || !_stormDomeRefs.empty())
            ClearStormDome();
        return;
    }
    if (_stormDomeUp || IsPlayerSheltered()) return;
    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player || !player->cachedWorldspace) return;

    // Clear any leftover refs first, mirroring SpawnFireRing. _stormDomeUp only latches on placed dome
    // ACTI layers (below); if those fail to resolve but the arena lights do, _stormDomeUp stays false
    // and every re-exposure would otherwise append another light pair, leaking the previous ones.
    ClearStormDome();

    const RE::NiPoint3 base = player->GetPosition();
    auto* const playerCell = player->GetParentCell();
    auto* const playerWorld = player->cachedWorldspace;
    int placed = 0;
    for (int k = 0; k < 4; ++k)
    {
        if (!_stormDomeForms[k]) continue;
        RE::NEW_REFR_DATA data;
        data.object    = static_cast<RE::TESBoundObject*>(_stormDomeForms[k]);
        data.location  = { base.x, base.y, base.z + kStormDomeZOffset };
        data.direction = { 0.0f, 0.0f, 0.0f };
        data.interior  = nullptr;
        data.world     = playerWorld;
        auto handle = dh->CreateReferenceAtLocation(data);
        if (static_cast<bool>(handle))
        {
            _stormDomeRefs.push_back({ handle, kStormDomeZOffset });
            ++placed;
        }
    }
    _stormDomeUp = placed > 0;
    Hooks::SetDomeFollowActive(_stormDomeUp);
    if (_stormDomeUp)
    {
        _stormDomeCell = playerCell;
        _stormDomeWorld = playerWorld;
    }
    _stormDomeAnimStarted = false;   // force-start the animation once these refs' 3D attaches

    // Arena lights: the cloud shapes are effect-LIT, so these lights ARE the glowing-vortex look (bright at
    // the eye, falling off outward). Mirror KORs' PlaceAtNode exactly: ArenaLight01 at TopLightNode and
    // ArenaLight02 (lens flare) at LensFlareNode, both UNROTATED (the nodes carry translation only).
    // Tracked in _stormDomeRefs so ClearStormDome tears them down with the dome.
    //
    // Gate the lights on a successful dome (placed > 0): they only illuminate the cloud shapes, so lights
    // without a dome are pointless — and, critically, if the dome ACTIs never resolve, _stormDomeUp stays
    // false and an exposed player re-enters SpawnStormDome every tick. Placing the lights here regardless
    // would then create them and (via the ClearStormDome above) destroy them every tick — retry churn.
    bool lit = false, flare = false;
    if (placed > 0)
    {
        auto placeLight = [&](RE::TESBoundObject* obj, float nodeZ) -> bool {
            if (!obj) return false;
            RE::NEW_REFR_DATA ld;
            ld.object    = obj;
            ld.location  = { base.x, base.y, base.z + kStormDomeZOffset + nodeZ };
            ld.direction = { 0.0f, 0.0f, 0.0f };
            ld.interior  = nullptr;
            ld.world     = playerWorld;
            auto lh = dh->CreateReferenceAtLocation(ld);
            if (!static_cast<bool>(lh)) return false;
            _stormDomeRefs.push_back({ lh, kStormDomeZOffset + nodeZ });
            return true;
        };
        lit   = placeLight(_stormDomeLight, kStormDomeLightNodeZ);
        flare = placeLight(_stormDomeFlare, kStormDomeLensFlareZ);
    }
    Log()->info("[StormFX] Native storm dome spawned: {}/4 layers at z+{:.0f}, arena light {}, lens flare {}.",
        placed, kStormDomeZOffset, lit ? "on" : "off", flare ? "on" : "off");
}
bool RadiationEngine::NativeShakeCamera(float strength, float duration)
{
    if (!REX::FModule::IsRuntimeOG()) return false;   // authority wrapper is currently OG-only; fail closed elsewhere
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->Get3D()) return false;    // 3D not up yet (streaming) — transient
    auto* cell = player->parentCell;
    if (!cell || cell->IsInterior()) return false;    // interior / no cell — no exterior shake

    if (strength > 1.0f) strength = 1.0f;       // native does not clamp; the Papyrus binding did
    if (strength <= 0.0f) return true;          // valid state, nothing to shake — not a failure

    const RE::NiPoint3 pos = player->data.location;
    // Authoritative commonlib_NonVR wrapper; distanceMult defaults to 1.0 ("no distance scaling").
    RE::ShakeCamera(strength, pos, duration);
    return true;
}
// Per-tick shake driver for an ACTIVE lethal storm: a decaying burst on the hit, then a low
// sustained rumble. Mirrors RadStormFX._StartShake/_kRumbleTimer, which never actually ran.
void RadiationEngine::UpdateStormShake()
{
    if (_shakeBurstStep < 0 && _shakeRumbleTick < 0) return;
    if (IsPlayerSheltered()) return;            // sheltered = no screen FX, same rule as the dome

    if (_shakeBurstStep >= 0)
    {
        const float t = static_cast<float>(kStormShakeBurstSteps - _shakeBurstStep) /
                        static_cast<float>(kStormShakeBurstSteps);
        if (NativeShakeCamera(kStormShakeBurstStrength * t, kStormShakeBurstDuration))
        {
            // Submitted — advance the decaying burst.
            _shakeBurstRetries = 0;
            if (++_shakeBurstStep >= kStormShakeBurstSteps)
            {
                _shakeBurstStep  = -1;
                _shakeRumbleTick = 0;           // hand over to the sustained rumble
            }
        }
        else if (++_shakeBurstRetries >= kStormShakeBurstMaxRetries)
        {
            // Player 3D/cell stayed invalid too long (e.g. long streaming stall): don't burn the burst
            // steps against a state where the native can't run. Abandon the burst and hand over to the
            // sustained rumble, which self-retries safely once the player is valid again.
            _shakeBurstRetries = 0;
            _shakeBurstStep    = -1;
            _shakeRumbleTick   = 0;
        }
        // else: transient failure this tick — keep the same step and retry next tick (bounded above).
        return;
    }

    if (--_shakeRumbleTick <= 0)
    {
        _shakeRumbleTick = kStormShakeRumbleTicks;
        NativeShakeCamera(kStormShakeRumbleStrength, kStormShakeRumbleDuration);
    }
}
// The dome NIFs are animated (rotation + a shader-float on the clouds), but a natively-placed ACTI ref
// doesn't auto-start its NiController the way KORs' STATIC records do — so it renders frozen (no rotation)
// with the shader float stuck at its initial value. Force-activate every layer's controller sequence once
// its 3D has attached (async, so poll). Idempotent + one-shot per dome via _stormDomeAnimStarted.
void RadiationEngine::UpdateDomeAnimation()
{
    if (!_stormDomeUp || _stormDomeAnimStarted || _stormDomeRefs.empty()) return;
    int animated = 0, pending = 0;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (!ref) continue;
        auto* obj3d = ref->Get3D();
        if (!obj3d) { ++pending; continue; }   // 3D not attached yet — retry next tick
        auto* mgr = RE::NiControllerManager::GetNiControllerManager(obj3d);
        if (!mgr) continue;                     // the light layer (no controller) — skip
        const std::uint16_t n = mgr->GetSequenceCount();
        for (std::uint16_t i = 0; i < n; ++i)
        {
            RE::NiControllerSequence* seq = mgr->GetSequenceAt(i);
            if (seq)
                seq->Activate(0, true, 1.0f, 0.0f, nullptr, false);
        }
        if (n > 0) ++animated;
    }
    if (pending == 0)   // every ref's 3D has loaded — stop polling
    {
        _stormDomeAnimStarted = true;
        Log()->info("[StormFX] Dome animation activated on {} layer(s).", animated);

        // DOME STATE DUMP (one-shot per dome): the dome has never been confirmed to actually RENDER, and
        // every fix so far patched a link we never measured. Dump each ref's real runtime state so the
        // break is located from evidence instead of inferred: does it have 3D, is it in the player's cell,
        // and is it where we think it is (player z + kStormDomeZOffset)?
        auto* pl = RE::PlayerCharacter::GetSingleton();
        const RE::NiPoint3 ppos = pl ? pl->GetPosition() : RE::NiPoint3{};
        auto* pcell = pl ? pl->GetParentCell() : nullptr;
        Log()->warn("[DomeDump] player pos=({:.0f},{:.0f},{:.0f}) cell=0x{:X}",
            ppos.x, ppos.y, ppos.z, reinterpret_cast<std::uintptr_t>(pcell));
        int i = 0;
        for (auto& layer : _stormDomeRefs)
        {
            auto ptr = layer.handle.get(); auto* r = ptr.get();
            if (!r) { Log()->warn("[DomeDump] [{}] handle DEAD", i++); continue; }
            const auto p = r->GetPosition();
            auto* c3d = r->Get3D();
            Log()->warn("[DomeDump] [{}] base=0x{:08X} 3D={} cell=0x{:X} sameCellAsPlayer={} "
                        "pos=({:.0f},{:.0f},{:.0f}) dz={:.0f} deleted={} disabled={}",
                i, r->GetObjectReference() ? r->GetObjectReference()->GetFormID() : 0u,
                c3d ? "YES" : "NO", reinterpret_cast<std::uintptr_t>(r->GetParentCell()),
                (r->GetParentCell() == pcell) ? "yes" : "NO",
                p.x, p.y, p.z, p.z - ppos.z,
                r->IsDeleted() ? "YES" : "no", r->IsDisabled() ? "YES" : "no");
            ++i;
        }
    }
}

void RadiationEngine::ClearStormDome()
{
    _stormDomeUp = false;
    Hooks::SetDomeFollowActive(false);
    _stormDomeRespawnRetryTicks = 0;
    _stormDomeCell = nullptr;
    _stormDomeWorld = nullptr;
    if (_stormDomeRefs.empty()) return;
    int cleared = 0;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        if (auto* ref = ptr.get())
        {
            ref->Disable();
            ref->SetDelete(true);
            ++cleared;
        }
    }
    _stormDomeRefs.clear();
    _stormDomeAnimStarted = false;
    Log()->info("[StormFX] Storm dome cleared ({} layers).", cleared);
}

void RadiationEngine::UpdateStormDomeLifecycle()
{
    if (_activeStormType != kAshStormIndex)
    {
        if (_stormDomeUp || !_stormDomeRefs.empty())
            ClearStormDome();
        return;
    }
    const bool domePhase = _stormPhase == StormPhase::Approach ||
        _stormPhase == StormPhase::Warning || _stormPhase == StormPhase::Active;
    const bool exposed = !IsPlayerSheltered();
    if (!domePhase || !exposed)
    {
        _stormDomeRespawnRetryTicks = 0;
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    // A transition can briefly leave the worldspace unresolved, or a target cell can reject all
    // placements. Keep retrying on the normal 4 Hz engine lifecycle cadence through every dome-bearing phase instead
    // of leaving the storm permanently dome-less after one failed recreation.
    if (!_stormDomeUp)
    {
        if (RadCorrectnessPolicy::ShouldRetryDomeSpawn(
                domePhase, exposed, false, ++_stormDomeRespawnRetryTicks))
        {
            _stormDomeRespawnRetryTicks = 0;
            Log()->info("[StormFX] Retrying pending dome spawn after transition failure.");
            SpawnStormDome();
        }
        return;
    }
    _stormDomeRespawnRetryTicks = 0;

    const bool sameCell = player->GetParentCell() == _stormDomeCell;
    const bool sameWorld = player->cachedWorldspace == _stormDomeWorld;
    if (RadCorrectnessPolicy::ShouldRecreateDomeForOwner(_stormDomeUp, sameCell, sameWorld))
    {
        Log()->info("[StormFX] Dome owner changed (cellSame={}, worldSame={}); recreating references.",
            sameCell, sameWorld);
        ClearStormDome();
        SpawnStormDome();
        return;
    }

}

void RadiationEngine::UpdateStormDomeFollow()
{
    static constexpr float kStormDomeFollowDistanceThreshold = 16.0f;

    if (!_stormDomeUp || _stormDomeRefs.empty())
    {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    if (!player->Get3D()) return;
    if (player->cachedWorldspace != _stormDomeWorld) return;

    const auto playerPosition = player->GetPosition();
    bool hasLoaded3D = false;
    bool needsMove = false;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (ref && !ref->IsDeleted() && !ref->IsDisabled() && ref->Get3D())
        {
            hasLoaded3D = true;
            const auto desired = RadCorrectnessPolicy::DomePosition(
                { playerPosition.x, playerPosition.y, playerPosition.z }, layer.zOffset);
            const auto current = ref->GetPosition();
            const float dx = desired.x - current.x;
            const float dy = desired.y - current.y;
            const float dz = desired.z - current.z;
            if ((dx * dx) + (dy * dy) + (dz * dz) >=
                kStormDomeFollowDistanceThreshold * kStormDomeFollowDistanceThreshold)
                needsMove = true;
        }
    }
    if (!hasLoaded3D || !needsMove) return;

    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (!ref || ref->IsDeleted() || ref->IsDisabled() || !ref->Get3D()) continue;

        const auto position = RadCorrectnessPolicy::DomePosition(
            { playerPosition.x, playerPosition.y, playerPosition.z }, layer.zOffset);
        ref->data.location = { position.x, position.y, position.z };
        ref->Update3DPosition(true);
    }
}

// Continuous camera dust for the Dust Storm. The weather referenceEffect (FXCameraAttachExplosionDustVE)
// attaches but produced no visible motes in practice, so we drive the blowing dust the reliable way:
// re-apply the heavy dust ArtObject to the camera slightly faster than it expires, so it overlaps into a
// steady effect. Same ApplyArtObject path as the wall-arrival burst, which DOES render.
void RadiationEngine::UpdateDustMotes()
{
    if (_activeStormType != kDustStormIndex || !_dustBurstArt ||
        (_stormPhase != StormPhase::Warning && _stormPhase != StormPhase::Active))
    {
        _dustMoteTick = 0;
        return;
    }
    auto* sky = RE::Sky::GetSingleton();
    if (!sky || sky->currentWeather != _stormWeathers[kDustStormIndex] || IsPlayerSheltered())
        return;
    if (--_dustMoteTick > 0) return;
    _dustMoteTick = kDustMoteReapplyTicks;
    if (auto* player = RE::PlayerCharacter::GetSingleton())
        player->ApplyArtObject(_dustBurstArt, kDustMoteSec, nullptr, /*attachToCamera=*/true,
            /*inheritRotation=*/true);
}
