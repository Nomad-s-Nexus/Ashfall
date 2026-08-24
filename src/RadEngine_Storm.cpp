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


void RadiationEngine::ScheduleNextStorm()
{
    static std::mt19937 rng{ std::random_device{}() };
    // Gap comes from the INI ([Storm] MinMinutes/MaxMinutes) so players can dial storm frequency down
    // without a rebuild — "storms happen too much" is the most common complaint.
    std::uniform_real_distribution<float> mins(_stormMinRealMin, _stormMaxRealMin);
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
    _stormFXVisible    = false;  // no screen FX until the storm hits (and only if exposed)
    _stormNukeFired    = false;  // re-arm the one-time firestorm nuke for this storm
    _firestormAwaitingNuke = false;  // set true at Active onset for a firestorm
    _nukeAttempts      = 0;      // reset actual nuke submissions
    _nukeWaitTicks     = 0;      // reset the absolute nuke wait
    _shakeBurstRetries = 0;      // reset the bounded shake-burst retry counter
    _debugSkipToActive = false;  // clear any leftover debug fast-forward from a prior storm
    const StormType& st = kStormTypes[typeIndex];

    // STAGED ONSET (R.'s spec): the dome appears ALONE, silent, first — a "something's coming" visual
    // warning for a lead window (DomeLeadMinutes, INI-configurable, default 3 min) with NO siren, NO
    // alert, and NO weather change. Only when that lead elapses does EnterWarningPhase() fire the alarm
    // and begin the dust-sky roll-in + the rest of the storm. This is the APPROACH phase.
    // A debug-forced storm may shorten the silent lead (_forcedLeadTicks); consume it here so the
    // NEXT natural storm is back to the configured DomeLeadMinutes.
    const int leadTicks = (_forcedLeadTicks >= 0) ? _forcedLeadTicks : _domeLeadTicks;
    _forcedLeadTicks = -1;
    _activeLeadTicks = leadTicks;
    logger->warn("========== STORM APPROACHING: {} ({}) — dome-only lead {} s ==========",
        st.name, st.lethal ? "LETHAL" : "ambient", leadTicks / 4);
    _sirenDelayTicks = 0;
    _dustBuildupActive = false;   // no weather build-up during the silent lead
    _debugWeatherHold  = false;   // a real storm takes over from any debug-held weather

    // Dome NOW (exterior only — a dome mesh would clip through a roof indoors). Native spawn.
    if (!IsPlayerSheltered())
        SpawnStormDome();

    EnterStormPhase(StormPhase::Approach, leadTicks);
    logger->info("[StormSeq] t=0 APPROACH begin: dome up, silent. Alarm in {} s.", leadTicks / 4);
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
        SpawnStormDome();   // re-ensure the dome if the player stepped out during the lead (self-guards on _stormDomeUp)
        PlayStormCue();
    }
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


void RadiationEngine::ReleaseStormWeather()
{
    // Stop any in-progress Dust Storm fog roll-in and leave the dust weather at its full profile, so
    // a later debug preview (weather:4) or the next storm's re-seed starts from a known-good state.
    // Stop any Dust Storm build-up / roll-in cross-fade (dust fog stays at its full profile from load).
    _dustBuildupActive   = false;
    _dustFogRollTick     = 0;
    _warnTicksTotal      = 0;
    _dustRollPrevWeather = nullptr;
    _dustRollElapsed     = 0;
    _dustRollTotal       = 0;
    // Restore the dust weather's full fog profile (the roll leaves it mid-ramp if interrupted) so a
    // later preview or the next storm starts from the known-good target, not a stale thin value.
    if (RE::TESWeather* dust = _stormWeathers[kDustStormIndex])
    {
        const StormType& s = kStormTypes[kDustStormIndex];
        ApplyWeatherFog(dust, s.fogNear, s.fogFar, s.fogPower, s.fogHeightMid, s.fogHeightRange, s.fogHiDensity);
    }
    // Fade the incoming-storm siren out with a short fall-off instead of leaving it ringing, and drop
    // any pending onset countdown so a storm that ends inside the dome-fade window can't start the
    // siren after the fact.
    _sirenDelayTicks = 0;
    StopStormCue(kRadstormCueFalloffMs);

    // Always attempt the release (don't gate on _weatherForced) — a forced sky can outlive our
    // flag if a save was loaded mid-storm. ReleaseWeatherOverride just sets an engine flag, so
    // it's safe anywhere (including interiors). We deliberately do NOT call Sky::ResetWeather()
    // here — it dereferences the sky's current weather and CTDs when called in an interior
    // (no active sky weather), which is what crashed "End Storm" indoors.
    auto* sky = RE::Sky::GetSingleton();
    if (!sky) { _weatherForced = false; return; }

    sky->ReleaseWeatherOverride();

    // INTERIOR FIX: while the player is sheltered/indoors the exterior sky isn't being simulated,
    // so a plain override-release leaves the forced STORM weather sitting as the "current" weather.
    // It then lingers — and its weather-conditioned rad magic effect (GetIsCurrentWeather) re-fires —
    // the instant the player steps back outside, so the storm appears to "never end" until they leave
    // the interior. Hard-snap to the Commonwealth clear weather in that case so the storm is genuinely
    // over regardless of location. Outdoors we keep the soft release so the sky transitions naturally.
    if (_clearWeather && IsPlayerSheltered())
        sky->ForceWeather(_clearWeather, true);

    _weatherForced = false;
}


void RadiationEngine::ForceClearAndSilence()
{
    // Silence ALL storm audio: the managed ambient AND the incoming-storm siren. Both FADE out now
    // (short fall-off) rather than hard-cutting — the siren was previously left ringing after the storm.
    StopStormCue(kRadstormCueFalloffMs);
    if (_stormSoundHandle.IsValid())  _stormSoundHandle.FadeOutAndRelease(kStormSoundFadeMs);
    _curStormBed = nullptr;    // UpdateAmbientAudio re-derives from the (now clear) weather next tick
    _radstormWarned = false;   // re-arm the cue for the next storm

    // Transition to the Commonwealth clear sky via the engine's OWN smooth path (locks it as override so
    // the region can't immediately re-roll a storm). BeginSmoothWeather sets currentWeather=clear right
    // away (so the storm's weather-conditioned rad effect stops immediately — no lingering), while the
    // engine blends the sky back smoothly instead of snapping. Fall back to a plain release if the
    // clear-weather form didn't resolve.
    if (auto* sky = RE::Sky::GetSingleton())
    {
        if (_clearWeather) BeginSmoothWeather(_clearWeather);
        else               sky->ReleaseWeatherOverride();
    }
    _weatherForced = false;
}


void RadiationEngine::SpawnFireRing()
{
    if (!_fireHazardForm) return;
    if (IsPlayerSheltered()) return;  // no indoor fire ring

    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player) return;

    // SpawnFireRing only called when !IsPlayerSheltered(), so player is in exterior.
    // Use cachedWorldspace for the placement; interior is null for exterior cells.
    auto* world = player->cachedWorldspace;
    if (!world) return;

    auto logger = Log();
    ClearFireRefs();  // clean up any leftover refs from a previous storm

    const RE::NiPoint3 base = player->GetPosition();
    constexpr float  kRingRadius = 350.0f;  // game units (~17 feet) — close enough to be menacing
    constexpr int    kCount      = 8;       // fire pillars in a ring
    constexpr float  kPi         = 3.14159265f;

    int spawned = 0;
    for (int i = 0; i < kCount; ++i)
    {
        const float angle = (static_cast<float>(i) / kCount) * 2.0f * kPi;

        RE::NEW_REFR_DATA data;
        data.object    = static_cast<RE::TESBoundObject*>(_fireHazardForm);
        data.location  = { base.x + kRingRadius * std::cos(angle),
                           base.y + kRingRadius * std::sin(angle),
                           base.z };
        data.direction = { 0.0f, 0.0f, 0.0f };
        data.interior  = nullptr;
        data.world     = world;

        auto handle = dh->CreateReferenceAtLocation(data);
        if (static_cast<bool>(handle))
        {
            _fireRefs.push_back(handle);
            ++spawned;
        }
    }
    logger->info("[Storm/Fire] Spawned {} fire hazard refs around player (radius {:.0f}).",
        spawned, kRingRadius);
}


// One distant, purely cinematic firestorm detonation. The 4 Hz engine tick (RadiationEngine::Update and
// everything it calls, this included) is marshaled to the verified Fallout 4 game-main thread by
// Hooks.cpp's dispatcher — this does NOT run on the worker Update hook. The EXPL record
// FARO_EmissionStrikeAlpha (0x885) still keeps PlacedObject = Null by design: it detonates without
// spawning a placed reference, which is exactly what a pure horizon visual wants.
// Returns true iff the explosion actually detonated (valid handle). false = a TRANSIENT failure the
// caller should retry within its bounded window: unsupported runtime, player 3D/worldspace not up,
// sheltered, or the target cell not yet streamed (engine returns a null handle). The one-shot latch
// (_stormNukeFired) must only be set on a true return, so a streaming stall can't consume the storm's
// single nuke.
bool RadiationEngine::SpawnDistantNuke()
{
    if (!REX::FModule::IsRuntimeOG()) return false;   // authority wrapper is currently OG-only; fail closed elsewhere
    if (!_nukeExplosionForm || IsPlayerSheltered()) return false;

    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player || !player->cachedWorldspace) return false;
    if (!player->Get3D()) return false;               // worldspace must actually be up

    static std::mt19937 rng{ std::random_device{}() };
    constexpr float kPi = 3.14159265f;
    std::uniform_real_distribution<float> yawJitter(-25.0f, 25.0f);
    // Keep the giant visual inside the exterior streaming envelope, but far enough away to
    // read as a horizon-scale detonation instead of a player-side blast.
    std::uniform_real_distribution<float> distanceDist(6000.0f, 7000.0f);

    // Place the single strike along the player's current view vector, never in a ring.
    // Engine angles are radians; the small jitter keeps each storm's composition natural.
    const float yaw = player->data.angle.z + yawJitter(rng) * (kPi / 180.0f);
    const float distance = distanceDist(rng);
    const RE::NiPoint3 base = player->GetPosition();
    const RE::NiPoint3 pos{ base.x + distance * std::sin(yaw),
                            base.y + distance * std::cos(yaw),
                            base.z + 1500.0f };   // matches NAC's nuclear-fallout MFX z-offset
    const RE::NiPoint3 rot{ 0.0f, 0.0f, 0.0f };   // explosions are radial; rotation is irrelevant

    // DETONATE (not place) via the authoritative commonlib_NonVR wrapper. Cell is passed null on
    // purpose: with a worldspace supplied the engine resolves the cell from the position and returns a
    // null handle if it is not loaded, instead of faulting.
    auto handle = dh->CreateExplosionAtLocation(
        static_cast<RE::TESBoundObject*>(_nukeExplosionForm),
        pos, rot, nullptr, player->cachedWorldspace);

    Log()->info("[Storm/Nuke] distant firestorm strike detonated={} distance={:.0f} z=+1500.",
        static_cast<bool>(handle), distance);
    if (!handle)
        Log()->warn("[Storm/Nuke] null handle — target cell not loaded at that position (engine gate), "
                    "or the EXPL form failed its type check. Will retry within the bounded window.");
    return static_cast<bool>(handle);
}

// Spawn the rotating cloud dome overhead. NATIVE (CreateReferenceAtLocation — the same call the fire
// ring uses), NOT the RadStormFX Papyrus quest, whose SetStage()/OnStageSet never fired reliably. The
// whole engine tick (this included) is marshaled to the verified game-main thread by Hooks.cpp's
// dispatcher. Exterior only — a dome mesh would clip through a roof indoors; UpdateStormFXVisibility
// re-spawns it if the player steps back outside.
void RadiationEngine::SpawnStormDome()
{
    if (_stormDomeUp || IsPlayerSheltered()) return;
    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player || !player->cachedWorldspace) return;

    // Clear any leftover refs first, mirroring SpawnFireRing. _stormDomeUp only latches on placed dome
    // ACTI layers (below); if those fail to resolve but the arena lights do, _stormDomeUp stays false
    // and every re-exposure would otherwise append another light pair, leaking the previous ones.
    ClearStormDome();
    _stormDomeFollowTick = 0;

    const RE::NiPoint3 base = player->GetPosition();
    int placed = 0;
    for (int k = 0; k < 4; ++k)
    {
        if (!_stormDomeForms[k]) continue;
        RE::NEW_REFR_DATA data;
        data.object    = static_cast<RE::TESBoundObject*>(_stormDomeForms[k]);
        data.location  = { base.x, base.y, base.z + kStormDomeZOffset };
        data.direction = { 0.0f, 0.0f, 0.0f };
        data.interior  = nullptr;
        data.world     = player->cachedWorldspace;
        auto handle = dh->CreateReferenceAtLocation(data);
        if (static_cast<bool>(handle))
        {
            _stormDomeRefs.push_back({ handle, kStormDomeZOffset });
            ++placed;
        }
    }
    _stormDomeUp = placed > 0;
    _stormDomeAnimStarted = false;   // force-start the animation once these refs' 3D attaches

    // Arena lights: the cloud shapes are effect-LIT, so these lights ARE the glowing-vortex look (bright at
    // the eye, falling off outward). Mirror KORs' PlaceAtNode exactly: ArenaLight01 at TopLightNode and
    // ArenaLight02 (lens flare) at LensFlareNode, both UNROTATED (the nodes carry translation only).
    // Tracked in _stormDomeRefs so ClearStormDome tears them down with the dome.
    //
    // Gate the lights on a successful dome (placed > 0): they only illuminate the cloud shapes, so lights
    // without a dome are pointless — and, critically, if the dome ACTIs never resolve, _stormDomeUp stays
    // false and an exposed player re-enters SpawnStormDome every tick. Placing the lights here regardless
    // would then create them and (via the ClearStormDome above) destroy them every tick — retry churn.
    bool lit = false, flare = false;
    if (placed > 0)
    {
        auto placeLight = [&](RE::TESBoundObject* obj, float nodeZ) -> bool {
            if (!obj) return false;
            RE::NEW_REFR_DATA ld;
            ld.object    = obj;
            ld.location  = { base.x, base.y, base.z + kStormDomeZOffset + nodeZ };
            ld.direction = { 0.0f, 0.0f, 0.0f };
            ld.interior  = nullptr;
            ld.world     = player->cachedWorldspace;
            auto lh = dh->CreateReferenceAtLocation(ld);
            if (!static_cast<bool>(lh)) return false;
            _stormDomeRefs.push_back({ lh, kStormDomeZOffset + nodeZ });
            return true;
        };
        lit   = placeLight(_stormDomeLight, kStormDomeLightNodeZ);
        flare = placeLight(_stormDomeFlare, kStormDomeLensFlareZ);
    }
    Log()->info("[StormFX] Native storm dome spawned: {}/4 layers at z+{:.0f}, arena light {}, lens flare {}.",
        placed, kStormDomeZOffset, lit ? "on" : "off", flare ? "on" : "off");
}


// Native camera shake, replacing the RadStormFX Papyrus shake that never fired. Uses the authoritative
// RE::ShakeCamera wrapper from commonlib_NonVR (see RE-Work/docs/rad-overhaul-natives-2026-08.md).
//
// This runs on the verified Fallout 4 game-main thread: the 4 Hz engine tick is marshaled there by
// Hooks.cpp's dispatcher (runtime probing proved F4SE AddTask is NOT a main-thread marshal on this
// setup — the update hook, the AddTask callback and the message handler are three different threads —
// which is why the dispatcher exists). The 3D/exterior guard below is still load-bearing: the native
// walks spWorldRoot into the camera node's world transform and will null-deref if the worldspace is
// not up. Do not call it unguarded.
// Returns true iff a shake was actually submitted to the engine. false means a TRANSIENT invalid
// state (player 3D not up, no/interior cell) or an unsupported runtime — the caller must NOT treat a
// false as "this shake step happened", or the decaying burst would silently skip steps during a load.
bool RadiationEngine::NativeShakeCamera(float strength, float duration)
{
    if (!REX::FModule::IsRuntimeOG()) return false;   // authority wrapper is currently OG-only; fail closed elsewhere
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->Get3D()) return false;    // 3D not up yet (streaming) — transient
    auto* cell = player->parentCell;
    if (!cell || cell->IsInterior()) return false;    // interior / no cell — no exterior shake

    if (strength > 1.0f) strength = 1.0f;       // native does not clamp; the Papyrus binding did
    if (strength <= 0.0f) return true;          // valid state, nothing to shake — not a failure

    const RE::NiPoint3 pos = player->data.location;
    // Authoritative commonlib_NonVR wrapper; distanceMult defaults to 1.0 ("no distance scaling").
    RE::ShakeCamera(strength, pos, duration);
    return true;
}


// Per-tick shake driver for an ACTIVE lethal storm: a decaying burst on the hit, then a low
// sustained rumble. Mirrors RadStormFX._StartShake/_kRumbleTimer, which never actually ran.
void RadiationEngine::UpdateStormShake()
{
    if (_shakeBurstStep < 0 && _shakeRumbleTick < 0) return;
    if (IsPlayerSheltered()) return;            // sheltered = no screen FX, same rule as the dome

    if (_shakeBurstStep >= 0)
    {
        const float t = static_cast<float>(kStormShakeBurstSteps - _shakeBurstStep) /
                        static_cast<float>(kStormShakeBurstSteps);
        if (NativeShakeCamera(kStormShakeBurstStrength * t, kStormShakeBurstDuration))
        {
            // Submitted — advance the decaying burst.
            _shakeBurstRetries = 0;
            if (++_shakeBurstStep >= kStormShakeBurstSteps)
            {
                _shakeBurstStep  = -1;
                _shakeRumbleTick = 0;           // hand over to the sustained rumble
            }
        }
        else if (++_shakeBurstRetries >= kStormShakeBurstMaxRetries)
        {
            // Player 3D/cell stayed invalid too long (e.g. long streaming stall): don't burn the burst
            // steps against a state where the native can't run. Abandon the burst and hand over to the
            // sustained rumble, which self-retries safely once the player is valid again.
            _shakeBurstRetries = 0;
            _shakeBurstStep    = -1;
            _shakeRumbleTick   = 0;
        }
        // else: transient failure this tick — keep the same step and retry next tick (bounded above).
        return;
    }

    if (--_shakeRumbleTick <= 0)
    {
        _shakeRumbleTick = kStormShakeRumbleTicks;
        NativeShakeCamera(kStormShakeRumbleStrength, kStormShakeRumbleDuration);
    }
}


// The dome NIFs are animated (rotation + a shader-float on the clouds), but a natively-placed ACTI ref
// doesn't auto-start its NiController the way KORs' STATIC records do — so it renders frozen (no rotation)
// with the shader float stuck at its initial value. Force-activate every layer's controller sequence once
// its 3D has attached (async, so poll). Idempotent + one-shot per dome via _stormDomeAnimStarted.
void RadiationEngine::UpdateDomeAnimation()
{
    if (!_stormDomeUp || _stormDomeAnimStarted || _stormDomeRefs.empty()) return;
    int animated = 0, pending = 0;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (!ref) continue;
        auto* obj3d = ref->Get3D();
        if (!obj3d) { ++pending; continue; }   // 3D not attached yet — retry next tick
        auto* mgr = RE::NiControllerManager::GetNiControllerManager(obj3d);
        if (!mgr) continue;                     // the light layer (no controller) — skip
        const std::uint16_t n = mgr->GetSequenceCount();
        for (std::uint16_t i = 0; i < n; ++i)
        {
            RE::NiControllerSequence* seq = mgr->GetSequenceAt(i);
            if (seq)
                seq->Activate(0, true, 1.0f, 0.0f, nullptr, false);
        }
        if (n > 0) ++animated;
    }
    if (pending == 0)   // every ref's 3D has loaded — stop polling
    {
        _stormDomeAnimStarted = true;
        Log()->info("[StormFX] Dome animation activated on {} layer(s).", animated);

        // DOME STATE DUMP (one-shot per dome): the dome has never been confirmed to actually RENDER, and
        // every fix so far patched a link we never measured. Dump each ref's real runtime state so the
        // break is located from evidence instead of inferred: does it have 3D, is it in the player's cell,
        // and is it where we think it is (player z + kStormDomeZOffset)?
        auto* pl = RE::PlayerCharacter::GetSingleton();
        const RE::NiPoint3 ppos = pl ? pl->GetPosition() : RE::NiPoint3{};
        auto* pcell = pl ? pl->GetParentCell() : nullptr;
        Log()->warn("[DomeDump] player pos=({:.0f},{:.0f},{:.0f}) cell=0x{:X}",
            ppos.x, ppos.y, ppos.z, reinterpret_cast<std::uintptr_t>(pcell));
        int i = 0;
        for (auto& layer : _stormDomeRefs)
        {
            auto ptr = layer.handle.get(); auto* r = ptr.get();
            if (!r) { Log()->warn("[DomeDump] [{}] handle DEAD", i++); continue; }
            const auto p = r->GetPosition();
            auto* c3d = r->Get3D();
            Log()->warn("[DomeDump] [{}] base=0x{:08X} 3D={} cell=0x{:X} sameCellAsPlayer={} "
                        "pos=({:.0f},{:.0f},{:.0f}) dz={:.0f} deleted={} disabled={}",
                i, r->GetObjectReference() ? r->GetObjectReference()->GetFormID() : 0u,
                c3d ? "YES" : "NO", reinterpret_cast<std::uintptr_t>(r->GetParentCell()),
                (r->GetParentCell() == pcell) ? "yes" : "NO",
                p.x, p.y, p.z, p.z - ppos.z,
                r->IsDeleted() ? "YES" : "no", r->IsDisabled() ? "YES" : "no");
            ++i;
        }
    }
}

void RadiationEngine::ClearStormDome()
{
    _stormDomeFollowTick = 0;
    if (_stormDomeRefs.empty()) { _stormDomeUp = false; return; }
    int cleared = 0;
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        if (auto* ref = ptr.get())
        {
            ref->Disable();
            ref->SetDelete(true);
            ++cleared;
        }
    }
    _stormDomeRefs.clear();
    _stormDomeUp = false;
    _stormDomeAnimStarted = false;
    Log()->info("[StormFX] Storm dome cleared ({} layers).", cleared);
}

void RadiationEngine::UpdateStormDomePosition()
{
    if (!_stormDomeUp || IsPlayerSheltered()) return;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    if (!RadCorrectnessPolicy::ShouldFollowDomeOnTick(++_stormDomeFollowTick)) return;
    _stormDomeFollowTick = 0;

    const auto playerPosition = player->GetPosition();
    for (auto& layer : _stormDomeRefs)
    {
        auto ptr = layer.handle.get();
        auto* ref = ptr.get();
        if (!ref || ref->IsDeleted() || ref->IsDisabled()) continue;

        const auto position = RadCorrectnessPolicy::DomePosition(
            { playerPosition.x, playerPosition.y, playerPosition.z }, layer.zOffset);
        ref->SetPosition({ position.x, position.y, position.z });
    }
}

// Continuous camera dust for the Dust Storm. The weather referenceEffect (FXCameraAttachExplosionDustVE)
// attaches but produced no visible motes in practice, so we drive the blowing dust the reliable way:
// re-apply the heavy dust ArtObject to the camera slightly faster than it expires, so it overlaps into a
// steady effect. Same ApplyArtObject path as the wall-arrival burst, which DOES render.
void RadiationEngine::UpdateDustMotes()
{
    if (_activeStormType != kDustStormIndex || !_dustBurstArt ||
        (_stormPhase != StormPhase::Warning && _stormPhase != StormPhase::Active))
    {
        _dustMoteTick = 0;
        return;
    }
    auto* sky = RE::Sky::GetSingleton();
    if (!sky || sky->currentWeather != _stormWeathers[kDustStormIndex] || IsPlayerSheltered())
        return;
    if (--_dustMoteTick > 0) return;
    _dustMoteTick = kDustMoteReapplyTicks;
    if (auto* player = RE::PlayerCharacter::GetSingleton())
        player->ApplyArtObject(_dustBurstArt, kDustMoteSec, nullptr, /*attachToCamera=*/true,
            /*inheritRotation=*/true);
}


void RadiationEngine::ClearFireRefs()
{
    if (_fireRefs.empty()) return;
    auto logger = Log();
    int cleared = 0;
    for (auto& handle : _fireRefs)
    {
        auto ptr = handle.get();
        if (auto* ref = ptr.get())
        {
            ref->Disable();
            ref->SetDelete(true);
            ++cleared;
        }
    }
    _fireRefs.clear();
    logger->info("[Storm/Fire] Cleared {} fire hazard refs.", cleared);
}


void RadiationEngine::SpawnStormGhouls()
{
    if (!_stormGhoulForm) return;
    if (IsPlayerSheltered()) return;

    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player) return;

    auto* world = player->cachedWorldspace;
    if (!world) return;

    auto logger = Log();
    static std::mt19937 rng{ std::random_device{}() };

    // % chance per lethal storm — most storms pass without a pack.
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(rng) > kStormGhoulSpawnChance)
    {
        logger->info("[Storm/Ghouls] No ghoul pack this storm (chance {:.0f}%).", kStormGhoulSpawnChance * 100.0f);
        return;
    }

    constexpr float kPi = 3.14159265f;
    std::uniform_int_distribution<int>    countDist(kStormGhoulMinCount, kStormGhoulMaxCount);
    std::uniform_real_distribution<float> radiusDist(kStormGhoulMinRadius, kStormGhoulMaxRadius);
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * kPi);

    const int          count = countDist(rng);
    const RE::NiPoint3 base  = player->GetPosition();
    int spawned = 0;

    for (int i = 0; i < count; ++i)
    {
        const float angle  = angleDist(rng);
        const float radius = radiusDist(rng);
        // Every ~6th is a Glowing One if available — adds menace to the pack.
        RE::TESBoundObject* form = (i % 6 == 0 && _stormGlowingOneForm) ? _stormGlowingOneForm : _stormGhoulForm;

        RE::NEW_REFR_DATA data;
        data.object    = form;
        data.location  = { base.x + radius * std::cos(angle),
                           base.y + radius * std::sin(angle),
                           base.z + 2.0f };
        data.direction = { 0.0f, 0.0f, 0.0f };
        data.interior  = nullptr;
        data.world     = world;

        // Tracked so we can despawn the pack once the player leaves the area (see CheckGhoulPackCleanup).
        // They are NOT cleared at storm end, so they persist and hunt the player meanwhile.
        auto handle = dh->CreateReferenceAtLocation(data);
        if (static_cast<bool>(handle))
        {
            _stormGhoulRefs.push_back(handle);
            ++spawned;
        }
    }
    // Remember where/when the pack was unleashed so cleanup can fire when the player moves on.
    _ghoulPackCenter = base;
    auto* pcell = player->GetParentCell();
    _ghoulPackWorldID = world ? world->GetFormID() : 0;   // for the cleanup worldspace-change check
    _ghoulPackInterior = pcell && pcell->IsInterior();
    logger->warn("[Storm/Ghouls] HUNT — unleashed {} feral ghoul(s); they hunt until killed or you leave the area.", spawned);
}


// Despawn the storm ghoul pack once the player has clearly left the area: traveled far from the
// spawn center, changed worldspace, or gone indoors. Keeps persistent hunters from accumulating.
void RadiationEngine::CheckGhoulPackCleanup()
{
    if (_stormGhoulRefs.empty()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto* cell = player->GetParentCell();
    const bool wentIndoors  = cell && cell->IsInterior() && !_ghoulPackInterior;
    // Worldspace change (fast travel to a different exterior). Checked FIRST because the point
    // distance below is computed in raw cell coordinates — meaningless across worldspaces, where
    // it can read small even though the player is nowhere near the pack, leaving it stuck alive.
    const auto* world       = player->cachedWorldspace;
    const std::uint32_t wid = world ? world->GetFormID() : 0;
    const bool changedWorld = _ghoulPackWorldID != 0 && wid != _ghoulPackWorldID;
    const float dist        = player->GetDistanceFromPoint(_ghoulPackCenter);
    const bool movedFar     = !changedWorld && dist > kGhoulDespawnDist;  // only trust dist in the same worldspace

    if (wentIndoors || changedWorld || movedFar)
    {
        Log()->info(
            "[Storm/Ghouls] Player left the area ({}). Despawning the hunter pack.",
            changedWorld ? "changed worldspace" : (wentIndoors ? "went indoors" : "moved far"));
        ClearStormGhouls();
    }
}


void RadiationEngine::ClearStormGhouls()
{
    if (_stormGhoulRefs.empty()) return;
    auto logger = Log();
    int cleared = 0;
    for (auto& handle : _stormGhoulRefs)
    {
        auto ptr = handle.get();
        if (auto* ref = ptr.get())
        {
            ref->Disable();
            ref->SetDelete(true);
            ++cleared;
        }
    }
    _stormGhoulRefs.clear();
    _ghoulPackWorldID = 0;   // pack gone — clear its area trackers so stale state can't mislead the next check
    logger->info("[Storm/Ghouls] Cleared {} storm ghoul ref(s).", cleared);
}


void RadiationEngine::ApplyStormIMAD(int typeIndex)
{
    if (typeIndex < 0 || typeIndex >= kStormTypeCount) return;

    // Dust Storm uses NO image-space modifier (like vanilla CommonwealthDusty): the brown TintColor
    // added over the dark-blue night sky rendered PURPLE, and it read as a flat "painted" grade on top.
    // The heavy fog + brown weather colours + camera-dust particles carry the look instead. Clear any
    // dust IMAD still running from a prior apply and bail.
    if (typeIndex == kDustStormIndex)
    {
        ClearStormIMAD();
        return;
    }

    RE::TESImageSpaceModifier* imad = _stormIMADs[typeIndex];
    if (!imad) return;

    // Stop any previously-running storm IMAD first to avoid stacking.
    if (_activeStormImadIndex >= 0 && _activeStormImadIndex < kStormTypeCount
        && _stormIMADs[_activeStormImadIndex])
    {
        RE::ImageSpaceModifierInstanceForm::Stop(_stormIMADs[_activeStormImadIndex]);
    }

    RE::ImageSpaceModifierInstanceForm::Trigger(imad, 1.0f, nullptr);
    _activeStormImadIndex = typeIndex;
    Log()->info("[StormIMAD] Applied atmosphere IMAD for '{}' (type {}).",
        kStormTypes[typeIndex].name, typeIndex);
}


void RadiationEngine::ClearStormIMAD()
{
    if (_activeStormImadIndex < 0) return;
    if (_activeStormImadIndex < kStormTypeCount && _stormIMADs[_activeStormImadIndex])
        RE::ImageSpaceModifierInstanceForm::Stop(_stormIMADs[_activeStormImadIndex]);
    _activeStormImadIndex = -1;
    Log()->debug("[StormIMAD] Storm atmosphere IMAD cleared.");
}


void RadiationEngine::EnsureQuestRunning(RE::TESQuest* quest)
{
    if (!quest) return;
    DumpQuestStageTable(quest);
    // TESQuest::SetStage (decompiled @ RVA 0x5D71E0) returns FALSE and does nothing unless the quest's
    // runtime ENABLED bit is set: `if ((quest+0xF4 & 1) != 0)`, i.e. QUEST_DATA.flags bit 0. A
    // StartGameEnabled quest added to an existing save via an ESL stays DORMANT (that record flag only
    // auto-fires on a NEW GAME), so the bit is clear, so every SetStage(10/11/12/20) silently failed —
    // the dome / camera shake / firestorm nuke never ran (log: "SetStage(11) result=false").
    //
    // Set the enabled bit NATIVELY. This is a plain memory write (safe on the main-thread Update tick)
    // — NOT Papyrus Quest.Start(), which is a latent native that CTDs when dispatched from here.
    if ((quest->data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kEnabled)) == 0)
    {
        quest->data.flags |= static_cast<std::uint16_t>(RE::QuestFlag::kEnabled);
        Log()->info("[StormFX] Quest 0x{:03X} was dormant (ESL-on-existing-save) -> enabled natively.",
            quest->GetFormID() & 0xFFF);
    }

    // NOTE: an older build here also rebuilt the quest's SetStage lookup table via a raw OG-only module
    // offset for TESQuest::InitSearchTables (0x5E07D0). That has been REMOVED. SetStage is no longer used
    // to drive any FX — the dome, camera shake, and firestorm nuke are all native now, and TriggerStormFX
    // treats SetStage's result as informational only — so rebuilding its scatter table served no purpose
    // (and, per the earlier logs, SetStage returned false even with the rebuild). No private RVA remains.
    static bool questTablesInit = false;   // session latch (EnsureQuestRunning is a static member)
    if (!questTablesInit)
    {
        questTablesInit = true;
        DumpQuestStageTable(quest);   // diagnostic dump of the stage table as the engine built it

        // STORM DOME: the RadStormFX script ships with `Bool Property EnableCloudDome = false`, so
        // _SpawnDome() has always bailed out immediately ("cloud dome disabled (using weather fog
        // instead)"). Flip it ON at runtime.
        //
        // Deliberately NOT done by editing the ESP: RadiationOverhaul.esp's raw TES4 master order is
        // [0] DLCCoast, [1] DLCNukaWorld, [2] Fallout4.esm — Fallout4.esm is NOT index 0. The plugin's
        // FormIDs are built against that mapping, so it is self-consistent, but a save_plugin that
        // re-sorts masters to load order would remap every Fallout4.esm reference and silently corrupt
        // the plugin (hang on load, no crash log). Not worth it for one boolean.
        //
        // This is a script PROPERTY read/write on the bound script object under attachedScriptsLock —
        // a locked data access, NOT a VM dispatch, so it is safe on the main-thread Update tick
        // (only DispatchMethodCall/latent natives would be unsafe here).
        if (auto* gameVM = RE::GameVM::GetSingleton())
        {
            if (auto* vm = gameVM->GetVM().get())
            {
                auto* concreteVM = static_cast<RE::BSScript::Internal::VirtualMachine*>(vm);
                RE::BSAutoLock lock(concreteVM->attachedScriptsLock);
                const auto handle = vm->GetObjectHandlePolicy().GetHandleForObject(
                    static_cast<std::uint32_t>(quest->GetFormType()), quest);
                if (const auto it = concreteVM->attachedScripts.find(handle);
                    it != concreteVM->attachedScripts.end())
                {
                    for (auto& attached : it->second)
                    {
                        auto* obj = attached.get();
                        if (!obj) continue;
                        if (auto* prop = obj->GetProperty(RE::BSFixedString("EnableCloudDome")))
                        {
                            *prop = true;
                            Log()->info("[StormFX] Storm dome ENABLED (RadStormFX.EnableCloudDome=true) — "
                                        "KORs CloudArena layers will spawn with the storm.");
                        }
                    }
                }
                else
                {
                    Log()->warn("[StormFX] Quest script not bound yet — could not enable the storm dome.");
                }
            }
        }
    }
}


void RadiationEngine::DumpQuestStageTable(RE::TESQuest* quest)
{
    // Called twice: once BEFORE InitSearchTables (expected: table empty -> why SetStage returned false)
    // and once AFTER (expected: stages 0/10/11/12/20 present -> SetStage can now resolve them).
    static int dumps = 0;
    if (!quest || dumps >= 2) return;
    ++dumps;
    Log()->info("[StormFX/StageDiag] ---- dump #{} ({}) ----", dumps,
        dumps == 1 ? "BEFORE InitSearchTables" : "AFTER InitSearchTables");

    const auto& table = quest->stageTable;
    Log()->info(
        "[StormFX/StageDiag] quest={:p} flags=0x{:04X} stages.size={} stages.data={:p} "
        "table.ptr={:p} size={} mask=0x{:X} avail={} lastFree={}",
        static_cast<void*>(quest), static_cast<unsigned>(quest->data.flags), quest->stages.size(),
        static_cast<void*>(quest->stages.data()), static_cast<void*>(table.table), table.size,
        table.mask, table.avail, table.lastFree);

    for (std::uint32_t i = 0; i < quest->stages.size(); ++i)
        Log()->info("[StormFX/StageDiag] stages[{}]={:p}", i,
            static_cast<void*>(quest->stages[i]));

    if (table.table && table.size > 0 && table.size <= 256)
    {
        for (std::uint32_t i = 0; i < table.size; ++i)
        {
            const auto& e = table.table[i];
            if (e.index != 0xFFFF || e.next != 0xFFFF)
                Log()->info("[StormFX/StageDiag] table[{}] next={} index={}", i, e.next, e.index);
        }
    }
}


void RadiationEngine::TriggerStormFX(bool on)
{
    // Storm screen FX via the RadStormFX Papyrus quest. Every storm spawns the cloud dome;
    // lethal storms also shake; the firestorm also detonates a distant nuke. No-op if the
    // companion ESP / quest isn't installed.
    //   stage 10 = ash      (shake + dome)
    //   stage 11 = firestorm (nuke + shake; dome deferred to the roll-in via TriggerStormDome)
    //   stage 12 = dome only (ambient sulfur/acid)
    //   stage 20 = stop (stop shake, remove dome)
    if (!_stormFXQuest) return;
    EnsureQuestRunning(_stormFXQuest);   // dormant on saves predating the quest; force-start
    // Sheltered = NO screen FX. The dome is a mesh that would follow the player indoors and clip
    // through walls (green fog inside buildings, see bug report), so a storm shows nothing on-screen
    // while the player is sheltered — only the exterior, exposed player sees the dome/shake/nuke.
    if (!on || IsPlayerSheltered())
    {
        _stormFXQuest->SetStage(20);
        return;
    }
    int stage = 12;  // default: ambient (exposed, non-lethal) — dome only, no shake or nuke
    const char* desc = "ambient: dome only (no shake)";
    if (_activeStormType >= 0 && _activeStormType < kStormTypeTableSize)
    {
        const StormType& st = kStormTypes[_activeStormType];
        // FIRESTORM nuke — now NATIVE, and driven per-tick from the Active handler (not fired here).
        // It used to be fired inline at this exposure change and latched _stormNukeFired=true *before*
        // knowing whether it detonated, so a transient streaming failure (cell not loaded yet) silently
        // consumed the storm's one shot. The Active-phase tick now calls SpawnDistantNuke() with a
        // bounded retry and only latches _stormNukeFired on a successful detonation.
        if (st.themeId == 1)
            desc = "firestorm: native nuke (fired on Active tick, bounded retry) + native shake";
        else if (st.lethal)
            { desc = "lethal: native shake + dome"; }
    }
    // The stage call is retained ONLY so a user-supplied RadStormFX script can still react if the
    // quest is ever repaired. Every FX it used to drive (dome, shake, nuke) is native now, so a
    // false return here is expected and harmless — it is not a failure path.
    const bool ok = _stormFXQuest->SetStage(static_cast<std::uint16_t>(stage));
    Log()->debug("[StormFX] SetStage({}) result={} ({}) — informational; FX are native.",
        stage, ok, desc);
}


void RadiationEngine::TriggerStormDome()
{
    // Spawn the cloud dome (stage 12) without re-triggering shake/nuke — used at the firestorm
    // roll-in so the dome arrives with the fog rather than at the detonation instant.
    if (!_stormFXQuest) return;
    EnsureQuestRunning(_stormFXQuest);
    _stormFXQuest->SetStage(12);
    Log()->info("[StormFX] SetStage(12) — cloud dome roll-in.");
}


void RadiationEngine::UpdateStormFXVisibility()
{
    // Storm fog/dome + atmosphere IMAD are EXTERIOR-only, mirroring the audio (UpdateAmbientAudio).
    // The dome is a mesh anchored to the player, so without this it follows them indoors and clips
    // through walls; the IMAD is a full-screen post-process colour grade that ignores geometry, so
    // it tints interiors too. Hide both when the player is sheltered, restore them when exposed.
    // Edge-triggered (only on a change) so we don't re-issue the quest stage / IMAD every tick.
    const bool exposed = !IsPlayerSheltered();
    if (exposed == _stormFXVisible) return;
    _stormFXVisible = exposed;
    if (exposed)
    {
        TriggerStormFX(true);                  // shake (Papyrus, lethal); nuke only if not yet fired
        SpawnStormDome();                      // re-spawn the native dome on stepping back outside
        ApplyStormIMAD(_activeStormType);      // re-apply the colour grade
        // Firestorm's ground fire ring also restores on re-exposure (every other FX does). Only during
        // Active — it's a peak effect, not a warning one — and only for the firestorm theme; SpawnFireRing
        // self-clears its old refs at entry, so re-calling is safe.
        if (_stormPhase == StormPhase::Active && _activeStormType >= 0 &&
            _activeStormType < kStormTypeTableSize && kStormTypes[_activeStormType].themeId == 1)
            SpawnFireRing();
    }
    else
    {
        TriggerStormFX(false);                 // stop shake
        ClearStormDome();                      // despawn the native dome indoors (it would clip walls)
        ClearStormIMAD();                      // remove the green colour grade indoors
    }
}


void RadiationEngine::ApplyStormExposure()
{
    if (_activeStormType < 0 || _activeStormType >= kStormTypeTableSize) return;
    const StormType& st = kStormTypes[_activeStormType];

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* av = RE::ActorValue::GetSingleton();
    if (!player || !av) return;

    const bool sheltered = IsPlayerSheltered();

    // Indoors = fully safe from storm exposure, for ALL storm types. Previously AMBIENT storms
    // still applied half damage when sheltered, which read as "storms hurt me even inside".
    if (sheltered) return;

    // A full Power Armor suit is a sealed shield: no storm rads, no storm HP loss. The block also
    // runs in Update's rad-refund path so this is the matching HP-side guard.
    if (IsPlayerInPowerArmor()) return;

    // Rad-gear protection: the direct ModActorValue path below bypasses the engine's rad-resist
    // mitigation, so apply it here. Storms now respect the player's radiation EXPOSURE resistance
    // (hazmat suit, gas masks, etc.) — a hazmat suit's high resist makes the player nearly immune,
    // gas masks get a meaningful reduction. (kStormResistHalf = damage halves at that resist value.)
    float resist = av->radExposureResistance ? player->GetActorValue(*av->radExposureResistance) : 0.0f;
    if (resist < 0.0f) resist = 0.0f;
    const float gearFactor = kStormResistHalf / (kStormResistHalf + resist);
    const float radsMul = gearFactor, hpMul = gearFactor;

    if (av->rads && st.radsPerTick > 0.0f)
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->rads, st.radsPerTick * radsMul);
    if (av->health && st.healthPerTick > 0.0f)
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->health, -st.healthPerTick * hpMul);
}


bool RadiationEngine::IsGhoulActor(RE::Actor* a) const
{
    return a && _ghoulKeyword && a->race && a->race->HasKeyword(_ghoulKeyword);
}


void RadiationEngine::ApplyStormPeakToNPCs()
{
    // Main-thread only: called from the 4Hz Update hook, which runs on the main thread, so the
    // ProcessLists handle iteration below is safe without a lock.
    auto* pl = RE::ProcessLists::GetSingleton();
    auto* av = RE::ActorValue::GetSingleton();
    if (!pl || !av) return;

    auto logger = Log();
    int healed = 0;
    for (auto& handle : pl->highActorHandles)
    {
        auto actorPtr = handle.get();
        RE::Actor* actor = actorPtr.get();
        if (!actor || actor->IsDead(false) || actor->IsPlayerRef()) continue;

        // Ghoul NPCs are rad-immune and HEAL in radiation (lore-accurate), so the storm peak mends
        // them. Every other NPC is left completely untouched — storms must NEVER wipe wandering NPCs
        // such as Diamond City guards, caravans, or patrols, which were not flagged essential.
        if (IsGhoulActor(actor))
        {
            if (av->health)
                actor->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->health, kStormGhoulHeal);
            if (av->rads)   // ghouls never carry rads
            {
                const float r = actor->GetActorValue(*av->rads);
                if (r > 0.0f)
                    actor->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->rads, -r);
            }
            ++healed;
        }
        // else: protected — the storm does no harm to any non-ghoul NPC.
    }
    logger->info("[Storm] Peak pulse — healed {} ghoul NPC(s); all other NPCs protected.", healed);
}


void RadiationEngine::MakeNearbyNPCsFlee()
{
    // Main-thread only (4Hz Update hook) — ProcessLists iteration without a lock is safe here.
    auto* pl     = RE::ProcessLists::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!pl || !player) return;

    int fled = 0;
    for (auto& handle : pl->highActorHandles)
    {
        auto actorPtr = handle.get();
        RE::Actor* actor = actorPtr.get();
        if (!actor || actor->IsDead(false) || actor->IsPlayerRef()) continue;
        if (IsGhoulActor(actor)) continue;                    // ghouls revel in the storm — no flee

        auto* cell = actor->GetParentCell();
        if (!cell || cell->IsInterior()) continue;            // already inside
        if (actor->GetDistanceFromReference(player, false, true) > kFleeRadius) continue;

        // Engine flee: panic and run from the storm-stricken area (referenced to the player, who is
        // the storm's moving epicenter). Best-effort — FO4 flee AI is approximate.
        actor->InitiateFlee(player, true, true, false, cell, player, kFleeFromDist, kFleeToDist);
        ++fled;
    }
    if (fled > 0)
        Log()->info("[Storm] {} NPC(s) panicked and fled the storm.", fled);
}


void RadiationEngine::UpdateStorm()
{
    auto logger = Log();
    UpdateStormDomePosition();

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
        if (exposed && !_stormDomeUp)      SpawnStormDome();
        else if (!exposed && _stormDomeUp) ClearStormDome();

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
                // Ash / ambient storms: atmosphere arrives immediately with the peak (exposed only).
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
                // NOTE: we deliberately do NOT touch _stormFXVisible here — the visibility manager owns it
                // and reconciles the real FX state next tick. Hand-setting it desynced when the player ran
                // indoors mid-roll-in (the nuke's shake stayed on because the manager thought FX were off).
                if (!IsPlayerSheltered())
                {
                    ApplyStormIMAD(_activeStormType);   // engine colour grade rolls in
                    if (st.themeId == 1)
                        SpawnFireRing();                // ground fire ring rolls in
                    TriggerStormDome();                 // cloud dome rolls in with the blast wave
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
            ClearStormIMAD();       // remove the engine atmosphere color grading
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
