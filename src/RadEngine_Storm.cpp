#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include "RadNeedsTuning.h"
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>

using namespace RadInternal;

// Storm lifecycle: scheduling, phases, FX (fire ring / ghouls / IMAD / quest stages), NPC effects, exposure.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


void RadiationEngine::EffectiveStormGap(float& a_min, float& a_max) const
{
    a_min = std::max(1.0f, RadNeedsTuning::Value(RadNeedsTuningPolicy::kStormMinMinutes, _stormMinRealMin));
    a_max = std::max(a_min, RadNeedsTuning::Value(RadNeedsTuningPolicy::kStormMaxMinutes, _stormMaxRealMin));
}


void RadiationEngine::ScheduleNextStorm()
{
    static std::mt19937 rng{ std::random_device{}() };
    // Gap comes from the INI ([Storm] MinMinutes/MaxMinutes) so players can dial storm frequency down
    // without a rebuild — "storms happen too much" is the most common complaint.
    float lo = 0.0f, hi = 0.0f;
    EffectiveStormGap(lo, hi);
    std::uniform_real_distribution<float> mins(lo, hi);
    const float realMin = mins(rng);

    // Schedule by REAL minutes of actual play (the 4Hz hook only ticks during active gameplay), NOT
    // game-time. GameDaysPassed is unreliable for this: it barely moves, the 2077->2287 jump does NOT
    // advance it, and a prewar-set game-time schedule would be reached early in postwar play (storms
    // firing minutes in). _playSeconds is a pure real-play clock, so storms are exactly realMin apart.
    _nextStormPlaySec = _playSeconds + static_cast<double>(realMin) * 60.0;
    _stormScheduled   = true;
    Log()->info(
        "[Storm] Next storm in ~{:.0f} real min of play (at {:.0f}s, now {:.0f}s).",
        realMin, _nextStormPlaySec, _playSeconds);
}
void RadiationEngine::BeginStorm(int typeIndex)
{
    if (typeIndex < 0 || typeIndex >= kStormTypeTableSize) return;
    auto logger = Log();

    _activeStormType   = typeIndex;
    _stormExposedTicks = 0;
    _fleeTick          = 0;
    _stormFXVisible    = false;  // no screen FX until the storm hits (and only if exposed)
    _stormNukeFired    = false;  // re-arm the one-time firestorm nuke for this storm
    _firestormAwaitingNuke = false;  // set true at Active onset for a firestorm
    _stormRollInTicks  = 0;
    _nukeAttempts      = 0;      // reset actual nuke submissions
    _nukeWaitTicks     = 0;      // reset the absolute nuke wait
    _shakeBurstRetries = 0;      // reset the bounded shake-burst retry counter
    _debugSkipToActive = false;  // clear any leftover debug fast-forward from a prior storm
    const StormType& st = kStormTypes[typeIndex];

    // STAGED ONSET (R.'s spec): dome-using storms begin with a silent visual lead; Firestorm begins
    // silently without a dome. The lead has NO siren, NO alert, and NO weather change. Only when it
    // elapses does EnterWarningPhase() fire the alarm and begin the dust-sky roll-in + the rest of the storm.
    // A debug-forced storm may shorten the silent lead (_forcedLeadTicks); consume it here so the
    // NEXT natural storm is back to the configured DomeLeadMinutes.
    const bool debugForced = _forcedStormType >= 0;
    const int leadTicks = (debugForced && _forcedLeadTicks >= 0) ? _forcedLeadTicks : _domeLeadTicks;
    _forcedLeadTicks = -1;
    _activeLeadTicks = leadTicks;
    _stormDomeBeginSec = _playSeconds;
    _stormDomeLeadShiftSec = RadCorrectnessPolicy::DomeLeadShiftSeconds(
        static_cast<double>(_domeLeadTicks) / 4.0, static_cast<double>(leadTicks) / 4.0);
    logger->warn("========== STORM APPROACHING: {} ({}) — silent lead {} s ==========",
        st.name, st.lethal ? "LETHAL" : "ambient", leadTicks / 4);
    _sirenDelayTicks = 0;
    _dustBuildupActive = false;   // no weather build-up during the silent lead
    _debugWeatherHold  = false;   // a real storm takes over from any debug-held weather

    SpawnStormDome();

    EnterStormPhase(StormPhase::Approach, leadTicks);
    logger->info("[StormSeq] t=0 APPROACH begin: silent. Alarm in {} s.", leadTicks / 4);
}
// APPROACH lead elapsed: the storm commits. Fire the alarm + alert together, (re)ensure the dome, and
// kick off the dust-sky roll-in — then hand to the existing Warning phase (dust build-up) -> Active.
void RadiationEngine::EnterWarningPhase()
{
    if (_activeStormType < 0 || _activeStormType >= kStormTypeTableSize) return;
    auto logger = Log();
    const StormType& st = kStormTypes[_activeStormType];
    const int warnTicks = st.warningTicks;
    EnterStormPhase(StormPhase::Warning, warnTicks);

    // The alarm goes up now (not at spawn), together with the alert text.
    if (!IsPlayerSheltered())
    {
        PlayStormCue();
    }
    UpdateStormFXVisibility();
    char msg[96];
    if (st.lethal)
        std::snprintf(msg, sizeof(msg), "WARNING: %s approaching! SEEK SHELTER NOW!", st.name);
    else
        std::snprintf(msg, sizeof(msg), "WARNING: %s is rolling in.", st.name);
    Notify(msg, true);
    logger->info("[StormSeq] WARNING begin: alarm + alert fired.");

    // Dust Storm horizon build-up (Option B) begins HERE (with the alarm), via the engine's own smooth
    // transition — no snap, no purple flash. Gameplay stays gated to Active (inRadWeather suppressed
    // during the build-up). Non-dust storms snap their sky at the Active hit instead.
    if (_activeStormType == kDustStormIndex)
    {
        if (auto* sky = RE::Sky::GetSingleton())
        {
            RE::TESWeather* dust = _stormWeathers[kDustStormIndex];
            RE::TESWeather* prev = sky->currentWeather;
            if (dust && prev && prev != dust)
            {
                CaptureWeatherReturnTarget();
                BeginSmoothWeather(dust);
                _dustBuildupActive = true;
                _warnTicksTotal    = warnTicks;
                _dustRollElapsed   = 0;
                _dustRollTotal     = warnTicks;
                logger->info("[StormSeq] dust-sky roll-in armed (from 0x{:06X}).", prev->GetFormID());
            }
        }
    }
}
int RadiationEngine::RollStormType() const
{
    int total = 0;
    for (const auto& s : kStormTypes) total += s.weight;
    if (total <= 0) return 0;

    static std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> pick(0, total - 1);
    int r = pick(rng);
    for (int i = 0; i < kStormTypeTableSize; ++i)
    {
        r -= kStormTypes[i].weight;
        if (r < 0) return i;
    }
    return 0;
}
void RadiationEngine::EnterStormPhase(StormPhase phase, int durationTicks)
{
    _stormPhase = phase;

    // Debug fast-forward (skipwarn / skiptoactive): collapse the lead + warning to a single tick each so
    // a tester reaches Active in ~2 ticks instead of ~100 s, while STILL running every real transition
    // (this function is the only transition path) and the Active onset. Cleared once Active is reached.
    if (_debugSkipToActive && (phase == StormPhase::Approach || phase == StormPhase::Warning))
        durationTicks = 1;
    if (phase == StormPhase::Active)
        _debugSkipToActive = false;

    _stormTicksLeft = durationTicks;

    // Leaving Active ends the shake and the firestorm nuke wait. The native shake decays on its own once
    // we stop re-triggering it, so disarming the counters is the whole teardown.
    if (phase != StormPhase::Active)
    {
        _shakeBurstStep    = -1;
        _shakeRumbleTick   = -1;
        _shakeBurstRetries = 0;
        _firestormAwaitingNuke = false;
        _nukeAttempts      = 0;
        _nukeWaitTicks     = 0;
    }
}
