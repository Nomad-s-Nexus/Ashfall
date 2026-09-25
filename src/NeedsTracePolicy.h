#pragma once

#include <cstdint>
#include <limits>

namespace RadNeedsTracePolicy
{
    enum class ContainerDirection : std::uint8_t
    {
        kUnrelated,
        kLeftPlayer,
        kEnteredPlayer,
        kWithinPlayer
    };

    constexpr ContainerDirection ClassifyContainerChange(
        std::uint32_t oldContainer, std::uint32_t newContainer, std::uint32_t player) noexcept
    {
        if (player == 0)
            return ContainerDirection::kUnrelated;
        const bool fromPlayer = oldContainer == player;
        const bool toPlayer = newContainer == player;
        if (fromPlayer && toPlayer)
            return ContainerDirection::kWithinPlayer;
        if (fromPlayer)
            return ContainerDirection::kLeftPlayer;
        if (toPlayer)
            return ContainerDirection::kEnteredPlayer;
        return ContainerDirection::kUnrelated;
    }

    constexpr const char* DirectionName(ContainerDirection direction) noexcept
    {
        switch (direction)
        {
        case ContainerDirection::kLeftPlayer: return "left-player";
        case ContainerDirection::kEnteredPlayer: return "entered-player";
        case ContainerDirection::kWithinPlayer: return "within-player";
        default: return "unrelated";
        }
    }

    constexpr bool IsTraceEnabledSetting(int value) noexcept
    {
        return value == 1;
    }

    constexpr std::int32_t CountDelta(std::int32_t oldCount, std::int32_t newCount) noexcept
    {
        const std::int64_t delta = static_cast<std::int64_t>(newCount) - static_cast<std::int64_t>(oldCount);
        if (delta > std::numeric_limits<std::int32_t>::max()) return std::numeric_limits<std::int32_t>::max();
        if (delta < std::numeric_limits<std::int32_t>::min()) return std::numeric_limits<std::int32_t>::min();
        return static_cast<std::int32_t>(delta);
    }
}
