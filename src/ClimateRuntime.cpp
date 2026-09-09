#include "pch.h"
#include "ClimateRuntime.h"
#include "ClimateEnvironmentPolicy.h"
#include "ClimateStagePolicy.h"

#include <RE/C/Calendar.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/Sky.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESWeather.h>

namespace
{
    float GameDays() noexcept
    {
        auto* cal = RE::Calendar::GetSingleton();
        if (cal && cal->gameDaysPassed)
            return cal->gameDaysPassed->GetValue();
        return 0.0f;
    }

    bool IsRainy(RE::TESWeather* weather) noexcept
    {
        if (!weather)
            return false;
        const auto flags = weather->weatherData[static_cast<int>(RE::TESWeather::WeatherData::kFlags)];
        return (flags & static_cast<std::int8_t>(RE::TESWeather::WeatherDataFlags::kRainy)) != 0;
    }

    std::int8_t WindByte(RE::TESWeather* weather) noexcept
    {
        if (!weather)
            return 0;
        return weather->weatherData[static_cast<int>(RE::TESWeather::WeatherData::kWindSpeed)];
    }

    const char* StageName(RadClimatePolicy::HypothermiaStage stage) noexcept
    {
        using enum RadClimatePolicy::HypothermiaStage;
        switch (stage)
        {
        case kChilled: return "chilled";
        case kMild: return "mild";
        case kModerate: return "moderate";
        case kSevere: return "severe";
        case kCritical: return "critical";
        case kNormal:
        default: return "normal";
        }
    }
}

void ClimateRuntime::Update()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player)
        return;

    auto* cell = player->GetParentCell();
    if (!cell)
        return;

    auto* sky = RE::Sky::GetSingleton();
    auto* weather = sky ? sky->currentWeather : nullptr;
    const bool indoors = cell->IsInterior();
    const bool rainy = !indoors && IsRainy(weather);
    const auto shelter = _shelter.Sample(player);

    RadClimatePolicy::EnvironmentSample environment{};
    environment.indoors = indoors;
    environment.shelter01 = shelter.thermal01;
    environment.precipitationShelter01 = shelter.precipitation01;
    environment.precipitation01 = rainy ? 1.0f : 0.0f;
    environment.submerged = player->IsSwimming();
    environment.windMps = indoors ? 0.0f : RadClimateEnvironmentPolicy::WindProxyMps(WindByte(weather));
    environment.ambientC = RadClimateEnvironmentPolicy::AmbientTemperatureC(indoors, rainy, GameDays());
    environment.heat01 = _heat.Sample(player);

    const auto protection = _protection.Sample(player);
    const auto result = RadClimatePolicy::Step(_state, environment, protection, 0.25f);
    auto nextState = result.state;
    nextState.stage = RadClimateStagePolicy::NextStage(_state.stage, nextState.coreC);
    _state = nextState;
    _lastAmbientC = environment.ambientC;
    _lastRainy = rainy;
    _lastIndoors = indoors;

    if (_state.stage != _lastLoggedStage)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Climate] stage={} core={:.2f}C ambient={:.1f}C wet={:.0f}% wind={:.1f} insulation={:.0f}% PA={} shelter={:.0f}% heat={:.0f}% roofed={}",
                StageName(_state.stage), _state.coreC, _lastAmbientC, _state.wetness01 * 100.0f,
                environment.windMps, protection.insulation01 * 100.0f, protection.powerArmor,
                environment.shelter01 * 100.0f, environment.heat01 * 100.0f, _shelter.LastRoofed());
        _lastLoggedStage = _state.stage;
    }

    if (++_statusTick >= 32)
    {
        _statusTick = 0;
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->debug("[Climate] core={:.2f}C ambient={:.1f}C wet={:.0f}% wind={:.1f} insulation={:.0f}% PA={} shelter={:.0f}% rainCover={:.0f}% heat={:.0f}% roofed={} indoors={} rainy={} swimming={} stage={}",
                _state.coreC, _lastAmbientC, _state.wetness01 * 100.0f, environment.windMps,
                protection.insulation01 * 100.0f, protection.powerArmor,
                environment.shelter01 * 100.0f, environment.precipitationShelter01 * 100.0f,
                environment.heat01 * 100.0f, _shelter.LastRoofed(), _lastIndoors, _lastRainy,
                environment.submerged, StageName(_state.stage));
    }
}

void ClimateRuntime::Reset()
{
    _state = {};
    _protection.Reset();
    _shelter.Reset();
    _heat.Reset();
    _lastLoggedStage = RadClimatePolicy::HypothermiaStage::kNormal;
    _lastAmbientC = RadClimatePolicy::kNeutralAmbientC;
    _lastRainy = false;
    _lastIndoors = false;
    _statusTick = 0;
}

RadClimateSerialization::PersistedState ClimateRuntime::SnapshotForSave() const noexcept
{
    return RadClimateSerialization::Snapshot(_state);
}

void ClimateRuntime::RestoreFromSave(const RadClimateSerialization::PersistedState& state) noexcept
{
    _state = RadClimateSerialization::Restore(state);
    _lastLoggedStage = _state.stage;
}
