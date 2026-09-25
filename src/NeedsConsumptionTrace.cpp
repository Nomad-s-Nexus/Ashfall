#include "pch.h"
#include "NeedsConsumptionTrace.h"

#include "Hooks.h"
#include "NeedsTracePolicy.h"
#include <RE/A/ActorEquipManager.h>
#include <RE/A/ActorEquipManagerEvent.h>
#include <RE/A/AlchemyItem.h>
#include <RE/B/BGSObjectInstance.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESContainerChangedEvent.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESMagicEffectApplyEvent.h>
#include <RE/T/TESObjectREFR.h>

#include <atomic>
#include <chrono>

namespace
{
    constexpr const char* kIni = ".\\Data\\F4SE\\Plugins\\FalloutAnomalyRadiation.ini";

    std::atomic_bool g_active{ false };
    std::atomic<std::uint64_t> g_sequence{ 0 };
    const auto g_epoch = std::chrono::steady_clock::now();

    struct Stamp
    {
        std::uint64_t sequence;
        unsigned long thread;
        long long ms;
    };

    Stamp NextStamp() noexcept
    {
        return {
            g_sequence.fetch_add(1, std::memory_order_relaxed) + 1,
            ::GetCurrentThreadId(),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_epoch).count()
        };
    }

    std::uint32_t PlayerFormID() noexcept
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        return player ? player->GetFormID() : 0;
    }

    class EquipSink final : public RE::BSTEventSink<RE::ActorEquipManagerEvent::Event>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::ActorEquipManagerEvent::Event& event,
            RE::BSTEventSource<RE::ActorEquipManagerEvent::Event>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || event.actorAffected != player)
                return RE::BSEventNotifyControl::kContinue;

            auto* object = event.itemAffected ? event.itemAffected->object : nullptr;
            const auto stamp = NextStamp();
            const bool alch = object && object->GetFormType() == RE::ENUM_FORM_ID::kALCH;
            const auto* potion = alch ? static_cast<const RE::AlchemyItem*>(object) : nullptr;
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->info("[NeedsTrace] seq={} tid={} t={} kind=equip type={} item=0x{:08X} formType={} stack={} alch={} food={} poison={}",
                    stamp.sequence, stamp.thread, stamp.ms,
                    event.changeType == RE::ActorEquipManagerEvent::Type::kUnequip ? "unequip" : "equip",
                    object ? object->GetFormID() : 0, object ? static_cast<int>(object->GetFormType()) : -1,
                    event.stackID, alch, potion && potion->IsFood(), potion && potion->IsPoison());
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    class ContainerSink final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESContainerChangedEvent& event,
            RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
        {
            const auto direction = RadNeedsTracePolicy::ClassifyContainerChange(
                event.oldContainerFormID, event.newContainerFormID, PlayerFormID());
            if (direction == RadNeedsTracePolicy::ContainerDirection::kUnrelated)
                return RE::BSEventNotifyControl::kContinue;

            const auto stamp = NextStamp();
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->info("[NeedsTrace] seq={} tid={} t={} kind=container dir={} old=0x{:08X} new=0x{:08X} base=0x{:08X} count={} ref=0x{:08X} unique={}",
                    stamp.sequence, stamp.thread, stamp.ms, RadNeedsTracePolicy::DirectionName(direction),
                    event.oldContainerFormID, event.newContainerFormID, event.baseObjectFormID,
                    event.itemCount, event.referenceFormID, event.uniqueID);
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    class EffectSink final : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESMagicEffectApplyEvent& event,
            RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || event.target.get() != player)
                return RE::BSEventNotifyControl::kContinue;

            const auto stamp = NextStamp();
            auto* caster = event.caster.get();
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->info("[NeedsTrace] seq={} tid={} t={} kind=effect mgef=0x{:08X} caster=0x{:08X}",
                    stamp.sequence, stamp.thread, stamp.ms, event.magicEffectFormID,
                    caster ? caster->GetFormID() : 0);
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    template <class Event>
    bool Register(RE::BSTEventSource<Event>* source, RE::BSTEventSink<Event>* sink, const char* name)
    {
        if (!source)
        {
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->warn("[NeedsTrace] {} event source unavailable; that stream is not traced.", name);
            return false;
        }
        source->RegisterSink(sink);
        return true;
    }
}

bool NeedsConsumptionTrace::Active() noexcept
{
    return g_active.load(std::memory_order_acquire);
}

bool NeedsConsumptionTrace::ActivateOnGameThread()
{
    if (g_active.load(std::memory_order_acquire))
        return true;
    if (!Hooks::IsGameMainThread())
        return false;

    const int setting = static_cast<int>(::GetPrivateProfileIntA("NeedsTrace", "Enabled", 0, kIni));
    if (!RadNeedsTracePolicy::IsTraceEnabledSetting(setting))
        return false;

    static EquipSink equipSink;
    static ContainerSink containerSink;
    static EffectSink effectSink;

    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    const bool equip = Register<RE::ActorEquipManagerEvent::Event>(equipManager, &equipSink, "ActorEquipManager");
    const bool container = Register(RE::TESContainerChangedEvent::GetEventSource(), &containerSink, "TESContainerChanged");
    const bool effect = Register(RE::TESMagicEffectApplyEvent::GetEventSource(), &effectSink, "TESMagicEffectApply");

    g_active.store(true, std::memory_order_release);
    if (auto logger = spdlog::get("FalloutAnomaly"))
        logger->info("[NeedsTrace] diagnostic consumption trace active (equip={}, container={}, effect={}); observation only.",
            equip, container, effect);
    return true;
}
