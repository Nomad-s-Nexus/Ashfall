#include "ClimateEnvironmentPolicy.h"
#include "ClimateInsulationPolicy.h"
#include "ClimateSerializationPolicy.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

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

    PieceSample invalidPiece{};
    invalidPiece.coverage = torsoCoverage;
    invalidPiece.radiationResistance = std::numeric_limits<float>::quiet_NaN();
    invalidPiece.energyResistance = std::numeric_limits<float>::infinity();
    Require(std::isfinite(PieceInsulation(invalidPiece)));

    PieceSample headPiece{};
    headPiece.coverage.head = true;
    PieceSample legPiece{};
    legPiece.coverage.leftLeg = true;
    std::array<PieceSample, 3> layered{ torsoPiece, headPiece, legPiece };
    const float combined = CombinedInsulation(layered);
    Require(combined > PieceInsulation(torsoPiece));
    Require(combined <= 0.95f);

    std::array<PieceSample, 1> oneLayer{ torsoPiece };
    Require(Near(CombinedInsulation(oneLayer), PieceInsulation(torsoPiece)));

    return 0;
}
