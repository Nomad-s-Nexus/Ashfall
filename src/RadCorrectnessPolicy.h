#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace RadCorrectnessPolicy
{
    struct Position
    {
        float x;
        float y;
        float z;

        auto operator==(const Position&) const -> bool = default;
    };

    constexpr Position DomePosition(Position player, float zOffset)
    {
        return { player.x, player.y, player.z + zOffset };
    }

    struct NukePlacement
    {
        Position strike;
        float facingYaw;
    };

    inline NukePlacement DistantNukePlacement(Position player, float originYaw, float distance, float zOffset)
    {
        const Position strike{
            player.x + distance * std::sin(originYaw),
            player.y + distance * std::cos(originYaw),
            player.z + zOffset
        };
        return { strike, std::atan2(player.x - strike.x, player.y - strike.y) };
    }

    constexpr bool ShouldReturnToClear(bool managedStorm, bool vanillaRadstorm)
    {
        return managedStorm || vanillaRadstorm;
    }

    constexpr bool ShouldRecreateDomeForOwner(bool domeUp, [[maybe_unused]] bool sameCell, bool sameWorld)
    {
        // Exterior parent-cell changes are normal streaming while the player moves through one
        // worldspace. Recreating every layer on those boundaries causes the visible dome snap/jitter.
        // Only a real worldspace ownership change requires destructive recreation.
        return domeUp && !sameWorld;
    }

    constexpr bool ShouldRetryDomeSpawn(bool domePhase, bool exposed, bool domeUp, int retryTicks)
    {
        return domePhase && exposed && !domeUp && retryTicks >= 4;
    }

    constexpr bool IsGasMask(bool fillsMouth, bool hasKeyword)
    {
        return fillsMouth && hasKeyword;
    }

    enum class CureSignal
    {
        kNone,
        kInventoryMovement,
        kVerifiedChelationUse,
        kVerifiedRadAwayUse
    };

    constexpr bool CuresPermanentTissue(CureSignal signal)
    {
        return signal == CureSignal::kVerifiedChelationUse;
    }

    constexpr bool ArmsRadAwayHealing(CureSignal signal)
    {
        return signal == CureSignal::kVerifiedRadAwayUse;
    }

    constexpr bool IsEngineActive(bool coreInitialized, bool updateProducerInstalled)
    {
        return coreInitialized && updateProducerInstalled;
    }

    constexpr bool IsCurrentLoadGeneration(std::uint64_t expected, std::uint64_t actual)
    {
        return expected == actual;
    }

    constexpr bool ShouldStartHudWait(bool alreadyWaiting, std::uint64_t expectedLoad, std::uint64_t actualLoad)
    {
        return !alreadyWaiting && IsCurrentLoadGeneration(expectedLoad, actualLoad);
    }

    inline std::uint64_t BeginNewGameLoad(
        std::atomic<std::uint64_t>& loadGeneration,
        std::atomic<bool>& hudWaiting)
    {
        // Invalidate queued old-generation work before releasing its waiter latch. Otherwise a stale
        // waiter can leave the new game unable to claim the single HUD-wait slot.
        const auto nextGeneration = loadGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
        hudWaiting.store(false, std::memory_order_release);
        return nextGeneration;
    }
}
