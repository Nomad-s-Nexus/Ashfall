#include "pch.h"
#include "WaterBottleFill.h"

#include "Hooks.h"
#include "RadWaterFillPolicy.h"

#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BSTEvent.h>
#include <RE/D/DifficultyLevel.h>
#include <RE/I/ITEM_REMOVE_REASON.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/SendHUDMessage.h>
#include <RE/T/TESActivateEvent.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESFurniture.h>
#include <RE/T/TESObjectACTI.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectREFR.h>

#include <array>
#include <atomic>
#include <chrono>

#include <Windows.h>

namespace
{
    constexpr const char* kIni = ".\\Data\\F4SE\\Plugins\\FalloutAnomalyRadiation.ini";
    constexpr auto        kSameTargetDebounce = std::chrono::milliseconds(400);

    struct Config
    {
        bool enabled{ true };
        bool skipVanillaSurvival{ true };
        bool requireModifier{ true };
    };

    std::atomic_bool g_active{ false };
    Config           g_config{};
    bool             g_configLoaded = false;
    bool             g_sinkRegistered = false;
    bool             g_formsResolved = false;
    bool             g_warnedUnresolved = false;
    bool             g_warnedNoSource = false;
    bool             g_warnedDisabled = false;

    std::array<RE::TESBoundObject*, RadWaterFillPolicy::kEmptyBottles.size()> g_bottles{};
    std::size_t                                                               g_bottleCount = 0;
    std::array<std::uint32_t, RadWaterFillPolicy::kWaterSources.size()>        g_sourceIDs{};
    std::size_t                                                               g_sourceCount = 0;
    std::array<std::uint32_t, RadWaterFillPolicy::kMaintainedVaultCells.size()> g_maintainedCellIDs{};
    std::size_t                                                                g_maintainedCellCount = 0;
    RE::BGSKeyword*                                                            g_waterPumpKeyword = nullptr;
    RE::TESBoundObject*                                                        g_filledWater = nullptr;
    RE::TESBoundObject*                                                        g_purifiedWater = nullptr;

    std::uint32_t                         g_lastTargetID = 0;
    std::chrono::steady_clock::time_point g_lastFill{};

    // Constraint: the INI is read once per load; no disk access on activation.
    void LoadConfig()
    {
        if (g_configLoaded)
            return;
        g_configLoaded = true;
        g_config.enabled = ::GetPrivateProfileIntA("Water", "Enabled", 1, kIni) != 0;
        g_config.skipVanillaSurvival = ::GetPrivateProfileIntA("Water", "SkipWhenVanillaSurvival", 1, kIni) != 0;
        g_config.requireModifier = ::GetPrivateProfileIntA("Water", "RequireModifier", 1, kIni) != 0;
    }

    // Constraint: an unresolved form is skipped, never guessed -- a wrong form ID would remove the wrong item.
    RE::TESBoundObject* AsBoundObject(RE::TESForm* a_form)
    {
        if (!a_form)
            return nullptr;
        switch (a_form->GetFormType())
        {
        case RE::ENUM_FORM_ID::kALCH:
        case RE::ENUM_FORM_ID::kMISC:
        case RE::ENUM_FORM_ID::kINGR:
        case RE::ENUM_FORM_ID::kBOOK:
            return static_cast<RE::TESBoundObject*>(a_form);
        default:
            return nullptr;
        }
    }

    const RE::BGSKeywordForm* KeywordsOf(const RE::TESBoundObject* a_base)
    {
        if (!a_base)
            return nullptr;
        switch (a_base->GetFormType())
        {
        case RE::ENUM_FORM_ID::kACTI:
            return static_cast<const RE::TESObjectACTI*>(a_base);
        case RE::ENUM_FORM_ID::kFURN:
            return static_cast<const RE::TESFurniture*>(a_base);
        default:
            return nullptr;
        }
    }

    bool ResolveForms()
    {
        if (g_formsResolved)
            return true;

        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data)
            return false;

        g_bottleCount = 0;
        std::size_t missingBottles = 0;
        for (const auto& ref : RadWaterFillPolicy::kEmptyBottles)
        {
            auto* bottle = AsBoundObject(data->LookupForm(ref.localID, ref.plugin));
            if (!bottle)
            {
                ++missingBottles;
                continue;
            }
            g_bottles[g_bottleCount++] = bottle;
        }

        g_sourceCount = 0;
        std::size_t missingSources = 0;
        for (const auto& ref : RadWaterFillPolicy::kWaterSources)
        {
            auto* form = data->LookupForm(ref.localID, ref.plugin);
            if (!form)
            {
                ++missingSources;
                continue;
            }
            g_sourceIDs[g_sourceCount++] = form->GetFormID();
        }

        g_maintainedCellCount = 0;
        std::size_t missingCells = 0;
        for (const auto& ref : RadWaterFillPolicy::kMaintainedVaultCells)
        {
            auto* cell = data->LookupForm(ref.localID, ref.plugin);
            if (!cell || cell->GetFormType() != RE::ENUM_FORM_ID::kCELL)
            {
                ++missingCells;
                continue;
            }
            g_maintainedCellIDs[g_maintainedCellCount++] = cell->GetFormID();
        }

        // Constraint: TESForm::As<T> is unlinkable here, so resolve non-template then static_cast after the type check.
        auto* keywordForm =
            data->LookupForm(RadWaterFillPolicy::kWaterPumpKeyword.localID, RadWaterFillPolicy::kWaterPumpKeyword.plugin);
        g_waterPumpKeyword = (keywordForm && keywordForm->GetFormType() == RE::ENUM_FORM_ID::kKYWD)
                                 ? static_cast<RE::BGSKeyword*>(keywordForm)
                                 : nullptr;
        g_filledWater = AsBoundObject(
            data->LookupForm(RadWaterFillPolicy::kFilledWater.localID, RadWaterFillPolicy::kFilledWater.plugin));
        g_purifiedWater = AsBoundObject(
            data->LookupForm(RadWaterFillPolicy::kPurifiedWater.localID, RadWaterFillPolicy::kPurifiedWater.plugin));

        auto logger = spdlog::get("FalloutAnomaly");
        const bool critical = g_bottleCount != 0 && g_sourceCount != 0 && g_waterPumpKeyword && g_filledWater;
        if (!critical)
        {
            // Constraint: warn once and stay OFF when the vanilla forms are incomplete; never fill with a guessed form.
            if (!g_warnedUnresolved)
            {
                g_warnedUnresolved = true;
                if (logger)
                    logger->warn("[Water] bottle / water source / pump keyword / Dirty Water forms incomplete; bottle filling is OFF.");
            }
            return false;
        }

        // Constraint: the resolution summary runs once because a completed resolve latches g_formsResolved above.
        if (logger)
        {
            logger->info("[Water] resolved {}/{} empty bottle form(s), {}/{} water source(s), {}/{} maintained Vault 81 cell(s), pumpKeyword={}, dirty={}, purified={}",
                g_bottleCount, RadWaterFillPolicy::kEmptyBottles.size(), g_sourceCount,
                RadWaterFillPolicy::kWaterSources.size(), g_maintainedCellCount,
                RadWaterFillPolicy::kMaintainedVaultCells.size(), g_waterPumpKeyword != nullptr,
                g_filledWater != nullptr, g_purifiedWater != nullptr);
            if (missingBottles != 0 || missingSources != 0 || missingCells != 0)
                logger->info("[Water] skipped {} bottle, {} source and {} Vault 81 cell entry(ies) whose plugin is not loaded.",
                    missingBottles, missingSources, missingCells);
            if (g_maintainedCellCount < RadWaterFillPolicy::kMaintainedVaultCells.size())
                logger->warn("[Water] only {}/{} maintained Vault 81 cell(s) resolved; an unresolved cell stays dirty.",
                    g_maintainedCellCount, RadWaterFillPolicy::kMaintainedVaultCells.size());
            if (!g_purifiedWater)
                logger->warn("[Water] Purified Water unresolved; Vault 81 fills fall back to Dirty Water.");
        }

        g_formsResolved = true;
        return true;
    }

    std::size_t FirstBottleHeld(RE::PlayerCharacter* a_player)
    {
        std::array<std::uint32_t, RadWaterFillPolicy::kEmptyBottles.size()> counts{};
        for (std::size_t i = 0; i < g_bottleCount; ++i)
            counts[i] = a_player->GetInventoryObjectCount(g_bottles[i]);
        return RadWaterFillPolicy::FirstAvailableBottle(counts.data(), g_bottleCount);
    }

    void FillOneBottle(
        RE::PlayerCharacter* a_player,
        RE::TESObjectREFR* a_target,
        std::size_t a_bottle,
        RadWaterFillPolicy::FilledWater a_water)
    {
        const bool purified =
            a_water == RadWaterFillPolicy::FilledWater::kPurified && g_purifiedWater != nullptr;
        auto* output = purified ? g_purifiedWater : g_filledWater;

        g_lastTargetID = a_target->GetFormID();
        g_lastFill = std::chrono::steady_clock::now();

        const auto heldBefore = a_player->GetInventoryObjectCount(g_bottles[a_bottle]);
        RE::TESObjectREFR::RemoveItemData remove(g_bottles[a_bottle], 1);
        a_player->RemoveItem(remove);
        if (a_player->GetInventoryObjectCount(g_bottles[a_bottle]) >= heldBefore)
        {
            // Constraint: never add water when the engine refused to take the bottle.
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->warn("[Water] engine refused to remove empty bottle 0x{:08X}; no water was added.", g_bottles[a_bottle]->GetFormID());
            return;
        }
        a_player->AddObjectToContainer(output, nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);

        // Constraint: every fill is reported, so a consumed bottle is never silent.
        RE::SendHUDMessage::ShowHUDMessage(
            purified ? "Filled a bottle with purified water." : "Filled a bottle with dirty water.", "", false, false);

        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->debug("[Water] filled one {} bottle from 0x{:08X} (empty bottle 0x{:08X}).",
                purified ? RadWaterFillPolicy::kPurifiedWater.editorID : RadWaterFillPolicy::kFilledWater.editorID,
                g_lastTargetID, g_bottles[a_bottle]->GetFormID());
    }

    class ActivateSink final : public RE::BSTEventSink<RE::TESActivateEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESActivateEvent& a_event,
            RE::BSTEventSource<RE::TESActivateEvent>*) override
        {
            if (!g_active.load(std::memory_order_acquire))
                return RE::BSEventNotifyControl::kContinue;

            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* target = a_event.objectActivated.get();
            if (!player || !target || a_event.actionRef.get() != player)
                return RE::BSEventNotifyControl::kContinue;

            auto* base = target->GetObjectReference();
            const auto* keywords = KeywordsOf(base);
            const bool  pump = keywords && g_waterPumpKeyword && keywords->HasKeyword(g_waterPumpKeyword);
            const bool  waterSource = RadWaterFillPolicy::IsWaterSource(
                base ? base->GetFormID() : 0, pump, g_sourceIDs.data(), g_sourceCount);

            const auto bottle = waterSource ? FirstBottleHeld(player) : RadWaterFillPolicy::kBottleNotFound;
            const bool vanillaSurvival =
                g_config.skipVanillaSurvival && player->GetDifficultyLevel() == RE::DifficultyLevel::kTrueSurvival;
            const bool modifierHeld = !g_config.requireModifier || player->IsSneaking();
            const auto decision = RadWaterFillPolicy::Decide(g_config.enabled, waterSource, vanillaSurvival, modifierHeld,
                bottle == RadWaterFillPolicy::kBottleNotFound ? 0 : 1);
            if (decision != RadWaterFillPolicy::Decision::kFill)
                return RE::BSEventNotifyControl::kContinue;

            // Constraint: a duplicated activate event on the same ref must not take two bottles.
            const auto now = std::chrono::steady_clock::now();
            if (target->GetFormID() == g_lastTargetID && now - g_lastFill < kSameTargetDebounce)
                return RE::BSEventNotifyControl::kContinue;

            const auto* cell = player->GetParentCell();
            const bool  maintained = RadWaterFillPolicy::IsMaintainedVaultCell(
                cell ? cell->GetFormID() : 0, g_maintainedCellIDs.data(), g_maintainedCellCount);
            FillOneBottle(player, target, bottle, RadWaterFillPolicy::FilledWaterFor(maintained));
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    ActivateSink g_sink;
}

bool WaterBottleFill::ActivateOnGameThread()
{
    if (g_active.load(std::memory_order_acquire))
        return true;
    if (!Hooks::IsGameMainThread())
        return false;

    LoadConfig();
    auto logger = spdlog::get("FalloutAnomaly");

    if (!g_config.enabled)
    {
        // Constraint: a disabled feature never registers the sink or resolves the forms.
        if (!g_warnedDisabled)
        {
            g_warnedDisabled = true;
            if (logger)
                logger->info("[Water] disabled by [Water] Enabled=0.");
        }
        return false;
    }

    auto* source = RE::TESActivateEvent::GetEventSource();
    if (!source)
    {
        if (!g_warnedNoSource)
        {
            g_warnedNoSource = true;
            if (logger)
                logger->warn("[Water] TESActivate event source unavailable; bottle filling is OFF.");
        }
        return false;
    }

    if (!ResolveForms())
        return false;

    if (!g_sinkRegistered)
    {
        source->RegisterSink(&g_sink);
        g_sinkRegistered = true;
    }

    g_active.store(true, std::memory_order_release);
    if (logger)
        logger->info("[Water] bottle filling active: {} empty bottle form(s), {} water source(s), {} maintained Vault 81 cell(s), dirty={}, purified={}, requireModifier={}, skipVanillaSurvival={}.",
            g_bottleCount, g_sourceCount, g_maintainedCellCount, RadWaterFillPolicy::kFilledWater.editorID,
            g_purifiedWater ? RadWaterFillPolicy::kPurifiedWater.editorID : "unresolved", g_config.requireModifier,
            g_config.skipVanillaSurvival);
    return true;
}
