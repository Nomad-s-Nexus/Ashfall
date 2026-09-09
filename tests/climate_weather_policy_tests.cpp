#include "ClimateEnvironmentPolicy.h"

#include <cmath>
#include <cstdlib>

namespace
{
    void Require(bool condition)
    {
        if (!condition)
            std::abort();
    }

    bool Near(float lhs, float rhs, float epsilon = 0.001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }
}

int main()
{
    using namespace RadClimateEnvironmentPolicy;

    const float afternoonDays = 14.0f / 24.0f;
    WeatherClass clear{};
    WeatherClass pleasant{};
    pleasant.pleasant = true;
    WeatherClass cloudy{};
    cloudy.cloudy = true;
    WeatherClass rainy{};
    rainy.rainy = true;
    WeatherClass snow{};
    snow.snow = true;
    WeatherClass mixed{};
    mixed.pleasant = true;
    mixed.cloudy = true;
    mixed.rainy = true;
    mixed.snow = true;

    const float clearC = AmbientTemperatureC(false, clear, afternoonDays);
    Require(Near(AmbientTemperatureC(true, snow, afternoonDays), kIndoorAmbientC));
    Require(Near(AmbientTemperatureC(false, pleasant, afternoonDays) - clearC, kPleasantBonusC));
    Require(Near(clearC - AmbientTemperatureC(false, cloudy, afternoonDays), kCloudyPenaltyC));
    Require(Near(clearC - AmbientTemperatureC(false, rainy, afternoonDays), kRainPenaltyC));
    Require(Near(clearC - AmbientTemperatureC(false, snow, afternoonDays), kSnowPenaltyC));
    Require(Near(AmbientTemperatureC(false, mixed, afternoonDays), AmbientTemperatureC(false, snow, afternoonDays)));

    Require(Near(PrecipitationIntensity01(clear), 0.0f));
    Require(Near(PrecipitationIntensity01(cloudy), 0.0f));
    Require(Near(PrecipitationIntensity01(rainy), 1.0f));
    Require(Near(PrecipitationIntensity01(snow), kSnowWetnessIntensity));
    Require(Near(PrecipitationIntensity01(mixed), 1.0f));

    return 0;
}
