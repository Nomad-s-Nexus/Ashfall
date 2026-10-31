#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include "Hooks.h"
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiUpdateData.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESWorldSpace.h>

using namespace RadInternal;

namespace
{
    bool IsValidDomeOwner(RE::PlayerCharacter* player, RE::TESObjectCELL*& cell, RE::TESWorldSpace*& world)
    {
        cell = player ? player->GetParentCell() : nullptr;
        world = player ? player->cachedWorldspace : nullptr;
        return player && player->Get3D() && cell && cell->IsExterior() && cell->IsAttached() && world &&
            cell->worldSpace == world;
    }

    bool IsValidDomeReference(RE::TESObjectREFR* ref, RE::TESWorldSpace* world)
    {
        if (!ref || ref->IsDeleted() || ref->IsDisabled()) return false;
        auto* cell = ref->GetParentCell();
        return cell && cell->IsExterior() && cell->IsAttached() && cell->worldSpace == world;
    }
}



void RadiationEngine::SpawnStormDome()
{
    if (_activeStormType < 0 || _activeStormType >= kStormTypeTableSize ||
        !RadCorrectnessPolicy::StormUsesDome(kStormTypes[_activeStormType].themeId))
    {
        if (_stormDomeUp || !_stormDomeRefs.empty())
            ClearStormDome();
        return;
    }
    if (_stormDomeUp || IsPlayerSheltered()) return;
    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player || !player->cachedWorldspace) return;

    ClearStormDome();

    const RE::NiPoint3 base = player->GetPosition();
    auto* const playerCell = player->GetParentCell();
    auto* const playerWorld = player->cachedWorldspace;
    if (!playerCell || !playerCell->IsExterior() || !playerCell->IsAttached() || playerCell->worldSpace != playerWorld)
        return;
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
    _stormDomeManualDrive = false;
    _stormDomeAnimStarted = false;   // force-start the animation once these refs' 3D attaches

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
    if (!REX::FModule::IsRuntimeOG()) return false;   // camera native is OG-only until its AE entry is verified
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->Get3D()) return false;    // 3D not up yet (streaming) — transient
    auto* cell = player->parentCell;
    if (!cell || cell->IsInterior()) return false;    // interior / no cell — no exterior shake

    if (strength > 1.0f) strength = 1.0f;       // native does not clamp; the Papyrus binding did
    if (strength <= 0.0f) return true;          // valid state, nothing to shake — not a failure

    const RE::NiPoint3 pos = player->data.location;
    RE::ShakeCamera(strength, pos, duration);
    return true;
}
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
            _shakeBurstRetries = 0;
            if (++_shakeBurstStep >= kStormShakeBurstSteps)
            {
                _shakeBurstStep  = -1;
                _shakeRumbleTick = 0;           // hand over to the sustained rumble
            }
        }
        else if (++_shakeBurstRetries >= kStormShakeBurstMaxRetries)
        {
            _shakeBurstRetries = 0;
            _shakeBurstStep    = -1;
            _shakeRumbleTick   = 0;
        }
        return;
    }

    if (--_shakeRumbleTick <= 0)
    {
        _shakeRumbleTick = kStormShakeRumbleTicks;
        NativeShakeCamera(kStormShakeRumbleStrength, kStormShakeRumbleDuration);
    }
}
void RadiationEngine::UpdateDomeAnimation()
{
    if (!_stormDomeUp || _stormDomeRefs.empty()) return;
    if (_stormDomeAnimStarted)
    {
        const float rel = static_cast<float>(_playSeconds - _stormDomeBeginSec);
        const bool audit = _stormDomeSeekAudit;
        _stormDomeSeekAudit = false;
        if (!audit && !_stormDomeManualDrive) return;
        bool anyAudited = false;
        for (auto& layer : _stormDomeRefs)
        {
            auto ptr = layer.handle.get();
            auto* obj3d = ptr.get() ? ptr.get()->Get3D() : nullptr;
            auto* mgr = obj3d ? RE::NiControllerManager::GetNiControllerManager(obj3d) : nullptr;
            if (!mgr) continue;
            for (std::uint16_t i = 0; audit && i < mgr->GetSequenceCount(); ++i)
            {
                auto* seq = mgr->GetSequenceAt(i);
                if (!seq) continue;
                anyAudited = true;
                Log()->info("[StormFX] Dome sequence audit {} offset={:.2f} startTime={:.2f} lastScaled={:.2f} state={} owner={}",
                    i, seq->offset, seq->startTime, seq->lastScaledTime, static_cast<int>(seq->state.get()), seq->owner == mgr);
                if (seq->offset < -1.0e30f && !_stormDomeManualDrive)
                {
                    _stormDomeManualDrive = true;
                    Log()->warn("[StormFX] Dome sequence never advanced; driving the controller manager manually.");
                }
            }
        }
        if (audit && !anyAudited) _stormDomeSeekAudit = true;
        if (!_stormDomeManualDrive) return;
        RE::NiUpdateData data;
        data.time = rel;
        for (auto& layer : _stormDomeRefs)
        {
            auto ptr = layer.handle.get();
            auto* obj3d = ptr.get() ? ptr.get()->Get3D() : nullptr;
            auto* mgr = obj3d ? RE::NiControllerManager::GetNiControllerManager(obj3d) : nullptr;
            if (!mgr) continue;
            mgr->Update(data);   // the engine never ticks this manager: drive it from storm time
            if (layer.manualSeeded) continue;
            for (std::uint16_t i = 0; i < mgr->GetSequenceCount(); ++i)
                if (auto* seq = mgr->GetSequenceAt(i))
                    seq->offset = RadCorrectnessPolicy::DomeSeekSeconds(rel, kStormDomeTimelineSec) - rel;
            layer.manualSeeded = true;
            if (audit) _stormDomeSeekAudit = true;   // re-audit next tick to prove lastScaled advances
        }
        return;
    }
    int animated = 0, pending = 0;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (!ref) continue;
        if (layer.animStarted) { ++animated; continue; }
        auto* obj3d = ref->Get3D();
        if (!obj3d) { ++pending; continue; }   // 3D not attached yet — retry next tick
        auto* mgr = RE::NiControllerManager::GetNiControllerManager(obj3d);
        if (!mgr) continue;                     // the light layer (no controller) — skip
        const std::uint16_t n = mgr->GetSequenceCount();
        for (std::uint16_t i = 0; i < n; ++i)
        {
            RE::NiControllerSequence* seq = mgr->GetSequenceAt(i);
            if (!seq) continue;
            seq->Activate(0, true, 1.0f, 0.0f, nullptr, false);
            const float seek = RadCorrectnessPolicy::DomeSeekSeconds(_playSeconds - _stormDomeBeginSec, kStormDomeTimelineSec);
            if (seek > 0.0f) seq->offset += seek * seq->frequency;
            Log()->info("[StormFX] Dome sequence {} seek={:.1f}s offset={:.2f} frequency={:.2f}", i, seek, seq->offset, seq->frequency);
        }
        if (n > 0) { layer.animStarted = true; ++animated; }
    }
    if (pending == 0)   // every ref's 3D has loaded — stop polling
    {
        _stormDomeAnimStarted = true;
        _stormDomeSeekAudit = true;
        Log()->info("[StormFX] Dome animation activated on {} layer(s).", animated);

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

void RadiationEngine::ResetStormDomeForLoad()
{
    // Constraint: a load kills the layer handles; drop them without resolving a stale pointer.
    _stormDomeRefs.clear();
    _stormDomeUp = false;
    _stormDomeAnimStarted = false;
    _stormDomeRespawnRetryTicks = 0;
    _stormDomeCell = nullptr;
    _stormDomeWorld = nullptr;
    Hooks::SetDomeFollowActive(false);
}

void RadiationEngine::UpdateStormDomeLifecycle()
{
    if (_activeStormType < 0 || _activeStormType >= kStormTypeTableSize ||
        !RadCorrectnessPolicy::StormUsesDome(kStormTypes[_activeStormType].themeId))
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

    RE::TESObjectCELL* playerCell = nullptr;
    RE::TESWorldSpace* playerWorld = nullptr;
    if (!IsValidDomeOwner(player, playerCell, playerWorld))
    {
        return;
    }

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

    const bool sameCell = playerCell == _stormDomeCell;
    const bool sameWorld = playerWorld == _stormDomeWorld;
    bool staleReference = false;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        if (!IsValidDomeReference(ptr.get(), _stormDomeWorld))
        {
            staleReference = true;
            break;
        }
    }
    if (RadCorrectnessPolicy::ShouldRecreateDomeForOwner(_stormDomeUp, sameCell, sameWorld) || staleReference)
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

    RE::TESObjectCELL* playerCell = nullptr;
    RE::TESWorldSpace* playerWorld = nullptr;
    if (!IsValidDomeOwner(player, playerCell, playerWorld) || playerWorld != _stormDomeWorld) return;

    const auto playerPosition = player->GetPosition();
    bool needsMove = false;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (!IsValidDomeReference(ref, _stormDomeWorld) || !ref->Get3D()) return;

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
    if (!needsMove) return;

    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        const auto position = RadCorrectnessPolicy::DomePosition(
            { playerPosition.x, playerPosition.y, playerPosition.z }, layer.zOffset);
        ref->data.location = { position.x, position.y, position.z };
        ref->Update3DPosition(true);
    }
}

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
            true);
}
