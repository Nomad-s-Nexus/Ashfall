#include "ClimateEnvironmentPolicy.h"
#include "ClimateHeatPolicy.h"
#include "ClimateInsulationPolicy.h"
#include "ClimatePowerArmorPolicy.h"
#include "ClimateSerializationPolicy.h"
#include "ClimateShelterPolicy.h"
#include "ClimateStagePolicy.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace
{
    void RequireImpl(bool condition, const char* expr, int line)
    {
        if (!condition)
        {
            std::fprintf(stderr, "Require failed at line %d: %s\n", line, expr);
            std::abort();
        }
    }

    bool Near(float lhs, float rhs, float epsilon = 0.001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }
}

#define Require(cond) RequireImpl((cond), #cond, __LINE__)

int main()
{
    using namespace RadClimateEnvironmentPolicy;

    const float afternoonDays = 14.0f / 24.0f;
    const float nightDays = 2.0f / 24.0f;
    Require(Near(HourOfDay(afternoonDays), 14.0f));
    Require(Near(HourOfDay(nightDays), 2.0f));
    Require(AmbientTemperatureC(false, false, afternoonDays) > AmbientTemperatureC(false, false, nightDays));
    Require(Near(
        AmbientTemperatureC(false, false, afternoonDays) - AmbientTemperatureC(false, true, afternoonDays),
        kRainPenaltyC));
    Require(Near(AmbientTemperatureC(true, true, afternoonDays), kIndoorAmbientC));
    Require(std::isfinite(AmbientTemperatureC(false, false, std::numeric_limits<float>::quiet_NaN())));
    Require(Near(WindProxyMps(0), 0.0f));
    Require(Near(WindProxyMps(static_cast<std::int8_t>(-1)), kMaximumWindProxyMps));
    const float midWind = WindProxyMps(127);
    Require(midWind > 9.0f && midWind < 11.0f);

    using namespace RadClimateSerialization;
    using namespace RadClimatePolicy;

    Require(Near(BodyWarmth01(28.0f), 0.0f));
    Require(Near(BodyWarmth01(37.0f), 1.0f));
    Require(Near(BodyWarmth01(40.0f), 1.0f));
    Require(Near(BodyWarmth01(20.0f), 0.0f));
    ClimateState besideFire{};
    besideFire.coreC = 35.0f;
    EnvironmentSample fireEnvironment{};
    fireEnvironment.ambientC = 8.0f;
    fireEnvironment.heat01 = 0.7f;
    for (int tick = 0; tick < 3600; ++tick)
        besideFire = Step(besideFire, fireEnvironment, {}, 0.25f).state;
    Require(Near(BodyWarmth01(besideFire.coreC), 1.0f));

    ClimateState state{};
    state.coreC = 34.75f;
    state.wetness01 = 0.65f;
    state.stage = HypothermiaStage::kCritical;
    const auto saved = Snapshot(state);
    Require(IsValid(saved));
    Require(Near(saved.coreC, 34.75f));
    Require(Near(saved.wetness01, 0.65f));

    const auto restored = Restore(saved);
    Require(Near(restored.coreC, saved.coreC));
    Require(Near(restored.wetness01, saved.wetness01));
    Require(restored.stage == StageForCore(saved.coreC));

    Require(!IsValid({ std::numeric_limits<float>::quiet_NaN(), 0.5f }));
    Require(!IsValid({ 36.0f, std::numeric_limits<float>::infinity() }));
    Require(!IsValid({ kMinimumCoreC - 0.01f, 0.5f }));
    Require(!IsValid({ kMaximumCoreC + 0.01f, 0.5f }));
    Require(!IsValid({ 36.0f, -0.01f }));
    Require(!IsValid({ 36.0f, 1.01f }));

    const auto invalidRestored = Restore({ std::numeric_limits<float>::quiet_NaN(), 0.5f });
    Require(Near(invalidRestored.coreC, kNormalCoreC));
    Require(Near(invalidRestored.wetness01, 0.0f));
    Require(invalidRestored.stage == HypothermiaStage::kNormal);
    Require(kPayloadSize == sizeof(float) * 2);

    using namespace RadClimateInsulationPolicy;

    Coverage fullCoverage{ true, true, true, true, true, true };
    Coverage torsoCoverage{};
    torsoCoverage.torso = true;
    Require(Near(CoverageFraction(fullCoverage), 1.0f));
    Require(Near(CoverageFraction(torsoCoverage), 0.35f));

    PieceSample torsoPiece{};
    torsoPiece.coverage = torsoCoverage;
    Require(Near(PieceWarmth(torsoPiece), kDefaultPieceWarmth));
    Require(Near(PieceInsulation(torsoPiece), kTorsoWeight * kDefaultPieceWarmth));

    PieceSample overriddenTorso = torsoPiece;
    overriddenTorso.overrideInsulation01 = 0.82f;
    Require(Near(PieceWarmth(overriddenTorso), 0.82f));
    Require(Near(PieceInsulation(overriddenTorso), kTorsoWeight * 0.82f));
    overriddenTorso.overrideInsulation01 = 5.0f;
    Require(Near(PieceWarmth(overriddenTorso), 1.0f));
    Require(Near(PieceInsulation(overriddenTorso), kTorsoWeight));

    PieceSample invalidOverride = torsoPiece;
    invalidOverride.overrideInsulation01 = std::numeric_limits<float>::quiet_NaN();
    Require(Near(PieceWarmth(invalidOverride), kDefaultPieceWarmth));
    Require(std::isfinite(PieceInsulation(invalidOverride)));

    PieceSample fullBody{};
    fullBody.coverage = fullCoverage;
    std::array<PieceSample, 1> fullBodySet{ fullBody };

    PieceSample headPiece{};
    headPiece.coverage.head = true;
    PieceSample torsoOnly{};
    torsoOnly.coverage.torso = true;
    PieceSample leftArm{};
    leftArm.coverage.leftArm = true;
    PieceSample rightArm{};
    rightArm.coverage.rightArm = true;
    PieceSample leftLeg{};
    leftLeg.coverage.leftLeg = true;
    PieceSample rightLeg{};
    rightLeg.coverage.rightLeg = true;
    std::array<PieceSample, 6> splitBody{ headPiece, torsoOnly, leftArm, rightArm, leftLeg, rightLeg };
    Require(Near(CombinedInsulation(fullBodySet), CombinedInsulation(splitBody)));
    Require(Near(CombinedInsulation(fullBodySet), PieceWarmth(fullBody)));

    PieceSample torsoLayerA{};
    torsoLayerA.coverage.torso = true;
    torsoLayerA.overrideInsulation01 = 0.60f;
    PieceSample torsoLayerB = torsoLayerA;
    std::array<PieceSample, 1> oneTorsoLayer{ torsoLayerA };
    std::array<PieceSample, 2> twoTorsoLayers{ torsoLayerA, torsoLayerB };
    Require(CombinedInsulation(twoTorsoLayers) > CombinedInsulation(oneTorsoLayer));
    Require(CombinedInsulation(twoTorsoLayers) < kTorsoWeight);

    std::vector<PieceSample> dynamicLayers{ torsoOnly, headPiece };
    Require(CombinedInsulation(std::span<const PieceSample>{ dynamicLayers }) > PieceInsulation(torsoOnly));

    using namespace RadClimatePowerArmorPolicy;

    Require(!IsCompleteSuit(false, 6));
    Require(!IsCompleteSuit(true, 5));
    Require(IsCompleteSuit(true, 6));
    Require(IsCompleteSuit(true, 7));
    ProtectionSample ordinaryProtection{};
    ordinaryProtection.insulation01 = 0.35f;
    ProtectionSample sealedPowerArmor = ordinaryProtection;
    sealedPowerArmor.powerArmor = true;
    Require(EffectiveInsulation(sealedPowerArmor) >= 0.85f);
    Require(EffectiveInsulation(sealedPowerArmor) > EffectiveInsulation(ordinaryProtection));

    using namespace RadClimateStagePolicy;

    Require(NextStage(HypothermiaStage::kNormal, 36.49f) == HypothermiaStage::kChilled);
    Require(NextStage(HypothermiaStage::kChilled, 36.55f) == HypothermiaStage::kChilled);
    Require(NextStage(HypothermiaStage::kChilled, 36.65f) == HypothermiaStage::kNormal);
    Require(NextStage(HypothermiaStage::kMild, 35.60f) == HypothermiaStage::kMild);
    Require(NextStage(HypothermiaStage::kMild, 35.66f) == HypothermiaStage::kChilled);
    Require(NextStage(HypothermiaStage::kModerate, 34.60f) == HypothermiaStage::kModerate);
    Require(NextStage(HypothermiaStage::kSevere, 33.10f) == HypothermiaStage::kSevere);
    Require(NextStage(HypothermiaStage::kCritical, 30.10f) == HypothermiaStage::kCritical);
    Require(NextStage(HypothermiaStage::kMild, 32.0f) == HypothermiaStage::kSevere);
    Require(NextStage(static_cast<HypothermiaStage>(255), 36.0f) == HypothermiaStage::kChilled);

    using namespace RadClimateHeatPolicy;

    SourceSample fire{};
    fire.distance = 0.0f;
    fire.radius = 1000.0f;
    fire.strength01 = 0.8f;
    Require(Near(Contribution(fire), 0.8f));

    fire.distance = 500.0f;
    const float halfRadius = Contribution(fire);
    Require(halfRadius > 0.0f && halfRadius < 0.8f);
    Require(Near(halfRadius, 0.2f));

    fire.distance = 1000.0f;
    Require(Near(Contribution(fire), 0.0f));
    fire.distance = 1500.0f;
    Require(Near(Contribution(fire), 0.0f));

    SourceSample invalidHeat{};
    invalidHeat.radius = std::numeric_limits<float>::quiet_NaN();
    invalidHeat.strength01 = 1.0f;
    Require(Near(Contribution(invalidHeat), 0.0f));

    SourceSample closeFire{};
    closeFire.distance = 100.0f;
    closeFire.radius = 1000.0f;
    closeFire.strength01 = 0.7f;
    std::array<SourceSample, 1> oneFire{ closeFire };
    std::array<SourceSample, 2> twoFires{ closeFire, closeFire };
    Require(CombinedHeat(twoFires) > CombinedHeat(oneFire));
    Require(CombinedHeat(twoFires) <= 1.0f);

    closeFire.active = false;
    Require(Near(Contribution(closeFire), 0.0f));
    Require(ShouldRefresh(false, 10, 10, 0.0f, 0.0f, 0.0f));
    Require(ShouldRefresh(true, 11, 10, 0.0f, 0.0f, 0.0f));
    Require(!ShouldRefresh(true, 10, 10, kDiscoveryMoveDistance - 1.0f, 0.0f, 0.0f));
    Require(ShouldRefresh(true, 10, 10, kDiscoveryMoveDistance, 0.0f, 0.0f));

    using namespace RadClimateShelterPolicy;

    const auto indoorShelter = FromState(true, false, false);
    Require(Near(indoorShelter.thermal01, 1.0f));
    Require(Near(indoorShelter.precipitation01, 1.0f));
    const auto roofShelter = FromState(false, true, false);
    Require(Near(roofShelter.thermal01, 0.25f));
    Require(Near(roofShelter.precipitation01, 1.0f));
    Require(ShouldProbe(false, 10, 10, 0.0f, 0.0f, 0.0f));
    Require(ShouldProbe(true, 11, 10, 0.0f, 0.0f, 0.0f));
    Require(!ShouldProbe(true, 10, 10, kProbeMoveDistance - 1.0f, 0.0f, 0.0f));
    Require(ShouldProbe(true, 10, 10, kProbeMoveDistance, 0.0f, 0.0f));

    ClimateState outdoorsFreezing{};
    outdoorsFreezing.coreC = 36.0f;
    outdoorsFreezing.wetness01 = 0.5f;
    outdoorsFreezing = SanitizeState(outdoorsFreezing);
    EnvironmentSample blizzardEnv{};
    blizzardEnv.ambientC = -15.0f;
    blizzardEnv.windMps = 15.0f;
    blizzardEnv.precipitation01 = 1.0f;

    ClimateState pausedState = outdoorsFreezing;
    for (int i = 0; i < 1000; ++i)
    {
        const auto pausedRes = Step(pausedState, blizzardEnv, {}, 0.0f);
        pausedState = pausedRes.state;
        Require(pausedRes.coolingCPerSecond > 0.0f);
    }
    Require(Near(pausedState.coreC, outdoorsFreezing.coreC));
    Require(Near(pausedState.wetness01, outdoorsFreezing.wetness01));
    Require(pausedState.stage == outdoorsFreezing.stage);

    ClimateState step4x025 = outdoorsFreezing;
    for (int i = 0; i < 4; ++i)
        step4x025 = Step(step4x025, blizzardEnv, {}, 0.25f).state;

    ClimateState step1x100 = Step(outdoorsFreezing, blizzardEnv, {}, 1.0f).state;
    Require(Near(step4x025.coreC, step1x100.coreC, 0.01f));
    Require(Near(step4x025.wetness01, step1x100.wetness01, 0.01f));

    ClimateState step4x5s = outdoorsFreezing;
    for (int i = 0; i < 4; ++i)
        step4x5s = Step(step4x5s, blizzardEnv, {}, kMaximumStepSeconds).state;

    const auto delayed20sResult = StepTimeJump(outdoorsFreezing, blizzardEnv, {}, 20.0f, kMaximumStepSeconds);
    Require(Near(delayed20sResult.state.coreC, step4x5s.coreC));
    Require(Near(delayed20sResult.state.wetness01, step4x5s.wetness01));
    Require(delayed20sResult.state.stage == step4x5s.stage);

    const auto single5sStep = Step(outdoorsFreezing, blizzardEnv, {}, kMaximumStepSeconds);
    Require(delayed20sResult.state.coreC < single5sStep.state.coreC);

    Require(Near(ElapsedGameSeconds(1.0f / 24.0f), 3600.0f));
    Require(Near(ElapsedGameSeconds(8.0f / 24.0f), 28800.0f));
    Require(Near(ElapsedGameSeconds(1.0f), 86400.0f));
    Require(Near(ElapsedGameSeconds(0.0f), 0.0f));
    Require(Near(ElapsedGameSeconds(-0.5f), 0.0f));
    Require(Near(ElapsedGameSeconds(std::numeric_limits<float>::quiet_NaN()), 0.0f));

    const auto clampedStep = Step(outdoorsFreezing, blizzardEnv, {}, 100.0f);
    const auto maxStep = Step(outdoorsFreezing, blizzardEnv, {}, kMaximumStepSeconds);
    Require(Near(clampedStep.state.coreC, maxStep.state.coreC));
    Require(Near(clampedStep.state.wetness01, maxStep.state.wetness01));

    ClimateState coldAndWet{};
    coldAndWet.coreC = 33.5f;
    coldAndWet.wetness01 = 0.90f;
    coldAndWet.stage = HypothermiaStage::kModerate;

    EnvironmentSample indoorBedEnv{};
    indoorBedEnv.indoors = true;
    indoorBedEnv.ambientC = 20.0f;
    indoorBedEnv.shelter01 = 1.0f;
    indoorBedEnv.precipitationShelter01 = 1.0f;
    indoorBedEnv.heat01 = 0.5f;

    const auto sleepResult = StepTimeJump(coldAndWet, indoorBedEnv, {}, 28800.0f);
    Require(Near(sleepResult.state.wetness01, 0.0f));
    Require(Near(sleepResult.state.coreC, kNormalCoreC));
    Require(sleepResult.state.stage == HypothermiaStage::kNormal);

    ClimateState mildCold{};
    mildCold.coreC = 36.0f;
    mildCold.stage = HypothermiaStage::kChilled;
    EnvironmentSample unheatedShelter = indoorBedEnv;
    unheatedShelter.heat01 = 0.0f;
    const auto mildRecovery = StepTimeJump(mildCold, unheatedShelter, {}, 28800.0f);
    Require(mildRecovery.state.coreC >= 36.6f);
    Require(mildRecovery.state.stage == HypothermiaStage::kNormal);

    ClimateState dryOutdoor{};
    dryOutdoor.coreC = 37.0f;
    dryOutdoor.wetness01 = 0.0f;
    dryOutdoor.stage = HypothermiaStage::kNormal;

    EnvironmentSample rainOutdoorEnv{};
    rainOutdoorEnv.indoors = false;
    rainOutdoorEnv.ambientC = 5.0f;
    rainOutdoorEnv.precipitation01 = 1.0f;
    rainOutdoorEnv.shelter01 = 0.0f;

    const auto rainWaitResult = StepTimeJump(dryOutdoor, rainOutdoorEnv, {}, 14400.0f);
    Require(Near(rainWaitResult.state.wetness01, 1.0f));
    Require(rainWaitResult.state.coreC < dryOutdoor.coreC);
    Require(rainWaitResult.state.stage != HypothermiaStage::kNormal);

    const auto travelResult = StepTimeJump(coldAndWet, indoorBedEnv, {}, 7200.0f);
    Require(travelResult.state.wetness01 < coldAndWet.wetness01);
    Require(travelResult.state.coreC > coldAndWet.coreC);

    const auto hugeJump = StepTimeJump(outdoorsFreezing, blizzardEnv, {}, 1000000.0f);
    Require(std::isfinite(hugeJump.state.coreC));
    Require(std::isfinite(hugeJump.state.wetness01));
    Require(hugeJump.state.coreC >= kMinimumCoreC && hugeJump.state.coreC <= kMaximumCoreC);

    return 0;
}
