#include "pch.h"
#include "ClimateProtectionRuntime.h"
#include "ClimateInsulationPolicy.h"
#include "ClimatePowerArmorPolicy.h"

#include <RE/A/ActorEquipManager.h>
#include <RE/A/ActorEquipManagerEvent.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSObjectInstance.h>
#include <RE/B/BIPED_OBJECT.h>
#include <RE/B/BipedAnim.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFile.h>
#include <RE/T/TESForm.h>
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

    class ClimateEquipSink final : public RE::BSTEventSink<RE::ActorEquipManagerEvent::Event>
    {
    public:
        explicit ClimateEquipSink(ClimateProtectionRuntime* runtime) : _runtime(runtime) {}

        RE::BSEventNotifyControl ProcessEvent(
            const RE::ActorEquipManagerEvent::Event& event,
            RE::BSTEventSource<RE::ActorEquipManagerEvent::Event>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && event.actorAffected == player && event.itemAffected && event.itemAffected->object &&
                event.itemAffected->object->GetFormType() == RE::ENUM_FORM_ID::kARMO)
                _runtime->Invalidate();

            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        ClimateProtectionRuntime* _runtime;
    };
}

void ClimateProtectionRuntime::Reset() noexcept
{
    _config.Reset();
    _cached = {};
    _lastWornForms.clear();
    _powerArmorKeyword = nullptr;
    _configLoaded = false;
    Invalidate();
}

void ClimateProtectionRuntime::Invalidate() noexcept
{
    _dirty.store(true, std::memory_order_release);
}

RadClimatePolicy::ProtectionSample ClimateProtectionRuntime::Sample(RE::PlayerCharacter* player)
{
    EnsureConfigLoaded();
    EnsureEventRegistration();

    if (!_dirty.exchange(false, std::memory_order_acq_rel))
        return _cached;

    if (!player || !player->biped)
    {
        Invalidate();
        return _cached;
    }

    _cached = Scan(player);
    return _cached;
}

void ClimateProtectionRuntime::EnsureConfigLoaded()
{
    if (_configLoaded)
        return;

    _config.Load("Data/F4SE/Plugins/FalloutAnomaly/climate_protection.json");
    if (auto* dataHandler = RE::TESDataHandler::GetSingleton())
    {
        auto* form = dataHandler->LookupForm(0x0004D8A1, "Fallout4.esm");
        if (form && form->GetFormType() == RE::ENUM_FORM_ID::kKYWD)
            _powerArmorKeyword = static_cast<RE::BGSKeyword*>(form);
    }
    if (!_powerArmorKeyword)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Climate] ArmorTypePower keyword unresolved; Power Armor climate floor disabled this session.");
    }
    _configLoaded = true;
}

void ClimateProtectionRuntime::EnsureEventRegistration()
{
    if (_eventsRegistered)
        return;

    if (auto* equipManager = RE::ActorEquipManager::GetSingleton())
    {
        static ClimateEquipSink sink(this);
        equipManager->RegisterSink(&sink);
        _eventsRegistered = true;
    }
}

RadClimatePolicy::ProtectionSample ClimateProtectionRuntime::Scan(RE::PlayerCharacter* player)
{
    RadClimatePolicy::ProtectionSample result{};
    if (!player || !player->biped)
        return result;

    std::unordered_map<std::uint32_t, WornArmor> piecesByForm;
    std::size_t powerArmorPieces = 0;
    bool powerArmorHelmet = false;
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
            if (_powerArmorKeyword && armor->HasKeyword(_powerArmorKeyword))
            {
                ++powerArmorPieces;
                if (Fills(armor, RE::BIPED_OBJECT::kHairTop))
                    powerArmorHelmet = true;
            }
        }
        if (const auto overrideValue = _config.ArmorOverride(armor))
            worn.sample.overrideInsulation01 = *overrideValue;
    }

    result.powerArmor = RadClimatePowerArmorPolicy::IsCompleteSuit(powerArmorHelmet, powerArmorPieces);

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
            logger->info("[Climate] Worn armor changed: {} unique armor record(s), powerArmor={}.",
                worn.size(), result.powerArmor);
            for (const auto& piece : worn)
            {
                auto automatic = piece.sample;
                automatic.overrideInsulation01 = -1.0f;
                const float automaticWarmth01 = RadClimateInsulationPolicy::PieceWarmth(automatic);
                const float effectiveWarmth01 = RadClimateInsulationPolicy::PieceWarmth(piece.sample);
                const float automaticContribution01 = RadClimateInsulationPolicy::PieceInsulation(automatic);
                const float effectiveContribution01 = RadClimateInsulationPolicy::PieceInsulation(piece.sample);
                const bool overridden = piece.sample.overrideInsulation01 >= 0.0f;
                auto* file = piece.armor->GetFile(0);
                if (file)
                {
                    logger->info(
                        "[Climate] armor {}|0x{:X} runtime=0x{:08X} warmth={:.0f}%->{:.0f}% contribution={:.0f}%->{:.0f}% override={} coverage=H{} T{} LA{} RA{} LL{} RL{}",
                        file->GetFilename(), piece.armor->GetLocalFormID(), piece.armor->GetFormID(),
                        automaticWarmth01 * 100.0f, effectiveWarmth01 * 100.0f,
                        automaticContribution01 * 100.0f, effectiveContribution01 * 100.0f, overridden,
                        piece.sample.coverage.head, piece.sample.coverage.torso,
                        piece.sample.coverage.leftArm, piece.sample.coverage.rightArm,
                        piece.sample.coverage.leftLeg, piece.sample.coverage.rightLeg);
                }
                else
                {
                    logger->info(
                        "[Climate] armor runtime=0x{:08X} warmth={:.0f}%->{:.0f}% contribution={:.0f}%->{:.0f}% override={} coverage=H{} T{} LA{} RA{} LL{} RL{}",
                        piece.armor->GetFormID(), automaticWarmth01 * 100.0f, effectiveWarmth01 * 100.0f,
                        automaticContribution01 * 100.0f, effectiveContribution01 * 100.0f, overridden,
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
