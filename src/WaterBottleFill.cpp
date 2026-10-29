#include "pch.h"
#include "WaterBottleFill.h"

#include "Hooks.h"
#include "RadWaterFillPolicy.h"

#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BSTEvent.h>
#include <RE/I/ITEM_REMOVE_REASON.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESActivateEvent.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESFurniture.h>
#include <RE/T/TESObjectACTI.h>
#include <RE/T/TESObjectREFR.h>

#include <array>
#include <atomic>
#include <chrono>

#include <Windows.h>

namespace
{
    constexpr const char* kIni = ".\\Data\\F4SE\\Plugins\\FalloutAnomalyRadiation.ini";
    constexpr auto        kSameTargetDebounce = std::chrono::milliseconds(400);

    std::atomic_bool g_active{ false };
    bool             g_sinkRegistered = false;
    bool             g_warnedUnresolved = false;

    std::array<RE::TESBoundObject*, RadWaterFillPolicy::kEmptyBottles.size()> g_bottles{};
    std::size_t                                                                g_bottleCount = 0;
    std::array<std::uint32_t, RadWaterFillPolicy::kWaterSources.size()>         g_sourceIDs{};
    std::size_t                                                                g_sourceCount = 0;
    RE::BGSKeyword*                                                            g_waterPumpKeyword = nullptr;
    RE::TESBoundObject*                                                        g_filledWater = nullptr;

    std::uint32_t                          g_lastTargetID = 0;
    std::chrono::steady_clock::time_point  g_lastFill{};

    bool EnabledInIni()
    {
        return ::GetPrivateProfileIntA("Water", "Enabled", 1, kIni) != 0;
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

    void ResolveForms(RE::TESDataHandler* a_data)
    {
        if (!a_data)
            return;

        g_bottleCount = 0;
        std::size_t missingBottles = 0;
        for (const auto& ref : RadWaterFillPolicy::kEmptyBottles)
        {
            auto* bottle = AsBoundObject(a_data->LookupForm(ref.localID, ref.plugin));
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
            auto* form = a_data->LookupForm(ref.localID, ref.plugin);
            if (!form)
            {
                ++missingSources;
                continue;
            }
            g_sourceIDs[g_sourceCount++] = form->GetFormID();
        }

        // Constraint: TESForm::As<T> is unlinkable here, so resolve non-template then static_cast after the type check.
        auto* keywordForm =
            a_data->LookupForm(RadWaterFillPolicy::kWaterPumpKeyword.localID, RadWaterFillPolicy::kWaterPumpKeyword.plugin);
        g_waterPumpKeyword = (keywordForm && keywordForm->GetFormType() == RE::ENUM_FORM_ID::kKYWD)
                                 ? static_cast<RE::BGSKeyword*>(keywordForm)
                                 : nullptr;
        g_filledWater = AsBoundObject(
            a_data->LookupForm(RadWaterFillPolicy::kFilledWater.localID, RadWaterFillPolicy::kFilledWater.plugin));

        if (auto logger = spdlog::get("FalloutAnomaly"))
        {
            logger->info("[Water] resolved {}/{} empty bottle form(s), {}/{} water source(s), pumpKeyword={}, output={}",
                g_bottleCount, RadWaterFillPolicy::kEmptyBottles.size(), g_sourceCount,
                RadWaterFillPolicy::kWaterSources.size(), g_waterPumpKeyword != nullptr,
                g_filledWater != nullptr);
            if (missingBottles != 0 || missingSources != 0)
                logger->info("[Water] skipped {} bottle and {} source entry(ies) whose plugin is not loaded.",
                    missingBottles, missingSources);
        }
    }

    std::size_t FirstBottleHeld(RE::PlayerCharacter* a_player)
    {
        std::array<std::uint32_t, RadWaterFillPolicy::kEmptyBottles.size()> counts{};
        for (std::size_t i = 0; i < g_bottleCount; ++i)
            counts[i] = a_player->GetInventoryObjectCount(g_bottles[i]);
        return RadWaterFillPolicy::FirstAvailableBottle(counts.data(), g_bottleCount);
    }

    void FillOneBottle(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_target, std::size_t a_bottle)
    {
        RE::TESObjectREFR::RemoveItemData remove(g_bottles[a_bottle], 1);
        a_player->RemoveItem(remove);
        a_player->AddObjectToContainer(g_filledWater, nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);

        g_lastTargetID = a_target->GetFormID();
        g_lastFill = std::chrono::steady_clock::now();
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->debug("[Water] filled one {} from 0x{:08X} (bottle 0x{:08X}).", RadWaterFillPolicy::kFilledWater.editorID,
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
            const auto decision = RadWaterFillPolicy::Decide(
                EnabledInIni(), waterSource, bottle == RadWaterFillPolicy::kBottleNotFound ? 0 : 1);
            if (decision != RadWaterFillPolicy::Decision::kFill)
                return RE::BSEventNotifyControl::kContinue;

            // Constraint: a duplicated activate event on the same ref must not take two bottles.
            const auto now = std::chrono::steady_clock::now();
            if (target->GetFormID() == g_lastTargetID && now - g_lastFill < kSameTargetDebounce)
                return RE::BSEventNotifyControl::kContinue;

            FillOneBottle(player, target, bottle);
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    ActivateSink g_sink;
}

bool WaterBottleFill::Active() noexcept
{
    return g_active.load(std::memory_order_acquire);
}

bool WaterBottleFill::ActivateOnGameThread()
{
    if (g_active.load(std::memory_order_acquire))
        return true;
    if (!Hooks::IsGameMainThread())
        return false;

    auto* source = RE::TESActivateEvent::GetEventSource();
    if (!source)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Water] TESActivate event source unavailable; bottle filling is OFF.");
        return false;
    }

    ResolveForms(RE::TESDataHandler::GetSingleton());

    auto logger = spdlog::get("FalloutAnomaly");
    if (g_bottleCount == 0 || g_sourceCount == 0 || !g_waterPumpKeyword || !g_filledWater)
    {
        // Constraint: warn once and stay OFF when the vanilla forms are incomplete; never fill with a guessed form.
        if (!g_warnedUnresolved)
        {
            g_warnedUnresolved = true;
            if (logger)
                logger->warn("[Water] bottle / water source / Dirty Water forms incomplete; bottle filling is OFF.");
        }
        return false;
    }

    if (!g_sinkRegistered)
    {
        source->RegisterSink(&g_sink);
        g_sinkRegistered = true;
    }

    g_active.store(true, std::memory_order_release);
    if (logger)
        logger->info("[Water] bottle filling active: {} empty bottle form(s), {} water source(s), output={}.", g_bottleCount,
            g_sourceCount, RadWaterFillPolicy::kFilledWater.editorID);
    return true;
}
