#include "pch.h"
#include "ClimateProtectionRuntime.h"
#include "ClimateInsulationPolicy.h"

#include <RE/B/BIPED_OBJECT.h>
#include <RE/B/BipedAnim.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESFile.h>
#include <RE/T/TESObjectARMO.h>

namespace
{
    bool Fills(RE::TESObjectARMO* armor, RE::BIPED_OBJECT slot) noexcept
    {
        return armor && armor->FillsBipedSlot(static_cast<std::int32_t>(slot));
    }

    RadClimateInsulationPolicy::Coverage AuthoredCoverage(RE::TESObjectARMO* armor) noexcept
    {
        using enum RE::BIPED_OBJECT;
        RadClimateInsulationPolicy::Coverage coverage{};
        coverage.head = Fills(armor, kHairTop);
        coverage.torso = Fills(armor, kBody) || Fills(armor, kUnderTorso) || Fills(armor, kAboveTorso);
        coverage.leftArm = Fills(armor, kUnderLeftArm) || Fills(armor, kAboveLeftArm);
        coverage.rightArm = Fills(armor, kUnderRightArm) || Fills(armor, kAboveRightArm);
        coverage.leftLeg = Fills(armor, kUnderLeftLeg) || Fills(armor, kAboveLeftLeg);
        coverage.rightLeg = Fills(armor, kUnderRightLeg) || Fills(armor, kAboveRightLeg);
        return coverage;
    }

    struct WornArmor
    {
        RE::TESObjectARMO* armor{ nullptr };
        RadClimateInsulationPolicy::PieceSample sample{};
    };
}

void ClimateProtectionRuntime::Reset() noexcept
{
    _config.Reset();
    _cached = {};
    _lastWornForms.clear();
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

RadClimatePolicy::ProtectionSample ClimateProtectionRuntime::Scan(RE::PlayerCharacter* player)
{
    RadClimatePolicy::ProtectionSample result{};
    if (!player || !player->biped)
        return result;

    std::unordered_map<std::uint32_t, WornArmor> piecesByForm;
    auto& biped = *player->biped;
    for (std::size_t i = 0; i < std::size(biped.object); ++i)
    {
        auto* form = biped.object[i].parent.object;
        if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO)
            continue;

        auto* armor = static_cast<RE::TESObjectARMO*>(form);
        auto [it, inserted] = piecesByForm.try_emplace(armor->GetFormID());
        auto& worn = it->second;
        if (inserted)
        {
            worn.armor = armor;
            worn.sample.coverage = AuthoredCoverage(armor);
        }
        if (const auto overrideValue = _config.ArmorOverride(armor->GetFormID()))
            worn.sample.overrideInsulation01 = *overrideValue;
    }

    std::vector<WornArmor> worn;
    worn.reserve(piecesByForm.size());
    for (const auto& [formID, piece] : piecesByForm)
    {
        (void)formID;
        worn.push_back(piece);
    }
    std::sort(worn.begin(), worn.end(), [](const WornArmor& lhs, const WornArmor& rhs) {
        return lhs.armor->GetFormID() < rhs.armor->GetFormID();
    });

    std::vector<std::uint32_t> wornForms;
    std::vector<RadClimateInsulationPolicy::PieceSample> samples;
    wornForms.reserve(worn.size());
    samples.reserve(worn.size());
    for (const auto& piece : worn)
    {
        wornForms.push_back(piece.armor->GetFormID());
        samples.push_back(piece.sample);
    }

    if (wornForms != _lastWornForms)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
        {
            logger->info("[Climate] Worn armor changed: {} unique armor record(s).", worn.size());
            for (const auto& piece : worn)
            {
                auto automatic = piece.sample;
                automatic.overrideInsulation01 = -1.0f;
                const float automatic01 = RadClimateInsulationPolicy::PieceInsulation(automatic);
                const float effective01 = RadClimateInsulationPolicy::PieceInsulation(piece.sample);
                const bool overridden = piece.sample.overrideInsulation01 >= 0.0f;
                auto* file = piece.armor->GetFile(0);
                if (file)
                {
                    logger->info(
                        "[Climate] armor {}|0x{:X} runtime=0x{:08X} auto={:.0f}% effective={:.0f}% override={} coverage=H{} T{} LA{} RA{} LL{} RL{}",
                        file->GetFilename(), piece.armor->GetLocalFormID(), piece.armor->GetFormID(),
                        automatic01 * 100.0f, effective01 * 100.0f, overridden,
                        piece.sample.coverage.head, piece.sample.coverage.torso,
                        piece.sample.coverage.leftArm, piece.sample.coverage.rightArm,
                        piece.sample.coverage.leftLeg, piece.sample.coverage.rightLeg);
                }
                else
                {
                    logger->info(
                        "[Climate] armor runtime=0x{:08X} auto={:.0f}% effective={:.0f}% override={} coverage=H{} T{} LA{} RA{} LL{} RL{}",
                        piece.armor->GetFormID(), automatic01 * 100.0f, effective01 * 100.0f, overridden,
                        piece.sample.coverage.head, piece.sample.coverage.torso,
                        piece.sample.coverage.leftArm, piece.sample.coverage.rightArm,
                        piece.sample.coverage.leftLeg, piece.sample.coverage.rightLeg);
                }
            }
        }
        _lastWornForms = wornForms;
    }

    result.insulation01 = RadClimateInsulationPolicy::CombinedInsulation(
        std::span<const RadClimateInsulationPolicy::PieceSample>{ samples });
    return result;
}
