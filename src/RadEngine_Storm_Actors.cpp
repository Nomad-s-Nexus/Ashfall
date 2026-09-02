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
    const bool wentIndoors  = cell && cell->IsInterior() && !_ghoulPackInterior;
    // Worldspace change (fast travel to a different exterior). Checked FIRST because the point
    // distance below is computed in raw cell coordinates — meaningless across worldspaces, where
    // it can read small even though the player is nowhere near the pack, leaving it stuck alive.
    const auto* world       = player->cachedWorldspace;
    const std::uint32_t wid = world ? world->GetFormID() : 0;
    const bool changedWorld = _ghoulPackWorldID != 0 && wid != _ghoulPackWorldID;
    const float dist        = player->GetDistanceFromPoint(_ghoulPackCenter);
    const bool movedFar     = !changedWorld && dist > kGhoulDespawnDist;  // only trust dist in the same worldspace

    if (wentIndoors || changedWorld || movedFar)
    {
        Log()->info(
            "[Storm/Ghouls] Player left the area ({}). Despawning the hunter pack.",
            changedWorld ? "changed worldspace" : (wentIndoors ? "went indoors" : "moved far"));
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
    if (typeIndex < 0 || typeIndex >= kStormTypeCount) return;
    if (_activeStormImadIndex == typeIndex) return;

    // Dust Storm uses NO image-space modifier (like vanilla CommonwealthDusty): the brown TintColor
    // added over the dark-blue night sky rendered PURPLE, and it read as a flat "painted" grade on top.
    // The heavy fog + brown weather colours + camera-dust particles carry the look instead. Clear any
    // dust IMAD still running from a prior apply and bail.
    if (typeIndex == kDustStormIndex)
    {
        ClearStormIMAD();
        return;
    }

    RE::TESImageSpaceModifier* imad = _stormIMADs[typeIndex];
    if (!imad) return;

    // Stop any previously-running storm IMAD first to avoid stacking.
    if (_activeStormImadIndex >= 0 && _activeStormImadIndex < kStormTypeCount
        && _stormIMADs[_activeStormImadIndex])
    {
        RE::ImageSpaceModifierInstanceForm::Stop(_stormIMADs[_activeStormImadIndex]);
    }

    RE::ImageSpaceModifierInstanceForm::Trigger(imad, 1.0f, nullptr);
    _activeStormImadIndex = typeIndex;
    Log()->info("[StormIMAD] Applied atmosphere IMAD for '{}' (type {}).",
        kStormTypes[typeIndex].name, typeIndex);
}
void RadiationEngine::ClearStormIMAD()
{
    if (_activeStormImadIndex < 0) return;
    if (_activeStormImadIndex < kStormTypeCount && _stormIMADs[_activeStormImadIndex])
        RE::ImageSpaceModifierInstanceForm::Stop(_stormIMADs[_activeStormImadIndex]);
    _activeStormImadIndex = -1;
    Log()->debug("[StormIMAD] Storm atmosphere IMAD cleared.");
}
