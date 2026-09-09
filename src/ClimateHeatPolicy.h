#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace RadClimateHeatPolicy
{
    inline constexpr float kDiscoveryMoveDistance = 96.0f;
    inline constexpr float kDiscoveryMoveDistanceSquared =
        kDiscoveryMoveDistance * kDiscoveryMoveDistance;

    struct SourceSample
    {
        float distance{ 0.0f };
        float radius{ 0.0f };
        float strength01{ 0.0f };
        bool active{ true };
    };

    [[nodiscard]] inline float Contribution(const SourceSample& source) noexcept
    {
        if (!source.active || !std::isfinite(source.distance) || !std::isfinite(source.radius) ||
            !std::isfinite(source.strength01) || source.radius <= 0.0f)
            return 0.0f;
        const float distance = std::max(0.0f, source.distance);
        if (distance >= source.radius)
            return 0.0f;
        const float normalized = 1.0f - distance / source.radius;
        return std::clamp(source.strength01, 0.0f, 1.0f) * normalized * normalized;
    }

    template <std::size_t N>
    [[nodiscard]] inline float CombinedHeat(const std::array<SourceSample, N>& sources) noexcept
    {
        float remaining = 1.0f;
        for (const auto& source : sources)
            remaining *= 1.0f - Contribution(source);
        return std::clamp(1.0f - remaining, 0.0f, 1.0f);
    }

    [[nodiscard]] constexpr bool ShouldRefresh(
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
        return distanceSquared >= kDiscoveryMoveDistanceSquared;
    }
}
