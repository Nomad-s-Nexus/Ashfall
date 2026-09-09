#include "ClimateEnvironmentPolicy.h"
#include "ClimateHeatPolicy.h"
#include "ClimateInsulationPolicy.h"
#include "ClimatePowerArmorPolicy.h"
#include "ClimateSerializationPolicy.h"
#include "ClimateShelterPolicy.h"
#include "ClimateStagePolicy.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

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
    const float plainTorso = PieceInsulation(torsoPiece);
    torsoPiece.radiationResistance = 500.0f;
    torsoPiece.energyResistance = 125.0f;
    const float resistantTorso = PieceInsulation(torsoPiece);
    Require(resistantTorso > plainTorso);

    PieceSample overriddenTorso = torsoPiece;
    overriddenTorso.overrideInsulation01 = 0.82f;
    Require(Near(PieceWarmth(overriddenTorso), 0.82f));
    Require(Near(PieceInsulation(overriddenTorso), kTorsoWeight * 0.82f));
    overriddenTorso.overrideInsulation01 = 5.0f;
    Require(Near(PieceWarmth(overriddenTorso), 1.0f));
    Require(Near(PieceInsulation(overriddenTorso), kTorsoWeight));

    PieceSample invalidPiece{};
    invalidPiece.coverage = torsoCoverage;
    invalidPiece.radiationResistance = std::numeric_limits<float>::quiet_NaN();
    invalidPiece.energyResistance = std::numeric_limits<float>::infinity();
    Require(std::isfinite(PieceInsulation(invalidPiece)));

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

    return 0;
}
