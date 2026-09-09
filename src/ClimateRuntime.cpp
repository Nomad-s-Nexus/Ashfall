#include "pch.h"
#include "ClimateRuntime.h"

#include <RE/C/Calendar.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/Sky.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESWeather.h>

#include <cmath>

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

    float AmbientTemperatureC(bool indoors, bool rainy, float gameDays) noexcept
    {
        if (indoors)
            return 20.0f;
        const float safeDays = std::isfinite(gameDays) ? gameDays : 0.5f;
        float fraction = safeDays - std::floor(safeDays);
        if (fraction < 0.0f)
            fraction += 1.0f;
        const float hour = fraction * 24.0f;
        constexpr float pi = 3.14159265358979323846f;
        const float diurnal = 3.0f * std::cos((hour - 14.0f) * (2.0f * pi / 24.0f));
        return 10.0f + diurnal - (rainy ? 4.0f : 0.0f);
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

    RadClimatePolicy::EnvironmentSample environment{};
    environment.indoors = indoors;
    environment.shelter01 = indoors ? 1.0f : 0.0f;
    environment.precipitation01 = rainy ? 1.0f : 0.0f;
    environment.ambientC = AmbientTemperatureC(indoors, rainy, GameDays());

    RadClimatePolicy::ProtectionSample protection{};
    const auto result = RadClimatePolicy::Step(_state, environment, protection, 0.25f);
    _state = result.state;
    _lastAmbientC = environment.ambientC;
    _lastRainy = rainy;
    _lastIndoors = indoors;

    if (_state.stage != _lastLoggedStage)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Climate] stage={} core={:.2f}C ambient={:.1f}C wet={:.0f}%",
                StageName(_state.stage), _state.coreC, _lastAmbientC, _state.wetness01 * 100.0f);
        _lastLoggedStage = _state.stage;
    }

    if (++_statusTick >= 32)
    {
        _statusTick = 0;
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->debug("[Climate] core={:.2f}C ambient={:.1f}C wet={:.0f}% indoors={} rainy={} stage={}",
                _state.coreC, _lastAmbientC, _state.wetness01 * 100.0f,
                _lastIndoors, _lastRainy, StageName(_state.stage));
    }
}

void ClimateRuntime::Reset()
{
    _state = {};
    _lastLoggedStage = RadClimatePolicy::HypothermiaStage::kNormal;
    _lastAmbientC = RadClimatePolicy::kNeutralAmbientC;
    _lastRainy = false;
    _lastIndoors = false;
    _statusTick = 0;
}
