#pragma once

// Shared internals for the Radiation Engine translation units.
//
// RadiationEngine.cpp had grown to ~4,300 lines. The implementation is now split by subsystem
// across several .cpp files (storm, weather, mask, shelter, tissue, audio, debug), all of which
// need the same tuning constants, the kStormTypes table and a handful of small helpers. Those
// used to live in an anonymous namespace inside the single .cpp; they live here now.
//
// Each engine .cpp does:  #include "RadEngine_Internal.h"  +  using namespace RadInternal;
// so every existing unqualified reference keeps working unchanged.

#include "pch.h"
#include "RadiationEngine.h"
#include "RadiationConfig.h"
#include "RadHud.h"

#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESGlobal.h>
#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/A/ACTOR_VALUE_MODIFIER.h>
#include <RE/A/ActorEquipManager.h>
#include <RE/B/BGSObjectInstance.h>
#include <RE/B/BGSReferenceEffect.h>
#include <RE/B/BGSArtObject.h>
#include <RE/T/TESImageSpace.h>
#include <RE/B/BGSLocation.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSPerk.h>
#include <RE/B/BipedAnim.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/B/BGSBipedObjectForm.h>
#include <RE/B/BIPED_OBJECT.h>
#include <RE/B/BGSListForm.h>
#include <RE/B/BGSInventoryList.h>
#include <RE/B/BGSInventoryItem.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESHitEvent.h>
#include <RE/B/BSTEvent.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESImageSpaceModifier.h>
#include <RE/I/ImageSpaceModifierInstanceForm.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESQuest.h>
#include <RE/G/GameScript.h>
#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSScript_IStackCallbackFunctor.h>
#include <RE/B/BSScript_IObjectHandlePolicy.h>
// Needed to read/write a bound script's PROPERTIES (storm dome enable). A locked data access on the
// script object, NOT a VM dispatch — see EnsureQuestRunning.
#include <RE/B/BSScript_Object.h>
#include <RE/B/BSScript_Variable.h>
#include <RE/B/BSScript_Internal_VirtualMachine.h>
#include <RE/B/BSScript_Internal_AttachedScript.h>
#include <RE/C/Calendar.h>
#include <RE/S/Sky.h>
#include <RE/T/TESWeather.h>
#include <RE/B/BSAudioManager.h>
#include <RE/B/BSSoundHandle.h>
#include <RE/B/BSResource_ID.h>
#include <RE/A/Actor.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESRace.h>
#include <RE/T/TESFaction.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESWorldSpace.h>
#include <RE/T/TESWeather.h>
#include <RE/B/bhkPickData.h>
#include <RE/B/BSHavok.h>
#include <RE/C/COL_LAYER.h>
#include <RE/N/NiPoint3.h>
#include <RE/U/UI.h>
#include <RE/P/ProcessLists.h>
#include <RE/S/SendHUDMessage.h>
#include <RE/T/TESRace.h>
#include <RE/T/TESQuest.h>
#include <RE/T/TESBoundObject.h>
#include <RE/I/ITEM_REMOVE_REASON.h>
#include <RE/B/BGSHazard.h>
#include <RE/B/BGSExplosion.h>
#include <RE/N/NEW_REFR_DATA.h>
// Active-effect enumeration — used by the Fix Stats button to name which foreign magic effects are
// lowering the player's SPECIAL stats (hunger/thirst/fatigue/disease/other mods).
#include <RE/M/MagicTarget.h>
#include <RE/A/ActiveEffect.h>
#include <RE/A/ActiveEffectList.h>
#include <RE/E/EffectItem.h>
#include <RE/E/EffectSetting.h>
#include <RE/M/MagicItem.h>

#include <random>
#include <cstdio>
#include <cstdlib>

// For the debug hotkey (GetAsyncKeyState). Included after RE/ headers to avoid Windows
// macros clobbering engine types. NOMINMAX keeps Windows' min/max macros from breaking
// std::min / std::max used elsewhere in this file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace RadInternal
{
    // Cached logger. spdlog::get() takes a global-mutex registry lookup on every call, and the 4Hz
    // hook plus its helpers log constantly, so the "FalloutAnomaly" logger is resolved once here.
    // It is registered in main.cpp's InitializeLogger() before any engine code runs, so the pointer
    // is non-null on first use and valid for the process lifetime.
    inline spdlog::logger* Log()
    {
        static spdlog::logger* s = spdlog::get("FalloutAnomaly").get();
        return s;
    }

    constexpr const char* kEslName = "RadiationOverhaul.esp";
    constexpr uint32_t     kPerkBaseFormID  = 0x820;  // matches gen_esl.py PERK_BASE
    constexpr uint32_t     kRadWeatherListID     = 0x802;  // FLST of weathers that count as "rad weather"
    constexpr uint32_t     kGasMaskFilterID      = 0x803;  // MISC consumable: RadOverhaul_GasMaskFilter
    constexpr uint32_t     kGasMaskRepairKitID   = 0x804;  // MISC consumable: RadOverhaul_GasMaskRepairKit
    constexpr uint32_t     kGasMaskKeywordID     = 0x809;  // KYWD FARO_GasMask; required with the mouth slot
    constexpr int          kNpcMaskRescanTicks   = 20;     // ~5 s at 4 Hz: re-scan to mask NPCs that loaded mid-storm
    constexpr uint32_t     kTissueGlobalID       = 0x850;
    constexpr uint32_t     kPermTissueGlobalID   = 0x851;  // permanent (scarred) tissue
    // Player-ghoulification (globals 0x854 / quest 0x870) was removed; those records remain in the ESL
    // as harmless orphans. The one-time save-revert migration was retired 2026-06-30.
    constexpr uint32_t     kStormFXQuestFormID   = 0x871;  // RadOverhaul_StormFXQuest — camera shake 10/20

    // Fix Stats debug button: how many points of SPECIAL deficit a single press claws back per stat.
    // The press fully removes THIS mod's own radiation penalty, then offsets up to this many points of
    // any remaining (foreign) negative on each SPECIAL — so repeated presses chip a heavy debuff away.
    constexpr int kFixStatsReliefPerPress = 5;

    // Pillar I tuning (per 4 Hz tick, dt = 0.25 s). Softened 2026-06-08: early-game tissue was
    // brutal (~400 by Concord). Gain more than halved, passive heal doubled, and tissue only builds
    // above moderate rads so light exposure no longer accumulates damage.
    constexpr float kTissueGainRate    = 0.00009f;  // fraction of current rads -> tissue / tick
    constexpr float kTissueHealRate    = 0.10f;     // tissue healed / tick when rads are low
    constexpr float kTissueHealBelow   = 150.0f;    // particulate rads under which tissue heals
    constexpr float kTissueMax         = 1000.0f;
    constexpr float kTissueWeight      = 1.0f;      // how much tissue adds to effective severity
    // Permanent scarring: accumulates slowly while tissue > threshold (chronic exposure).
    // Chelation's item effect is disabled fail-closed pending a verified source-identifying
    // item-use adapter. Starts later and accrues slower now so a rough early game does not
    // permanently scar the player toward ghoulification.
    constexpr float kPermAccrualThresh = 200.0f;    // tissue level that starts scarring
    constexpr float kPermAccrualRate   = 0.006f;    // perm tissue accrued per tick above threshold

    // ---- CHRONIC TISSUE CONSEQUENCES (2026-07-13) --------------------------------------------
    // Severity is normalised: t = tissue / kTissueMax (0..1). Since tissue can never heal below the
    // permanent scar, every penalty below automatically FLOORS at the scar level.
    //
    // Layer 1 — chronic wasting (always on, scales continuously with t):
    constexpr float kTissueCarryMax  = 120.0f;  // max carry-weight loss at full tissue (muscle wasting)
    constexpr float kTissueHealMax   = 50.0f;   // max healRateMult loss (stimpaks/food heal far less)
    constexpr float kTissueSpeedMax  = 15.0f;   // max speedMult loss (sluggish)
    constexpr float kTissueSpeedFrom = 0.50f;   // t at which sluggishness begins
    constexpr float kTissueApMax     = 40.0f;   // max actionPoints loss (fatigue)
    constexpr float kTissueApFrom    = 0.40f;   // t at which fatigue begins
    // Death spiral: a radiation-sick body absorbs damage FASTER. Tissue accrual is multiplied by
    // (1 + t * this), so letting tissue run away genuinely compounds instead of being linear.
    constexpr float kTissueSpiral    = 1.50f;

    // Permanent scarring gets its OWN penalty (it previously did nothing whatsoever): lost ENDURANCE,
    // i.e. lost MAX HP you cannot heal, rest off, or RadAway away. It remains until a verified item
    // cure adapter is implemented; no doctor-treatment path is active. This is folded into the
    // existing floor-guarded END penalty so the two can never floor the stat.
    constexpr int   kScarEndMax      = 4;       // max END lost to full permanent scarring

    // Layer 2 — visceral symptoms, layered by severity (t thresholds).
    constexpr float kSymCoughFrom    = 0.30f;   // occasional cough
    constexpr float kSymNauseaFrom   = 0.55f;   // nausea: gagging + warning
    constexpr float kSymVomitFrom    = 0.75f;   // vomiting fits: retch + HP hit
    constexpr float kSymVomitHp      = 6.0f;    // HP lost per vomiting fit
    constexpr float kSymVomitFloorHp = 20.0f;   // never let a vomiting fit drop the player below this (no symptom kills)
    constexpr int   kSymMinTicks     = 40;      // ~10 s at 4 Hz — fastest a fit can recur
    constexpr int   kSymMaxTicks     = 140;     // ~35 s — slowest
    // Rest (sleep / wait / fast-travel) heals reducible tissue per GAME-hour elapsed. The 4Hz hook
    // pauses during those, so the passive per-tick regression can't run — this applies it in bulk.
    constexpr float kRestTissueHealPerGameHour = 40.0f;

    // Vanilla radstorm weather FormIDs (Fallout4.esm). When one of these becomes the
    // active weather we play the warning sound once.
    constexpr uint32_t kRadstormWeathers[] = { 0x1C3D5E, 0x2392A3, 0x222394, 0x24A3C0 };

    inline bool IsRadstormWeather(uint32_t formID)
    {
        for (uint32_t rw : kRadstormWeathers)
            if (formID == rw) return true;
        return false;
    }

    // Generic "is this a rain weather" test via the weather record's classification flags
    // (weatherData[kFlags] bit kRainy). Catches vanilla AND any weather-mod rain weather without
    // hardcoding FormIDs — used to keep rain from blowing in on the first spawn into the world.
    inline bool IsRainyWeather(RE::TESWeather* w)
    {
        if (!w) return false;
        const auto flags = w->weatherData[static_cast<int>(RE::TESWeather::WeatherData::kFlags)];
        return (flags & static_cast<std::int8_t>(RE::TESWeather::WeatherDataFlags::kRainy)) != 0;
    }

    // Veto a queued radstorm only while its transition is still at the very start (the sky has not
    // visibly blended in yet). Above this the radstorm is already on-screen, so a cut would be
    // visible — we let it run instead of double-shocking the player.
    constexpr float kRadstormVetoMaxPct = 0.10f;

    // Storm onset sequence: the cloud dome fades in overhead first, THEN the siren goes up. This is the
    // gap between the two (4 Hz ticks) — long enough for the dome's engine fade-in to actually read on
    // screen before the alarm starts, so the storm builds instead of everything landing at once.
    constexpr int kSirenAfterDomeTicks = 8 * 4;   // ~8 s

    // Custom storm audio. WAV/XWM only — engine does not play MP3.
    // Vanilla-radstorm soft warning cue. Repointed off the old loose "My Audio.wav" (a stereo PCM wav,
    // which FO4's file-based audio silently fails to play — see the loose-wav gotcha) to a proper xwm.
    constexpr const char* kStormSoundPath = "Sound\\FalloutAnomaly\\ambient\\psi_storm_siren.xwm";

    // STALKER-style "blowout" storm set (converted from .ogg to .xwm, see convert_sounds.ps1).
    // impact = firestorm nuke detonation, wave = the blast wave rolling in. (The incoming-siren and
    // separate ambient-bed cues were removed by request; their plumbing is gone with them.)
    constexpr const char* kSndNukeImpact   = "Sound\\FalloutAnomaly\\ambient\\blowout\\blowout_impact.xwm";
    constexpr const char* kSndBlastWave    = "Sound\\FalloutAnomaly\\ambient\\blowout\\blowout_wave_01.xwm";

    // Looping ambient BEDS. Per-storm beds are driven by the CURRENT WEATHER (UpdateAmbientAudio), so
    // a debug ForceWeather plays them exactly like a real storm. The general bed plays quietly in any
    // exterior when no storm is up. All xwm (loose stereo wav will not play). Files live under
    // Sound\FalloutAnomaly\{anomaly,ambient,ambient\blowout}\.
    // INLINE IS LOAD-BEARING, do not drop it. EnsureBed decides "did the bed change?" by comparing
    // these as POINTERS (`curPath != wantPath`), not by content. A plain `constexpr const char*` in a
    // header has INTERNAL linkage, so every .cpp would get its OWN copy pointing at its own string
    // literal — and the compare would then be false across translation units, re-triggering the bed
    // EVERY TICK (an audio stutter with no compile error). `inline constexpr` gives exactly one object
    // program-wide, which is the guarantee these had when the engine was a single .cpp.
    inline constexpr const char* kBedAshFallout  = "Sound\\FalloutAnomaly\\ambient\\blowout\\blowout_wind_01.xwm";
    inline constexpr const char* kBedFirestorm   = "Sound\\FalloutAnomaly\\anomaly\\psi_storm_01.xwm";
    inline constexpr const char* kBedSulfurStorm = "Sound\\FalloutAnomaly\\ambient\\blowout\\blowout_bkgrnd.xwm";
    inline constexpr const char* kBedAcidRain    = "Sound\\FalloutAnomaly\\ambient\\blowout\\blowout_wind_02.xwm";
    inline constexpr const char* kBedDustStorm   = "Sound\\FalloutAnomaly\\ambient\\blowout\\blowout_wind_03.xwm";
    inline constexpr const char* kAmbientBedPath = "Sound\\FalloutAnomaly\\ambient\\blowout\\envcreepywind.xwm";

    // Gas-mask foley (METRO EXODUS set from the GMW tweaked-audio mod, copied into our own tree so
    // the mod stays self-contained — see docs/GMW_GAP_ANALYSIS.md). Male/female variants picked by
    // the player NPC's female flag.
    // NOTE: these are .xwm, NOT .wav — FO4's file-based BSAudioManager::GetSoundHandleByFile reliably
    // streams xwm (every shipped storm sound here is xwm) but silently fails on loose stereo PCM wav,
    // which is why the first slice's .wav foley never played (2026-07-06). Converted with xWMAEncode.
    constexpr const char* kSndMaskDrawM      = "Sound\\FalloutAnomaly\\mask\\gasmaskdraw.xwm";
    constexpr const char* kSndMaskDrawF      = "Sound\\FalloutAnomaly\\mask\\gasmaskdraw_f.xwm";
    constexpr const char* kSndMaskOffM       = "Sound\\FalloutAnomaly\\mask\\gasmaskwithdraw.xwm";
    constexpr const char* kSndMaskOffF       = "Sound\\FalloutAnomaly\\mask\\gasmaskwithdraw_f.xwm";
    constexpr const char* kSndFilterA        = "Sound\\FalloutAnomaly\\mask\\gasmaskfilter1.xwm";
    constexpr const char* kSndFilterB        = "Sound\\FalloutAnomaly\\mask\\gasmaskfilter2.xwm";
    constexpr const char* kSndFilterFastA    = "Sound\\FalloutAnomaly\\mask\\gasmaskfilterfast1.xwm";
    constexpr const char* kSndFilterFastB    = "Sound\\FalloutAnomaly\\mask\\gasmaskfilterfast2.xwm";
    constexpr const char* kSndChokeNoMaskM   = "Sound\\FalloutAnomaly\\mask\\gasmask_choking_male_nomask.xwm";
    constexpr const char* kSndChokeNoMaskF   = "Sound\\FalloutAnomaly\\mask\\gasmask_choking_female_nomask.xwm";
    constexpr const char* kSndChokeWithMaskM = "Sound\\FalloutAnomaly\\mask\\gasmask_choking_male_withmask.xwm";
    constexpr const char* kSndChokeWithMaskF = "Sound\\FalloutAnomaly\\mask\\gasmask_choking_female_withmask.xwm";

    inline bool IsPlayerFemale()
    {
        auto* pc  = RE::PlayerCharacter::GetSingleton();
        auto* npc = pc ? pc->GetNPC() : nullptr;
        return npc && npc->IsFemale();
    }

    // Fire-and-forget 2D sound; fills 'out' so the caller can stop it later. usage 0x10 = 2D.
    inline bool Play2DSound(const char* path, RE::BSSoundHandle& out)
    {
        auto* audio = RE::BSAudioManager::GetSingleton();
        if (!audio) { spdlog::warn("[Sound] no BSAudioManager for '{}'", path); return false; }
        RE::BSResource::ID file(path);
        audio->GetSoundHandleByFile(out, file, 0x10, 0);
        if (out.IsValid()) { out.Play(); return true; }
        // A false here means the engine couldn't build a handle for the file (missing path, or an
        // unsupported format — loose stereo PCM wav fails this way; xwm works). Surface it, don't
        // swallow it: silent no-ops here are exactly what hid the .wav foley failure.
        spdlog::warn("[Sound] GetSoundHandleByFile failed (no handle) for '{}'", path);
        return false;
    }

    // ---- STORM SYSTEM tuning ----
    constexpr uint32_t kStormImadBase        = 0x880;  // 0x880-0x884: Ash/Fire/Sulfur/Acid/Dust atmosphere IMADs
    constexpr uint32_t kNextEmissionGlobalID = 0x852;  // matches gen_esl.py NEXT_EMISSION_GLOBAL_FORMID
    constexpr uint32_t kForceStormGlobalID   = 0x853;  // console debug trigger (xEdit/scripts only)
    constexpr int      kExposedNagTicks      = 5 * 4;  // re-warn an exposed player every 5 s
    constexpr std::uint16_t kStormSoundFadeMs = 4000; // fade the storm ambient out over 4 s at storm end

    // Vanilla radstorms are a passing weather event, not a scripted mod set-piece, so their cue is
    // softened: it eases in over a couple seconds (no abrupt blast) at reduced volume, reading as
    // ambient atmosphere rather than an alarm. (Attenuation is in centibels — higher = quieter.)
    constexpr std::uint16_t kRadstormCueFadeMs      = 2500;
    constexpr std::uint16_t kRadstormCueFalloffMs   = 2000;  // short fall-off when the storm ends/passes
    constexpr std::uint16_t kRadstormCueAttenuation = 1200;

    // Storm fog is set at runtime (EnsureEslLoaded) per storm type from kStormTypes[].fog* — see the
    // table below. Each storm gets its own visibility wall (Dust Storm = densest); keeping the near
    // distance >= ~900 avoids the old camera-centred "fog sphere" dome. Vanilla radstorm is near=1200
    // far=8500 for reference.
    constexpr int      kNukeRollInTicks      = 12 * 4; // firestorm: nuke detonates, then ~12 s later
                                                       // the fog/fire/IMAD + dome + exposure roll in
                                                       // (the shockwave sweeping in from the horizon)
    // Firestorm nuke: SpawnDistantNuke can fail transiently (player 3D/worldspace not up, target cell
    // not yet streamed, engine returns a null handle). Count actual native submissions separately from
    // the absolute Active-phase wait so shelter cannot spend the attempt budget and no invalid state can
    // hold the atmosphere forever. Both limits are ~10 s at 4 Hz.
    constexpr int      kFirestormNukeMaxAttempts = 40;
    constexpr int      kFirestormNukeMaxWaitTicks = 40;
    // VK_F8 — Left Shift + F8 opens the debug menu. NOT F9 (the game's Quick Load — a passive
    // GetAsyncKeyState poll can't consume it, so Shift+F9 also quick-loaded), NOT F11 (used by the
    // user's chat overlay), and NOT F10 (Windows reserves it and GetAsyncKeyState reads it unreliably).
    constexpr int      kDebugMenuKey         = 0x77;

    // Game-hours between storms (scheduling uses game-time so sleep advances it). A storm
    // rolls a weighted type; lethal blowouts are rarer (see weights below).
    // Gap between storms, expressed in REAL-WORLD minutes. ScheduleNextStorm converts this to
    // game-time using the live timescale, so storm frequency is the SAME regardless of the
    // modlist's timescale. (The old fixed 4-14 GAME-hour gap fired storms back-to-back under a
    // fast Hardcore timescale — over an hour of non-stop storms.)
    constexpr float kStormMinRealMin = 35.0f;
    constexpr float kStormMaxRealMin = 90.0f;

    // Minimum real-play gap between VANILLA radstorms (Fallout4.esm, scheduled by the region/weather
    // system — NOT by us). The base game can chain these every few minutes; we "thin them out" by
    // letting one through, then forcing clear weather on any vanilla radstorm that rolls in within
    // this window. 60 min keeps occasional natural radstorms while killing the back-to-back chains
    // (raised from 30 min — back-to-back radstorms on leaving Sanctuary made the area unplayable).
    constexpr double kRadstormMinGapSec = 60.0 * 60.0;

    // Cell-exit grace window (in 4Hz ticks): after the player steps from shelter into the open, veto
    // any vanilla radstorm regardless of its transition pct for this long. ~60s covers the load/settle
    // window in which a region-queued radstorm is already fully rendered the instant the exterior loads.
    constexpr int kCellExitGraceTicks = 60 * 4;

    // New-game / early-game grace: suppress ALL vanilla radstorms until this much real-play time has
    // elapsed on the _playSeconds clock. Stops a radstorm from "blowing in" the moment the player
    // first reaches the Commonwealth (intro / vault exit), which read as a jarring scripted event.
    // Only affects fresh games — existing saves restore a large _playSeconds from the co-save, so the
    // grace is already long past. The mod's own managed storms (35-90 real-min) are NOT gated by this.
    constexpr double kRadstormStartupGraceSec = 30.0 * 60.0;

    // Cure detection ignores rads/HP deltas across a game-time jump larger than this (sleep, wait,
    // fast-travel pause the 4Hz hook). 0.01 game-days = ~14 game-min, far above any single tick's
    // advance even at a fast timescale, far below any sleep/wait.
    constexpr float kCureGapDays = 0.01f;

    // Lethal-storm peak: ghoul NPCs thrive in radiation, so they get HEALED. All other NPCs are
    // left untouched (storms must never wipe wandering NPCs — Diamond City guards, caravans, patrols).
    constexpr float kStormGhoulHeal     = 150.0f;  // HP restored to exterior ghouls at the storm peak

    // Vanilla Fallout4.esm keyword that marks ghoul races (feral, glowing one, ghoul player race, etc.).
    constexpr uint32_t kActorTypeGhoulKeyword = 0x00013794;

    // Storm ghoul HUNT: a lethal storm has a chance to unleash a PERSISTENT pack of feral ghouls that
    // hunt the player and do NOT despawn when the storm ends (they remain in the world until killed).
    constexpr int   kStormGhoulMinCount    = 5;
    constexpr int   kStormGhoulMaxCount    = 20;
    constexpr float kStormGhoulMinRadius   = 800.0f;  // spawn ring around the player (game units)
    constexpr float kStormGhoulMaxRadius   = 1500.0f;
    // Cleanup: the hunter pack persists past the storm, but despawns once the player leaves the area
    // (travels far from where they spawned, or goes indoors / to a new worldspace) to avoid buildup.
    constexpr float kGhoulDespawnDist      = 10000.0f;  // ~140 m from the pack's spawn center
    constexpr int   kGhoulCleanupTicks     = 8;         // check ~every 2 s at 4 Hz

    // NPC flee: during a lethal storm, non-ghoul NPCs caught outside panic and flee (engine InitiateFlee).
    constexpr float kFleeRadius   = 5000.0f;   // only panic NPCs within this range of the player
    constexpr float kFleeFromDist = 4096.0f;
    constexpr float kFleeToDist   = 8192.0f;
    constexpr int   kFleeRecheckTicks = 20;    // re-issue the flee order ~every 5 s at 4 Hz

    // Rad-gear protection vs storms. The storm applies rads via a direct ModActorValue, which
    // bypasses the engine's rad-resist mitigation, so we apply it ourselves: storm damage is scaled
    // by kStormResistHalf / (kStormResistHalf + radExposureResistance). At resist == this value the
    // damage is halved; a hazmat suit's high resistance makes the player nearly immune, and gas
    // masks (any radExposureResistance) get a meaningful reduction. Lower = gear protects more.
    constexpr float kStormResistHalf = 80.0f;

    // New-game clear-weather safety only fires once we are POST-war. The 2077 prewar intro
    // (Sanctuary) is an EXTERIOR cell in the Commonwealth worldspace, so an interior check alone
    // would pop the clear weather during the intro. GameDaysPassed is NOT reliable for this (it
    // barely moves and the 2077->2287 jump does not advance it), so we check the calendar YEAR,
    // which the game sets to 2287 after the vault.
    constexpr float kPostwarMinYear = 2100.0f;

    // Storm-type table — our self-contained versions of NAC's rad storms. weatherID matches
    // gen_esl.py (0x840..0x844). themeId selects the PrismaUI HTML overlay theme.
    // For LETHAL types, rads/hp apply per tick only when the player is EXPOSED (outdoors);
    // AMBIENT types apply while exposed too (no shelter-death); sheltered players take nothing
    // (ApplyStormExposure returns for all types when sheltered).
    struct StormType
    {
        uint32_t    weatherID;
        const char* name;
        int         themeId;        // HTML: 1 fire, 2 sulfur, 3 acid, 4 ash, 5 dust
        bool        lethal;
        float       radsPerTick;
        float       healthPerTick;
        int         warningTicks;
        int         activeTicks;
        int         aftermathTicks;
        int         weight;         // weighted random selection
        // Per-type fog (the DLL overrides the ESP fog at load so we tune visibility here, not the
        // record). near = where the fog starts, far = where it reaches full density (smaller = denser
        // wall), power = falloff curve (< 0.6 = aggressive nonlinear "wall of fog").
        float       fogNear;
        float       fogFar;
        float       fogPower;
        // Height fog (decompile-verified vs Sky::UpdateFog, RVA 0x64F950): the "rolling ground fog"
        // depth. heightMid = world-Z the fog is centred on; heightRange = vertical span it fills
        // (LARGE = fog fills the whole column, small = thin low band); hiDensity = HighDensityScale,
        // the particulate thickness (0..1). Our generated records shipped these at 0 -> flat "painted"
        // fog with no depth. Values below are anchored on vanilla storm/dust weathers: the Dust Storm
        // matches CommonwealthDusty (huge range + high density = a full-height dust wall), the others
        // get a moderate ground-fog band scaled by severity. fogData index map: near/far [8..17].
        float       fogHeightMid;
        float       fogHeightRange;
        float       fogHiDensity;
        const char* ambient;        // looping audio bed while this storm's weather is current (exposed)
    };

    constexpr StormType kStormTypes[] = {
        // weatherID, name,            theme, lethal, rads,  hp,   warn,    active,   after,  weight,  fogNear, fogFar, fogPow, hMid, hRange, hiDens, ambient bed
        // warn = lead time (seconds*4 ticks) the player gets to seek shelter BEFORE the rad sky +
        // damage hit at Active. Kept generous so storms never feel instant.
        // Total event (warn + active + after) is kept under ~5 real minutes per storm.
        { 0x840, "Ash Fallout",        4,     true,   18.0f, 2.5f, 20 * 4,  120 * 4,  30 * 4, 15,  1200.0f, 8000.0f, 0.45f, 128.0f,  2000.0f, 0.30f, kBedAshFallout  },  // 2.8 min
        { 0x841, "Rad Firestorm",      1,     true,   14.0f, 4.0f, 20 * 4,  100 * 4,  30 * 4, 15,  1200.0f, 4500.0f, 0.60f, 128.0f,  2000.0f, 0.25f, kBedFirestorm   },  // 2.5 min
        { 0x842, "Toxic Sulfur Storm", 2,     false,  4.0f,  0.5f, 15 * 4,  180 * 4,  20 * 4, 35,  1000.0f, 3200.0f, 0.55f, 128.0f,  2000.0f, 0.20f, kBedSulfurStorm },  // 3.6 min
        { 0x843, "Acid Rain",          3,     false,  3.0f,  0.8f, 15 * 4,  180 * 4,  20 * 4, 35,  1400.0f, 6000.0f, 0.65f, 128.0f,  2000.0f, 0.45f, kBedAcidRain    },  // 3.6 min
        { 0x844, "Dust Storm",         5,     false,  3.0f,  0.3f, 15 * 4,  150 * 4,  25 * 4, 30,   150.0f,  5000.0f, 0.30f,  64.0f,  3000.0f, 0.90f, kBedDustStorm   },  // 3.2 min — thick ground brownout (near=150, hiDens 0.90) that THINS above ~3000 (hRange) so the rotating dome (z+3800) is visible overhead; far=5000 so the dome isn't radially fogged out. + camera-dust VE, no IMAD
    };
    constexpr int kStormTypeTableSize = static_cast<int>(std::size(kStormTypes));

    // Dust-storm roll-in: the whole dust weather cross-fades IN from the outgoing sky over
    // kDustFogRollTicks (driven by ramping Sky::currentWeatherPct) instead of ForceWeather's hard snap.
    constexpr int   kAshStormIndex     = 0;         // index of "Ash Fallout" in kStormTypes
    constexpr int   kDustStormIndex    = 4;         // index of "Dust Storm" in kStormTypes
    constexpr int   kDustFogRollTicks  = 40;        // ~10 s at 4 Hz (Active roll-in: build-up peak -> full)
    constexpr float kDustBurstSec      = 4.0f;      // heavy camera-dust burst duration at wall arrival
    // Horizon build-up (Option B): during the WARNING phase the dust sky cross-fades in only to this
    // pct, so the horizon hazes + the sky darkens as the storm approaches, WITHOUT the full wall or any
    // gameplay effects (see the inRadWeather gate). The Active roll-in then ramps this -> 1.0.
    constexpr float kWarningBuildupMaxPct = 0.35f;

    // Dust roll (2026-07-10, purple-flash fix): the sky is snapped to dust in ONE frame at the arm point
    // (ForceWeather sets pct=1) and NEVER blended, because a held weather blend is exactly what renders
    // the modlist's ~0.5 s purple TRANSITION flash (forcing any vanilla weather flashes it too). Only the
    // FOG rolls in, over _dustRollTotal ticks, via per-tick fogData writes (the engine re-reads fogData
    // live every frame — a fog write is NOT a weather blend, so it can't flash purple). Instant brown sky
    // + dust wall closing in over the full duration, no smear.
    constexpr float kDustFogThinNear  = 8000.0f;    // roll START: fog begins far out (near-clear visibility)
    constexpr float kDustFogThinFar   = 40000.0f;   // roll START: fog reaches full only at the horizon
    constexpr float kDustFogThinPow   = 1.0f;       // roll START: gentle linear falloff (no wall yet)

    // Write every fog field on a weather. Indices are decompile-VERIFIED against the engine's own
    // reads in Sky::UpdateFog (RVA 0x64F950): [0]/[2]=Day/Night near, [1]/[3]=far, [4]/[5]=power,
    // [8][10][14][16]=height mid (near+far, day+night), [9][11][15][17]=height range,
    // [12][13]=HighDensityScale. Day==Night here for a uniform storm sky. (TESWeather::fogData is
    // float[18] at struct off 0xA80; TESWeather::GetFogData confirms index == fogData subscript.)
    // Safe perk-name accessor for logging (never returns nullptr). Used by both the ESL form
    // resolution (core) and the stage-perk swap (tissue), so it is shared here.
    inline const char* PerkName(RE::BGSPerk* perk)
    {
        if (!perk) return "<null>";
        const char* n = perk->GetFullName();
        return (n && n[0]) ? n : "<unnamed>";
    }

    // fogData[18] index map is decompile-verified from Sky::UpdateFog (RVA 0x64F950) — see
    // ReverseEngineering\Organized\1.10.163\README-Weather.md:
    //   0/2 near, 1/3 far, 4/5 power, 6/7 CLAMP, 8/10 heightMid(near), 9/11 heightRange(near),
    //   12/13 highDensityScale, 14/16 heightMid(far), 15/17 heightRange(far)   (each pair = Day/Night)
    inline void ApplyWeatherFog(RE::TESWeather* w, float fNear, float fFar, float fPow,
                                float hMid, float hRange, float hiDens)
    {
        if (!w) return;
        w->fogData[0]  = w->fogData[2]  = fNear;
        w->fogData[1]  = w->fogData[3]  = fFar;
        w->fogData[4]  = w->fogData[5]  = fPow;
        // NOTE: fogData[6]/[7] is the fog CLAMP (Day/Night), per the Sky::UpdateFog decompile. We
        // deliberately DO NOT touch it — the dust storm already renders correctly with the record's
        // existing value, and the clamp's zero-vs-no-clamp semantics aren't decompile-verified here, so
        // writing a guessed value could silently cap the storm fog. Leave it to the record.
        w->fogData[8]  = w->fogData[10] = w->fogData[14] = w->fogData[16] = hMid;
        w->fogData[9]  = w->fogData[11] = w->fogData[15] = w->fogData[17] = hRange;
        w->fogData[12] = w->fogData[13] = hiDens;
    }

    // -----------------------------------------------------------------------
    // Sky-occlusion roof fan — a radstorm is radiation from the SKY, so the meaningful question is
    // "is the sky blocked above me?". We fire an upward fan from the player's head: one ray straight
    // up plus four angled ~30 deg off vertical. Covered = at least kRoofFanMinHits of the 5 rays hit
    // structure. The fan (vs a single up-ray) tolerates roof edges/gaps; this also lets a real
    // overhang count as shelter, which is physically correct for sky radiation. Interior-cell and
    // Pulowski checks run first, so this only fires for outdoor (incl. player-built) cover.
    // -----------------------------------------------------------------------
    // Storm dome Z offset from the player. MUST be ~0: the CloudArena mesh's ORIGIN IS AT ITS BOTTOM and
    // it extends ~11000 units UPWARD (authored ObjectBounds z = -1042 .. +11040; the NIF's TopLightNode
    // sits at local z+10000, near the mesh top). So any positive offset lifts the whole dome — at the old
    // 3800/6000 the bottom RIM floated above the player's head and only a grazing upper band was visible
    // (read as "too high / too large / blurry"). At 0 the rim sits at player-1042 (below the feet, hidden
    // by terrain) and the dome arches over the entire sky as authored. Those old offsets were ours, not
    // KORs' — the stripped plugin has no placed refs and no script places it, so there was never an
    // original offset to copy.
    constexpr float kStormDomeZOffset     = 0.0f;
    // -----------------------------------------------------------------------
    // Native camera shake and explosion-detonation RE now live in the authority, commonlib_NonVR:
    //   RE::ShakeCamera(strength, pos, duration, distanceMult=1.0)      (RE/P/PlayerCamera.h)
    //   TESDataHandler::CreateExplosionAtLocation(base, pos, rot, cell, world)  (RE/T/TESDataHandler.h)
    // Full provenance is recorded in commonlib_NonVR RE-Work/docs/rad-overhaul-natives-2026-08.md.
    // Match the old RadStormFX values so the feel is unchanged: a decaying burst on the storm hit,
    // then a low sustained rumble for as long as the lethal storm is active.
    constexpr float kStormShakeBurstStrength     = 0.75f;  // psc BurstStrength
    constexpr int   kStormShakeBurstSteps        = 5;
    constexpr float kStormShakeBurstDuration     = 0.4f;
    constexpr float kStormShakeRumbleStrength    = 0.12f;  // psc RumbleStrength
    constexpr float kStormShakeRumbleDuration    = 0.7f;   // psc RumbleInterval + 0.1
    constexpr int   kStormShakeRumbleTicks       = 2;      // 0.5 s at 4 Hz; overlaps the 0.7 s duration
    // A burst step only advances once the native shake actually submits. If the player's 3D/cell is
    // transiently invalid (streaming), retry the step up to this many ticks, then abandon the burst and
    // hand over to the sustained rumble (which self-retries safely) so the burst can't spin forever.
    constexpr int   kStormShakeBurstMaxRetries   = 20;     // ~5 s at 4 Hz
    // Arena lights for the effect-LIT cloud shapes. KORs' script does PlaceAtNode on two nodes in the dome
    // NIF; we mirror BOTH at their authored local heights. The glowing-vortex look IS this lighting: the
    // clouds are lit brightest at the eye and fall off outward. (Do NOT bake the clouds self-emissive to
    // "fix" darkness — that flattens the gradient and makes the light a no-op.)
    // Both nodes carry translation only (identity rotation), so PlaceAtNode applies NO rotation — place
    // these unrotated. An earlier pitch-down guess was wrong.
    constexpr float kStormDomeLightNodeZ  = 10000.0f;  // TopLightNode  -> ArenaLight01 (radius 20000)
    constexpr float kStormDomeLensFlareZ  = 7800.0f;   // LensFlareNode -> ArenaLight02 (lens flare/glow)

    // Continuous camera dust (Dust Storm). Re-apply the dust ArtObject to the camera slightly faster
    // than it expires so it overlaps into a steady blowing-dust effect rather than pulsing.
    constexpr float kDustMoteSec          = 5.0f;    // per-application lifetime
    constexpr int   kDustMoteReapplyTicks = 18;      // ~4.5 s at 4 Hz — re-apply before it expires

    constexpr float kShelterHeadZ         = 96.0f;   // raise the ray origin from the feet toward the head
    constexpr float kRoofRayDist          = 768.0f;  // overhead reach of each roof ray (~38 ft) — catches taller roofs
    // Angled rays are ~20 deg off vertical. They reach ~263 units SIDEWAYS on the way up, which is what
    // made the old "any 2 of 5 rays" rule wildly over-permissive: standing in a street beside a building,
    // the two rays leaning toward it hit the building's WALL (not a roof), scored 2/5 and reported
    // "covered" with completely open sky overhead. Cover from SKY radiation must be OVERHEAD, so the
    // straight-up ray is now MANDATORY — no roof directly above you, no shelter, no matter what the
    // angled rays clip. The angled rays only corroborate that it's a real roof and not a lamp post or
    // pipe: for a roof ~200 units up they cross it just ~73 units out, so any genuine roof (incl. a
    // small player-built shack) still lights all four and reads 5/5 from anywhere underneath.
    constexpr float kRoofFanHoriz         = 0.342f;  // sin(20 deg) — tighter horizontal spread
    constexpr float kRoofFanVert          = 0.940f;  // cos(20 deg) — their vertical component
    constexpr int   kRoofFanMinHits       = 3;       // straight-up ray (REQUIRED) + at least 2 angled
    constexpr int   kEnclosedRecheckTicks = 4;       // re-evaluate ~every 1 s at 4 Hz (snappy shelter detect)

    // Power Armor counts as a radiation shield ONLY as a COMPLETE, SEALED rig: helmet + torso + 2 arms
    // + 2 legs = 6 distinct ArmorTypePower pieces. The helmet is separately mandatory (see
    // IsActorInPowerArmor) — it seals the player's breathing, so a helmet-less frame is not protection.
    constexpr std::size_t kPowerArmorFullPieces = 6;

    // Fire one ray against the cell's physics; true if it hits anything. TESObjectCELL::Pick is the
    // game's own activation-pick, called by the engine with GAME-WORLD coordinates (it handles the
    // Havok scaling internally), so we pass raw game coords — NOT pre-scaled — and use the default
    // collision filter the ctor sets (the same one the game's crosshair pick uses, which hits statics).
    inline bool RayHitsStructure(RE::TESObjectCELL* cell, const RE::NiPoint3& from, const RE::NiPoint3& to)
    {
        if (!cell) return false;
        RE::bhkPickData pick;
        pick.SetStartEnd(from, to);
        return cell->Pick(pick) != nullptr || pick.HasHit();
    }
}
