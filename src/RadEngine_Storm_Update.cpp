#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>

using namespace RadInternal;



void RadiationEngine::UpdateStorm()
{
    auto logger = Log();
    UpdateWeatherHandoff();

    if (_stormPhase != StormPhase::Active && _wasInRadWeather == 0 &&
        !_autoMaskedNpcs.empty() && --_npcMaskScanTick <= 0)
    {
        _npcMaskScanTick = kNpcMaskRescanTicks;
        EquipNearbyNpcMasks(false);
    }

    if (_forceClearOnEntry)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* cell   = player ? player->GetParentCell() : nullptr;
        auto* ui     = RE::UI::GetSingleton();
        auto* sky    = RE::Sky::GetSingleton();
        const bool exterior = cell && !cell->IsInterior();      // not an interior load door
        const bool inGame   = ui && ui->menuMode == 0;          // not a loading screen / menu
        float year = 0.0f;
        if (auto* cal = RE::Calendar::GetSingleton(); cal && cal->gameYear)
            year = cal->gameYear->GetValue();
        const bool postwar = year > kPostwarMinYear;            // 2287, not the 2077 prewar intro

        RE::TESWeather* cur = sky ? sky->currentWeather : nullptr;
        const bool spawnedInStorm = cur && (IsRadstormWeather(cur->GetFormID())
                                            || _dustWeathers.contains(cur->GetFormID())
                                            || IsRainyWeather(cur));

        if (exterior && inGame && (postwar || spawnedInStorm))
        {
            if (sky)
            {
                sky->ReleaseWeatherOverride();              // drop any stale forced sky first
                if (_clearWeather) sky->ForceWeather(_clearWeather, false);
            }
            StopStormCue(kRadstormCueFalloffMs);
            if (auto* av = RE::ActorValue::GetSingleton(); av && av->rads)
            {
                auto* pc = RE::PlayerCharacter::GetSingleton();
                const float rads = pc ? pc->GetActorValue(*av->rads) : 0.0f;
                if (pc && rads > 0.0f)
                    pc->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->rads, -rads);
            }
            _spawnRadBlockTicks = 20;     // ~5s: refund any trailing rads as the storm weather fades
            _weatherForced     = false;   // not our storm override
            _forceClearOnEntry = false;
            _stormScheduled    = false;   // first storm is a full gap out from here
            _stormPhase        = StormPhase::Idle;
            _activeStormType   = -1;
            _radstormWarned    = false;   // intercepted; re-arm for a later real radstorm
            logger->info("[Storm] New-game entry: forced clear weather (0x0002B52A) + cleared spawn rads. "
                         "spawnedInStorm={}, weatherWas=0x{:06X}, year={:.0f}.",
                spawnedInStorm, cur ? cur->GetFormID() : 0u, year);
        }
    }

    if (!_nextEmissionGlobal) return;

    if (_forceStormGlobal)
    {
        const int v = static_cast<int>(_forceStormGlobal->GetValue());
        if (v != 0)
        {
            _forceStormGlobal->value = 0.0f;  // consume
            if (v >= 1 && v <= kStormTypeTableSize)
                DebugForceStorm(v - 1);
        }
    }

    if (_stormPhase != StormPhase::Idle &&
        (_activeStormType < 0 || _activeStormType >= kStormTypeTableSize))
    {
        logger->warn("[Storm] Invalid active type {} in phase {} — resetting to Idle.",
            _activeStormType, static_cast<int>(_stormPhase));
        EnterStormPhase(StormPhase::Idle, 0);
        _activeStormType = -1;
    }

    switch (_stormPhase)
    {
    case StormPhase::Idle:
    {
        if (_forceStormNow)
        {
            _forceStormNow = false;
            BeginStorm(_forcedStormType >= 0 ? _forcedStormType : RollStormType());
            _forcedStormType = -1;
            break;
        }
        if (!_stormsEnabled)
            break;
        if (!_stormScheduled)
        {
            ScheduleNextStorm();
            break;
        }
        if (_playSeconds >= _nextStormPlaySec)
            BeginStorm(RollStormType());
        break;
    }

    case StormPhase::Approach:
    {
        if (--_stormTicksLeft <= 0)
        {
            logger->info("[StormSeq] APPROACH complete ({} s elapsed) -> WARNING.", _activeLeadTicks / 4);
            EnterWarningPhase();
        }
        break;
    }

    case StormPhase::Warning:
    {
        UpdateStormFXVisibility();
        if (--_stormTicksLeft <= 0)
        {
            const StormType& st = kStormTypes[_activeStormType];
            if (st.lethal)
            {
                logger->warn("[Storm] {} PEAK — exposure is now lethal. Seek shelter!", st.name);
                Notify(IsPlayerSheltered() ? "The radiation storm has hit. Stay inside."
                                           : "The radiation storm has hit - GET INSIDE!", true);
            }
            else
            {
                logger->info("[Storm] {} settling in.", st.name);
            }
            EnterStormPhase(StormPhase::Active, st.activeTicks);
            _stormPeakTick  = st.activeTicks / 2;  // lethal NPC burst fires at the midpoint
            _stormPeakFired = false;               // arm the one-time peak burst

            StopStormCue(kRadstormCueFalloffMs);

            if (RE::TESWeather* w = StormSkyWeather(_activeStormType))
            {
                if (auto* sky = RE::Sky::GetSingleton())
                {
                    if (sky->currentWeather == w)
                    {
                        sky->overrideWeather = w;
                        _weatherForced = true;
                        logger->info("[StormFog] '{}' sky already current at Active — no re-transition.", st.name);
                    }
                    else
                    {
                        RE::TESWeather* prev = sky->currentWeather;
                        CaptureWeatherReturnTarget();
                        BeginSmoothWeather(w);
                        if (_activeStormType == kDustStormIndex && prev && prev != w)
                        {
                            _dustBuildupActive = true;   // let UpdateDustFogRoll watch for arrival -> dust burst
                            _dustRollElapsed   = 0;
                        }
                        logger->info("[StormFog] '{}' armed (engine-native smooth transition, from 0x{:06X}).",
                            st.name, prev ? prev->GetFormID() : 0);
                    }
                }
            }


            _stormFXVisible = !IsPlayerSheltered();
            TriggerStormFX(true);

            if (st.themeId == 1)
            {
                _firestormAwaitingNuke = true;
                _stormRollInTicks      = 0;   // not started yet; armed when the nuke resolves
            }
            else
            {
                _firestormAwaitingNuke = false;
                _stormRollInTicks = 0;
                if (_stormFXVisible)
                    ApplyStormIMAD(_activeStormType);
            }
            if (_firestormAwaitingNuke)
                logger->info("[Storm] {} — awaiting distant nuke detonation before atmosphere roll-in.", st.name);

        }
        break;
    }

    case StormPhase::Active:
    {
        const StormType& st = kStormTypes[_activeStormType];

        if (st.lethal && _shakeBurstStep < 0 && _shakeRumbleTick < 0)
        {
            _shakeBurstStep    = 0;
            _shakeBurstRetries = 0;   // fresh burst — reset the transient-failure retry counter
        }
        UpdateStormShake();

        if (_firestormAwaitingNuke)
        {
            ++_nukeWaitTicks;
            if (!_stormNukeFired && !IsPlayerSheltered() && _nukeAttempts < kFirestormNukeMaxAttempts)
            {
                ++_nukeAttempts;
                _stormNukeFired = SpawnDistantNuke();
                if (_stormNukeFired)
                {
                    RE::BSSoundHandle boom{};
                    if (!Play2DSound(kSndNukeImpact, boom))
                        logger->warn("[StormAudio] nuke impact '{}' failed to load.", kSndNukeImpact);
                }
            }

            if (_stormNukeFired || _nukeAttempts >= kFirestormNukeMaxAttempts ||
                _nukeWaitTicks >= kFirestormNukeMaxWaitTicks)
            {
                _firestormAwaitingNuke = false;
                _stormRollInTicks      = kNukeRollInTicks;   // begin the fixed atmosphere roll-in
                logger->info("[Storm] {} — firestorm nuke {} after {} actual attempt(s) / {} wait tick(s); atmosphere roll-in begins ({} ticks).",
                    st.name, _stormNukeFired ? "detonated" : "abandoned (bounded limit hit)",
                    _nukeAttempts, _nukeWaitTicks, kNukeRollInTicks);
            }
            else
            {
                if (_stormFXVisible && IsPlayerSheltered())
                {
                    TriggerStormFX(false);
                    _stormFXVisible = false;
                }
                --_stormTicksLeft;
                break;
            }
        }

        if (_stormRollInTicks > 0)
        {
            if (--_stormRollInTicks == 0)
            {
                if (!IsPlayerSheltered())
                {
                    ApplyStormIMAD(_activeStormType);
                    if (st.themeId == 1)
                        SpawnFireRing();                // ground fire ring rolls in
                    _stormFXVisible = true;
                    if (_stormNukeFired)
                    {
                        RE::BSSoundHandle wave{};
                        if (!Play2DSound(kSndBlastWave, wave))
                            logger->warn("[StormAudio] blast wave '{}' failed to load.", kSndBlastWave);
                    }
                }
                logger->info("[Storm] {} — atmosphere rolling in{}.",
                    st.name, _stormNukeFired ? " after the blast wave" : " without nuke detonation");
            }
            else if (_stormFXVisible && IsPlayerSheltered())
            {
                TriggerStormFX(false);
                _stormFXVisible = false;
            }
            --_stormTicksLeft;
            break;
        }

        ApplyStormExposure();

        UpdateStormFXVisibility();

        if (--_fleeTick <= 0)
        {
            _fleeTick = kFleeRecheckTicks;
            EquipNearbyNpcMasks(true);
            MakeNearbyNPCsFlee();
        }

        if (st.lethal && !_stormPeakFired && _stormTicksLeft <= _stormPeakTick)
        {
            ApplyStormPeakToNPCs();
            _stormPeakFired = true;
        }

        if (st.lethal)
        {
            if (IsPlayerSheltered())
            {
                _stormExposedTicks = 0;
            }
            else
            {
                if (_stormExposedTicks % kExposedNagTicks == 0)
                    Notify("You are exposed to lethal radiation!", true);
                ++_stormExposedTicks;
            }
        }

        if (--_stormTicksLeft <= 0)
        {
            logger->info("[Storm] {} fading (aftermath).", st.name);
            Notify("The radiation storm is passing.", false);
            ClearFireRefs();        // remove any spawned fire hazards
            FadeOutStormIMAD(3.0f);
            TriggerStormFX(false);  // stop shake (Papyrus, if it ever works)
            _stormFXVisible = false;
            EnterStormPhase(StormPhase::Aftermath, st.aftermathTicks);
            ReleaseStormWeather();  // hand the sky back to the vanilla system
        }
        break;
    }

    case StormPhase::Aftermath:
    {
        if (--_stormTicksLeft <= 0)
        {
            logger->info("[Storm] Storm over. Rescheduling.");
            Notify("The radiation storm has cleared.", false);
            _activeStormType = -1;
            EnterStormPhase(StormPhase::Idle, 0);
            ScheduleNextStorm();
        }
        break;
    }
    }

    float intensity = 0.0f;
    switch (_stormPhase)
    {
    case StormPhase::Warning:  intensity = 0.35f; break;
    case StormPhase::Active:   intensity = 1.0f;  break;
    case StormPhase::Aftermath:
    {
        const StormType& st = kStormTypes[_activeStormType >= 0 ? _activeStormType : 0];
        intensity = st.aftermathTicks > 0
            ? 0.5f * (static_cast<float>(_stormTicksLeft) / st.aftermathTicks) : 0.0f;
        break;
    }
    default: intensity = 0.0f; break;
    }
    int theme  = (_activeStormType >= 0) ? kStormTypes[_activeStormType].themeId : 0;
    const bool lethal = (_activeStormType >= 0) ? kStormTypes[_activeStormType].lethal : false;
    if (_stormRollInTicks > 0)
        theme = 0;
    if (_stormPhase == StormPhase::Approach)
        theme = 0;
    RadHud::GetSingleton().PushStorm(static_cast<int>(_stormPhase), theme, intensity,
        IsPlayerSheltered(), lethal);
}
