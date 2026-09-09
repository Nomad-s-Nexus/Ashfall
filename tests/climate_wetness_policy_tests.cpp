#include "ClimatePolicy.h"

#include <cmath>
#include <cstdlib>

namespace
{
    void Require(bool condition)
    {
        if (!condition)
            std::abort();
    }

    bool Near(float lhs, float rhs, float epsilon = 0.0001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }
}

int main()
{
    using namespace RadClimatePolicy;

    EnvironmentSample openRain{};
    openRain.precipitation01 = 1.0f;

    EnvironmentSample roofRain = openRain;
    roofRain.precipitationShelter01 = 1.0f;

    EnvironmentSample partialCover = openRain;
    partialCover.precipitationShelter01 = 0.5f;

    const float start = 0.20f;
    const float openWet = NextWetness(start, openRain, 1.0f);
    const float roofWet = NextWetness(start, roofRain, 1.0f);
    const float partialWet = NextWetness(start, partialCover, 1.0f);

    Require(openWet > start);
    Require(roofWet < start);
    Require(partialWet > roofWet);
    Require(partialWet < openWet);

    EnvironmentSample roofRainNoDry = roofRain;
    roofRainNoDry.heat01 = 0.0f;
    const float expectedDry = start - 0.0012f;
    Require(Near(NextWetness(start, roofRainNoDry, 1.0f), expectedDry));

    EnvironmentSample submergedRoof = roofRain;
    submergedRoof.submerged = true;
    Require(NextWetness(start, submergedRoof, 1.0f) > openWet);

    EnvironmentSample indoorRain = openRain;
    indoorRain.indoors = true;
    Require(NextWetness(start, indoorRain, 1.0f) < roofWet);

    return 0;
}
