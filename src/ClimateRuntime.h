#pragma once

#include "ClimatePolicy.h"

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

private:
    ClimateRuntime() = default;

    RadClimatePolicy::ClimateState _state{};
    RadClimatePolicy::HypothermiaStage _lastLoggedStage{ RadClimatePolicy::HypothermiaStage::kNormal };
    float _lastAmbientC{ RadClimatePolicy::kNeutralAmbientC };
    bool _lastRainy{ false };
    bool _lastIndoors{ false };
    int _statusTick{ 0 };
};
