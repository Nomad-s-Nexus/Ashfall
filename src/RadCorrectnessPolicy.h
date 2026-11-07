#pragma once

#include "RadDomeVisibilityTable.h"
#include "RadIniParse.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
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

    enum class DomePhaseSlot { kApproach, kWarning, kActive, kAftermath };

    constexpr double kDomeSeqApproachEnd = 180.0;
    constexpr double kDomeSeqWarningEnd = 200.0;
    constexpr double kDomeSeqActiveEnd = 320.0;
    constexpr double kDomeSeqAftermathEnd = 360.0;

    // Constraint: a phase fraction that is NaN or outside [0,1] clamps to the phase edge, so no sequence time ever wraps.
    constexpr double DomeSequenceForPhase(DomePhaseSlot a_slot, double a_fraction)
    {
        double lo = 0.0;
        double hi = 0.0;
        switch (a_slot)
        {
        case DomePhaseSlot::kApproach: lo = 0.0; hi = kDomeSeqApproachEnd; break;
        case DomePhaseSlot::kWarning: lo = kDomeSeqApproachEnd; hi = kDomeSeqWarningEnd; break;
        case DomePhaseSlot::kActive: lo = kDomeSeqWarningEnd; hi = kDomeSeqActiveEnd; break;
        case DomePhaseSlot::kAftermath: lo = kDomeSeqActiveEnd; hi = kDomeSeqAftermathEnd; break;
        }
        const double f = !(a_fraction > 0.0) ? 0.0 : (a_fraction > 1.0 ? 1.0 : a_fraction);
        return lo + (hi - lo) * f;
    }

    // Constraint: a tunable that is not a number, or outside its range, falls back to its default.
    constexpr float DomeTunableOr(float raw, float lo, float hi, float fallback)
    {
        return (raw >= lo && raw <= hi) ? raw : fallback;
    }

    constexpr float kDomeVortexDistance = 1800.0f;
    constexpr float kDomeVortexHeight = 900.0f;
    constexpr float kDomeVortexScale = 3.0f;
    constexpr float kDomeRootScale = 0.1f;
    constexpr Position kDomeEyeAuthoredWorld = { 1575.0f, 1725.0f, 3281.25f };

    // Constraint: the vortex is moved by translating its nodes in the NIF's own space, so the shell meshes stay centred on the player.
    inline Position VortexLocalDelta(float a_yawRad, float a_distance, float a_height)
    {
        const float wantX = a_distance * std::sin(a_yawRad);
        const float wantY = a_distance * std::cos(a_yawRad);
        return {
            (wantX - kDomeEyeAuthoredWorld.x) / kDomeRootScale,
            (wantY - kDomeEyeAuthoredWorld.y) / kDomeRootScale,
            (a_height - kDomeEyeAuthoredWorld.z) / kDomeRootScale
        };
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

    // Invariant: sequence time is animTime + offset, so the offset that lands on the seek is the seek itself.
    constexpr float DomeSequenceOffset(float seekSec)
    {
        return (seekSec >= 0.0f && seekSec < 1.0e9f) ? seekSec : 0.0f;
    }

    constexpr int DomeShapeIndex(std::string_view name)
    {
        for (int i = 0; i < RadDomeVisibility::kShapeCount; ++i)
            if (name == RadDomeVisibility::kShapeNames[i]) return i;
        return -1;
    }

    constexpr std::uint64_t kAllDomeShapesVisible = (std::uint64_t{ 1 } << RadDomeVisibility::kShapeCount) - 1;

    constexpr std::string_view kDomeHiddenShapes[] = {
        "CoreBack", "CoreFront",
    };

    // Constraint: the vortex tube shapes are never drawn; see docs/code-notes/Ashfall.md.
    constexpr std::uint64_t DomeHiddenShapeMask()
    {
        std::uint64_t mask = 0;
        for (const auto name : kDomeHiddenShapes)
        {
            const int i = DomeShapeIndex(name);
            if (i >= 0) mask |= std::uint64_t{ 1 } << i;
        }
        return mask;
    }

    constexpr std::uint64_t kDomeShownShapes = kAllDomeShapesVisible & ~DomeHiddenShapeMask();

    // Invariant: a non-finite, negative or absurd sequence time yields every non-hidden shape, never a wider cull.
    constexpr std::uint64_t VisibleShapeMask(double seqTimeSec)
    {
        if (!(seqTimeSec >= 0.0) || seqTimeSec > 1.0e12) return kDomeShownShapes;
        const double period = static_cast<double>(RadDomeVisibility::kCycleSec);
        const double t = seqTimeSec - period * static_cast<double>(static_cast<long long>(seqTimeSec / period));
        std::uint64_t mask = 0;
        for (int i = 0; i < RadDomeVisibility::kShapeCount; ++i)
        {
            for (int w = RadDomeVisibility::kWindowBegin[i]; w < RadDomeVisibility::kWindowBegin[i + 1]; ++w)
            {
                const auto& win = RadDomeVisibility::kWindows[w];
                if (t >= static_cast<double>(win.start) && t <= static_cast<double>(win.end))
                {
                    mask |= std::uint64_t{ 1 } << i;
                    break;
                }
            }
        }
        return mask & kDomeShownShapes;
    }

    constexpr std::string_view kDomeDeckShapes[] = {
        "PlainHigh", "PlainMid", "PlainLow",
    };
    constexpr double kDomeDeckWindowStartSec = 128.0;
    constexpr double kDomeDeckWindowEndSec = 320.0;
    constexpr double kDomeDeckFadeSec = 12.0;

    // Constraint: a deck floor only raises alpha, so an authored key above the floor passes through unchanged.
    constexpr float DomeDeckFloorFor(std::string_view name)
    {
        if (name.rfind("Plain", 0) == 0) return 0.6f;
        return 0.0f;
    }

    constexpr std::uint64_t DomeDeckShapeMask()
    {
        std::uint64_t mask = 0;
        for (const auto name : kDomeDeckShapes)
        {
            const int i = DomeShapeIndex(name);
            if (i >= 0) mask |= std::uint64_t{ 1 } << i;
        }
        return mask;
    }

    // Constraint: the deck floor weight is 0 outside the portal window and ramps smoothly over the fade at each edge.
    constexpr float DomeDeckFloorWeight(double seqTimeSec)
    {
        if (!(seqTimeSec >= 0.0)) return 0.0f;
        const float t = static_cast<float>(seqTimeSec);
        const float rise = SmoothStep01(static_cast<float>(kDomeDeckWindowStartSec - kDomeDeckFadeSec),
            static_cast<float>(kDomeDeckWindowStartSec), t);
        const float fall = 1.0f - SmoothStep01(static_cast<float>(kDomeDeckWindowEndSec),
            static_cast<float>(kDomeDeckWindowEndSec + kDomeDeckFadeSec), t);
        return rise < fall ? rise : fall;
    }

    constexpr float DomeFloorAlpha(float keyedAlpha, float floorAlpha, float weight)
    {
        const float target = floorAlpha * weight;
        return keyedAlpha > target ? keyedAlpha : target;
    }

    constexpr std::uint64_t DomeDeckWindowMask(double seqTimeSec)
    {
        return (seqTimeSec >= kDomeDeckWindowStartSec && seqTimeSec <= kDomeDeckWindowEndSec) ? DomeDeckShapeMask() : 0;
    }

    // Constraint: the deck shapes are forced visible through the portal window only when the deck floor is switched on.
    constexpr std::uint64_t DomeShapeVisibility(double seqTimeSec, bool deckFloorOn)
    {
        if (!deckFloorOn) return VisibleShapeMask(seqTimeSec);
        return VisibleShapeMask(seqTimeSec) | (DomeDeckWindowMask(seqTimeSec) & kDomeShownShapes);
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

    // Constraint: god mode with [Survival] RespectGodMode keeps a plugin health drain from taking health to 0; heals (kDamage >= 0) pass through.
    constexpr float kGodModeFloorHealth = 1.0f;
    constexpr float GodModeClampedDrain(float currentHealth, float kDamage, bool godMode, bool respectGodMode)
    {
        if (!godMode || !respectGodMode || !(kDamage < 0.0f))
            return kDamage;
        const float room = currentHealth - kGodModeFloorHealth;
        if (!(room > 0.0f))
            return 0.0f;
        const float take = (-kDamage < room) ? -kDamage : room;
        return -take;
    }

    // Constraint: UI text keeps valid UTF-8 up to U+FFFF; controls, code points above U+FFFF and malformed or truncated sequences each become one '?'.
    inline std::string SanitizeUiText(std::string_view a_text)
    {
        std::string out;
        out.reserve(a_text.size());
        std::size_t i = 0;
        while (i < a_text.size())
        {
            const auto lead = static_cast<unsigned char>(a_text[i]);
            if (lead < 0x80)
            {
                out.push_back((lead < 0x20 || lead == 0x7F) ? '?' : static_cast<char>(lead));
                ++i;
                continue;
            }
            std::size_t len = 0;
            unsigned char lo = 0x80;
            unsigned char hi = 0xBF;
            if (lead >= 0xC2 && lead <= 0xDF)
                len = 2;
            else if (lead >= 0xE0 && lead <= 0xEF)
            {
                len = 3;
                if (lead == 0xE0) lo = 0xA0;
                if (lead == 0xED) hi = 0x9F;
            }
            else if (lead >= 0xF0 && lead <= 0xF4)
            {
                len = 4;
                if (lead == 0xF0) lo = 0x90;
                if (lead == 0xF4) hi = 0x8F;
            }
            if (len == 0)
            {
                out.push_back('?');
                ++i;
                continue;
            }
            std::size_t valid = 1;
            while (valid < len && i + valid < a_text.size())
            {
                const auto next = static_cast<unsigned char>(a_text[i + valid]);
                const unsigned char low = valid == 1 ? lo : 0x80;
                const unsigned char high = valid == 1 ? hi : 0xBF;
                if (next < low || next > high)
                    break;
                ++valid;
            }
            if (valid < len || len == 4)
            {
                out.push_back('?');
                i += valid;
                continue;
            }
            out.append(a_text.substr(i, len));
            i += len;
        }
        return out;
    }

    // Constraint: the debug-menu close refresh destroys and re-creates the overlay view 100 ms after close, pumped by the 4 Hz tick.
    constexpr std::uint64_t kDebugViewRefreshDelayMs = 100;

    struct DebugViewRefreshState
    {
        enum class Phase : std::uint8_t { kIdle, kArmed, kDestroyed };
        Phase phase = Phase::kIdle;
        std::uint64_t dueMs = 0;
    };

    enum class DebugViewRefreshAction : std::uint8_t { kNone, kWait, kDestroy, kCreate };

    struct DebugViewRefreshResult
    {
        DebugViewRefreshState state;
        DebugViewRefreshAction action;
    };

    // Constraint: only a close that follows an open arms the refresh, so a never-opened focus give-up does not.
    constexpr DebugViewRefreshState DebugViewRefreshArm(DebugViewRefreshState state, bool wasOpen, std::uint64_t nowMs)
    {
        if (!wasOpen || state.phase != DebugViewRefreshState::Phase::kIdle)
            return state;
        return { DebugViewRefreshState::Phase::kArmed, nowMs + kDebugViewRefreshDelayMs };
    }

    // Constraint: an open request cancels an armed refresh, and a destroyed view is always re-created.
    constexpr DebugViewRefreshResult DebugViewRefreshStep(DebugViewRefreshState state, std::uint64_t nowMs, bool openRequested)
    {
        using Phase = DebugViewRefreshState::Phase;
        switch (state.phase)
        {
        case Phase::kArmed:
            if (openRequested)
                return { DebugViewRefreshState{}, DebugViewRefreshAction::kNone };
            if (nowMs < state.dueMs)
                return { state, DebugViewRefreshAction::kWait };
            return { DebugViewRefreshState{ Phase::kDestroyed, 0 }, DebugViewRefreshAction::kDestroy };
        case Phase::kDestroyed:
            return { DebugViewRefreshState{}, DebugViewRefreshAction::kCreate };
        case Phase::kIdle:
        default:
            return { state, DebugViewRefreshAction::kNone };
        }
    }

    // Constraint: the 4 Hz engine tick is 250 ms of game time, so tick counts convert to the settle clock exactly.
    inline constexpr std::uint64_t kEngineTickMs = 250;
    // Constraint: after PostLoad/NewGame, no Prisma view is created, finalized, ordered or registered for 3 s (12 ticks).
    inline constexpr std::uint64_t kLoadSettleMs = 3000;
    // Constraint: the companion layout bridge waits at most 10 s (40 ticks) after load for PhosphorHUD's views.
    inline constexpr std::uint64_t kCompanionBridgeMaxTicks = 40;

    // Constraint: the settle window is closed while now is before loadMs + settleMs, and a clock behind the load is never settled.
    constexpr bool LoadSettleElapsed(std::uint64_t nowMs, std::uint64_t loadMs, std::uint64_t settleMs)
    {
        return nowMs >= loadMs && nowMs - loadMs >= settleMs;
    }

    // Constraint: outside companion mode the layout bridge never waits; in companion mode it waits for PhosphorHUD views or the tick cap.
    constexpr bool CompanionBridgeReady(bool companion, bool phosphorViewsSeen, std::uint64_t ticksSinceLoad)
    {
        return !companion || phosphorViewsSeen || ticksSinceLoad >= kCompanionBridgeMaxTicks;
    }

    // Constraint: a deferred DOM-ready handle is applied only while it still names the live view; 0 means nothing is pending.
    constexpr bool IsLiveDomReady(std::uint64_t pendingView, std::uint64_t liveView)
    {
        return pendingView != 0 && pendingView == liveView;
    }
}
