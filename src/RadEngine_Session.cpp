#include "RadEngine_Internal.h"
#include "RadMaskOwnership.h"

using namespace RadInternal;


float RadiationEngine::GameDays() const
{
    auto* cal = RE::Calendar::GetSingleton();
    if (cal && cal->gameDaysPassed)
        return cal->gameDaysPassed->GetValue();
    return 0.0f;
}


void RadiationEngine::RevertState()
{
    _playSeconds      = 0.0;
    _nextStormPlaySec = 0.0;
    _lastRadstormSec  = -1.0e30;
    _lastRadWeatherFormID = 0;
    _stormScheduled   = false;
    ResetStormGhoulStateForLoad();
    ResetStormDomeForLoad();
    // Constraint: fire handles die with the session; there is nothing left here to disable.
    _fireRefs.clear();
    _filterCharge     = 100.0f;   // fresh full filter for a new game / co-saveless load
    _filterLowWarned  = 0;
    _maskCondition    = 100.0f;
    _conditionLowWarned = 0;
    RadMaskOwnership::Clear();
    Log()->info("[Revert] Storm clock reset (new game / pre-load).");
}


void RadiationEngine::Notify(const char* message, bool warning) const
{
    RE::SendHUDMessage::ShowHUDMessage(message, "", true, warning);
}


void RadiationEngine::OnNewGame()
{
    _forceClearOnEntry = true;
}


void RadiationEngine::OnPostLoad()
{
    auto logger = Log();
    ResetStormGhoulStateForLoad();
    _forceClearOnEntry = false;
    _spawnRadBlockTicks = 0;

    if (_tissueGlobal)
        _tissueDamage = _tissueGlobal->GetValue();
    if (_permanentTissueGlobal)
        _permanentTissueDamage = _permanentTissueGlobal->GetValue();

    _prevRads = -1.0f;
    _prevHP   = -1.0f;
    _prevGameDays = -1.0f;
    _radAwayWindowTicks = 0;

    _maskWasOn = -1;

    _chokeTicks = 0;
    _chokeCalmTicks = 0;
    _chokeSoundTick = 0;
    _chokeNotified = false;

    _wasInRadWeather = -1;
    _lastRadWeatherFormID = 0;
    _autoMaskEquipped = false;
    _npcMaskScanTick = 0;
    _autoMaskedNpcs.clear();
    const auto ownership = RadMaskOwnership::Snapshot();
    if (!ownership.empty())
    {
        _wasInRadWeather = 1;
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto playerID = player ? player->GetFormID() : 0;
        for (const auto& entry : ownership)
        {
            if (playerID != 0 && entry.actorFormID == playerID)
                _autoMaskEquipped = true;
            else
                _autoMaskedNpcs.insert(entry.actorFormID);
        }
    }

    _appliedEndPenalty = 0;
    _appliedStrPenalty = 0;
    _appliedCarryPen = 0.0f;
    _appliedHealPen  = 0.0f;
    _appliedSpeedPen = 0.0f;
    _appliedApPen    = 0.0f;
    _symptomTick     = 0;
    _symptomWarned   = 0;

    DispelNegativeSpecials();


    if (_stormScheduled && _playSeconds >= _nextStormPlaySec)
    {
        ScheduleNextStorm();
        logger->info("[Storm] Loaded a save with a past-due storm schedule — re-armed a full gap out "
                     "(was 4-8 min, which caused back-to-back storms on reload).");
    }
    ClearFireRefs();
    ClearStormGhouls();
    ClearStormIMAD();
    TriggerStormFX(false);  // clear shake left dangling by the load
    ClearStormDome();
    _pendingLoadFXCleanup = true;
    if (_stormSoundHandle.IsValid()) _stormSoundHandle.FadeOutAndRelease(500);  // kill leftover storm audio
    if (_ambientBedHandle.IsValid()) _ambientBedHandle.FadeOutAndRelease(500);  // and the general bed
    _curStormBed = nullptr; _curAmbientBed = nullptr;  // re-derived from weather after load
    _stormPhase        = StormPhase::Idle;
    _shakeBurstStep    = -1;   // disarm shake (direct reset bypasses EnterStormPhase teardown); a save made
    _shakeRumbleTick   = -1;   // mid-lethal-storm otherwise leaves these armed and the NEXT storm rumbles
    _stormTicksLeft    = 0;
    _activeStormType   = -1;
    _forcedStormType   = -1;
    _forceStormNow     = false;
    _stormFXVisible    = false;
    _stormNukeFired    = false;
    _firestormAwaitingNuke = false;  // drop any mid-firestorm nuke-wait so a load can't strand Active
    _nukeAttempts      = 0;
    _nukeWaitTicks     = 0;
    _shakeBurstRetries = 0;
    _debugSkipToActive = false;
    _stormRollInTicks  = 0;   // clear any mid-firestorm-roll-in state so a stale value can't force the HUD theme
    _dustBuildupActive = false; // else a save loaded mid-dust-warning leaves this set and the NEXT storm's
    _stormPeakFired    = false; // Warning phase is wrongly suppressed by the inRadWeather build-up gate
    _stormExposedTicks = 0;
    _weatherForced     = false;
    _weatherHandoffWaitTicks = 0;
    _debugWeatherHold  = false;   // never let a debug weather hold survive a load
    RadHud::GetSingleton().PushStorm(static_cast<int>(StormPhase::Idle), 0, 0.0f, true, false);

    auto* cal = RE::Calendar::GetSingleton();
    if (cal)
    {
        float currentTime = GameDays();
        if (_lastGameTime > 0.0f && currentTime > _lastGameTime)
        {
            float hoursPassed = (currentTime - _lastGameTime) * 24.0f;
            if (hoursPassed >= 0.5f && hoursPassed <= 48.0f)
            {
                float rads = 0.0f;
                if (auto* p = RE::PlayerCharacter::GetSingleton())
                    if (auto* av = RE::ActorValue::GetSingleton(); av && av->rads)
                        rads = p->GetActorValue(*av->rads);
                ApplyRestTissueHeal(hoursPassed, rads);
            }
        }
        _lastGameTime = currentTime;
    }
    else
    {
        logger->warn("[Sleep] RE::Calendar unavailable; sleep healing skipped this load.");
    }
}
