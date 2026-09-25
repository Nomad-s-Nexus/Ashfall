#pragma once

#include "ClimateHeatRuntime.h"
#include "ClimatePolicy.h"
#include "ClimateProtectionRuntime.h"
#include "ClimateSerializationPolicy.h"
#include "ClimateShelterRuntime.h"
#include "ClimateWeatherConfig.h"

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
    [[nodiscard]] RadClimateSerialization::PersistedState SnapshotForSave() const noexcept;
    void RestoreFromSave(const RadClimateSerialization::PersistedState& state) noexcept;

    [[nodiscard]] const RadClimatePolicy::ClimateState& State() const noexcept { return _state; }
    [[nodiscard]] float LastAmbientC() const noexcept { return _lastAmbientC; }
    [[nodiscard]] bool LastRainy() const noexcept { return _lastRainy; }
    [[nodiscard]] bool LastIndoors() const noexcept { return _lastIndoors; }
    [[nodiscard]] float LastInsulation01() const noexcept { return _protection.LastInsulation01(); }
    [[nodiscard]] float LastEffectiveInsulation01() const noexcept { return _lastEffectiveInsulation01; }
    [[nodiscard]] bool LastPowerArmor() const noexcept { return _protection.LastPowerArmor(); }
    [[nodiscard]] const std::vector<ClimateProtectionRuntime::ArmorInfo>& LastArmor() const noexcept { return _protection.LastArmor(); }
    [[nodiscard]] bool LastRoofed() const noexcept { return _shelter.LastRoofed(); }
    [[nodiscard]] float LastHeat01() const noexcept { return _heat.LastHeat01(); }
    [[nodiscard]] float PrevGameDays() const noexcept { return _prevGameDays; }

private:
    ClimateRuntime() = default;

    RadClimatePolicy::ClimateState _state{};
    ClimateProtectionRuntime _protection;
    ClimateShelterRuntime _shelter;
    ClimateHeatRuntime _heat;
    ClimateWeatherConfig _weatherConfig;
    RadClimatePolicy::HypothermiaStage _lastLoggedStage{ RadClimatePolicy::HypothermiaStage::kNormal };
    float _lastAmbientC{ RadClimatePolicy::kNeutralAmbientC };
    float _lastEffectiveInsulation01{ 0.0f };
    float _prevGameDays{ -1.0f };
    bool _lastRainy{ false };
    bool _lastIndoors{ false };
    bool _weatherConfigLoaded{ false };
    int _statusTick{ 0 };
};
