#pragma once

#include "RadIniParse.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string_view>

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

    // Invariant: a shortened debug lead shifts the dome sequence by (natural - actual) seconds.
    constexpr double DomeLeadShiftSeconds(double naturalLeadSec, double actualLeadSec)
    {
        if (!(naturalLeadSec > 0.0) || !(actualLeadSec >= 0.0)) return 0.0;
        return naturalLeadSec > actualLeadSec ? naturalLeadSec - actualLeadSec : 0.0;
    }

    constexpr float DomeSeekSecondsForStorm(double elapsedSec, float timelineSec, double leadShiftSec)
    {
        if (!(elapsedSec > 0.0)) elapsedSec = 0.0;
        if (!(leadShiftSec > 0.0)) leadShiftSec = 0.0;
        return DomeSeekSeconds(elapsedSec + leadShiftSec, timelineSec);
    }

    constexpr float SmoothStep01(float edge0, float edge1, float x)
    {
        if (!(edge1 > edge0)) return x >= edge1 ? 1.0f : 0.0f;
        const float t = x <= edge0 ? 0.0f : (x >= edge1 ? 1.0f : (x - edge0) / (edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }

    constexpr float kDomeBlendRiseStartSec = 20.0f;
    constexpr float kDomeBlendRiseEndSec = 70.0f;
    constexpr float kDomeBlendFallSec = 30.0f;

    // Invariant: vanilla clouds fade out only as the dome cloud layers fade in on the baked sequence clock.
    constexpr float BlendForSequence(double seqTimeSec, float timelineSec)
    {
        if (!(seqTimeSec > 0.0) || !(timelineSec > kDomeBlendRiseEndSec + kDomeBlendFallSec)) return 0.0f;
        const float t = DomeSeekSeconds(seqTimeSec, timelineSec);
        const float rise = SmoothStep01(kDomeBlendRiseStartSec, kDomeBlendRiseEndSec, t);
        const float fall = 1.0f - SmoothStep01(timelineSec - kDomeBlendFallSec, timelineSec, t);
        return rise < fall ? rise : fall;
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

    // Constraint: the power-armor HUD replaces the visor overlay, so a worn PA helmet hides it; a debug override wins.
    // Constraint: the visor is the wearer's own view, so any non-first-person camera hides it.
    constexpr bool MaskOverlayVisible(int a_debugOverride, bool a_wornMask, bool a_powerArmorHelmet, bool a_firstPerson)
    {
        if (a_debugOverride == 1)
            return true;
        if (a_debugOverride == 0)
            return false;
        return a_wornMask && !a_powerArmorHelmet && a_firstPerson;
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

    // Constraint: the auto-don needs 3 s of stable exposure at the 4 Hz update rate.
    inline constexpr int kAutoMaskStableTicks = 12;

    // Constraint: the choke warning waits one tick past the auto-don so a masked player never hears a spurious gasp.
    inline constexpr int kChokeGraceTicks = kAutoMaskStableTicks + 1;

    // Constraint: the auto-don needs a stable exposure reading, so a doorway or roof edge cannot flap the mask.
    struct AutoMaskStep
    {
        bool equip;
        bool remove;
        int  applied;
        int  candidate;
        int  streak;
    };

    constexpr AutoMaskStep StepAutoMask(
        int  a_applied,
        int  a_candidate,
        int  a_streak,
        bool a_exposedNow,
        int  a_requiredStableTicks)
    {
        const int sample = a_exposedNow ? 1 : 0;
        if (a_applied < 0)
            return { false, false, sample, sample, 0 };
        int candidate = a_candidate;
        int streak = a_streak;
        if (candidate == sample)
            ++streak;
        else
        {
            candidate = sample;
            streak = 1;
        }
        if (streak < a_requiredStableTicks || candidate == a_applied)
            return { false, false, a_applied, candidate, streak };
        return { candidate == 1, candidate == 0, candidate, candidate, 0 };
    }

    // Constraint: an INI hotkey accepts decimal or 0x-hex with an optional trailing "; comment"; 0 disables it, a leading '+', negatives and junk are rejected.
    inline bool ParseHotkey(std::string_view a_text, int& a_out)
    {
        a_text = a_text.substr(0, a_text.find(';'));
        std::uint32_t value = 0;
        if (!RadIniParse::ParseNumber(a_text, 0xFFF, value))
            return false;
        a_out = static_cast<int>(value);
        return true;
    }

    // Constraint: F8 is the debug menu key, mouse buttons and Shift/Esc are never mask keys, 0 disables one.
    constexpr bool IsUsableHotkey(int a_key)
    {
        if (a_key == 0)
            return true;
        if (a_key < 1 || a_key > 0xFF)
            return false;
        if (a_key <= 6)
            return false;
        return a_key != 0x10 && a_key != 0x1B && a_key != 0x77 && a_key != 0xA0 && a_key != 0xA1;
    }

    struct HotkeyPair
    {
        int  replace;
        int  repair;
        bool replaceRejected;
        bool repairRejected;
        bool duplicateRejected;
    };

    // Constraint: a rejected key disables only its action, but a duplicate disables both so load order never decides.
    constexpr HotkeyPair ValidateHotkeyPair(int a_replace, int a_repair)
    {
        HotkeyPair result{ a_replace, a_repair, false, false, false };
        if (!IsUsableHotkey(a_replace))
        {
            result.replace = 0;
            result.replaceRejected = true;
        }
        if (!IsUsableHotkey(a_repair))
        {
            result.repair = 0;
            result.repairRejected = true;
        }
        if (result.replace != 0 && result.replace == result.repair)
        {
            result.replace = 0;
            result.repair = 0;
            result.duplicateRejected = true;
        }
        return result;
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
