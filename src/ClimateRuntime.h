#pragma once

#include "ClimatePolicy.h"
#include "ClimateProtectionRuntime.h"
#include "ClimateShelterRuntime.h"

class ClimateRuntime
{
public:
    static ClimateRuntime& GetSingleton()
    {
        static ClimateRuntime instance;
        return instance;
    }

    void Update();
    void Reset();

    [[nodiscard]] const RadClimatePolicy::ClimateState& State() const noexcept { return _state; }
    [[nodiscard]] float LastAmbientC() const noexcept { return _lastAmbientC; }
    [[nodiscard]] bool LastRainy() const noexcept { return _lastRainy; }
    [[nodiscard]] bool LastIndoors() const noexcept { return _lastIndoors; }
    [[nodiscard]] float LastInsulation01() const noexcept { return _protection.LastInsulation01(); }
    [[nodiscard]] bool LastRoofed() const noexcept { return _shelter.LastRoofed(); }

private:
    ClimateRuntime() = default;

    RadClimatePolicy::ClimateState _state{};
    ClimateProtectionRuntime _protection;
    ClimateShelterRuntime _shelter;
    RadClimatePolicy::HypothermiaStage _lastLoggedStage{ RadClimatePolicy::HypothermiaStage::kNormal };
    float _lastAmbientC{ RadClimatePolicy::kNeutralAmbientC };
    bool _lastRainy{ false };
    bool _lastIndoors{ false };
    int _statusTick{ 0 };
};
