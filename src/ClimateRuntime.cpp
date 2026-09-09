#include "pch.h"
#include "ClimateRuntime.h"
#include "ClimateEnvironmentPolicy.h"
#include "ClimateStagePolicy.h"
#include "RadHud.h"

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

    RadClimateEnvironmentPolicy::WeatherClass ClassifyWeather(RE::TESWeather* weather) noexcept
    {
        RadClimateEnvironmentPolicy::WeatherClass result{};
        if (!weather)
            return result;

        const auto raw = static_cast<std::uint8_t>(
            weather->weatherData[static_cast<int>(RE::TESWeather::WeatherData::kFlags)]);
        const auto has = [raw](RE::TESWeather::WeatherDataFlags flag) {
            return (raw & static_cast<std::uint8_t>(flag)) != 0;
        };
        result.pleasant = has(RE::TESWeather::WeatherDataFlags::kPleasant);
        result.cloudy = has(RE::TESWeather::WeatherDataFlags::kCloudy);
        result.rainy = has(RE::TESWeather::WeatherDataFlags::kRainy);
        result.snow = has(RE::TESWeather::WeatherDataFlags::kSnow);
        return result;
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

    if (!_weatherConfigLoaded)
    {
        _weatherConfig.Load("Data/F4SE/Plugins/FalloutAnomaly/climate_weather.json");
        _weatherConfigLoaded = true;
    }

    auto* sky = RE::Sky::GetSingleton();
    auto* weather = sky ? sky->currentWeather : nullptr;
    const bool indoors = cell->IsInterior();
    const auto weatherClass = indoors
        ? RadClimateEnvironmentPolicy::WeatherClass{}
        : ClassifyWeather(weather);
    const auto weatherOverride = (!indoors && weather)
        ? _weatherConfig.Override(weather->GetFormID())
        : std::nullopt;
    const auto shelter = _shelter.Sample(player);

    RadClimatePolicy::EnvironmentSample environment{};
    environment.indoors = indoors;
    environment.shelter01 = shelter.thermal01;
    environment.precipitationShelter01 = shelter.precipitation01;
    environment.precipitation01 = indoors
        ? 0.0f
        : RadClimateEnvironmentPolicy::PrecipitationIntensity01(weatherClass);
    if (weatherOverride && weatherOverride->precipitation01)
        environment.precipitation01 = *weatherOverride->precipitation01;
    environment.submerged = player->IsSwimming();
    environment.windMps = indoors ? 0.0f : RadClimateEnvironmentPolicy::WindProxyMps(WindByte(weather));
    environment.ambientC = RadClimateEnvironmentPolicy::AmbientTemperatureC(indoors, weatherClass, GameDays());
    if (weatherOverride)
        environment.ambientC += weatherOverride->temperatureOffsetC;
    environment.heat01 = _heat.Sample(player);
    environment.activity01 = player->DoGetSprinting() ? 1.0f : 0.0f;

    const auto protection = _protection.Sample(player);
    const auto result = RadClimatePolicy::Step(_state, environment, protection, 0.25f);
    auto nextState = result.state;
    nextState.stage = RadClimateStagePolicy::NextStage(_state.stage, nextState.coreC);
    _state = nextState;
    _lastAmbientC = environment.ambientC;
    _lastRainy = weatherClass.rainy;
    _lastIndoors = indoors;

    RadHud::GetSingleton().PushClimate(
        _state.coreC,
        _lastAmbientC,
        _state.wetness01,
        result.effectiveInsulation01,
        environment.heat01,
        static_cast<int>(_state.stage));

    if (_state.stage != _lastLoggedStage)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Climate] stage={} core={:.2f}C ambient={:.1f}C wet={:.0f}% wind={:.1f} insulation={:.0f}% PA={} shelter={:.0f}% heat={:.0f}% roofed={} sprinting={} snow={} weatherOverride={}",
                StageName(_state.stage), _state.coreC, _lastAmbientC, _state.wetness01 * 100.0f,
                environment.windMps, protection.insulation01 * 100.0f, protection.powerArmor,
                environment.shelter01 * 100.0f, environment.heat01 * 100.0f, _shelter.LastRoofed(),
                environment.activity01 > 0.0f, weatherClass.snow, weatherOverride.has_value());
        _lastLoggedStage = _state.stage;
    }

    if (++_statusTick >= 32)
    {
        _statusTick = 0;
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->debug("[Climate] core={:.2f}C ambient={:.1f}C wet={:.0f}% wind={:.1f} insulation={:.0f}% PA={} shelter={:.0f}% rainCover={:.0f}% heat={:.0f}% roofed={} indoors={} pleasant={} cloudy={} rainy={} snow={} swimming={} sprinting={} weatherOverride={} stage={}",
                _state.coreC, _lastAmbientC, _state.wetness01 * 100.0f, environment.windMps,
                protection.insulation01 * 100.0f, protection.powerArmor,
                environment.shelter01 * 100.0f, environment.precipitationShelter01 * 100.0f,
                environment.heat01 * 100.0f, _shelter.LastRoofed(), _lastIndoors,
                weatherClass.pleasant, weatherClass.cloudy, weatherClass.rainy, weatherClass.snow,
                environment.submerged, environment.activity01 > 0.0f, weatherOverride.has_value(), StageName(_state.stage));
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
