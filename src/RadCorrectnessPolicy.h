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

    // Constraint: the power-armor HUD replaces the visor overlay, so a worn PA helmet hides it; a debug override wins.
    constexpr bool MaskOverlayVisible(int a_debugOverride, bool a_wornMask, bool a_powerArmorHelmet)
    {
        if (a_debugOverride == 1)
            return true;
        if (a_debugOverride == 0)
            return false;
        return a_wornMask && !a_powerArmorHelmet;
    }

    // Constraint: an interior, a sheltered spot, a powered shelter and a sealed suit are not rad-weather exposure.
    constexpr bool RadWeatherExposed(
        bool a_inRadWeather,
        bool a_interior,
        bool a_sheltered,
        bool a_poweredShelter,
        bool a_powerArmor)
    {
        return a_inRadWeather && !a_interior && !a_sheltered && !a_poweredShelter && !a_powerArmor;
    }

    // Constraint: the auto-don acts only on the exposure transition, so a manual unequip while exposed is respected.
    constexpr bool ShouldAutoEquipMask(bool a_wasExposed, bool a_exposed, bool a_wearingMask)
    {
        return a_exposed && !a_wasExposed && !a_wearingMask;
    }

    constexpr bool ShouldAutoRemoveMask(bool a_wasExposed, bool a_exposed, bool a_autoEquipped)
    {
        return !a_exposed && a_wasExposed && a_autoEquipped;
    }

    // Constraint: F8 is reserved for the debug menu and the two mask keys must differ; 0 means disabled.
    constexpr bool IsUsableHotkey(int a_key, int a_otherKey)
    {
        if (a_key == 0)
            return true;
        if (a_key < 1 || a_key > 0xFF)
            return false;
        if (a_key == 0x77)
            return false;
        return a_otherKey == 0 || a_key != a_otherKey;
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
