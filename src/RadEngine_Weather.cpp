#include "RadEngine_Internal.h"

using namespace RadInternal;

// Weather and sky: vanilla-radstorm thinning, engine-native smooth transitions, the dust roll-in.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


void RadiationEngine::CheckRadstormWarning()
{
    auto logger = Log();

    // Cell-exit grace: arm a short veto window whenever the player goes from sheltered to exposed
    // (interior load-door OR a roofed exterior base — IsPlayerSheltered covers both). While armed, the
    // veto below force-clears a vanilla radstorm regardless of its transition pct, so leaving Sanctuary
    // can't reliably dump the player straight into an already-rendered storm. Runs every tick, before
    // the early-outs below, so the countdown always advances.
    {
        const bool sheltered = IsPlayerSheltered();
        if (_prevSheltered && !sheltered)
        {
            _cellExitGraceTicks = kCellExitGraceTicks;
            logger->info("[Radstorm] Shelter->exposed transition — radstorm grace armed ({} ticks).",
                _cellExitGraceTicks);
        }
        _prevSheltered = sheltered;
    }
    if (_cellExitGraceTicks > 0) --_cellExitGraceTicks;

    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !sky->currentWeather)
    {
        _radstormWarned = false;
        return;
    }

    // DEBUG WEATHER HOLD: the debug menu's force-weather sets a sky with _stormPhase still Idle, which is
    // exactly the state the vetoes below fire in — so every forced dust/rain/radstorm sky was instantly
    // reverted to clear (the "it just turns into a clear day" bug). While a debug weather is held, stand
    // down entirely; "Release Forced Weather" (or a real storm starting) clears the hold.
    if (_debugWeatherHold) return;

    const uint32_t wid = sky->currentWeather->GetFormID();

    // New-game grace, dust + rain storms: GMW's (non-radioactive) dust weathers and any rain weather
    // aren't radstorms, but we still don't want one rolling in during the startup window. Veto at the
    // very start of the transition (invisible — the previous sky is still on screen). One already up at
    // load (full pct) is handled instead by the clear-on-entry force in UpdateStorm. Startup-only.
    const bool isDust = _dustWeathers.contains(wid);
    const bool isRain = IsRainyWeather(sky->currentWeather);
    if (_stormPhase == StormPhase::Idle &&
        _playSeconds < kRadstormStartupGraceSec &&
        (isDust || isRain))
    {
        if (sky->currentWeatherPct < kRadstormVetoMaxPct)
        {
            RE::TESWeather* revertTo = sky->lastWeather;
            if (!revertTo || _dustWeathers.contains(revertTo->GetFormID()) ||
                IsRadstormWeather(revertTo->GetFormID()) || IsRainyWeather(revertTo))
                revertTo = _clearWeather;   // don't revert into another storm/dust/rain sky
            if (revertTo)
            {
                sky->ForceWeather(revertTo, false);
                logger->info("[Radstorm] Startup grace: {} 0x{:06X} vetoed at pct {:.2f} -> "
                             "0x{:06X} (play={:.0f}s).",
                    isDust ? "dust storm" : "rain", wid, sky->currentWeatherPct,
                    revertTo->GetFormID(), _playSeconds);
            }
        }
        return;   // dust/rain handled; not a radstorm, nothing further to do
    }

    if (!IsRadstormWeather(wid))
    {
        // The radstorm weather cleared. Fade the incoming siren out with a short fall-off — previously
        // it only reset the flag and left the looping cue ringing long after the storm was gone.
        if (_radstormWarned) StopStormCue(kRadstormCueFalloffMs);
        _radstormWarned = false;   // armed again for the next storm
        return;
    }
    if (_radstormWarned) return;   // same storm we already accepted — let it run its course

    // A NEW vanilla radstorm just rolled in. Veto it if EITHER:
    //   * we're still in the new-game startup grace (no storms right as the player enters the world), or
    //   * another radstorm occurred within the cooldown (thin out back-to-back chains).
    // We don't touch the sky while our own managed storm is active (Idle phase only), and we leave
    // _radstormWarned false so a re-roll during the suppressed window is cancelled too.
    const bool inStartupGrace = _playSeconds < kRadstormStartupGraceSec;
    const bool inCooldown     = (_playSeconds - _lastRadstormSec) < _vanillaRadstormGapSec;
    const bool inCellExitGrace = _cellExitGraceTicks > 0;
    if (_stormPhase == StormPhase::Idle && (inStartupGrace || inCooldown || inCellExitGrace))
    {
        // Veto at ANY transition pct. This used to only intercept at the START (pct < kRadstormVetoMaxPct)
        // and otherwise FALL THROUGH and accept the storm ("caught too late — better one storm than a
        // jarring cut"). That was a real hole, and it is exactly why storms clustered around interior/
        // exterior transitions: our 4Hz hook does NOT tick during a loading screen, so a radstorm the
        // region system queued arrives at FULL pct the instant the exterior loads. Outside the 60s
        // cell-exit grace, every such storm was accepted even though the cooldown said veto — and
        // accepting it reset _lastRadstormSec, restarting the cooldown, so the next one came through too.
        //
        // The ONLY reason for the pct gate was to avoid snapping the sky mid-render. We now have the
        // engine-native smooth transition, so we revert SMOOTHLY at any pct (non-locking, so the region
        // system keeps control) and the hole is closed.
        RE::TESWeather* revertTo = sky->lastWeather;   // the sky still on screen right now
        if (!revertTo || IsRadstormWeather(revertTo->GetFormID()))
            revertTo = _clearWeather;                  // fallback only if no safe prior weather
        if (revertTo)
        {
            BeginSmoothWeather(revertTo, /*lockOverride=*/false);
            logger->info("[Radstorm] Vanilla radstorm 0x{:06X} vetoed at pct {:.2f} -> smoothly reverted to "
                         "0x{:06X} (reason: {}, play={:.0f}s).",
                wid, sky->currentWeatherPct, revertTo->GetFormID(),
                inCellExitGrace ? "cell-exit grace" : (inStartupGrace ? "startup grace" : "cooldown"),
                _playSeconds);
        }
        return;   // leave _radstormWarned false so a re-roll during the window is vetoed too
    }

    // Accepted: record the time so the next vanilla radstorm is gated by the cooldown.
    _lastRadstormSec = _playSeconds;
    _radstormWarned  = true;

    logger->info("[Radstorm] Incoming vanilla radstorm (weather 0x{:06X}) — soft ambient cue.", wid);
    PlayStormCue();
}


void RadiationEngine::BeginSmoothWeather(RE::TESWeather* a_target, bool a_lockOverride)
{
    // VANILLA-NATIVE smooth weather transition (decompiled from Sky::UpdateWeather @ RVA 0x64E530, the
    // path the region system uses for natural weather changes — NOT Sky::ForceWeather, which SNAPS).
    // The engine ramps currentWeatherPct 0->1 on its own whenever BOTH currentWeather AND lastWeather are
    // set, blending sky/colours/fog/imagespace smoothly over the target weather's Trans-Delta byte
    // (weatherData[3] @ +0xA6F). We set that transition up ONCE and then DO NOT TOUCH pct/lastWeather again
    // — that was the old bug (writing pct every tick fought the engine's ramp -> invalid blend -> the
    // sustained purple). overrideWeather locks the target so the region system doesn't transition away.
    //
    // WHY WE HAND-ROLL THIS instead of calling the engine's own Sky::SetWeatherExternal (RVA 0x651A60,
    // the real public smooth-transition API — decompile-verified in RE README-Weather.md): that function
    // sets overrideWeather/defaultWeather and DEFERS the actual currentWeather swap to the NEXT
    // UpdateWeather tick. Our per-tick storm logic reads `sky->currentWeather == <storm weather>`
    // (RadEngine_Storm.cpp Active-hit handoff, UpdateDustFogRoll's arrival watch) to know the storm sky
    // is live — so we need currentWeather set IMMEDIATELY and deterministically, which SetWeatherExternal
    // does not do. Switching to it is a real (tested) integration change, not a drop-in.
    //   KNOWN MINOR EDGE we forgo by hand-rolling: SetWeatherExternal(w, override, true) also arms a
    //   restart-anchor (Sky+0x394 bit 8 + Sky+0x390 = current pct) so a weather change that INTERRUPTS an
    //   in-progress transition resumes from where it was instead of restarting at 0%. We always reset pct
    //   to 0, so a storm that begins mid-transition can cosmetically jump once. Rare, and not worth
    //   destabilising the (hard-won) working path over — documented so it's a choice, not an oversight.
    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !a_target || sky->currentWeather == a_target) return;
    if (a_lockOverride)
        sky->overrideWeather = a_target;            // lock target (region won't override it back)
    sky->lastWeather       = sky->currentWeather;   // transition SOURCE (engine blends from this)
    sky->currentWeather    = a_target;              // transition TARGET
    sky->lastWeatherUpdate  = sky->currentGameHour; // stamp start time so elapsed begins at 0
    sky->currentWeatherPct  = 0.0f;                 // begin the blend; engine ramps it to 1.0 from here
    _weatherForced = true;
}


void RadiationEngine::UpdateDustFogRoll()
{
    // The dust sky now transitions via the ENGINE'S OWN smooth path (BeginSmoothWeather): the engine ramps
    // currentWeatherPct 0->1 and blends everything (colours, fog, imagespace) gradually — no snap, and no
    // sustained purple (the old manual per-tick pct writes were what produced an invalid blend). We do NOT
    // drive the sky here anymore; we only WATCH for the transition to finish (pct ~1, lastWeather cleared)
    // to fire the one-shot wall-arrival dust burst. Fog rolls in as part of the engine transition.
    if (!_dustBuildupActive) return;
    auto* sky = RE::Sky::GetSingleton();
    RE::TESWeather* dust = _stormWeathers[kDustStormIndex];
    if (!sky || !dust || sky->currentWeather != dust)
    {
        _dustBuildupActive = false;   // storm swapped/ended out from under us
        _dustFogRollTick   = 0;
        return;
    }

    // Arrival = the engine transition has completed (lastWeather cleared, pct at full).
    const bool arrived = (sky->lastWeather == nullptr) || (sky->currentWeatherPct >= 0.98f);
    if (_dustRollElapsed++ % 8 == 0)
        Log()->info("[RollDiag] pct={:.2f} lastWeather={} (engine-native transition)",
            sky->currentWeatherPct, sky->lastWeather ? "set" : "cleared");
    if (arrived)
    {
        _dustBuildupActive = false;
        _dustFogRollTick   = 0;
        Log()->info("[StormFog] Dust Storm transition complete (engine-native, no snap).");

        // PURPLE-FIX RUNTIME VERIFICATION: read BACK the LIVE dust weather's imagespace slots now that
        // the storm is fully current (pct=1). If all 8 read a CommonwealthDusty id (0x001F61Ax) the
        // engine is holding our repoint, not the old radstorm grades (0x0022458x) -- so if the sky is
        // STILL purple, the tint is NOT the imagespace (look to the Aurora RadStormSkyEffect.nif / ENB).
        // This is the evidence, not the write-side "repointed 8/8" claim.
        {
            auto lg = Log();
            float hour = -1.0f;
            if (auto* cal = RE::Calendar::GetSingleton(); cal && cal->gameHour)
                hour = cal->gameHour->GetValue();
            RE::TESImageSpace* isNight = dust->imageSpace[3];
            RE::TESImageSpace* isDay   = dust->imageSpace[1];
            lg->info("[PurpleDiag] LIVE dust weather @ hour {:.1f}: imageSpace Day(1)=0x{:06X} Night(3)=0x{:06X} "
                     "(CommonwealthDusty=0x1F61A5/0x1F61A9, old radstorm=0x224586/0x22458C).",
                hour, isDay ? isDay->GetFormID() : 0, isNight ? isNight->GetFormID() : 0);
            bool allDusty = true;
            for (int k = 0; k < 8; ++k)
            {
                RE::TESImageSpace* is = dust->imageSpace[k];
                const std::uint32_t id = is ? is->GetFormID() : 0;
                if (id < 0x1F61A2 || id > 0x1F61A9) allDusty = false;
            }
            lg->info("[PurpleDiag] all 8 imagespace slots CommonwealthDusty (brown) = {}.", allDusty ? "YES" : "NO");
        }

        // WALL ARRIVAL: fire a short heavy camera-dust gust over the steady dust VE (exterior only).
        // ApplyArtObject is a native temp-effect spawn (same class as the storm's ghoul spawns already
        // placed on the main-thread tick), attach-to-camera + inherit-rotation, self-expires
        // after kDustBurstSec so it reverts to the steady FXCameraAttachExplosionDustVE on its own.
        if (_dustBurstArt && !IsPlayerSheltered())
        {
            if (auto* player = RE::PlayerCharacter::GetSingleton())
            {
                player->ApplyArtObject(_dustBurstArt, kDustBurstSec, nullptr, /*attachToCamera=*/true,
                    /*inheritRotation=*/true);
                Log()->info("[StormFog] Dust wall-arrival heavy-dust burst fired ({:.0f}s).", kDustBurstSec);
            }
        }
    }
}
