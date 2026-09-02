#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>

using namespace RadInternal;

// Storm lifecycle: scheduling, phases, FX (fire ring / ghouls / IMAD / quest stages), NPC effects, exposure.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


void RadiationEngine::UpdateStorm()
{
    auto logger = Log();
    UpdateWeatherHandoff();
    UpdateStormDomeLifecycle();

    // New-game safety: the first time the player is outdoors after a new game, force the
    // Commonwealth clear weather so a vault exit / alt-start never opens straight into a rad storm
    // (vanilla radstorm, a stale forced sky, or anything else). Re-rolls the storm schedule from
    // here so the first managed storm is a full gap away. Runs before the gate so it always applies.
    // Invariant: _forceClearOnEntry is armed ONLY by OnNewGame() — never by save-load — so the rad
    // refund below cannot wipe legitimate rads from an existing save.
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

        // Did we spawn straight into a storm/dust/rain sky? Year-INDEPENDENT — alt-start mods skip the
        // vault's 2077->2287 calendar jump, so 'postwar' can read false even in the real Commonwealth.
        // Treating "current weather is a storm/dust/rain weather" as the trigger covers that case too.
        RE::TESWeather* cur = sky ? sky->currentWeather : nullptr;
        const bool spawnedInStorm = cur && (IsRadstormWeather(cur->GetFormID())
                                            || _dustWeathers.contains(cur->GetFormID())
                                            || IsRainyWeather(cur));

        // Fire on the first outdoor in-game tick once we're either confirmed postwar OR clearly
        // sitting in a storm. Prewar intro (benign weather, year < 2100) leaves us armed for later.
        if (exterior && inGame && (postwar || spawnedInStorm))
        {
            if (sky)
            {
                sky->ReleaseWeatherOverride();              // drop any stale forced sky first
                if (_clearWeather) sky->ForceWeather(_clearWeather, true);
            }
            // Silence the incoming-storm siren that may have started as we spawned into the storm.
            StopStormCue(kRadstormCueFalloffMs);
            // Refund any rads the spawn storm dealt before we intercepted it: we're suppressing the
            // storm, so it shouldn't punish the player. A fresh new-game spawn should be at 0 rads
            // anyway, so zeroing here is safe. (ForceWeather stops the weather-conditioned rad effect
            // going forward; this clears what already landed in the moments before the clear.)
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

    // Need the gate global to run the system at all.
    if (!_nextEmissionGlobal) return;

    // Script/xEdit debug trigger: a quest/script can set RadOverhaul_ForceStorm to 1-4.
    // (The in-game console `set` by EditorID does NOT work in FO4 — use the
    // Shift+F8 debug menu.)
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

    // Defensive: every non-Idle phase indexes kStormTypes[_activeStormType]. If state ever desyncs
    // (a future path leaving the phase non-Idle with an invalid type), reset to Idle rather than
    // read out of bounds.
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
        // Debug-forced storm fires immediately, bypassing the schedule.
        if (_forceStormNow)
        {
            _forceStormNow = false;
            BeginStorm(_forcedStormType >= 0 ? _forcedStormType : RollStormType());
            _forcedStormType = -1;
            break;
        }
        // [Storm] Enabled=0 turns the mod's own storms off entirely (players who only want the
        // radiation/mask systems, or who find storms too frequent even at the longest gap). The
        // debug-force above still works, and vanilla radstorm thinning is unaffected.
        if (!_stormsEnabled)
            break;
        // First run / after a storm: pick the next time.
        if (!_stormScheduled)
        {
            ScheduleNextStorm();
            break;
        }
        // Natural trigger once enough REAL play-time has elapsed.
        if (_playSeconds >= _nextStormPlaySec)
            BeginStorm(RollStormType());
        break;
    }

    case StormPhase::Approach:
    {
        // Dome-only SILENT lead. Keep the dome tracking the player's exposure (spawn outside, clear when
        // sheltered) but fire NOTHING else — no siren, no alert, no weather. When the lead elapses, the
        // storm commits: EnterWarningPhase() fires the alarm and starts the dust-sky roll-in.
        const bool exposed = !IsPlayerSheltered();
        if (!exposed && _stormDomeUp) ClearStormDome();

        if (--_stormTicksLeft <= 0)
        {
            logger->info("[StormSeq] APPROACH complete ({} s elapsed) -> WARNING.", _activeLeadTicks / 4);
            EnterWarningPhase();
        }
        break;
    }

    case StormPhase::Warning:
    {
        // (Dome + siren + alert fired at EnterWarningPhase when the APPROACH lead elapsed.)
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

            // The incoming-storm siren has done its job (the storm is here) — fade it out with a short
            // fall-off; the per-storm ambient bed takes over from here.
            StopStormCue(kRadstormCueFalloffMs);

            // Firestorm: the distant nuke detonates now (exposed player only — a one-shot impact).
            if (st.themeId == 1 && !IsPlayerSheltered())
            {
                RE::BSSoundHandle boom{};
                if (!Play2DSound(kSndNukeImpact, boom))  // fire-and-forget; short one-shot
                    logger->warn("[StormAudio] nuke impact '{}' failed to load.", kSndNukeImpact);
            }

            // The storm HITS now: force this storm's sky for the rest of the event. (Moved here
            // from BeginStorm so the Warning phase keeps a normal sky and reads as a real warning.)
            if (RE::TESWeather* w = _stormWeathers[_activeStormType])
            {
                if (auto* sky = RE::Sky::GetSingleton())
                {
                    if (sky->currentWeather == w)
                    {
                        // The storm sky is ALREADY current — the dust warning build-up transitioned it in
                        // (and with a fast Trans-Delta that transition often finishes BEFORE the warning
                        // ends, so _dustBuildupActive is already false by now — that is why this used to be
                        // gated on _dustBuildupActive and mis-fell-through). Re-forcing here would start a
                        // redundant dust->dust transition that resets pct AND, via the camera-VE end
                        // threshold, tears down the blowing-dust VE mid-storm (the "dust vanished after a
                        // second" bug). Do NOT re-force — the sky is right, just mark it.
                        _weatherForced = true;
                        logger->info("[StormFog] '{}' sky already current at Active — no re-transition.", st.name);
                    }
                    else
                    {
                        // No build-up (non-dust storm, or dust loaded mid-storm): begin the storm sky via the
                        // engine's OWN smooth transition (vanilla path) — no jarring snap, no purple flash.
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

            // The storm ambient bed is owned by UpdateAmbientAudio() (driven by current weather): now
            // that this storm's weather is current, it starts the bed on the next tick while exposed.

            // Screen FX are EXTERIOR-only: an exposed player gets the cloud dome (lethal storms also
            // shake; the firestorm detonates the distant nuke first and defers its dome + fog/IMAD to
            // the roll-in). A sheltered player gets NOTHING on-screen — TriggerStormFX/ApplyStormIMAD
            // no-op while sheltered, and UpdateStormFXVisibility restores them if they step outside.
            _stormFXVisible = !IsPlayerSheltered();
            TriggerStormFX(true);

            if (st.themeId == 1)
            {
                // Firestorm: HOLD the atmosphere roll-in until the distant nuke actually detonates (or
                // its bounded retry window expires). The Active tick drives SpawnDistantNuke and starts
                // the fixed roll-in only once the nuke resolves — so a transient streaming failure can't
                // let the fog/fire/dome roll in as though the nuke had fired.
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

            // Lethal storms: spawn feral ghouls that emerge from the radiation.
            if (st.lethal)
                SpawnStormGhouls();
        }
        break;
    }

    case StormPhase::Active:
    {
        const StormType& st = kStormTypes[_activeStormType];

        // Camera shake for lethal storms, driven natively per tick. Papyrus used to own this via
        // quest stages 10/11, which never fired (SetStage always false). Armed on the first Active
        // tick; UpdateStormShake self-gates on shelter and tears down in EnterStormPhase(Idle).
        if (st.lethal && _shakeBurstStep < 0 && _shakeRumbleTick < 0)
        {
            _shakeBurstStep    = 0;
            _shakeBurstRetries = 0;   // fresh burst — reset the transient-failure retry counter
        }
        UpdateStormShake();

        // Firestorm distant nuke: fire it here with a bounded retry, and HOLD the atmosphere roll-in
        // until it resolves. _stormNukeFired latches only on a successful detonation, so a transient
        // streaming failure (target cell not yet loaded) cannot consume the storm's one shot. The
        // Actual submission attempts only advance when SpawnDistantNuke is called. A separate absolute
        // wait advances every awaiting tick, including shelter, so an invalid state cannot hold Active
        // forever without claiming that it made native attempts.
        if (_firestormAwaitingNuke)
        {
            ++_nukeWaitTicks;
            if (!_stormNukeFired && !IsPlayerSheltered() && _nukeAttempts < kFirestormNukeMaxAttempts)
            {
                ++_nukeAttempts;
                _stormNukeFired = SpawnDistantNuke();
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
                // Still awaiting the nuke: hold the atmosphere exactly like the roll-in-hold below —
                // if the player ducked inside, drop the exterior FX; tick the storm clock; skip exposure.
                if (_stormFXVisible && IsPlayerSheltered())
                {
                    TriggerStormFX(false);
                    ClearStormDome();
                    _stormFXVisible = false;
                }
                --_stormTicksLeft;
                break;
            }
        }

        // Firestorm roll-in: hold off the fog/fire/IMAD + exposure for a few seconds after the
        // nuke so the blast clearly precedes the atmosphere. When the timer elapses, roll it in.
        if (_stormRollInTicks > 0)
        {
            if (--_stormRollInTicks == 0)
            {
                // The atmosphere rolls in with the blast wave — but only for an EXPOSED player, so the
                // fog/fire/dome never appear inside. If sheltered, leave it hidden; UpdateStormFXVisibility
                // rolls it in the moment they step outside. (SpawnFireRing already self-gates on shelter.)
                if (!IsPlayerSheltered())
                {
                    ApplyStormIMAD(_activeStormType);
                    if (st.themeId == 1)
                        SpawnFireRing();                // ground fire ring rolls in
                    _stormFXVisible = true;
                    RE::BSSoundHandle wave{};
                    if (!Play2DSound(kSndBlastWave, wave))   // the shockwave sweeping in (one-shot)
                        logger->warn("[StormAudio] blast wave '{}' failed to load.", kSndBlastWave);
                }
                logger->info("[Storm] {} — blast wave arrived; atmosphere rolling in.", st.name);
            }
            // The nuke's camera shake / dome are exterior-only during the inbound-wave window too. If
            // the player ducks inside while the blast is still rolling in, stop them now so the rumble
            // doesn't keep shaking the camera indoors. We only ever turn FX OFF here — turning them back
            // on is left to the arrival rollout / UpdateStormFXVisibility so the deferred fog can't
            // appear before the wave lands. (Only meaningful on non-arrival ticks; the arrival block
            // above already self-gates on shelter.)
            else if (_stormFXVisible && IsPlayerSheltered())
            {
                TriggerStormFX(false);
                ClearStormDome();
                _stormFXVisible = false;
            }
            // Tick the storm clock but skip exposure/peak FX until the wave arrives.
            --_stormTicksLeft;
            break;
        }

        ApplyStormExposure();

        // Keep the storm dome + IMAD exterior-only as the player moves between cover and the open
        // (mirrors UpdateAmbientAudio) so no fog/colour-grade bleeds into interiors mid-storm.
        UpdateStormFXVisibility();

        // Lethal storms: nearby NPCs panic and flee. Throttled (the engine flee order only needs
        // re-issuing every few seconds to keep them running for the storm's duration).
        if (st.lethal && (--_fleeTick <= 0))
        {
            _fleeTick = kFleeRecheckTicks;
            MakeNearbyNPCsFlee();
        }

        // One-time pulse at the peak (lethal storms only): heals exterior ghoul NPCs, leaves every
        // other NPC unharmed. Fired-flag + "<=" rather than exact-tick equality so it still fires if
        // the exact peak tick is ever skipped (e.g. a short storm whose peak lands in the firestorm roll-in).
        if (st.lethal && !_stormPeakFired && _stormTicksLeft <= _stormPeakTick)
        {
            ApplyStormPeakToNPCs();
            _stormPeakFired = true;
        }

        // Re-warn an EXPOSED player during a lethal storm every few seconds; reset the
        // nag the moment they reach cover so the next exposure warns again promptly.
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
            // Inform the player the storm is ending, for ALL storm types (not just lethal) —
            // ambient storms previously gave no "passing" cue at all.
            Notify("The radiation storm is passing.", false);
            ClearFireRefs();        // remove any spawned fire hazards
            // NOTE: hunter ghouls are NOT cleared here — they persist past the storm and keep hunting,
            // despawning only when the player leaves the area (CheckGhoulPackCleanup).
            FadeOutStormIMAD(3.0f);
            TriggerStormFX(false);  // stop shake (Papyrus, if it ever works)
            ClearStormDome();       // despawn the native cloud dome
            _stormFXVisible = false;
            // Storm bed: ReleaseStormWeather (below) hands the sky back, so once the weather is no
            // longer this storm's, UpdateAmbientAudio fades the bed out and returns to the general
            // ambient — no explicit fade needed here.
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

    // Drive the HUD storm overlay (no-op if PrismaUI absent). Pushes the theme so the
    // overlay reskins per storm type; intensity 0..1; sheltered dims it.
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
    // During the firestorm roll-in window, force theme 0 so the HUD clears the fog/fire
    // overlay — only the distant nuke flash shows until the blast wave arrives.
    if (_stormRollInTicks > 0)
        theme = 0;
    // APPROACH is a SILENT dome-only lead — no HUD storm banner at all until the alarm (Warning).
    if (_stormPhase == StormPhase::Approach)
        theme = 0;
    RadHud::GetSingleton().PushStorm(static_cast<int>(_stormPhase), theme, intensity,
        IsPlayerSheltered(), lethal);
}
