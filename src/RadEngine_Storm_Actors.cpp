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

// Storm lifecycle: scheduling, phases, FX (fire ring / ghouls / IMAD / quest stages), NPC effects, exposure.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


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
void RadiationEngine::SpawnStormGhouls()
{
    if (!_stormGhoulForm) return;
    if (IsPlayerSheltered()) return;

    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player) return;

    auto* world = player->cachedWorldspace;
    if (!world) return;

    auto logger = Log();
    static std::mt19937 rng{ std::random_device{}() };

    // % chance per lethal storm — most storms pass without a pack.
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(rng) > kStormGhoulSpawnChance)
    {
        logger->info("[Storm/Ghouls] No ghoul pack this storm (chance {:.0f}%).", kStormGhoulSpawnChance * 100.0f);
        return;
    }

    constexpr float kPi = 3.14159265f;
    std::uniform_int_distribution<int>    countDist(kStormGhoulMinCount, kStormGhoulMaxCount);
    std::uniform_real_distribution<float> radiusDist(kStormGhoulMinRadius, kStormGhoulMaxRadius);
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * kPi);

    const int          count = countDist(rng);
    const RE::NiPoint3 base  = player->GetPosition();
    int spawned = 0;

    for (int i = 0; i < count; ++i)
    {
        const float angle  = angleDist(rng);
        const float radius = radiusDist(rng);
        // Every ~6th is a Glowing One if available — adds menace to the pack.
        RE::TESBoundObject* form = (i % 6 == 0 && _stormGlowingOneForm) ? _stormGlowingOneForm : _stormGhoulForm;

        RE::NEW_REFR_DATA data;
        data.object    = form;
        data.location  = { base.x + radius * std::cos(angle),
                           base.y + radius * std::sin(angle),
                           base.z + 2.0f };
        data.direction = { 0.0f, 0.0f, 0.0f };
        data.interior  = nullptr;
        data.world     = world;

        // Tracked so we can despawn the pack once the player leaves the area (see CheckGhoulPackCleanup).
        // They are NOT cleared at storm end, so they persist and hunt the player meanwhile.
        auto handle = dh->CreateReferenceAtLocation(data);
        if (static_cast<bool>(handle))
        {
            _stormGhoulRefs.push_back(handle);
            ++spawned;
        }
    }
    // Remember where/when the pack was unleashed so cleanup can fire when the player moves on.
    _ghoulPackCenter = base;
    auto* pcell = player->GetParentCell();
    _ghoulPackWorldID = world ? world->GetFormID() : 0;   // for the cleanup worldspace-change check
    _ghoulPackInterior = pcell && pcell->IsInterior();
    logger->warn("[Storm/Ghouls] HUNT — unleashed {} feral ghoul(s); they hunt until killed or you leave the area.", spawned);
}
// Despawn the storm ghoul pack once the player has clearly left the area: traveled far from the
// spawn center, changed worldspace, or gone indoors. Keeps persistent hunters from accumulating.
void RadiationEngine::CheckGhoulPackCleanup()
{
    if (_stormGhoulRefs.empty()) return;
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
        Log()->info("[Storm/Ghouls] Player ownership is unresolved; despawning the hunter pack.");
        ClearStormGhouls();
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
    if (_stormGhoulRefs.empty()) return;
    auto logger = Log();
    int cleared = 0;
    for (auto& handle : _stormGhoulRefs)
    {
        auto ptr = handle.get();
        if (auto* ref = ptr.get())
        {
            ref->Disable();
            ref->SetDelete(true);
            ++cleared;
        }
    }
    _stormGhoulRefs.clear();
    _ghoulPackWorldID = 0;   // pack gone — clear its area trackers so stale state can't mislead the next check
    logger->info("[Storm/Ghouls] Cleared {} storm ghoul ref(s).", cleared);
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
