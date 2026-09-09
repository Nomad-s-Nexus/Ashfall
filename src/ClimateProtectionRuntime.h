#pragma once

#include "ClimatePolicy.h"
#include "ClimateProtectionConfig.h"

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
    [[nodiscard]] RadClimatePolicy::ProtectionSample Scan(RE::PlayerCharacter* player) const;

    ClimateProtectionConfig _config;
    RadClimatePolicy::ProtectionSample _cached{};
    bool _configLoaded{ false };
    int _refreshTick{ 0 };
};
