#include "RadEngine_Internal.h"
#include "ClimateShelterPolicy.h"

#include <RE/B/BSTEvent.h>
#include <RE/C/CameraStates.h>
#include <RE/P/PlayerCamera.h>
#include <RE/T/TESObjectLoadedEvent.h>

#include <atomic>

using namespace RadInternal;

// Shelter and cover: the overhead roof-fan raycast, powered Pulowski shelters, sealed power armour.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.

namespace
{
    struct ShelterProbeCache
    {
        std::atomic<bool> dirty{ true };
        std::uint32_t cellID{ 0 };
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
        bool covered{ false };
        bool known{ false };
        bool debugWasEnabled{ false };
    };

    ShelterProbeCache g_shelterProbeCache;

    class ShelterLoadSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESObjectLoadedEvent&,
            RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
        {
            g_shelterProbeCache.dirty.store(true, std::memory_order_release);
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void EnsureShelterLoadSink()
    {
        static bool registered = false;
        static ShelterLoadSink sink;
        if (registered)
            return;
        if (auto* source = RE::TESObjectLoadedEvent::GetEventSource())
        {
            source->RegisterSink(&sink);
            registered = true;
        }
    }
}


// Power Armor SEALED-suit detection. Scans the actor's biped slots for armor carrying the vanilla
// ArmorTypePower keyword (Fallout4.esm 0x4D8A1), de-duping by FormID so one piece filling several slots
// isn't counted twice.
//
// The suit only shields against radiation if it is actually SEALED, so we require the COMPLETE rig:
// the HELMET (mandatory — it is what seals the player's breathing; an open-headed frame is not a sealed
// suit no matter how many limb pieces are on) plus all 6 pieces (helmet + torso + 2 arms + 2 legs).
// Earlier thresholds of 4, then 5, both let an incomplete rig with an open socket read as "sealed" and
// hand out full storm immunity.
bool RadiationEngine::IsActorInPowerArmor(RE::Actor* actor) const
{
    if (!_armorTypePowerKeyword || !actor || !actor->biped) return false;

    // PA helmets fill FO4 armor slot 30 (BIPED_OBJECT::kHairTop).
    constexpr auto kHelmetSlot = static_cast<std::int32_t>(RE::BIPED_OBJECT::kHairTop);

    std::unordered_set<std::uint32_t> seen;
    bool helmet = false;
    auto& biped = *actor->biped;
    for (std::size_t i = 0; i < std::size(biped.object); ++i)
    {
        RE::TESForm* form = biped.object[i].parent.object;
        if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO) continue;
        auto* armo = static_cast<RE::TESObjectARMO*>(form);
        if (!armo->HasKeyword(_armorTypePowerKeyword)) continue;
        seen.insert(armo->formID);
        if (armo->FillsBipedSlot(kHelmetSlot)) helmet = true;
    }
    return helmet && seen.size() >= kPowerArmorFullPieces;
}


bool RadiationEngine::IsPlayerInPowerArmor() const
{
    return IsActorInPowerArmor(RE::PlayerCharacter::GetSingleton());
}


// Ask the Pulowski Preservation Protocol DLL whether the player is sealed inside / at a powered
// Pulowski shelter. No-op (false) if that plugin isn't installed.
bool RadiationEngine::IsPlayerInPoweredShelter() const
{
    using PPPShelteredFn = bool (*)();
    static PPPShelteredFn s_fn = nullptr;
    if (!s_fn) {
        if (auto h = GetModuleHandleW(L"PulowskiProtocol.dll"))
            s_fn = reinterpret_cast<PPPShelteredFn>(GetProcAddress(h, "PPP_IsPlayerSheltered"));
    }
    return s_fn && s_fn();
}


bool RadiationEngine::ComputeEnclosedByStructure() const
{
    EnsureShelterLoadSink();

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return true;
    auto* cell = player->GetParentCell();
    if (!cell) return true;
    if (cell->IsInterior()) return true;
    if (!cell->IsAttached()) return true;

    const RE::NiPoint3 p = player->GetPosition();
    const auto cellID = cell->GetFormID();
    const bool debugJustEnabled = _debugShelter && !g_shelterProbeCache.debugWasEnabled;
    g_shelterProbeCache.debugWasEnabled = _debugShelter;
    const bool eventDirty = g_shelterProbeCache.dirty.exchange(false, std::memory_order_acq_rel);

    if (!debugJustEnabled && !eventDirty && !RadClimateShelterPolicy::ShouldProbe(
            g_shelterProbeCache.known,
            cellID,
            g_shelterProbeCache.cellID,
            p.x - g_shelterProbeCache.x,
            p.y - g_shelterProbeCache.y,
            p.z - g_shelterProbeCache.z))
        return g_shelterProbeCache.covered;

    const RE::NiPoint3 head{ p.x, p.y, p.z + kShelterHeadZ };

    const float h = kRoofRayDist * kRoofFanHoriz;
    const float v = kRoofRayDist * kRoofFanVert;

    const bool up = RayHitsStructure(cell, head, { head.x, head.y, head.z + kRoofRayDist });
    const bool px = RayHitsStructure(cell, head, { head.x + h, head.y, head.z + v });
    const bool nx = RayHitsStructure(cell, head, { head.x - h, head.y, head.z + v });
    const bool py = RayHitsStructure(cell, head, { head.x, head.y + h, head.z + v });
    const bool ny = RayHitsStructure(cell, head, { head.x, head.y - h, head.z + v });

    const int hits = (up ? 1 : 0) + (px ? 1 : 0) + (nx ? 1 : 0) + (py ? 1 : 0) + (ny ? 1 : 0);
    const bool covered = up && hits >= kRoofFanMinHits;

    g_shelterProbeCache.cellID = cellID;
    g_shelterProbeCache.x = p.x;
    g_shelterProbeCache.y = p.y;
    g_shelterProbeCache.z = p.z;
    g_shelterProbeCache.covered = covered;
    g_shelterProbeCache.known = true;

    Log()->debug("[Shelter] roof-fan {}/5 (up={}, need up + {} total) -> covered={}",
        hits, up, kRoofFanMinHits, covered);

    if (_debugShelter)
    {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "[Shelter] %d/5 -> %s  (up:%c%s +X:%c -X:%c +Y:%c -Y:%c)",
            hits, covered ? "COVERED" : "EXPOSED",
            up ? 'Y' : '-', up ? "" : " NO ROOF OVERHEAD",
            px ? 'Y' : '-', nx ? 'Y' : '-', py ? 'Y' : '-', ny ? 'Y' : '-');
        Notify(msg, !covered);
    }
    return covered;
}


bool RadiationEngine::IsPlayerSheltered() const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return true;
    auto* cell = player->GetParentCell();
    if (cell && cell->IsInterior()) return true;
    if (IsPlayerInPoweredShelter()) return true;
    if (_playerEnclosed) return true;
    return false;
}

bool RadiationEngine::PlayerInFirstPersonView() const
{
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!camera) return true;
    return camera->QCameraEquals(RE::CameraStates::kFirstPerson) || camera->QCameraEquals(RE::CameraStates::kIronSights);
}
