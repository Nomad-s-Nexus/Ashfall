#include "pch.h"
#include "ClimateProtectionRuntime.h"
#include "ClimateInsulationPolicy.h"

#include <RE/B/BIPED_OBJECT.h>
#include <RE/B/BipedAnim.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESObjectARMO.h>

namespace
{
    void AddSlotCoverage(RadClimateInsulationPolicy::Coverage& coverage, RE::BIPED_OBJECT slot) noexcept
    {
        using enum RE::BIPED_OBJECT;
        switch (slot)
        {
        case kHairTop:
            coverage.head = true;
            break;
        case kBody:
        case kUnderTorso:
        case kAboveTorso:
            coverage.torso = true;
            break;
        case kUnderLeftArm:
        case kAboveLeftArm:
            coverage.leftArm = true;
            break;
        case kUnderRightArm:
        case kAboveRightArm:
            coverage.rightArm = true;
            break;
        case kUnderLeftLeg:
        case kAboveLeftLeg:
            coverage.leftLeg = true;
            break;
        case kUnderRightLeg:
        case kAboveRightLeg:
            coverage.rightLeg = true;
            break;
        default:
            break;
        }
    }
}

void ClimateProtectionRuntime::Reset() noexcept
{
    _config.Reset();
    _cached = {};
    _configLoaded = false;
    _refreshTick = 0;
}

RadClimatePolicy::ProtectionSample ClimateProtectionRuntime::Sample(RE::PlayerCharacter* player)
{
    EnsureConfigLoaded();
    if (_refreshTick > 0)
    {
        --_refreshTick;
        return _cached;
    }

    _cached = Scan(player);
    _refreshTick = 3;
    return _cached;
}

void ClimateProtectionRuntime::EnsureConfigLoaded()
{
    if (_configLoaded)
        return;
    _config.Load("Data/F4SE/Plugins/FalloutAnomaly/climate_protection.json");
    _configLoaded = true;
}

RadClimatePolicy::ProtectionSample ClimateProtectionRuntime::Scan(RE::PlayerCharacter* player) const
{
    RadClimatePolicy::ProtectionSample result{};
    if (!player || !player->biped)
        return result;

    std::unordered_map<std::uint32_t, RadClimateInsulationPolicy::PieceSample> piecesByForm;
    auto& biped = *player->biped;
    for (std::size_t i = 0; i < std::size(biped.object); ++i)
    {
        auto* form = biped.object[i].parent.object;
        if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO)
            continue;

        auto* armor = static_cast<RE::TESObjectARMO*>(form);
        auto& piece = piecesByForm[armor->GetFormID()];
        AddSlotCoverage(piece.coverage, static_cast<RE::BIPED_OBJECT>(i));
        if (const auto overrideValue = _config.ArmorOverride(armor->GetFormID()))
            piece.overrideInsulation01 = *overrideValue;
    }

    std::vector<RadClimateInsulationPolicy::PieceSample> pieces;
    pieces.reserve(piecesByForm.size());
    for (const auto& [formID, piece] : piecesByForm)
    {
        (void)formID;
        pieces.push_back(piece);
    }

    result.insulation01 = RadClimateInsulationPolicy::CombinedInsulation(
        std::span<const RadClimateInsulationPolicy::PieceSample>{ pieces });
    return result;
}
