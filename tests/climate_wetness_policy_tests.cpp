#include "ClimateEnvironmentPolicy.h"
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

    using RadClimateEnvironmentPolicy::ShelteredAmbientC;
    using RadClimateEnvironmentPolicy::SanitizeEnclosedFloorC;
    Require(Near(ShelteredAmbientC(-20.0f, 1.0f), 8.0f));
    Require(Near(ShelteredAmbientC(-20.0f, 0.0f), -20.0f));
    Require(Near(ShelteredAmbientC(-20.0f, 0.5f), -6.0f));
    Require(Near(ShelteredAmbientC(20.0f, 1.0f), 20.0f));
    Require(Near(ShelteredAmbientC(5.0f, 1.0f), 8.0f));
    Require(Near(ShelteredAmbientC(-20.0f, 1.0f, 2.0f), 2.0f));
    Require(Near(SanitizeEnclosedFloorC(std::nanf("")), 8.0f));
    Require(Near(SanitizeEnclosedFloorC(50.0f), 20.0f));
    Require(Near(SanitizeEnclosedFloorC(-50.0f), -10.0f));

    EnvironmentSample enclosedGale{};
    enclosedGale.windMps = 20.0f;
    enclosedGale.shelter01 = 1.0f;
    enclosedGale.windShelter01 = 1.0f;
    enclosedGale.precipitationShelter01 = 1.0f;
    Require(Near(EffectiveWindShelter01(enclosedGale), 1.0f));
    Require(Near(VisibleWindMps(enclosedGale), 0.0f));

    EnvironmentSample roofedGale = enclosedGale;
    roofedGale.shelter01 = 0.5f;
    roofedGale.windShelter01 = 0.75f;
    Require(Near(VisibleWindMps(roofedGale), 5.0f));

    EnvironmentSample openGale = enclosedGale;
    openGale.shelter01 = 0.0f;
    openGale.windShelter01 = 0.0f;
    Require(Near(VisibleWindMps(openGale), 20.0f));

    EnvironmentSample indoorGale = openGale;
    indoorGale.indoors = true;
    Require(Near(VisibleWindMps(indoorGale), 0.0f));

    EnvironmentSample sheltered{};
    sheltered.ambientC = ShelteredAmbientC(-20.0f, 1.0f);
    sheltered.windMps = 20.0f;
    sheltered.shelter01 = 1.0f;
    sheltered.windShelter01 = 1.0f;
    sheltered.precipitationShelter01 = 1.0f;
    ClimateState chilledStart{};
    chilledStart.coreC = 37.0f;
    const auto enclosedEquilibrium = StepTimeJump(chilledStart, sheltered, {}, 6.0f * 3600.0f);
    Require(enclosedEquilibrium.state.coreC >= 35.5f);
    Require(enclosedEquilibrium.state.stage <= HypothermiaStage::kChilled);
    Require(enclosedEquilibrium.state.wetness01 < 0.001f);

    EnvironmentSample exposedGale = sheltered;
    exposedGale.ambientC = -20.0f;
    exposedGale.shelter01 = 0.0f;
    exposedGale.windShelter01 = 0.0f;
    exposedGale.precipitationShelter01 = 0.0f;
    const auto exposedEquilibrium = StepTimeJump(chilledStart, exposedGale, {}, 6.0f * 3600.0f);
    Require(exposedEquilibrium.state.coreC < enclosedEquilibrium.state.coreC);

    EnvironmentSample roofedOpenSides = sheltered;
    roofedOpenSides.ambientC = -20.0f;
    roofedOpenSides.shelter01 = 0.5f;
    roofedOpenSides.windShelter01 = 0.75f;
    const auto roofedEquilibrium = StepTimeJump(chilledStart, roofedOpenSides, {}, 6.0f * 3600.0f);
    Require(roofedEquilibrium.state.coreC < enclosedEquilibrium.state.coreC);
    Require(roofedEquilibrium.state.coreC > exposedEquilibrium.state.coreC);

    return 0;
}
