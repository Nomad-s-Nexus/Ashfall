#include "RadEngine_Internal.h"

using namespace RadInternal;

// Shelter and cover: the overhead roof-fan raycast, powered Pulowski shelters, sealed power armour.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


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


// Raycast the player's surroundings to decide if a player-built structure encloses them. Throttled
// by the caller (UpdateStorm path) — do NOT call every tick. Const: only reads world state.
bool RadiationEngine::ComputeEnclosedByStructure() const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;
    auto* cell = player->GetParentCell();
    if (!cell) return false;

    const RE::NiPoint3 p = player->GetPosition();
    const RE::NiPoint3 head{ p.x, p.y, p.z + kShelterHeadZ };

    // Upward fan: one ray straight up + four angled ~30 deg off vertical (N/E/S/W). Count how many
    // hit overhead structure; the fan tolerates roof edges/gaps a single up-ray would miss.
    const float h = kRoofRayDist * kRoofFanHoriz;   // horizontal offset of the angled rays
    const float v = kRoofRayDist * kRoofFanVert;    // their vertical component

    const bool up =     RayHitsStructure(cell, head, { head.x,     head.y,     head.z + kRoofRayDist });  // straight up
    const bool px =     RayHitsStructure(cell, head, { head.x + h, head.y,     head.z + v });             // +X
    const bool nx =     RayHitsStructure(cell, head, { head.x - h, head.y,     head.z + v });             // -X
    const bool py =     RayHitsStructure(cell, head, { head.x,     head.y + h, head.z + v });             // +Y
    const bool ny =     RayHitsStructure(cell, head, { head.x,     head.y - h, head.z + v });             // -Y

    const int  hits    = (up ? 1 : 0) + (px ? 1 : 0) + (nx ? 1 : 0) + (py ? 1 : 0) + (ny ? 1 : 0);
    // The straight-up ray is MANDATORY: sky radiation needs something directly overhead. Without this
    // the angled rays clipping a nearby building's wall counted as a "roof" out in the open street.
    const bool covered = up && hits >= kRoofFanMinHits;
    Log()->debug("[Shelter] roof-fan {}/5 (up={}, need up + {} total) -> covered={}",
        hits, up, kRoofFanMinHits, covered);

    // Debug overlay: live on-screen readout so the player can walk their base and see exactly which
    // overhead rays connect (toggle with the "shelter" debug command / Shelter Debug button).
    if (_debugShelter)
    {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "[Shelter] %d/5 -> %s  (up:%c%s +X:%c -X:%c +Y:%c -Y:%c)",
            hits, covered ? "COVERED" : "EXPOSED",
            up ? 'Y' : '-', up ? "" : " NO ROOF OVERHEAD",
            px ? 'Y' : '-', nx ? 'Y' : '-', py ? 'Y' : '-', ny ? 'Y' : '-');
        Notify(msg, !covered);  // warning style when exposed so it stands out
    }
    return covered;
}


bool RadiationEngine::IsPlayerSheltered() const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return true;  // fail-safe: never punish on unknown state
    auto* cell = player->GetParentCell();
    if (cell && cell->IsInterior()) return true;
    if (IsPlayerInPoweredShelter()) return true;  // powered Pulowski shelter
    if (_playerEnclosed) return true;             // player-built 4-walls + roof (raycast, see Update)
    return false;
}
