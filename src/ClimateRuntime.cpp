#include "pch.h"
#include "ClimateRuntime.h"
#include "ClimateEnvironmentPolicy.h"
#include "ClimateStagePolicy.h"
#include "RadHud.h"

#include <RE/B/BSSpinLock.h>
#include <RE/C/Calendar.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/Sky.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESWeather.h>
#include <RE/U/UI.h>

namespace
{
    float GameDays() noexcept
    {
        auto* cal = RE::Calendar::GetSingleton();
        if (cal && cal->gameDaysPassed)
            return cal->gameDaysPassed->GetValue();
        return 0.0f;
    }

    std::uint32_t GameMonth() noexcept
    {
        if (auto* cal = RE::Calendar::GetSingleton())
            return cal->GetMonth();
        return 8;
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

    bool IsGameplayPaused() noexcept
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui)
            return false;

        if (ui->freezeFramePause > 0)
            return true;

        RE::BSAutoReadLock l{ RE::UI::GetMenuMapRWLock() };
        for (const auto& menu : ui->menuStack)
        {
            if (menu && menu->menuFlags.all(RE::UI_MENU_FLAGS::kPausesGame))
                return true;
        }
        return false;
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
    const auto gameMonth = GameMonth();

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
    environment.ambientC = RadClimateEnvironmentPolicy::AmbientTemperatureC(indoors, weatherClass, GameDays(), gameMonth);
    if (weatherOverride)
        environment.ambientC += weatherOverride->temperatureOffsetC;
    environment.heat01 = _heat.Sample(player);
    environment.activity01 = player->DoGetSprinting() ? 1.0f : 0.0f;

    const auto protection = _protection.Sample(player);
    const float nowDays = GameDays();
    const bool paused = IsGameplayPaused();

    RadClimatePolicy::StepResult result{};

    if (_prevGameDays < 0.0f)
    {
        _prevGameDays = nowDays;
        result = RadClimatePolicy::Step(_state, environment, protection, 0.0f);
    }
    else
    {
        const float gameDaysElapsed = nowDays - _prevGameDays;
        _prevGameDays = nowDays;

        if (paused || gameDaysElapsed <= 0.0f)
        {
            result = RadClimatePolicy::Step(_state, environment, protection, 0.0f);
        }
        else if (gameDaysElapsed > RadClimatePolicy::kTimeJumpThresholdDays)
        {
            float timeScale = 20.0f;
            if (auto* cal = RE::Calendar::GetSingleton(); cal && cal->timeScale)
            {
                const float ts = cal->timeScale->GetValue();
                if (ts > 0.0f)
                    timeScale = ts;
            }
            const float gameHours = gameDaysElapsed * 24.0f;
            const float effectiveHours = std::min(gameHours, RadClimatePolicy::kMaximumTimeJumpHours);
            const float jumpRealSeconds = (effectiveHours * 3600.0f) / timeScale;
            result = RadClimatePolicy::StepTimeJump(_state, environment, protection, jumpRealSeconds);
        }
        else
        {
            float timeScale = 20.0f;
            if (auto* cal = RE::Calendar::GetSingleton(); cal && cal->timeScale)
            {
                const float ts = cal->timeScale->GetValue();
                if (ts > 0.0f)
                    timeScale = ts;
            }
            const float realSeconds = (gameDaysElapsed * 86400.0f) / timeScale;
            const float dt = std::clamp(realSeconds, 0.0f, RadClimatePolicy::kMaximumStepSeconds);
            result = RadClimatePolicy::Step(_state, environment, protection, dt);
        }
    }
    auto nextState = result.state;
    nextState.stage = RadClimateStagePolicy::NextStage(_state.stage, nextState.coreC);
    _state = nextState;
    _lastAmbientC = environment.ambientC;
    _lastEffectiveInsulation01 = result.effectiveInsulation01;
    _lastRainy = weatherClass.rainy;
    _lastIndoors = indoors;

    RadHud::GetSingleton().PushClimate(_state, environment, result);
    RadHud::GetSingleton().PushArmor(
        protection.insulation01,
        result.effectiveInsulation01,
        protection.powerArmor,
        _protection.LastArmor());

    if (_state.stage != _lastLoggedStage)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Climate] stage={} core={:.2f}C ambient={:.1f}C wet={:.0f}% wind={:.1f} insulation={:.0f}% PA={} shelter={:.0f}% heat={:.0f}% roofed={} sprinting={} snow={} month={} weatherOverride={}",
                StageName(_state.stage), _state.coreC, _lastAmbientC, _state.wetness01 * 100.0f,
                environment.windMps, protection.insulation01 * 100.0f, protection.powerArmor,
                environment.shelter01 * 100.0f, environment.heat01 * 100.0f, _shelter.LastRoofed(),
                environment.activity01 > 0.0f, weatherClass.snow, gameMonth, weatherOverride.has_value());
        _lastLoggedStage = _state.stage;
    }

    if (++_statusTick >= 32)
    {
        _statusTick = 0;
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->debug("[Climate] core={:.2f}C ambient={:.1f}C wet={:.0f}% wind={:.1f} insulation={:.0f}% PA={} shelter={:.0f}% rainCover={:.0f}% heat={:.0f}% roofed={} indoors={} pleasant={} cloudy={} rainy={} snow={} swimming={} sprinting={} month={} weatherOverride={} stage={}",
                _state.coreC, _lastAmbientC, _state.wetness01 * 100.0f, environment.windMps,
                protection.insulation01 * 100.0f, protection.powerArmor,
                environment.shelter01 * 100.0f, environment.precipitationShelter01 * 100.0f,
                environment.heat01 * 100.0f, _shelter.LastRoofed(), _lastIndoors,
                weatherClass.pleasant, weatherClass.cloudy, weatherClass.rainy, weatherClass.snow,
                environment.submerged, environment.activity01 > 0.0f, gameMonth,
                weatherOverride.has_value(), StageName(_state.stage));
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
    _lastEffectiveInsulation01 = 0.0f;
    _lastRainy = false;
    _lastIndoors = false;
    _statusTick = 0;
    _prevGameDays = -1.0f;
}

RadClimateSerialization::PersistedState ClimateRuntime::SnapshotForSave() const noexcept
{
    return RadClimateSerialization::Snapshot(_state);
}

void ClimateRuntime::RestoreFromSave(const RadClimateSerialization::PersistedState& state) noexcept
{
    _state = RadClimateSerialization::Restore(state);
    _lastLoggedStage = _state.stage;
    _prevGameDays = -1.0f;
}
