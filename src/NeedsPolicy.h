#pragma once

#include <algorithm>
#include <cmath>

namespace RadNeedsPolicy
{
    struct NeedsState
    {
        float hunger01{ 0.0f };
        float thirst01{ 0.0f };
        float sleepDebt01{ 0.0f };
    };

    struct HourlyRates
    {
        float hungerPerHour{ 0.0f };
        float thirstPerHour{ 0.0f };
        float sleepDebtPerHour{ 0.0f };
    };

    struct Relief
    {
        float hunger01{ 0.0f };
        float thirst01{ 0.0f };
        float sleepDebt01{ 0.0f };
    };

    [[nodiscard]] inline float Clamp01(float value) noexcept
    {
        if (!std::isfinite(value))
            return 0.0f;
        return std::clamp(value, 0.0f, 1.0f);
    }

    [[nodiscard]] inline NeedsState Sanitize(NeedsState state) noexcept
    {
        state.hunger01 = Clamp01(state.hunger01);
        state.thirst01 = Clamp01(state.thirst01);
        state.sleepDebt01 = Clamp01(state.sleepDebt01);
        return state;
    }

    [[nodiscard]] inline NeedsState Advance(
        NeedsState state,
        float elapsedGameHours,
        const HourlyRates& rates) noexcept
    {
        state = Sanitize(state);
        const float hours = std::isfinite(elapsedGameHours)
            ? std::max(0.0f, elapsedGameHours)
            : 0.0f;
        const float hungerRate = std::isfinite(rates.hungerPerHour)
            ? std::max(0.0f, rates.hungerPerHour)
            : 0.0f;
        const float thirstRate = std::isfinite(rates.thirstPerHour)
            ? std::max(0.0f, rates.thirstPerHour)
            : 0.0f;
        const float sleepRate = std::isfinite(rates.sleepDebtPerHour)
            ? std::max(0.0f, rates.sleepDebtPerHour)
            : 0.0f;

        state.hunger01 = Clamp01(state.hunger01 + hungerRate * hours);
        state.thirst01 = Clamp01(state.thirst01 + thirstRate * hours);
        state.sleepDebt01 = Clamp01(state.sleepDebt01 + sleepRate * hours);
        return state;
    }

    [[nodiscard]] inline NeedsState ApplyRelief(NeedsState state, const Relief& relief) noexcept
    {
        state = Sanitize(state);
        state.hunger01 = Clamp01(state.hunger01 - Clamp01(relief.hunger01));
        state.thirst01 = Clamp01(state.thirst01 - Clamp01(relief.thirst01));
        state.sleepDebt01 = Clamp01(state.sleepDebt01 - Clamp01(relief.sleepDebt01));
        return state;
    }
}
