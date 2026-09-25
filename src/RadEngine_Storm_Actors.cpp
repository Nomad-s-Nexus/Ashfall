#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include "Hooks.h"
#include <algorithm>
#include <cmath>
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>
#include <RE/T/TESObjectCELL.h>

using namespace RadInternal;



void RadiationEngine::ClearFireRefs()
{
    if (_fireRefs.empty()) return;
    auto logger = Log();
    int cleared = 0;
    for (auto& handle : _fireRefs)
    {
        auto ptr = handle.get();
        if (auto* ref = ptr.get())
        {
            ref->Disable();
            ref->SetDelete(true);
            ++cleared;
        }
    }
    _fireRefs.clear();
    logger->info("[Storm/Fire] Cleared {} fire hazard refs.", cleared);
}
void RadiationEngine::RetryPendingStormGhouls()
{
    if (_stormGhoulPendingRefs.empty()) return;

    auto logger = Log();
    std::vector<RE::ObjectRefHandle> stillPending;
    stillPending.reserve(_stormGhoulPendingRefs.size());
    std::size_t promoted = 0;
    std::size_t cleared = 0;

    for (auto& handle : _stormGhoulPendingRefs)
    {
        auto ptr = handle.get();
        auto* ref = ptr.get();
        if (!ref)
        {
            stillPending.push_back(handle);
            continue;
        }

        auto* actor = ref->GetFormType() == RE::ENUM_FORM_ID::kACHR
            ? static_cast<RE::Actor*>(ref) : nullptr;
        if (!actor)
        {
            if (!_stormGhoulsEnabled)
            {
                ref->Disable();
                ref->SetDelete(true);
                ++cleared;
            }
            else
            {
                stillPending.push_back(handle);
            }
            continue;
        }
        if (!_stormGhoulsEnabled)
        {
            ref->Disable();
            ref->SetDelete(true);
            ++cleared;
            continue;
        }

        actor->Enable(true);
        const bool clamped = actor->ClampToGround();
        const auto finalPos = actor->GetPosition();
        auto* actorCell = actor->GetParentCell();
        const auto actorCellID = actorCell ? actorCell->GetFormID() : 0;
        const auto actorWorldID = actorCell && actorCell->worldSpace
            ? actorCell->worldSpace->GetFormID() : 0;
        const bool has3D = actor->Get3D() != nullptr;
        logger->debug(
            "[Storm/Ghouls] pending actor pos=({:.0f},{:.0f},{:.0f}) grounded={} 3D={} cell=0x{:08X} world=0x{:08X}",
            finalPos.x, finalPos.y, finalPos.z, clamped, has3D, actorCellID, actorWorldID);
        _stormGhoulRefs.push_back(handle);
        ++promoted;
    }

    _stormGhoulPendingRefs.swap(stillPending);
    if (_stormGhoulPendingRefs.empty())
        _stormGhoulPendingTicks = 0;
    else if (++_stormGhoulPendingTicks >= kStormGhoulPendingTimeoutTicks)
    {
        logger->warn(
            "[Storm/Ghouls] Moving {} unresolved activation ref(s) to cleanup after {} ticks; allowing future pack rolls.",
            _stormGhoulPendingRefs.size(), _stormGhoulPendingTicks);
        _stormGhoulCleanupRefs.insert(
            _stormGhoulCleanupRefs.end(), _stormGhoulPendingRefs.begin(), _stormGhoulPendingRefs.end());
        _stormGhoulPendingRefs.clear();
        _stormGhoulPendingTicks = 0;
    }

    if (promoted || cleared)
    {
        logger->info("[Storm/Ghouls] Pending refs processed: activated {}, cleared {}.", promoted, cleared);
    }
}
void RadiationEngine::RetryStormGhoulCleanup()
{
    if (_stormGhoulCleanupRefs.empty()) return;

    auto logger = Log();
    std::vector<RE::ObjectRefHandle> stillPending;
    stillPending.reserve(_stormGhoulCleanupRefs.size());
    std::size_t cleared = 0;
    for (auto& handle : _stormGhoulCleanupRefs)
    {
        auto ptr = handle.get();
        if (auto* ref = ptr.get())
        {
            ref->Disable();
            ref->SetDelete(true);
            ++cleared;
        }
        else
        {
            stillPending.push_back(handle);
        }
    }
    _stormGhoulCleanupRefs.swap(stillPending);
    if (cleared)
        logger->info("[Storm/Ghouls] Cleanup queue processed {} ref(s); {} remain unresolved.",
            cleared, _stormGhoulCleanupRefs.size());
}
void RadiationEngine::RetireUnresolvedStormGhouls()
{
    if (_stormGhoulRefs.empty())
    {
        _stormGhoulUnresolvedTicksByHandle.clear();
        return;
    }

    auto logger = Log();
    std::vector<RE::ObjectRefHandle> stillLive;
    stillLive.reserve(_stormGhoulRefs.size());
    std::size_t retired = 0;
    for (auto& handle : _stormGhoulRefs)
    {
        const auto handleValue = handle.get_handle();
        if (handle.get())
        {
            _stormGhoulUnresolvedTicksByHandle.erase(handleValue);
            stillLive.push_back(handle);
            continue;
        }

        if (++_stormGhoulUnresolvedTicksByHandle[handleValue] < kStormGhoulPendingTimeoutTicks)
        {
            stillLive.push_back(handle);
            continue;
        }

        _stormGhoulCleanupRefs.push_back(handle);
        _stormGhoulUnresolvedTicksByHandle.erase(handleValue);
        ++retired;
    }
    _stormGhoulRefs.swap(stillLive);
    if (retired)
        logger->warn(
            "[Storm/Ghouls] Moving {} unresolved live ref(s) to cleanup after {} consecutive ticks; pack replacement is no longer blocked.",
            retired, kStormGhoulPendingTimeoutTicks);
}
void RadiationEngine::ResetStormGhoulStateForLoad()
{
    _stormGhoulRefs.clear();
    _stormGhoulPendingRefs.clear();
    _stormGhoulCleanupRefs.clear();
    _stormGhoulPendingTicks = 0;
    _stormGhoulUnresolvedTicksByHandle.clear();
    _ghoulPackCenter = {};
    _ghoulPackWorldID = 0;
    _ghoulPackInterior = false;
}
void RadiationEngine::SpawnStormGhouls()
{
    if (!_stormGhoulsEnabled) return;
    if (!_stormGhoulForm) return;
    if (IsPlayerSheltered()) return;

    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player) return;

    auto* cell = player->GetParentCell();
    auto* world = player->cachedWorldspace;
    if (!cell || !cell->IsExterior() || !cell->IsAttached() || !world || cell->worldSpace != world)
        return;

    if (!_stormGhoulPendingRefs.empty())
    {
        Log()->debug("[Storm/Ghouls] {} pending hunter ref(s) unresolved; deferring pack replacement.",
            _stormGhoulPendingRefs.size());
        return;
    }

    std::size_t liveHunters = 0;
    std::size_t unresolvedHunters = 0;
    for (auto& handle : _stormGhoulRefs)
    {
        auto ptr = handle.get();
        auto* ref = ptr.get();
        if (!ref)
        {
            ++unresolvedHunters;
            continue;
        }
        auto* actor = ref && ref->GetFormType() == RE::ENUM_FORM_ID::kACHR
            ? static_cast<RE::Actor*>(ref) : nullptr;
        if (actor && !actor->IsDead(false))
            ++liveHunters;
    }
    if (unresolvedHunters > 0)
    {
        Log()->debug("[Storm/Ghouls] {} hunter ref(s) unresolved during streaming; deferring pack replacement.",
            unresolvedHunters);
        return;
    }
    if (liveHunters > 0)
    {
        Log()->info("[Storm/Ghouls] Existing hunter pack still active ({} live); suppressing another pack.",
            liveHunters);
        return;
    }
    if (!_stormGhoulRefs.empty())
    {
        Log()->info("[Storm/Ghouls] Previous hunter pack defeated; cleaning {} tracked ref(s) before the next pack roll.",
            _stormGhoulRefs.size());
        ClearStormGhouls();
    }
    _ghoulPackWorldID = 0;
    _ghoulPackInterior = false;

    auto logger = Log();
    static std::mt19937 rng{ std::random_device{}() };

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(rng) >= _stormGhoulSpawnChance)
    {
        logger->info("[Storm/Ghouls] No ghoul pack this storm (chance {:.0f}%).", _stormGhoulSpawnChance * 100.0f);
        return;
    }

    constexpr float kPi = 3.14159265f;
    std::uniform_int_distribution<int>    countDist(kStormGhoulMinCount, kStormGhoulMaxCount);
    std::uniform_real_distribution<float> radiusDist(kStormGhoulMinRadius, kStormGhoulMaxRadius);
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * kPi);

    const int          count = countDist(rng);
    const RE::NiPoint3 base  = player->GetPosition();
    int spawned = 0;
    int grounded = 0;
    int visible3D = 0;
    int pending = 0;

    for (int i = 0; i < count; ++i)
    {
        const float angle  = angleDist(rng);
        const float radius = radiusDist(rng);
        RE::TESBoundObject* form = (i % 6 == 0 && _stormGlowingOneForm) ? _stormGlowingOneForm : _stormGhoulForm;

        RE::NEW_REFR_DATA data;
        data.object    = form;
        data.location  = { base.x + radius * std::cos(angle),
                           base.y + radius * std::sin(angle),
                           base.z + 2.0f };
        data.direction = { 0.0f, 0.0f, 0.0f };
        data.interior  = nullptr;
        data.world     = world;
        data.initiallyDisabled = true;
        data.clearStillLoadingFlag = true;

        auto handle = dh->CreateReferenceAtLocation(data);
        if (static_cast<bool>(handle))
        {
            auto ptr = handle.get();
            auto* ref = ptr.get();
            auto* actor = ref && ref->GetFormType() == RE::ENUM_FORM_ID::kACHR
                ? static_cast<RE::Actor*>(ref) : nullptr;
            if (actor)
            {
                actor->Enable(true);
                const bool clamped = actor->ClampToGround();
                const auto finalPos = actor->GetPosition();
                auto* actorCell = actor->GetParentCell();
                const auto actorCellID = actorCell ? actorCell->GetFormID() : 0;
                const auto actorWorldID = actorCell && actorCell->worldSpace
                    ? actorCell->worldSpace->GetFormID() : 0;
                const bool has3D = actor->Get3D() != nullptr;
                logger->debug(
                    "[Storm/Ghouls] actor {} pos=({:.0f},{:.0f},{:.0f}) grounded={} 3D={} cell=0x{:08X} world=0x{:08X}",
                    spawned + 1, finalPos.x, finalPos.y, finalPos.z, clamped, has3D,
                    actorCellID, actorWorldID);
                if (clamped) ++grounded;
                if (has3D) ++visible3D;
                _stormGhoulRefs.push_back(handle);
                ++spawned;
            }
            else
            {
                if (ref)
                    ref->Enable(true);
                _stormGhoulPendingRefs.push_back(handle);
                ++pending;
            }
        }
    }
    _ghoulPackCenter = base;
    _ghoulPackWorldID = world->GetFormID();
    _ghoulPackInterior = false;
    logger->warn(
        "[Storm/Ghouls] HUNT — unleashed {} feral ghoul(s); grounded {}/{}, 3D {}/{}, pending {}; they hunt until killed or you leave the area.",
        spawned, grounded, spawned, visible3D, spawned, pending);
}
void RadiationEngine::CheckGhoulPackCleanup()
{
    if (_stormGhoulRefs.empty() && _stormGhoulPendingRefs.empty()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto* cell = player->GetParentCell();
    const auto* world = player->cachedWorldspace;
    const bool wentIndoors = cell && cell->IsInterior() && !_ghoulPackInterior;
    if (wentIndoors)
    {
        Log()->info("[Storm/Ghouls] Player went indoors. Despawning the hunter pack.");
        ClearStormGhouls();
        return;
    }

    if (!cell || !cell->IsExterior() || !cell->IsAttached() || !world || cell->worldSpace != world)
    {
        Log()->debug("[Storm/Ghouls] Player ownership unresolved during streaming; deferring pack cleanup.");
        return;
    }

    const std::uint32_t wid = world->GetFormID();
    const bool changedWorld = _ghoulPackWorldID != 0 && wid != _ghoulPackWorldID;
    const float dist        = player->GetDistanceFromPoint(_ghoulPackCenter);
    const bool movedFar     = !changedWorld && dist > kGhoulDespawnDist;

    if (changedWorld || movedFar)
    {
        Log()->info(
            "[Storm/Ghouls] Player left the area ({}). Despawning the hunter pack.",
            changedWorld ? "changed worldspace" : "moved far");
        ClearStormGhouls();
    }
}
void RadiationEngine::ClearStormGhouls()
{
    if (_stormGhoulRefs.empty() && _stormGhoulPendingRefs.empty()) return;
    auto logger = Log();
    std::vector<RE::ObjectRefHandle> refs;
    refs.reserve(_stormGhoulRefs.size() + _stormGhoulPendingRefs.size());
    refs.insert(refs.end(), _stormGhoulRefs.begin(), _stormGhoulRefs.end());
    refs.insert(refs.end(), _stormGhoulPendingRefs.begin(), _stormGhoulPendingRefs.end());
    int cleared = 0;
    std::size_t unresolved = 0;
    for (auto& handle : refs)
    {
        auto ptr = handle.get();
        if (auto* ref = ptr.get())
        {
            ref->Disable();
            ref->SetDelete(true);
            ++cleared;
        }
        else
        {
            _stormGhoulCleanupRefs.push_back(handle);
            ++unresolved;
        }
    }
    _stormGhoulRefs.clear();
    _stormGhoulPendingRefs.clear();
    _stormGhoulPendingTicks = 0;
    _stormGhoulUnresolvedTicksByHandle.clear();
    _ghoulPackCenter = {};
    _ghoulPackWorldID = 0;
    _ghoulPackInterior = false;
    logger->info("[Storm/Ghouls] Cleared {} storm ghoul ref(s); {} moved to cleanup ownership.",
        cleared, unresolved);
}
void RadiationEngine::ApplyStormIMAD(int typeIndex)
{
    SetStormIMADTarget(typeIndex, 1.0f, 2.5f);
}

void RadiationEngine::SetStormIMADTarget(int typeIndex, float strength, float fadeSeconds)
{
    strength = std::clamp(strength, 0.0f, 1.0f);
    if (strength <= 0.0f || typeIndex < 0 || typeIndex >= kStormTypeCount || typeIndex == kDustStormIndex)
    {
        FadeOutStormIMAD(fadeSeconds);
        return;
    }

    auto* imad = _stormIMADs[typeIndex];
    if (!imad)
    {
        FadeOutStormIMAD(fadeSeconds);
        return;
    }

    if (_activeStormImadIndex != typeIndex || !_stormImadInstance)
    {
        if (_stormImadInstance)
            static_cast<RE::ImageSpaceModifierInstance*>(_stormImadInstance.get())->Stop();
        _stormImadInstance = RE::ImageSpaceModifierInstanceForm::Trigger(imad, 0.0f, nullptr);
        if (!_stormImadInstance)
        {
            _activeStormImadIndex = -1;
            _stormImadTargetStrength = 0.0f;
            _stormImadFadeRate = 0.0f;
            Hooks::SetStormIMADFadeActive(false);
            return;
        }
        _activeStormImadIndex = typeIndex;
        Log()->info("[StormIMAD] Started atmosphere IMAD for '{}' (type {}).",
            kStormTypes[typeIndex].name, typeIndex);
    }

    const float current = _stormImadInstance->strength;
    _stormImadTargetStrength = strength;
    _stormImadStopWhenFaded = false;
    _stormImadFadeRate = fadeSeconds > 0.0f ? std::abs(strength - current) / fadeSeconds : 0.0f;
    if (fadeSeconds <= 0.0f)
    {
        _stormImadInstance->strength = strength;
        _stormImadFadeRate = 0.0f;
        Hooks::SetStormIMADFadeActive(false);
    }
    else
    {
        Hooks::SetStormIMADFadeActive(current != strength);
    }
}

void RadiationEngine::FadeOutStormIMAD(float fadeSeconds)
{
    if (!_stormImadInstance)
    {
        Hooks::SetStormIMADFadeActive(false);
        return;
    }
    _stormImadTargetStrength = 0.0f;
    _stormImadStopWhenFaded = true;
    _stormImadFadeRate = fadeSeconds > 0.0f ? _stormImadInstance->strength / fadeSeconds : 0.0f;
    if (fadeSeconds <= 0.0f || _stormImadInstance->strength <= 0.001f)
    {
        static_cast<RE::ImageSpaceModifierInstance*>(_stormImadInstance.get())->Stop();
        _stormImadInstance = nullptr;
        _activeStormImadIndex = -1;
        _stormImadStopWhenFaded = false;
        Hooks::SetStormIMADFadeActive(false);
    }
    else
        Hooks::SetStormIMADFadeActive(true);
}

void RadiationEngine::UpdateStormIMADFade()
{
    if (!_stormImadInstance)
    {
        Hooks::SetStormIMADFadeActive(false);
        return;
    }

    const float current = _stormImadInstance->strength;
    const float delta = _stormImadTargetStrength - current;
    if (std::abs(delta) <= 0.001f)
    {
        _stormImadInstance->strength = _stormImadTargetStrength;
        if (_stormImadStopWhenFaded && _stormImadTargetStrength <= 0.0f)
        {
            static_cast<RE::ImageSpaceModifierInstance*>(_stormImadInstance.get())->Stop();
            _stormImadInstance = nullptr;
            _activeStormImadIndex = -1;
            _stormImadFadeRate = 0.0f;
            _stormImadStopWhenFaded = false;
            Hooks::SetStormIMADFadeActive(false);
        }
        else
        {
            _stormImadFadeRate = 0.0f;
            Hooks::SetStormIMADFadeActive(false);
        }
        return;
    }

    const float step = _stormImadFadeRate > 0.0f ? _stormImadFadeRate / 30.0f : std::abs(delta);
    _stormImadInstance->strength = current + std::copysign(std::min(std::abs(delta), step), delta);
}

void RadiationEngine::ClearStormIMAD()
{
    if (_stormImadInstance)
        static_cast<RE::ImageSpaceModifierInstance*>(_stormImadInstance.get())->Stop();
    else if (_activeStormImadIndex >= 0 && _activeStormImadIndex < kStormTypeCount && _stormIMADs[_activeStormImadIndex])
        RE::ImageSpaceModifierInstanceForm::Stop(_stormIMADs[_activeStormImadIndex]);
    _stormImadInstance = nullptr;
    _activeStormImadIndex = -1;
    _stormImadTargetStrength = 0.0f;
    _stormImadFadeRate = 0.0f;
    _stormImadStopWhenFaded = false;
    Hooks::SetStormIMADFadeActive(false);
    Log()->debug("[StormIMAD] Storm atmosphere IMAD cleared.");
}
