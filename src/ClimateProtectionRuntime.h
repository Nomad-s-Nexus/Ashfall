#pragma once

#include "ClimatePolicy.h"
#include "ClimateProtectionConfig.h"

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
    [[nodiscard]] RadClimatePolicy::ProtectionSample Sample(RE::PlayerCharacter* player);
    [[nodiscard]] float LastInsulation01() const noexcept { return _cached.insulation01; }
    [[nodiscard]] bool LastPowerArmor() const noexcept { return _cached.powerArmor; }

private:
    void EnsureConfigLoaded();
    [[nodiscard]] RadClimatePolicy::ProtectionSample Scan(RE::PlayerCharacter* player);

    ClimateProtectionConfig _config;
    RadClimatePolicy::ProtectionSample _cached{};
    std::vector<std::uint32_t> _lastWornForms;
    RE::BGSKeyword* _powerArmorKeyword{ nullptr };
    bool _configLoaded{ false };
    int _refreshTick{ 0 };
};
