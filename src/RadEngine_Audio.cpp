#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"

using namespace RadInternal;

// Audio: the incoming-storm siren and the looping ambient/storm beds.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


// Play the incoming-storm siren (psi_storm_siren.xwm) on the shared _radstormCueHandle. Used by BOTH
// the vanilla-radstorm path (CheckRadstormWarning) and our managed storms (BeginStorm). A soft fade-in
// so it doesn't blast; tracked in a member handle so StopStormCue can fade it out later.
void RadiationEngine::PlayStormCue()
{
    auto* audio = RE::BSAudioManager::GetSingleton();
    if (!audio) { Log()->warn("[StormCue] BSAudioManager unavailable; cannot play siren."); return; }
    if (_radstormCueHandle.IsValid()) _radstormCueHandle.Stop();  // drop any prior cue before restarting
    RE::BSResource::ID file(kStormSoundPath);
    audio->GetSoundHandleByFile(_radstormCueHandle, file, 0x10, 0);  // 0x10 = 2D/non-positional
    if (_radstormCueHandle.IsValid())
    {
        _radstormCueHandle.SetStaticAttenuation(kRadstormCueAttenuation);
        _radstormCueHandle.FadeInPlay(kRadstormCueFadeMs);
        Log()->info("[StormCue] Siren started ('{}').", kStormSoundPath);
    }
    else
    {
        Log()->warn("[StormCue] Sound handle invalid for '{}' (check file exists / format).", kStormSoundPath);
    }
}


// Fade the incoming-storm siren out over fadeMs (short fall-off) instead of letting it ring on.
void RadiationEngine::StopStormCue(std::uint16_t fadeMs)
{
    if (_radstormCueHandle.IsValid()) _radstormCueHandle.FadeOutAndRelease(fadeMs);
}


// (Re)start / switch / stop a LOOPING ambient bed on 'handle'. curPath tracks what's playing.
// wantPath==nullptr fades it out. Otherwise: switch beds if the desired one changed, and re-trigger
// when the file has finished so it loops (the ~0.25s tick may leave a tiny seam — fine for a bed).
//
// The "did it change?" test below is a POINTER compare, not a content compare. That is only valid
// because the bed paths are `inline constexpr` in RadEngine_Internal.h — exactly one object each,
// program-wide. If they ever lose `inline`, each .cpp gets its own copy pointing at its own string
// literal, the compare goes permanently true across TUs, and the bed re-triggers EVERY TICK.
void RadiationEngine::EnsureBed(RE::BSSoundHandle& handle, const char*& curPath,
                                const char* wantPath, std::uint16_t atten)
{
    if (!wantPath)
    {
        if (handle.IsValid()) { handle.FadeOutAndRelease(1000); handle = RE::BSSoundHandle{}; }
        curPath = nullptr;
        return;
    }
    if (curPath != wantPath)   // switching to a different bed: fade the old out first
    {
        if (handle.IsValid()) { handle.FadeOutAndRelease(500); handle = RE::BSSoundHandle{}; }
        curPath = wantPath;
    }
    if (!handle.IsValid() || !handle.IsPlaying())   // initial start OR looped past the end
    {
        if (Play2DSound(wantPath, handle))
            handle.SetStaticAttenuation(atten);
    }
}


// Ambient audio bed manager. Runs every tick from Update() (NOT just during a managed storm) so it
// also covers debug ForceWeather and the general no-storm background. Layered by priority:
//   1. Storm bed  — if the CURRENT weather is one of our storm weathers (kStormTypes) and the player
//      is EXPOSED (outdoors). Driven off current weather, so a debug `weather:N` force plays it too.
//   2. General bed — quiet exterior background when no storm bed applies and the player is outdoors.
// Only one bed plays at a time; interiors are silent (both fade out).
void RadiationEngine::UpdateAmbientAudio()
{
    auto* sky = RE::Sky::GetSingleton();
    const std::uint32_t wid = (sky && sky->currentWeather) ? sky->currentWeather->GetFormID() : 0;

    // Which storm bed does the current weather call for? (match against kStormTypes)
    const char* stormBed = nullptr;
    if (wid)
        for (int i = 0; i < kStormTypeTableSize; ++i)
            if (kStormTypes[i].weatherID == wid) { stormBed = kStormTypes[i].ambient; break; }

    // Dust horizon build-up: the dust sky is "current" during the Warning approach, but hold its loud
    // wind bed until the storm actually HITS (Active), matching the gameplay gate — otherwise the wind
    // jumps to full under a still-clear sky. The general exterior bed covers the approach.
    if (stormBed && _dustBuildupActive && _stormPhase == StormPhase::Warning)
        stormBed = nullptr;
    if (!stormBed && _stormPhase == StormPhase::Active &&
        _activeStormType >= 0 && _activeStormType < kStormTypeTableSize &&
        RadCorrectnessPolicy::UsesVanillaRadstormSky(_activeStormType))
    {
        const RE::TESWeather* shared = StormSkyWeather(_activeStormType);
        if (shared && shared->GetFormID() == wid)
            stormBed = kStormTypes[_activeStormType].ambient;
    }

    const bool interior = [] {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        auto* cell = pc ? pc->GetParentCell() : nullptr;
        return cell && cell->IsInterior();
    }();
    const bool exposed = !IsPlayerSheltered();

    if (stormBed && exposed)
    {
        // Storm active + outdoors: play its bed, silence the general one.
        EnsureBed(_stormSoundHandle, _curStormBed, stormBed, _stormBedAtten);
        EnsureBed(_ambientBedHandle, _curAmbientBed, nullptr, 0);
    }
    else
    {
        EnsureBed(_stormSoundHandle, _curStormBed, nullptr, 0);
        // General background bed: any exterior when enabled (indoors is silent).
        const char* want = (_ambientEnabled && !interior) ? kAmbientBedPath : nullptr;
        EnsureBed(_ambientBedHandle, _curAmbientBed, want, _ambientAtten);
    }
}
