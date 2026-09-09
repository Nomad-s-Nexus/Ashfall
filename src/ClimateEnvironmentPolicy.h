#pragma once

#include <cmath>

namespace RadClimateEnvironmentPolicy
{
    inline constexpr float kIndoorAmbientC = 20.0f;
    inline constexpr float kOutdoorBaseC = 10.0f;
    inline constexpr float kDiurnalAmplitudeC = 3.0f;
    inline constexpr float kRainPenaltyC = 4.0f;

    [[nodiscard]] inline float HourOfDay(float gameDays) noexcept
    {
        const float safeDays = std::isfinite(gameDays) ? gameDays : 0.5f;
        float fraction = safeDays - std::floor(safeDays);
        if (fraction < 0.0f)
            fraction += 1.0f;
        return fraction * 24.0f;
    }

    [[nodiscard]] inline float AmbientTemperatureC(bool indoors, bool rainy, float gameDays) noexcept
    {
        if (indoors)
            return kIndoorAmbientC;
        constexpr float pi = 3.14159265358979323846f;
        const float hour = HourOfDay(gameDays);
        const float diurnal = kDiurnalAmplitudeC * std::cos((hour - 14.0f) * (2.0f * pi / 24.0f));
        return kOutdoorBaseC + diurnal - (rainy ? kRainPenaltyC : 0.0f);
    }
}
