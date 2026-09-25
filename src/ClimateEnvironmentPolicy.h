#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace RadClimateEnvironmentPolicy
{
    inline constexpr float kIndoorAmbientC = 20.0f;
    inline constexpr float kOutdoorBaseC = 10.0f;
    inline constexpr float kDiurnalAmplitudeC = 3.0f;
    inline constexpr float kPleasantBonusC = 1.5f;
    inline constexpr float kCloudyPenaltyC = 2.0f;
    inline constexpr float kRainPenaltyC = 4.0f;
    inline constexpr float kSnowPenaltyC = 10.0f;
    inline constexpr float kSnowWetnessIntensity = 0.35f;
    inline constexpr float kMaximumWindProxyMps = 20.0f;
    inline constexpr std::array<float, 12> kSeasonalOffsetC{
        -7.0f, -6.0f, -3.0f, 0.0f, 3.0f, 5.0f,
         6.0f,  5.0f,  2.0f, 0.0f, -3.0f, -6.0f
    };

    struct WeatherClass
    {
        bool pleasant{ false };
        bool cloudy{ false };
        bool rainy{ false };
        bool snow{ false };
    };

    [[nodiscard]] inline float HourOfDay(float gameDays) noexcept
    {
        const float safeDays = std::isfinite(gameDays) ? gameDays : 0.5f;
        float fraction = safeDays - std::floor(safeDays);
        if (fraction < 0.0f)
            fraction += 1.0f;
        return fraction * 24.0f;
    }

    [[nodiscard]] constexpr float SeasonalOffsetC(std::uint32_t month) noexcept
    {
        return kSeasonalOffsetC[month % kSeasonalOffsetC.size()];
    }

    [[nodiscard]] inline float WeatherTemperatureAdjustmentC(const WeatherClass& weather) noexcept
    {
        if (weather.snow)
            return -kSnowPenaltyC;
        if (weather.rainy)
            return -kRainPenaltyC;
        if (weather.cloudy)
            return -kCloudyPenaltyC;
        if (weather.pleasant)
            return kPleasantBonusC;
        return 0.0f;
    }

    [[nodiscard]] inline float PrecipitationIntensity01(const WeatherClass& weather) noexcept
    {
        if (weather.rainy)
            return 1.0f;
        if (weather.snow)
            return kSnowWetnessIntensity;
        return 0.0f;
    }

    [[nodiscard]] inline float AmbientTemperatureC(
        bool indoors,
        const WeatherClass& weather,
        float gameDays,
        std::uint32_t month = 8) noexcept
    {
        if (indoors)
            return kIndoorAmbientC;
        constexpr float pi = 3.14159265358979323846f;
        const float hour = HourOfDay(gameDays);
        const float diurnal = kDiurnalAmplitudeC * std::cos((hour - 14.0f) * (2.0f * pi / 24.0f));
        return kOutdoorBaseC + diurnal + SeasonalOffsetC(month) + WeatherTemperatureAdjustmentC(weather);
    }

    [[nodiscard]] inline float AmbientTemperatureC(
        bool indoors,
        bool rainy,
        float gameDays,
        std::uint32_t month = 8) noexcept
    {
        WeatherClass weather{};
        weather.rainy = rainy;
        return AmbientTemperatureC(indoors, weather, gameDays, month);
    }

    [[nodiscard]] inline float WindProxyMps(std::int8_t weatherWindByte) noexcept
    {
        const auto raw = static_cast<std::uint8_t>(weatherWindByte);
        return kMaximumWindProxyMps * static_cast<float>(raw) / 255.0f;
    }
}
