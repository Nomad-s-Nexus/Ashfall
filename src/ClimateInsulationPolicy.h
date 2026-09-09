#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace RadClimateInsulationPolicy
{
    struct Coverage
    {
        bool head{ false };
        bool torso{ false };
        bool leftArm{ false };
        bool rightArm{ false };
        bool leftLeg{ false };
        bool rightLeg{ false };
    };

    struct PieceSample
    {
        Coverage coverage{};
        float radiationResistance{ 0.0f };
        float energyResistance{ 0.0f };
    };

    [[nodiscard]] inline float CoverageFraction(const Coverage& coverage) noexcept
    {
        float value = 0.0f;
        value += coverage.head ? 0.10f : 0.0f;
        value += coverage.torso ? 0.35f : 0.0f;
        value += coverage.leftArm ? 0.10f : 0.0f;
        value += coverage.rightArm ? 0.10f : 0.0f;
        value += coverage.leftLeg ? 0.175f : 0.0f;
        value += coverage.rightLeg ? 0.175f : 0.0f;
        return std::clamp(value, 0.0f, 1.0f);
    }

    [[nodiscard]] inline float PieceInsulation(const PieceSample& piece) noexcept
    {
        const float coverage = CoverageFraction(piece.coverage);
        const float rad = std::isfinite(piece.radiationResistance)
            ? std::clamp(piece.radiationResistance / 1000.0f, 0.0f, 1.0f)
            : 0.0f;
        const float energy = std::isfinite(piece.energyResistance)
            ? std::clamp(piece.energyResistance / 250.0f, 0.0f, 1.0f)
            : 0.0f;
        return coverage * (0.65f + 0.20f * rad + 0.15f * energy);
    }

    template <std::size_t N>
    [[nodiscard]] inline float CombinedInsulation(const std::array<PieceSample, N>& pieces) noexcept
    {
        float retainedExposure = 1.0f;
        for (const auto& piece : pieces)
            retainedExposure *= 1.0f - std::clamp(PieceInsulation(piece), 0.0f, 0.95f);
        return std::clamp(1.0f - retainedExposure, 0.0f, 0.95f);
    }
}
