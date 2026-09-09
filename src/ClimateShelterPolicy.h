#pragma once

#include <cstdint>

namespace RadClimateShelterPolicy
{
    inline constexpr float kProbeMoveDistance = 96.0f;
    inline constexpr float kProbeMoveDistanceSquared = kProbeMoveDistance * kProbeMoveDistance;

    struct Factors
    {
        float thermal01{ 0.0f };
        float precipitation01{ 0.0f };
    };

    [[nodiscard]] constexpr Factors FromState(bool indoors, bool roofed, bool unresolved) noexcept
    {
        if (indoors || unresolved)
            return { 1.0f, 1.0f };
        if (roofed)
            return { 0.25f, 1.0f };
        return {};
    }

    [[nodiscard]] constexpr bool ShouldProbe(
        bool sampleKnown,
        std::uint32_t currentCellID,
        std::uint32_t lastCellID,
        float deltaX,
        float deltaY,
        float deltaZ) noexcept
    {
        if (!sampleKnown || currentCellID != lastCellID)
            return true;

        const float distanceSquared =
            deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
        return distanceSquared >= kProbeMoveDistanceSquared;
    }
}
