#pragma once

#include "ClimatePolicy.h"
#include "ClimateProtectionConfig.h"

#include <RE/A/ActorEquipManagerEvent.h>
#include <RE/B/BSTEvent.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace RE
{
    class BGSKeyword;
    class PlayerCharacter;
}

class ClimateProtectionRuntime :
    public RE::BSTEventSink<RE::ActorEquipManagerEvent::Event>
{
public:
    void Reset() noexcept;
    [[nodiscard]] RadClimatePolicy::ProtectionSample Sample(RE::PlayerCharacter* player);
    [[nodiscard]] float LastInsulation01() const noexcept { return _cached.insulation01; }
    [[nodiscard]] bool LastPowerArmor() const noexcept { return _cached.powerArmor; }

private:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::ActorEquipManagerEvent::Event& event,
        RE::BSTEventSource<RE::ActorEquipManagerEvent::Event>* source) override;
    void EnsureConfigLoaded();
    void EnsureEventRegistration();
    [[nodiscard]] RadClimatePolicy::ProtectionSample Scan(RE::PlayerCharacter* player);

    ClimateProtectionConfig _config;
    RadClimatePolicy::ProtectionSample _cached{};
    std::vector<std::uint32_t> _lastWornForms;
    RE::BGSKeyword* _powerArmorKeyword{ nullptr };
    std::atomic<bool> _dirty{ true };
    bool _configLoaded{ false };
    bool _eventsRegistered{ false };
};
