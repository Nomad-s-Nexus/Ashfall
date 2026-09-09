#pragma once

#include "ClimatePolicy.h"
#include "ClimateProtectionConfig.h"

#include <cstdint>
#include <vector>

namespace RE
{
    class PlayerCharacter;
}

class ClimateProtectionRuntime
{
public:
    void Reset() noexcept;
    [[nodiscard]] RadClimatePolicy::ProtectionSample Sample(RE::PlayerCharacter* player);
    [[nodiscard]] float LastInsulation01() const noexcept { return _cached.insulation01; }

private:
    void EnsureConfigLoaded();
    [[nodiscard]] RadClimatePolicy::ProtectionSample Scan(RE::PlayerCharacter* player);

    ClimateProtectionConfig _config;
    RadClimatePolicy::ProtectionSample _cached{};
    std::vector<std::uint32_t> _lastWornForms;
    bool _configLoaded{ false };
    int _refreshTick{ 0 };
};
