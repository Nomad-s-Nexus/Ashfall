#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace RadClimateInsulationPolicy
{
    inline constexpr float kHeadWeight = 0.10f;
    inline constexpr float kTorsoWeight = 0.35f;
    inline constexpr float kArmWeight = 0.10f;
    inline constexpr float kLegWeight = 0.175f;

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
        float overrideInsulation01{ -1.0f };
    };

    [[nodiscard]] inline float CoverageFraction(const Coverage& coverage) noexcept
    {
        float value = 0.0f;
        value += coverage.head ? kHeadWeight : 0.0f;
        value += coverage.torso ? kTorsoWeight : 0.0f;
        value += coverage.leftArm ? kArmWeight : 0.0f;
        value += coverage.rightArm ? kArmWeight : 0.0f;
        value += coverage.leftLeg ? kLegWeight : 0.0f;
        value += coverage.rightLeg ? kLegWeight : 0.0f;
        return std::clamp(value, 0.0f, 1.0f);
    }

    [[nodiscard]] inline float PieceWarmth(const PieceSample& piece) noexcept
    {
        if (std::isfinite(piece.overrideInsulation01) && piece.overrideInsulation01 >= 0.0f)
            return std::clamp(piece.overrideInsulation01, 0.0f, 1.0f);

        const float rad = std::isfinite(piece.radiationResistance)
            ? std::clamp(piece.radiationResistance / 1000.0f, 0.0f, 1.0f)
            : 0.0f;
        const float energy = std::isfinite(piece.energyResistance)
            ? std::clamp(piece.energyResistance / 250.0f, 0.0f, 1.0f)
            : 0.0f;
        return std::clamp(0.65f + 0.20f * rad + 0.15f * energy, 0.0f, 1.0f);
    }

    [[nodiscard]] inline float PieceInsulation(const PieceSample& piece) noexcept
    {
        return CoverageFraction(piece.coverage) * PieceWarmth(piece);
    }

    [[nodiscard]] inline float RegionInsulation(
        std::span<const PieceSample> pieces,
        bool Coverage::* region) noexcept
    {
        float retainedExposure = 1.0f;
        for (const auto& piece : pieces)
        {
            if (piece.coverage.*region)
                retainedExposure *= 1.0f - PieceWarmth(piece);
        }
        return std::clamp(1.0f - retainedExposure, 0.0f, 1.0f);
    }

    [[nodiscard]] inline float CombinedInsulation(std::span<const PieceSample> pieces) noexcept
    {
        const float total =
            kHeadWeight * RegionInsulation(pieces, &Coverage::head) +
            kTorsoWeight * RegionInsulation(pieces, &Coverage::torso) +
            kArmWeight * RegionInsulation(pieces, &Coverage::leftArm) +
            kArmWeight * RegionInsulation(pieces, &Coverage::rightArm) +
            kLegWeight * RegionInsulation(pieces, &Coverage::leftLeg) +
            kLegWeight * RegionInsulation(pieces, &Coverage::rightLeg);
        return std::clamp(total, 0.0f, 0.95f);
    }

    template <std::size_t N>
    [[nodiscard]] inline float CombinedInsulation(const std::array<PieceSample, N>& pieces) noexcept
    {
        return CombinedInsulation(std::span<const PieceSample>{ pieces });
    }
}
