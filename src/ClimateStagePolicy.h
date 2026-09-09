#pragma once

#include "ClimatePolicy.h"

#include <cmath>
#include <cstdint>

namespace RadClimateStagePolicy
{
    inline constexpr float kRecoveryHysteresisC = 0.15f;

    [[nodiscard]] inline float RecoveryBoundary(RadClimatePolicy::HypothermiaStage stage) noexcept
    {
        using enum RadClimatePolicy::HypothermiaStage;
        switch (stage)
        {
        case kChilled: return 36.5f + kRecoveryHysteresisC;
        case kMild: return 35.5f + kRecoveryHysteresisC;
        case kModerate: return 34.5f + kRecoveryHysteresisC;
        case kSevere: return 33.0f + kRecoveryHysteresisC;
        case kCritical: return 30.0f + kRecoveryHysteresisC;
        case kNormal:
        default: return RadClimatePolicy::kMaximumCoreC;
        }
    }

    [[nodiscard]] inline RadClimatePolicy::HypothermiaStage NextStage(
        RadClimatePolicy::HypothermiaStage current,
        float coreC) noexcept
    {
        using RadClimatePolicy::HypothermiaStage;
        const auto raw = RadClimatePolicy::StageForCore(coreC);
        const auto currentValue = static_cast<std::uint8_t>(current);
        const auto criticalValue = static_cast<std::uint8_t>(HypothermiaStage::kCritical);
        if (!std::isfinite(coreC) || currentValue > criticalValue)
            return raw;
        const auto rawValue = static_cast<std::uint8_t>(raw);
        if (rawValue >= currentValue)
            return raw;
        if (coreC < RecoveryBoundary(current))
            return current;
        return raw;
    }
}
