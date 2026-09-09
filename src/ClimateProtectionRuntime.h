#pragma once

#include "ClimatePolicy.h"
#include "ClimateProtectionConfig.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace RE
{
    class BGSKeyword;
    class PlayerCharacter;
}

class ClimateProtectionRuntime
{
public:
    void Reset() noexcept;
    void Invalidate() noexcept;
    [[nodiscard]] RadClimatePolicy::ProtectionSample Sample(RE::PlayerCharacter* player);
    [[nodiscard]] float LastInsulation01() const noexcept { return _cached.insulation01; }
    [[nodiscard]] bool LastPowerArmor() const noexcept { return _cached.powerArmor; }

private:
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
