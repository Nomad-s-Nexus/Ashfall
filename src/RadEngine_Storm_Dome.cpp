#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include "Hooks.h"
#include <chrono>
#include <cmath>
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiUpdateData.h>
#include <algorithm>
#include <atomic>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESWorldSpace.h>
#include <RE/B/BSGeometry.h>
#include <RE/B/BSShaderProperty.h>
#include <RE/N/NiRTTI.h>
#include <cstdlib>

using namespace RadInternal;

namespace
{
    constexpr int kDomeDeckCount = static_cast<int>(sizeof(RadCorrectnessPolicy::kDomeDeckShapes) / sizeof(RadCorrectnessPolicy::kDomeDeckShapes[0]));
    static_assert(kDomeDeckCount <= 8, "StormDomeLayer::decks holds 8 shapes");

    constexpr const char* kDomeIniPath = ".\\Data\\F4SE\\Plugins\\FalloutAnomalyRadiation.ini";

    float ReadDomeTunable(const char* a_key, float a_lo, float a_hi, float a_fallback)
    {
        char buf[32] = {};
        ::GetPrivateProfileStringA("StormDome", a_key, "", buf, sizeof(buf), kDomeIniPath);
        if (buf[0] == '\0') return a_fallback;
        char* end = nullptr;
        const float raw = std::strtof(buf, &end);
        if (end == buf) return a_fallback;
        return RadCorrectnessPolicy::DomeTunableOr(raw, a_lo, a_hi, a_fallback);
    }

    // Constraint: properties[1] is cast to BSShaderProperty only after its runtime type is confirmed.
    RE::BSShaderProperty* ShaderPropertyOf(RE::BSGeometry* a_geom)
    {
        auto* prop = a_geom ? a_geom->properties[1].get() : nullptr;
        if (!prop || !prop->GetRTTI()) return nullptr;
        static REL::Relocation<const RE::NiRTTI*> shaderRtti{ RE::BSShaderProperty::Ni_RTTI };
        return prop->GetRTTI()->IsKindOf(shaderRtti.get()) ? static_cast<RE::BSShaderProperty*>(prop) : nullptr;
    }

    bool IsValidDomeOwner(RE::PlayerCharacter* player, RE::TESObjectCELL*& cell, RE::TESWorldSpace*& world)
    {
        cell = player ? player->GetParentCell() : nullptr;
        world = player ? player->cachedWorldspace : nullptr;
        if (!player || !player->Get3D() || !cell || !cell->IsAttached()) return false;
        if (cell->IsInterior()) { world = nullptr; return true; }
        return cell->IsExterior() && world && cell->worldSpace == world;
    }

    bool IsValidDomeReference(RE::TESObjectREFR* ref, RE::TESWorldSpace* world)
    {
        if (!ref || ref->IsDeleted() || ref->IsDisabled()) return false;
        auto* cell = ref->GetParentCell();
        if (!cell || !cell->IsAttached()) return false;
        return cell->IsInterior() ? world == nullptr : cell->IsExterior() && cell->worldSpace == world;
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
    if (_stormDomeUp) return;
    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player) return;

    ClearStormDome();

    const RE::NiPoint3 base = player->GetPosition();
    RE::TESObjectCELL* playerCell = nullptr;
    RE::TESWorldSpace* playerWorld = nullptr;
    if (!IsValidDomeOwner(player, playerCell, playerWorld)) return;
    auto* const interiorCell = playerCell->IsInterior() ? playerCell : nullptr;
    int placed = 0;
    for (int k = 0; k < 4; ++k)
    {
        if (!_stormDomeForms[k]) continue;
        RE::NEW_REFR_DATA data;
        data.object    = static_cast<RE::TESBoundObject*>(_stormDomeForms[k]);
        data.location  = { base.x, base.y, base.z + kStormDomeZOffset };
        data.direction = { 0.0f, 0.0f, 0.0f };
        data.interior  = interiorCell;
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
    _stormDomeAnimStarted = false;
    _domeAnimTime = 0.0;
    _domeSeqNowSec = 0.0;
    _domeLoggedSlot = -1;
    _domeDeckLogged = false;
    _domeAnimLogIndex = 0;
    _domeAnimDriverLogged = false;
    _domeSpinLogged = false;
    _stormDomeSpawnSec = _playSeconds;
    _domeDriveLastSec = -1.0e9;
    _stormDomeStatsLogged = false;
    ResetDomeShapeMask();
    _domeVortexYaw = player->data.angle.z;
    const float distance = ReadDomeTunable("VortexDistance", 500.0f, 5000.0f, RadCorrectnessPolicy::kDomeVortexDistance);
    const float height = ReadDomeTunable("VortexHeight", 100.0f, 3000.0f, RadCorrectnessPolicy::kDomeVortexHeight);
    _domeVortexScale = ReadDomeTunable("VortexScale", 0.5f, 6.0f, RadCorrectnessPolicy::kDomeVortexScale);
    _domeDeckFloorOn = ::GetPrivateProfileIntA("StormDome", "DeckFloor", 0, kDomeIniPath) != 0;
    _domeVortexDelta = RadCorrectnessPolicy::VortexLocalDelta(_domeVortexYaw, distance, height);
    Log()->info("[StormFX] dome portal placed: yaw {:.1f} deg, eye {:.0f} units ahead and {:.0f} up, scale x{:.2f}, "
                "node shift ({:.0f},{:.0f},{:.0f}) NIF units, deck floor {}.",
        _domeVortexYaw * 180.0f / 3.14159265f, distance, height, _domeVortexScale,
        _domeVortexDelta.x, _domeVortexDelta.y, _domeVortexDelta.z, _domeDeckFloorOn ? "on" : "off");

    bool lit = false, flare = false;
    if (placed > 0 && !interiorCell)
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
    if (!cell) return false;

    if (strength > 1.0f) strength = 1.0f;       // native does not clamp; the Papyrus binding did
    if (strength <= 0.0f) return true;          // valid state, nothing to shake — not a failure
    strength = RadCorrectnessPolicy::StormShakeStrength(strength, cell->IsInterior());

    const RE::NiPoint3 pos = player->data.location;
    RE::ShakeCamera(strength, pos, duration);
    return true;
}
void RadiationEngine::UpdateStormShake()
{
    if (_shakeBurstStep < 0 && _shakeRumbleTick < 0) return;

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
    const float dt = static_cast<float>(std::clamp(_playSeconds - _stormDomeBlendSec, 0.0, 1.0));
    _stormDomeBlendSec = _playSeconds;
    const float step = dt / kStormDomeBlendRampSec;
    const float target = (_stormDomeUp && _stormDomeAnimStarted)
        ? RadCorrectnessPolicy::BlendForSequence(_domeSeekSec + _domeAnimTime, kStormDomeTimelineSec)
        : 0.0f;
    _stormDomeBlend = std::clamp(std::clamp(target, _stormDomeBlend - step, _stormDomeBlend + step), 0.0f, 1.0f);
    Hooks::SetStormDomeBlend(_stormDomeBlend);
    if (_stormDomeUp && !_stormDomeStatsLogged && _playSeconds - _stormDomeSpawnSec >= kStormDomeStatsLogSec)
    {
        _stormDomeStatsLogged = true;
        std::uint32_t passes = 0, scaled = 0;
        Hooks::GetSkyCloudStats(passes, scaled);
        Log()->info("[StormFX] sky cloud hook stats at spawn+{:.0f}s: passes={} scaled={} blend={:.2f} seq={:.1f}s.",
            kStormDomeStatsLogSec, passes, scaled, _stormDomeBlend, _domeSeekSec + _domeAnimTime);
    }

    if (!_stormDomeUp || _stormDomeRefs.empty() || _stormDomeAnimStarted) return;
    int animated = 0, pending = 0;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (!ref) continue;
        if (layer.animStarted) { ++animated; continue; }
        auto* obj3d = ref->Get3D();
        if (!obj3d) { ++pending; continue; }
        auto* mgr = RE::NiControllerManager::GetNiControllerManager(obj3d);
        if (!mgr) continue;
        const std::uint16_t n = mgr->GetSequenceCount();
        for (std::uint16_t i = 0; i < n; ++i)
        {
            RE::NiControllerSequence* seq = mgr->GetSequenceAt(i);
            if (seq && static_cast<std::int32_t>(seq->state.get()) == 0)
                seq->Activate(0, true, 1.0f, 0.0f, nullptr, false);
        }
        if (n > 0) { layer.animStarted = true; ++animated; }
    }
    if (pending == 0)
    {
        _stormDomeAnimStarted = true;
        _domeSeekSec = CurrentDomeSequenceSec();
        _domeAnimTime = 0.0;
        _domeDriveLastSec = -1.0e9;
        _domeAnimLogIndex = 0;
        _domeAnimDriverLogged = false;
        _domeSpinLogged = false;
        Log()->info("[StormFX] Dome animation activated on {} layer(s), storm seek {:.1f}s.", animated, _domeSeekSec);
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
    ResetDomeShapeMask();
    Log()->info("[StormFX] Storm dome cleared ({} layers).", cleared);
}

void RadiationEngine::ResetStormDomeForLoad()
{
    // Constraint: a load kills the layer handles; drop them without resolving a stale pointer.
    _stormDomeRefs.clear();
    _stormDomeUp = false;
    _stormDomeBlend = 0.0f;
    Hooks::SetStormDomeBlend(0.0f);
    _stormDomeAnimStarted = false;
    ResetDomeShapeMask();
    Hooks::SetDomeFollowActive(false);
    _stormDomeRespawnRetryTicks = 0;
    _stormDomeCell = nullptr;
    _stormDomeWorld = nullptr;
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
    constexpr bool exposed = true;
    if (!domePhase)
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
    if (RadCorrectnessPolicy::ShouldRecreateDomeForOwner(_stormDomeUp, sameCell, sameWorld) || staleReference ||
        (!playerWorld && !sameCell))
    {
        Log()->info("[StormFX] Dome owner changed (cellSame={}, worldSame={}); recreating references.",
            sameCell, sameWorld);
        ClearStormDome();
        SpawnStormDome();
        return;
    }

}

double RadiationEngine::CurrentDomeSequenceSec()
{
    using Slot = RadCorrectnessPolicy::DomePhaseSlot;
    Slot slot = Slot::kApproach;
    switch (_stormPhase)
    {
    case StormPhase::Approach: slot = Slot::kApproach; break;
    case StormPhase::Warning:  slot = Slot::kWarning;  break;
    case StormPhase::Active:   slot = Slot::kActive;   break;
    case StormPhase::Aftermath: slot = Slot::kAftermath; break;
    default: return _domeSeqNowSec;
    }
    const double frac = _stormPhaseDurSec > 0.0 ? (_playSeconds - _stormPhaseStartSec) / _stormPhaseDurSec : 1.0;
    _domeSeqNowSec = RadCorrectnessPolicy::DomeSequenceForPhase(slot, frac);
    if (static_cast<int>(slot) != _domeLoggedSlot)
    {
        _domeLoggedSlot = static_cast<int>(slot);
        static constexpr const char* kSlotNames[] = { "approach", "warning", "active", "aftermath" };
        Log()->info("[StormFX] dome sequence milestone: {} begins at seq {:.0f} s (spawn+{:.0f} s).",
            kSlotNames[_domeLoggedSlot], _domeSeqNowSec, _playSeconds - _stormDomeSpawnSec);
    }
    return _domeSeqNowSec;
}

void RadiationEngine::AdvanceDomeClock()
{
    _domeAnimTime = std::max(0.0, CurrentDomeSequenceSec() - _domeSeekSec);
}

static void SpinDomeNode(RE::NiAVObject* a_node, double a_degPerSec, double a_seconds)
{
    if (!a_node) return;
    const double angle = RadCorrectnessPolicy::SpinAngleRad(a_degPerSec, a_seconds);
    const float c = static_cast<float>(std::cos(angle));
    const float s = static_cast<float>(std::sin(angle));
    a_node->SetLocalRotate(RE::NiMatrix3(c, -s, 0.0f, 0.0f, s, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
}

void RadiationEngine::ResolveDomeLayerNodes(StormDomeLayer& a_layer, RE::NiAVObject* a_root)
{
    a_layer.root = a_root;
    a_layer.resolved = true;
    _domeShapeMaskDirty = true;
    a_layer.hasManager = RE::NiControllerManager::GetNiControllerManager(a_root) != nullptr;
    a_layer.hole4 = nullptr;
    a_layer.vortex2 = nullptr;
    a_layer.vortex3 = nullptr;
    if (!a_layer.hasManager) return;
    a_layer.hole4 = a_root->GetObjectByName(RE::BSFixedString("Hole4"));
    a_layer.vortex2 = a_root->GetObjectByName(RE::BSFixedString("Vortex2"));
    a_layer.vortex3 = a_root->GetObjectByName(RE::BSFixedString("Vortex3"));
    // Constraint: the authored translation is captured only when a node pointer first appears, so a re-resolve never compounds the shift.
    static constexpr const char* kVortexNodeNames[] = { "Vortex2", "Vortex3", "Hole4" };
    for (int i = 0; i < 3; ++i)
    {
        auto* node = a_root->GetObjectByName(RE::BSFixedString(kVortexNodeNames[i]));
        auto& state = a_layer.vortex[i];
        if (node != state.node.get())
        {
            state.node = RE::NiPointer<RE::NiAVObject>(node);
            state.writtenScale = -1.0f;
            state.baseTranslate = node ? node->local.translate : RE::NiPoint3{};
        }
    }
    for (int i = 0; i < kDomeDeckCount; ++i)
    {
        auto* node = a_root->GetObjectByName(RE::BSFixedString(RadCorrectnessPolicy::kDomeDeckShapes[i].data()));
        auto* geometry = node ? node->IsGeometry() : nullptr;
        auto& deck = a_layer.decks[i];
        if (geometry == deck.geom.get()) continue;
        deck.geom = RE::NiPointer<RE::BSGeometry>(geometry);
        deck.writtenAlpha = -1.0f;
        deck.floorAlpha = RadCorrectnessPolicy::DomeDeckFloorFor(RadCorrectnessPolicy::kDomeDeckShapes[i]);
    }
}

void RadiationEngine::ApplyDomeVortexLayout(StormDomeLayer& a_layer)
{
    for (auto& state : a_layer.vortex)
    {
        if (!state.node) continue;
        const auto& t = state.baseTranslate;
        state.node->SetLocalTranslate({ t.x + _domeVortexDelta.x, t.y + _domeVortexDelta.y, t.z + _domeVortexDelta.z });
        const float keyed = state.node->local.scale;
        if (keyed != state.writtenScale)
            state.baseScale = keyed;
        state.writtenScale = state.baseScale * _domeVortexScale;
        state.node->SetLocalScale(state.writtenScale);
    }
}

void RadiationEngine::ResetDomeShapeMask()
{
    _domeShapeMask = RadCorrectnessPolicy::kAllDomeShapesVisible;
    _domeShapeMaskDirty = false;
}

void RadiationEngine::ApplyDomeDeckFloor(StormDomeLayer& a_layer, double a_seqTimeSec)
{
    const float weight = RadCorrectnessPolicy::DomeDeckFloorWeight(a_seqTimeSec);
    const bool logFirst = !_domeDeckLogged && a_layer.root;
    int shaded = 0;
    int sampleIndex = -1;
    float sampleBefore = 0.0f;
    float sampleAfter = 0.0f;
    for (int i = 0; i < kDomeDeckCount; ++i)
    {
        auto& deck = a_layer.decks[i];
        // Constraint: the shader property is read from the live geometry on every apply, never cached.
        auto* prop = ShaderPropertyOf(deck.geom.get());
        if (!prop) continue;
        ++shaded;
        const float keyed = prop->alpha;
        if (keyed != deck.writtenAlpha) deck.keyedAlpha = keyed;
        deck.writtenAlpha = RadCorrectnessPolicy::DomeFloorAlpha(deck.keyedAlpha, deck.floorAlpha, weight);
        if (!_domeDeckFloorOn) continue;
        if (logFirst && sampleIndex < 0)
        {
            sampleIndex = i;
            sampleBefore = keyed;
            sampleAfter = deck.writtenAlpha;
        }
        prop->alpha = deck.writtenAlpha;
    }
    if (!logFirst) return;
    _domeDeckLogged = true;
    Log()->info("[StormFX] dome deck floor: {} of {} deck shapes shaded; weight {:.2f} at seq {:.1f}.",
        shaded, kDomeDeckCount, weight, a_seqTimeSec);
    if (sampleIndex >= 0)
        Log()->info("[StormFX] dome deck floor sample {}: alpha {:.3f} -> {:.3f}.",
            RadCorrectnessPolicy::kDomeDeckShapes[sampleIndex], sampleBefore, sampleAfter);
    for (const char* name : { "PortalA", "DiskOuter", "PlainHigh", "OuterHigh" })
    {
        auto* node = a_layer.root->GetObjectByName(RE::BSFixedString(name));
        auto* geometry = node ? node->IsGeometry() : nullptr;
        auto* prop = ShaderPropertyOf(geometry);
        if (!prop)
        {
            Log()->info("[StormFX] dome depth flags {}: no shader property.", name);
            continue;
        }
        Log()->info("[StormFX] dome depth flags {} (before, unchanged): ZBufferTest={} ZBufferWrite={} alpha={:.2f}.",
            name, prop->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferTest),
            prop->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferWrite), prop->alpha);
    }
}
void RadiationEngine::ApplyDomeShapeMask(double a_seqTimeSec)
{
    const std::uint64_t mask = RadCorrectnessPolicy::DomeShapeVisibility(a_seqTimeSec, _domeDeckFloorOn);
    if (mask == _domeShapeMask && !_domeShapeMaskDirty) return;
    _domeShapeMask = mask;
    _domeShapeMaskDirty = false;
    for (auto& layer : _stormDomeRefs)
    {
        if (!layer.hasManager) continue;
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        auto* obj3d = ref ? ref->Get3D() : nullptr;
        if (!obj3d || layer.root != obj3d) continue;
        for (int i = 0; i < RadDomeVisibility::kShapeCount; ++i)
        {
            auto* shape = obj3d->GetObjectByName(RE::BSFixedString(RadDomeVisibility::kShapeNames[i]));
            if (shape) shape->SetAppCulled(((mask >> i) & 1u) == 0u);
        }
    }
}

void RadiationEngine::DriveDomeAnimation(bool moved)
{
    RE::NiUpdateData data;
    data.time = static_cast<float>(_domeAnimTime);
    if (!_stormDomeAnimStarted)
    {
        if (moved)
        {
            for (auto& layer : _stormDomeRefs)
            {
                auto ptr = layer.handle.get();
                if (auto* ref = ptr.get()) ref->Update3DPosition(true);
            }
        }
        return;
    }

    if (!moved && _domeAnimTime - _domeDriveLastSec < kDomeDriveIntervalSec) return;
    _domeDriveLastSec = _domeAnimTime;

    RE::NiControllerSequence* storm = nullptr;
    RE::NiAVObject* hole4 = nullptr;
    RE::NiAVObject* vortex2 = nullptr;
    RE::NiAVObject* vortex3 = nullptr;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (!ref) continue;
        auto* obj3d = ref->Get3D();
        if (!obj3d) continue;
        const bool recreated = layer.root && layer.root != obj3d;
        if (layer.root != obj3d || !layer.resolved)
            ResolveDomeLayerNodes(layer, obj3d);
        if (!layer.hasManager) continue;
        auto* mgr = RE::NiControllerManager::GetNiControllerManager(obj3d);
        if (!mgr) continue;
        if (recreated)
        {
            for (std::uint16_t i = 0; i < mgr->GetSequenceCount(); ++i)
            {
                auto* seq = mgr->GetSequenceAt(i);
                if (seq && static_cast<std::int32_t>(seq->state.get()) == 0)
                    seq->Activate(0, true, 1.0f, 0.0f, nullptr, false);
            }
            layer.manualSeeded = false;
            _domeShapeMaskDirty = true;
            Log()->info("[StormFX] dome layer 3D recreated; sequences reactivated and reseeded.");
        }
        const std::uint16_t flagsBefore = mgr->flags.flags;
        mgr->flags.flags = static_cast<std::uint16_t>(flagsBefore | kDomeManagerActiveFlag);
        if (!_domeAnimDriverLogged)
        {
            _domeAnimDriverLogged = true;
            Log()->info("[StormFX] dome anim driver: manager flags 0x{:04X} -> 0x{:04X}, sequences={}.",
                flagsBefore, mgr->flags.flags, mgr->GetSequenceCount());
        }
        static_cast<RE::NiTimeController*>(mgr)->Update(data);
        if (!layer.manualSeeded)
        {
            for (std::uint16_t i = 0; i < mgr->GetSequenceCount(); ++i)
                if (auto* seq = mgr->GetSequenceAt(i))
                    seq->offset = RadCorrectnessPolicy::DomeSequenceOffset(static_cast<float>(_domeSeekSec));
            layer.manualSeeded = true;
        }
        if (_domeAnimLogIndex < kDomeAnimLogCount && !storm)
        {
            storm = mgr->GetSequenceByName(RE::BSFixedString("Storm"));
            if (!storm) storm = mgr->GetSequenceAt(0);
        }
        if (!hole4) hole4 = layer.hole4;
        if (!vortex2) vortex2 = layer.vortex2;
        if (!vortex3) vortex3 = layer.vortex3;
    }
    if (storm)
    {
        while (_domeAnimLogIndex < kDomeAnimLogCount && _domeAnimTime >= kDomeAnimLogSeconds[_domeAnimLogIndex])
        {
            Log()->info("[StormFX] dome anim t={:.1f}s state={} offset={:.1f}", storm->lastScaledTime,
                static_cast<std::int32_t>(storm->state.get()), storm->offset);
            ++_domeAnimLogIndex;
        }
    }

    if (!_domeSpinLogged)
    {
        _domeSpinLogged = true;
        Log()->info("[StormFX] dome spin: Hole4={} Vortex2={} Vortex3={}",
            hole4 ? "found" : "missing", vortex2 ? "found" : "missing", vortex3 ? "found" : "missing");
    }
    SpinDomeNode(hole4, kPortalSpinDegPerSec, _domeAnimTime);
    SpinDomeNode(vortex2, -kCoreSpinDegPerSec, _domeAnimTime);
    SpinDomeNode(vortex3, kCoreSpinDegPerSec * kVortex3SpinScale, _domeAnimTime);
    for (auto& layer : _stormDomeRefs)
        if (layer.hasManager && layer.root) ApplyDomeVortexLayout(layer);
    for (auto& layer : _stormDomeRefs)
        if (layer.hasManager && layer.root) ApplyDomeDeckFloor(layer, _domeSeekSec + _domeAnimTime);
    ApplyDomeShapeMask(_domeSeekSec + _domeAnimTime);

    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (!ref) continue;
        auto* obj3d = ref->Get3D();
        if (!obj3d) continue;
        if (!moved)
        {
            if (layer.hasManager && layer.root == obj3d) obj3d->UpdateDownwardPass(data, 0);
            continue;
        }
        auto* mgr = layer.hasManager ? RE::NiControllerManager::GetNiControllerManager(obj3d) : nullptr;
        const bool wasActive = mgr && (mgr->flags.flags & kDomeManagerActiveFlag) != 0;
        if (mgr) mgr->flags.flags = static_cast<std::uint16_t>(mgr->flags.flags & ~kDomeManagerActiveFlag);
        ref->Update3DPosition(true);
        if (layer.hasManager && layer.root == obj3d) ApplyDomeVortexLayout(layer);
        if (mgr && wasActive) mgr->flags.flags = static_cast<std::uint16_t>(mgr->flags.flags | kDomeManagerActiveFlag);
    }
}
void RadiationEngine::UpdateStormDomeFollow()
{
    if (!_stormDomeUp || _stormDomeRefs.empty()) return;
    AdvanceDomeClock();

    bool moved = false;
    auto* player = RE::PlayerCharacter::GetSingleton();
    RE::TESObjectCELL* playerCell = nullptr;
    RE::TESWorldSpace* playerWorld = nullptr;
    if (player && IsValidDomeOwner(player, playerCell, playerWorld) && playerWorld == _stormDomeWorld &&
        (playerWorld || playerCell == _stormDomeCell))
    {
        const auto playerPosition = player->GetPosition();
        bool needsMove = false;
        bool valid = true;
        for (auto& layer : _stormDomeRefs)
        {
            auto ptr = layer.handle.get();
            auto* ref = ptr.get();
            if (!IsValidDomeReference(ref, _stormDomeWorld) || !ref->Get3D())
            {
                valid = false;
                break;
            }
            const auto desired = RadCorrectnessPolicy::DomePosition(
                { playerPosition.x, playerPosition.y, playerPosition.z }, layer.zOffset);
            const auto current = ref->GetPosition();
            const float dx = desired.x - current.x;
            const float dy = desired.y - current.y;
            const float dz = desired.z - current.z;
            if (RadCorrectnessPolicy::ShouldMoveDomeLayer((dx * dx) + (dy * dy) + (dz * dz),
                    kStormDomeFollowEpsilon * kStormDomeFollowEpsilon))
                needsMove = true;
        }
        if (valid && needsMove)
        {
            for (auto& layer : _stormDomeRefs)
            {
                auto ptr = layer.handle.get();
                auto* ref = ptr.get();
                const auto position = RadCorrectnessPolicy::DomePosition(
                    { playerPosition.x, playerPosition.y, playerPosition.z }, layer.zOffset);
                ref->data.location = { position.x, position.y, position.z };
            }
            moved = true;
        }
    }
    DriveDomeAnimation(moved);
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
