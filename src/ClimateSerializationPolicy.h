#pragma once

#include "ClimatePolicy.h"

#include <cmath>
#include <cstdint>

namespace RadClimateSerialization
{
    inline constexpr std::uint32_t kRecord = 'CLIM';
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::uint32_t kPayloadSize = sizeof(float) * 2;

    struct PersistedState
    {
        float coreC{ RadClimatePolicy::kNormalCoreC };
        float wetness01{ 0.0f };
    };

    [[nodiscard]] inline bool IsValid(const PersistedState& state) noexcept
    {
        return std::isfinite(state.coreC) &&
               std::isfinite(state.wetness01) &&
               state.coreC >= RadClimatePolicy::kMinimumCoreC &&
               state.coreC <= RadClimatePolicy::kMaximumCoreC &&
               state.wetness01 >= 0.0f &&
               state.wetness01 <= 1.0f;
    }

    [[nodiscard]] inline PersistedState Snapshot(const RadClimatePolicy::ClimateState& state) noexcept
    {
        const auto sanitized = RadClimatePolicy::SanitizeState(state);
        return { sanitized.coreC, sanitized.wetness01 };
    }

    [[nodiscard]] inline RadClimatePolicy::ClimateState Restore(const PersistedState& state) noexcept
    {
        if (!IsValid(state))
            return {};
        return RadClimatePolicy::SanitizeState({ state.coreC, state.wetness01, RadClimatePolicy::HypothermiaStage::kNormal });
    }
}
