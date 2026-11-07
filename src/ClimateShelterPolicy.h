#pragma once

#include <cmath>
#include <cstdint>

namespace RadClimateShelterPolicy
{
    inline constexpr float kProbeMoveDistance = 96.0f;
    inline constexpr float kProbeMoveDistanceSquared = kProbeMoveDistance * kProbeMoveDistance;
    inline constexpr float kReprobeSecondsOpen = 1.5f;
    inline constexpr float kReprobeSecondsEnclosed = 3.0f;
    inline constexpr int kRoofMinimumHits = 3;
    inline constexpr int kWallMinimumHits = 5;
    inline constexpr int kWallRays = 8;

    enum class Level : std::uint8_t
    {
        kOpen = 0,
        kRoofed,
        kEnclosed
    };

    struct Factors
    {
        float thermal01{ 0.0f };
        float precipitation01{ 0.0f };
        float enclosed01{ 0.0f };
        float windShelter01{ 0.0f };
    };

    struct Enclosure
    {
        bool roofed{ false };
        bool walled{ false };
        bool enclosed{ false };
    };

    struct Settled
    {
        Level value{ Level::kOpen };
        Level candidate{ Level::kOpen };
        bool known{ false };
        bool pending{ false };
    };

    [[nodiscard]] constexpr Factors FromLevel(Level level) noexcept
    {
        switch (level)
        {
        case Level::kEnclosed:
            return { 1.0f, 1.0f, 1.0f, 1.0f };
        case Level::kRoofed:
            return { 0.5f, 1.0f, 0.0f, 0.75f };
        case Level::kOpen:
        default:
            return {};
        }
    }

    [[nodiscard]] constexpr Factors FromState(bool indoors, bool roofed, bool unresolved) noexcept
    {
        if (indoors)
            return { 1.0f, 1.0f, 1.0f, 1.0f };
        if (unresolved)
            return { 1.0f, 1.0f, 0.0f, 1.0f };
        return FromLevel(roofed ? Level::kRoofed : Level::kOpen);
    }

    // Constraint: a listed roofless interior keeps indoor thermal shelter; its rain cover comes from the roof raycast.
    [[nodiscard]] constexpr Factors FromRooflessInterior(bool roofed) noexcept
    {
        return { 1.0f, roofed ? 1.0f : 0.0f, roofed ? 1.0f : 0.0f, 1.0f };
    }

    [[nodiscard]] constexpr bool RoofCover(bool upHit, int roofHits) noexcept
    {
        return upHit && roofHits >= kRoofMinimumHits;
    }

    [[nodiscard]] constexpr Enclosure Classify(bool roofed, int wallHits, bool enabled) noexcept
    {
        Enclosure result{};
        result.roofed = roofed;
        result.walled = wallHits >= kWallMinimumHits;
        result.enclosed = enabled && result.roofed && result.walled;
        return result;
    }

    [[nodiscard]] constexpr Level LevelFor(Enclosure enclosure) noexcept
    {
        if (enclosure.enclosed)
            return Level::kEnclosed;
        return enclosure.roofed ? Level::kRoofed : Level::kOpen;
    }

    [[nodiscard]] constexpr Settled Observe(Settled settled, Level observed) noexcept
    {
        if (!settled.known)
            return { observed, observed, true, false };
        if (observed == settled.value)
            return { settled.value, settled.value, true, false };
        if (settled.pending && observed == settled.candidate)
            return { observed, observed, true, false };
        return { settled.value, observed, true, true };
    }

    [[nodiscard]] inline bool ShouldReprobeTimed(Level settled, float elapsedSeconds) noexcept
    {
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0f)
            return true;
        const float limit = settled == Level::kEnclosed ? kReprobeSecondsEnclosed : kReprobeSecondsOpen;
        return elapsedSeconds >= limit;
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
