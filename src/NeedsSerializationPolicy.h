#pragma once

#include "NeedsPolicy.h"

#include <cmath>
#include <cstdint>

namespace RadNeedsSerialization
{
    inline constexpr std::uint32_t kRecord =
        static_cast<std::uint32_t>('N') |
        (static_cast<std::uint32_t>('E') << 8) |
        (static_cast<std::uint32_t>('E') << 16) |
        (static_cast<std::uint32_t>('D') << 24);
    inline constexpr std::uint32_t kVersion = 1;

    struct PersistedState
    {
        float hunger01{ 0.0f };
        float thirst01{ 0.0f };
        float sleepDebt01{ 0.0f };
    };

    [[nodiscard]] inline PersistedState Snapshot(const RadNeedsPolicy::NeedsState& state) noexcept
    {
        const auto safe = RadNeedsPolicy::Sanitize(state);
        return { safe.hunger01, safe.thirst01, safe.sleepDebt01 };
    }

    [[nodiscard]] inline bool IsValid(const PersistedState& state) noexcept
    {
        const auto valid01 = [](float value) {
            return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        };
        return valid01(state.hunger01) &&
               valid01(state.thirst01) &&
               valid01(state.sleepDebt01);
    }

    [[nodiscard]] inline RadNeedsPolicy::NeedsState Restore(const PersistedState& state) noexcept
    {
        if (!IsValid(state))
            return {};
        return { state.hunger01, state.thirst01, state.sleepDebt01 };
    }
}
