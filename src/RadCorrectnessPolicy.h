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

    inline NukePlacement DistantNukePlacementXY(Position player, float originYaw, float distance)
    {
        const Position strike{
            player.x + distance * std::sin(originYaw),
            player.y + distance * std::cos(originYaw),
            0.0f
        };
        return { strike, std::atan2(player.x - strike.x, player.y - strike.y) };
    }

    constexpr bool StormUsesDome(int theme)
    {
        return theme != 1;
    }

    constexpr float DomeSeekSeconds(double elapsedSec, float timelineSec)
    {
        if (!(elapsedSec > 0.0) || !(timelineSec > 1.0f)) return 0.0f;
        const double period = static_cast<double>(timelineSec);
        const double wrapped = elapsedSec - period * static_cast<double>(static_cast<long long>(elapsedSec / period));
        if (!(wrapped > 0.0) || !(wrapped < period)) return 0.0f;
        return static_cast<float>(wrapped);
    }

    constexpr double WrapAngleRad(double a)
    {
        constexpr double kTau = 6.283185307179586;
        const double r = a - kTau * static_cast<double>(static_cast<long long>(a / kTau));
        const double w = r < 0.0 ? r + kTau : r;
        return w >= kTau ? 0.0 : w;
    }

    constexpr double SpinAngleRad(double degPerSec, double seconds)
    {
        constexpr double kPi = 3.141592653589793;
        return WrapAngleRad(degPerSec * seconds * kPi / 180.0);
    }

    constexpr double ClampDomeAnimDelta(double dt, double maxDt)
    {
        return !(dt > 0.0) ? 0.0 : (dt > maxDt ? maxDt : dt);
    }

    constexpr bool ShouldMoveDomeLayer(float distSq, float epsSq)
    {
        return distSq > epsSq;
    }

    constexpr float StormShakeStrength(float base, bool interior)
    {
        return interior ? base * 0.35f : base;
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

    // Constraint: a mask listed in the INI is protective whatever slot it fills, because the entry is the declaration.
    constexpr bool IsConfiguredMask(bool listed, bool fillsMouth, bool hasKeyword)
    {
        return listed || IsGasMask(fillsMouth, hasKeyword);
    }

    // Constraint: a mouth-slot armor with radiation resistance is a gas mask, so a mod's mask needs no INI entry.
    constexpr bool IsRadShieldedMask(bool fillsMouth, bool radResistant)
    {
        return fillsMouth && radResistant;
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
