#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <RE/B/BSPointerHandle.h>
#include <RE/B/BSSoundHandle.h>
#include <RE/I/ImageSpaceModifierInstanceForm.h>
#include <RE/N/NiPoint3.h>

#include "RadCorrectnessPolicy.h"

class RadiationConfig;
namespace F4SE { class SerializationInterface; }
namespace RE {
    class Actor;
    class ActorValueInfo;
    class PlayerCharacter;
    class BGSHazard;
    class BGSExplosion;
    class BGSArtObject;
    class BGSKeyword;
    class BGSLocation;
    class BGSPerk;
    class TESImageSpaceModifier;
    class TESGlobal;
    class TESObjectCELL;
    class TESObjectREFR;
    class TESObjectARMO;
    class TESBoundObject;
    class TESWeather;
    class TESQuest;
    class TESWorldSpace;
    class BGSListForm;
}

// STALKER-style storm event phases. Driven off the existing 4Hz tick. Used by both
// lethal blowouts (Ash Fallout, Firestorm) and ambient hazard weathers (Sulfur, Acid).
enum class StormPhase
{
    Idle,       // counting down to the next storm
    Warning,    // siren + sky shift; (lethal) player has time to seek shelter
    Active,     // the storm itself — lethal types harm exposed players; ambient tick mild effects
    Aftermath,  // sky clearing, effects fading, then reschedule
    Approach    // dome-only SILENT lead ("something's coming") before Warning — no siren/alert/weather yet.
                // Appended (not inserted) so Idle/Warning/Active/Aftermath keep their int values for the HUD.
};

class RadiationEngine
{
public:
    static RadiationEngine& GetSingleton()
    {
        static RadiationEngine instance;
        return instance;
    }

    bool Initialize();
    void Update();
    // Called EVERY frame (not the 4Hz tick) so hotkeys are never missed between ticks.
    void PollInput();
    void UpdateStormDomeFollow();
    void UpdateStormIMADFade();
    void ApplyDebuffs(uint32_t radLevel);

    // Called on game load / new game: re-sync penalty tracking with the freshly
    // loaded actor state so we don't fight stale in-memory modifier bookkeeping.
    void ResetAppliedPenalties()
    {
        _appliedEndPenalty = 0;
        _appliedStrPenalty = 0;
        _specialSuspendStage = -1;   // manual-reset suspension does not persist across loads
        _currentStageLevel = 0;
        _appliedPerkIndex = -1;  // perks persist in the save; re-sync from scratch
        _activeStormImadIndex = -1;
        _stormImadInstance = nullptr;
        _stormImadTargetStrength = 0.0f;
        _stormImadFadeRate = 0.0f;
        _stormImadStopWhenFaded = false;
        _radstormWarned = false;
        if (_radstormCueHandle.IsValid()) _radstormCueHandle.FadeOutAndRelease(500);
        _stormRollInTicks = 0;
        // -1 sentinel = "no previous tick yet": both the sheltered rad-gain refund and the
        // cure-delta check skip the first tick after a load instead of reading the entire
        // saved rad pool as fresh gain (which silently erased rads when loading in PA/shelter).
        _prevRads = -1.0f;
        _prevHP   = -1.0f;
        _radAwayWindowTicks = 0;
    }

    // Called on kPostLoadGame / kNewGame: measures elapsed game time for sleep
    // healing, then resets per-tick tracking so cures don't fire spuriously on load.
    void OnPostLoad();

    // Called on kNewGame only: arm the one-shot clear-weather-on-first-exterior so a new
    // game (vault exit or alt-start) never opens straight into a rad storm.
    void OnNewGame();

    // F4SE co-save persistence — the storm play-clock + schedule survive save/load (registered in
    // main.cpp). Without this the play-clock resets to 0 each launch and storms never reach trigger.
    void SaveState(const F4SE::SerializationInterface* a_intfc) const;
    void LoadState(const F4SE::SerializationInterface* a_intfc);
    void RevertState();

    // Debug controls — driven by the PrismaUI debug menu (opened with Left Shift + F8).
    // Safe no-ops if the relevant globals/forms aren't resolved.
    void DebugForceStorm(int typeIndex);              // 0..4 -> fire that storm now
    void DebugEndStorm();                             // clear any active storm
    void DebugAddTissue(float tissue, float permanent);
    void DebugAddRads(float rads);
    void DebugClearAll();                             // clear rads + tissue + permanent
    void DebugGiveItem(uint32_t formID, const char* plugin, int count);  // add item to player
    void DebugPreviewWeather(int idx);                // force a storm weather (visual only)
    void DebugForceWeatherByForm(const std::string& spec);  // "Plugin.esp|1C8556" -> force ANY weather (debug WEATHER tab)
    void DebugScanWornMask();                         // log worn biped slots (mask detection = slot 44)
    void DebugClearWeather();                         // release any forced weather
    void PushGhoulSettingsToHud();
    void DebugHeal();                                 // full heal the player
    void DebugFixSpecial();                           // FULL reset: clear temp modifiers on all 7 SPECIALs (button)
    void DispelNegativeSpecials();                    // auto-heal any SPECIAL driven below 1 (corruption / foreign mods)
    // Called from the PrismaUI JS listener (Ultralight thread). It ONLY enqueues the command;
    // the actual engine work runs on the main thread in Update() via DispatchDebugCommand,
    // because engine calls (weather/VM/spawns) crash if made off the main thread.
    void HandleDebugCommand(const char* action);
    [[nodiscard]] bool StormGhoulsEnabled() const noexcept { return _stormGhoulsEnabled; }

    // Called by the gas-mask hit sink (main thread) when the player is struck: damages the worn mask.
    void OnPlayerHit();

    // Called only by a future source-identifying item-use adapter on the game main thread. Inventory
    // movement is deliberately not a cure signal because it also covers dropping, selling and transfer.
    void NotifyVerifiedCureUse(RadCorrectnessPolicy::CureSignal signal);

private:
    RadiationEngine() = default;
    ~RadiationEngine() = default;

    // Debug command queue — filled on the Ultralight thread by HandleDebugCommand, drained on
    // the main thread in Update() by DispatchDebugCommand (the actual if/else handler).
    void DispatchDebugCommand(const std::string& action);
    std::mutex               _debugCmdMutex;
    std::vector<std::string> _pendingDebugCmds;
    void DrainDebugCommands();

    RadiationConfig* _config = nullptr;
    uint32_t _currentStageLevel = 0;  // Track current stage for debuff changes
    std::chrono::high_resolution_clock::time_point _lastUpdateTime;

    // Currently-applied SPECIAL penalties. Stat modifications are applied as a
    // NET DELTA only when these change — never re-applied every tick (which would
    // stack cumulatively and crash actor values / max health to zero).
    int32_t _appliedEndPenalty = 0;
    int32_t _appliedStrPenalty = 0;

    // Set by the Fix Stats button to the radiation stage at which the player manually cleared their
    // SPECIAL. While the player's stage stays equal to this, ApplyStatModification does NOT re-apply
    // the END/STR penalty (so the manual reset isn't undone on the very next tick). Cleared when the
    // stage changes in EITHER direction (recovered a stage -> resume normal; got worse -> penalties
    // resume because the player chose not to cure). -1 = not suspended. Not persisted (re-derived).
    int32_t _specialSuspendStage = -1;

    // Companion ESL (RadiationOverhaul.esl) stage perks. Each perk carries an
    // ability that applies the SPECIAL penalties AND makes the effect visible in
    // the Pipboy PERKS + STATUS tabs. _eslAvailable is false if the ESL is not
    // installed/active, in which case we fall back to the C++ kTemporary penalties.
    static constexpr int kStageCount = 10;       // stages 100..1000
    // Sane END/STR penalties per stage index (0=Minor .. 9=Systemic Failure stage label). These
    // intentionally REPLACE the huge thresholds.json endPenalty/strPenalty values (5..100), which
    // floored SPECIAL to 1 every tick and corrupted the base when gear was equipped/unequipped.
    // Kept in lockstep with gen_esl.py's STAGES table (the Pip-Boy perk labels). Applied with a
    // hard "never below 1" guard. Shared by ApplyStatModification (the applier) and the
    // ApplyDebuffs stage banner (so the log reports the values actually applied).
    struct StagePenalty { int end; int str; };
    static constexpr StagePenalty kStagePenalties[kStageCount] = {
        {1, 0}, {1, 1}, {2, 1}, {2, 2}, {3, 2}, {3, 3}, {4, 3}, {5, 4}, {6, 4}, {7, 5},
    };
    RE::BGSPerk* _stagePerks[kStageCount] = {};
    int _appliedPerkIndex = -1;                  // -1 = none applied
    bool _eslAvailable = false;
    bool _eslChecked = false;

    RE::TESQuest* _stormFXQuest = nullptr;
    void TriggerStormFX(bool on);            // start (per storm type) / stop the storm FX
    int  _sirenDelayTicks = 0;               // >0: ticks until the incoming-storm siren starts (dome fades in first)
    // StartGameEnabled only auto-runs a quest on a NEW game; a quest added to the ESP after a
    // save was made stays dormant and SetStage() silently fails. Force-start via the Papyrus VM
    // (Quest.Start()) so SetStage works on existing saves. Idempotent — safe to call repeatedly.
    static void EnsureQuestRunning(RE::TESQuest* quest);
    static void DumpQuestStageTable(RE::TESQuest* quest);

    // PILLAR I — twofold radiation. Particulate = vanilla Rads (RadAway clears it);
    // tissue damage is a persistent pool (stored in a GlobalFloat) that builds up
    // from exposure and heals slowly, keeping a penalty floor after RadAway.
    RE::TESGlobal* _tissueGlobal = nullptr;
    float _tissueDamage = 0.0f;
    float UpdateTissueDamage(float particulateRads);  // returns effective severity

    // Permanent tissue damage: the portion of tissue scarring that cannot be cleared
    // by RadAway or sleep. Item-based cures are disabled fail-closed until a source-identifying
    // item-use adapter is verified; no doctor-treatment path is currently implemented.
    // HealTissue() floors at this value. Only HealPermanentTissue() reduces it.
    RE::TESGlobal* _permanentTissueGlobal = nullptr;
    float _permanentTissueDamage = 0.0f;
    void HealPermanentTissue(float amount, const char* source);

    // Per-tick tracking for the sheltered rad-gain refund and verified RadAway tissue healing.
    float _prevRads = -1.0f;   // -1 = no previous tick recorded (skip delta logic once)
    float _prevHP   = -1.0f;
    // Game-time at the last tick; -1 = unset. Used to detect sleep/wait/fast-travel jumps so the
    // verified RadAway healing window cannot survive a discontinuity.
    float _prevGameDays = -1.0f;

    // Debug-menu hotkey edge detection (Left Shift + F8). Track prev DOWN state ourselves
    // (reliable 0x8000 bit) and fire only on the up->down transition. The old `& 0x0001`
    // ("pressed since last call") LSB is documented-unreliable and false-fired. Not persisted.
    bool _f8WasDown = false;

    // STORM SYSTEM — our self-contained versions of NAC's rad storms. Lethal blowouts
    // (Ash Fallout, Firestorm) and ambient hazard weathers (Sulfur, Acid Rain) share one
    // scheduler that rolls a weighted type when a storm fires.
    // Scheduling uses game-time (rawDaysPassed) so sleeping advances toward the next storm;
    // phase timing uses tick counts so player-facing durations are real-time and pause with
    // gameplay (the update hook stops ticking in menus).
    static constexpr int kStormTypeCount = 5;
    RE::TESWeather* _stormWeathers[kStormTypeCount] = {};  // forced skies (0x840..0x844)
    RE::TESGlobal*  _nextEmissionGlobal = nullptr;  // persists next storm game-time (0x852)
    RE::TESGlobal*  _forceStormGlobal = nullptr;    // console debug trigger (0x853)
    StormPhase      _stormPhase = StormPhase::Idle;
    int             _activeStormType = -1;          // index into kStormTypes (-1 = none)
    int             _dustFogRollTick = 0;           // (legacy) Dust Storm Active roll-in ticks remaining
    bool            _dustBuildupActive = false;      // dust roll is running (sky settle + fog ramp)
    int             _warnTicksTotal = 0;             // total Warning ticks, for build-up progress
    RE::TESWeather* _dustRollPrevWeather = nullptr; // neutral/outgoing sky the sky-colour settle blends from
    int             _dustRollElapsed = 0;            // ticks since the dust roll armed (drives both ramps)
    int             _dustRollTotal   = 0;            // total ticks for the fog density ramp (~10s debug / ~75s natural)
    RE::BGSArtObject* _dustBurstArt = nullptr;      // heavy camera-dust burst art fired at wall arrival
    int             _forcedStormType = -1;          // debug-forced type for the next trigger
    bool            _forceStormNow = false;         // debug: begin a storm on the next Idle tick
    bool            _stormScheduled = false;        // a next-storm time has been picked
    // Storms are scheduled by REAL minutes of actual play, NOT game-time. _playSeconds accumulates
    // 0.25s per 4Hz tick (the hook only ticks during active gameplay), so it is a pure play clock,
    // immune to the GameDaysPassed quirks (prewar carryover, the 2077->2287 jump not advancing it).
    double          _playSeconds      = 0.0;        // accumulated real seconds of active play
    double          _nextStormPlaySec = 0.0;        // _playSeconds value at which the next storm fires
    double          _stormDomeBeginSec = 0.0;
    int             _stormTicksLeft = 0;            // ticks remaining in the current phase
    int             _stormExposedTicks = 0;         // consecutive ticks the player has been exposed
    bool            _weatherForced = false;         // we currently hold a weather override
    RE::TESWeather* _weatherReturnTarget = nullptr; // weather live before Rad/debug took sky control
    bool            _weatherReturnCaptured = false; // keep the original return target while test/storm weathers change
    bool            _releaseWeatherOverrideWhenSettled = false; // release only after the return blend is stable
    int             _weatherHandoffSettleTicks = 0; // short stable hold prevents an edge snap on override release
    int             _weatherHandoffWaitTicks = 0;
    bool            _debugWeatherHold = false;      // a debug-menu forced weather is held: stand the
                                                    // radstorm/dust/rain vetoes down so they can't revert it
    int             _stormPeakTick = 0;             // tick index at which the lethal NPC burst fires
    bool            _stormPeakFired = false;        // guards the one-time peak burst per storm
    int             _stormRollInTicks = 0;          // firestorm: ticks until fog/fire/IMAD roll in after the nuke
    bool            _stormFXVisible = false;        // dome/IMAD currently shown (exterior-only; toggled by shelter)
    bool            _stormNukeFired = false;        // firestorm nuke SUCCESSFULLY detonated once this storm (latched on success only)
    bool            _firestormAwaitingNuke = false; // firestorm Active: holding the atmosphere roll-in until the nuke resolves
    int             _nukeAttempts = 0;              // actual firestorm-nuke native submissions this storm
    int             _nukeWaitTicks = 0;             // absolute Active-phase nuke wait, including shelter/invalid states
    int             _shakeBurstRetries = 0;         // bounded retries for a burst step whose native shake couldn't submit (invalid 3D/cell)
    bool            _debugSkipToActive = false;     // debug fast-forward: collapse Approach/Warning to reach Active quickly (testing)
    RE::BSSoundHandle _stormSoundHandle{};          // storm ambient (per-type looping bed) — faded out at storm end
    RE::BSSoundHandle _radstormCueHandle{};         // vanilla-radstorm soft warning cue — tracked so End Storm/Clear can stop it
    RE::BSSoundHandle _ambientBedHandle{};          // general exterior ambient bed (background, no storm)
    const char*     _curStormBed   = nullptr;       // path currently playing on _stormSoundHandle (nullptr = none)
    const char*     _curAmbientBed = nullptr;       // path currently playing on _ambientBedHandle
    // [Ambient] INI — the general (non-storm) exterior background bed.
    bool            _ambientEnabled = true;         // [Ambient] Enabled
    std::uint16_t   _ambientAtten   = 1500;         // [Ambient] attenuation (centibels; higher = quieter)
    std::uint16_t   _stormBedAtten  = 500;          // storm bed attenuation (louder than the general bed)

    // [Storm] frequency — user-tuneable without a rebuild. Gap between OUR managed storms, in REAL
    // minutes of play. "Storms too often" is THE most common player complaint, so this is exposed.
    bool            _stormsEnabled     = true;      // [Storm] Enabled (0 = no managed storms at all)
    float           _stormMinRealMin   = 35.0f;     // [Storm] MinMinutes
    float           _stormMaxRealMin   = 90.0f;     // [Storm] MaxMinutes
    double          _vanillaRadstormGapSec = 60.0 * 60.0;  // [Storm] VanillaRadstormGapMinutes
    int             _domeLeadTicks     = 3 * 60 * 4; // [Storm] DomeLeadMinutes — silent dome-only lead before the alarm (default 3 min)
    // Debug-only lead override, consumed by the next BeginStorm. -1 = use _domeLeadTicks. A forced
    // storm shortens ONLY the silent lead; warningTicks and everything after stay at real timings.
    int             _forcedLeadTicks   = -1;
    int             _activeLeadTicks   = 0;         // lead the in-flight storm actually used (for logging)
    // Post-load storm-FX stop: OnPostLoad runs BEFORE the lazy EnsureEslLoaded, so its
    // TriggerStormFX(false)/ClearStormIMAD calls no-op on the session's FIRST load (quest/IMADs
    // still null) — a save made mid-storm would resume the persisted Papyrus rumble loop forever.
    // This latches the cleanup so the first Update tick re-fires it once the forms resolve.
    bool _pendingLoadFXCleanup = false;

    // Stop ALL storm audio (managed ambient + radstorm cue) and force the Commonwealth clear sky.
    // Used by the End Storm / Clear Weather debug buttons so they actually silence + reset the sky.
    void ForceClearAndSilence();

    // New-game weather safety: on a fresh game (vault exit or alt-start), force the Commonwealth
    // clear weather the first time the player is outdoors, so the player is never dropped straight
    // into a rad storm. Matches the `fw 0002b52a` console workaround. Armed by OnNewGame().
    RE::TESWeather* _clearWeather       = nullptr;  // Fallout4.esm 0x0002B52A (Commonwealth clear)
    bool            _forceClearOnEntry  = false;
    int             _spawnRadBlockTicks = 0;        // >0: refund rad gain after a spawn-storm intercept

    // Non-radioactive dust/sand storm weathers from "Gas Masks of the Wasteland" (+ vanilla
    // CommonwealthDusty). Not rads, but they read as a storm and can roll in at spawn — so they're
    // suppressed during the new-game grace and counted as "bad weather" by the clear-on-entry safety.
    // Resolved at ESL load (empty if GMW isn't installed). Runtime FormIDs.
    std::unordered_set<std::uint32_t> _dustWeathers;

    // Per-tick storm state machine: schedules, triggers, and resolves storms.
    void UpdateStorm();
    // Engine-native SMOOTH transition (vanilla path), not a snap. lockOverride=false leaves
    // overrideWeather alone so the region system keeps control (used by the vanilla-radstorm veto).
    void BeginSmoothWeather(RE::TESWeather* a_target, bool a_lockOverride = true);
    void UpdateDustFogRoll();          // per-tick: watches the smooth transition, fires the wall-arrival burst on arrival
    void PlayStormCue();               // start the incoming-storm siren (managed + vanilla radstorm)
    void StopStormCue(std::uint16_t fadeMs);  // fade the incoming-storm siren out (short fall-off)
    void BeginStorm(int typeIndex);    // start the dome-only APPROACH lead for a storm type
    void EnterWarningPhase();          // APPROACH lead elapsed -> alarm + alert + dust roll-in + Warning
    void EnterStormPhase(StormPhase phase, int durationTicks);
    void ScheduleNextStorm();          // pick the next storm's game-time
    float GameDays() const;            // current GameDaysPassed (0 if unavailable)
    int  RollStormType() const;        // weighted random index into kStormTypes
    bool IsPlayerSheltered() const;
    // True only when the player is in a powered Pulowski shelter (via the PulowskiProtocol DLL).
    bool IsPlayerInPoweredShelter() const;
    // True when the player is wearing a Power Armor frame with enough PA pieces equipped (helmet,
    // torso, both arms, both legs — any single missing piece is OK; missing 2+ is "not a full suit").
    // A full suit fully blocks rad gain AND storm HP damage.
    bool IsPlayerInPowerArmor() const;
    bool IsActorInPowerArmor(RE::Actor* actor) const;   // PA scan: requires a FULL suit — helmet + 6 distinct ArmorTypePower pieces
    RE::BGSKeyword* _armorTypePowerKeyword = nullptr;   // Fallout4.esm 0x4D8A1 — marks PA armor pieces
    // Gas-mask detection requires the FARO_GasMask keyword and the MOUTH biped slot. The generic
    // form works for the player OR any NPC; the no-arg form is the player.
    bool IsRecognizedGasMask(const RE::TESObjectARMO* armor) const;
    RE::TESObjectARMO* GetActorWornMask(RE::Actor* actor) const;
    RE::TESObjectARMO* GetWornMask() const;
    bool IsWearingGasMask() const;
    RE::BGSKeyword* _gasMaskKeyword = nullptr;  // RadiationOverhaul.esp 0x809 — FARO_GasMask
    int  _maskOverride = -1;            // debug: -1 auto (worn detection), 0 force off, 1 force on
    // Constraint: masks from other plugins are declared in the INI, never by editing their records.
    std::vector<RE::BGSKeyword*> _extraMaskKeywords;      // [Masks] ExtraKeywords
    std::vector<const RE::TESObjectARMO*> _extraMasks;    // [Masks] ExtraMasks
    bool _inferMasksFromRadResist = true;                 // [Masks] InferFromRadResist
    int _minMaskRadResist = 10;                           // [Masks] MinRadResist
    bool IsListedMask(const RE::TESObjectARMO* armor) const;
    bool HasRadResistance(const RE::TESObjectARMO* armor) const;

    // Auto-equip a gas mask while a rad weather is active (data-driven by the RadWeatherList FormList).
    // Equips on entering rad weather, removes (only what it auto-equipped) on leaving. Applies to the
    // player AND nearby NPCs. Generic helpers take an Actor*; the player wrappers delegate to them.
    RE::TESObjectARMO* FindActorInventoryMask(RE::Actor* actor) const;  // first owned recognized gas mask
    RE::TESObjectARMO* FindInventoryMask() const;                       // player
    void SetActorMaskEquipped(RE::Actor* actor, RE::TESObjectARMO* mask, bool equip);  // silent, via VM
    void SetMaskEquipped(RE::TESObjectARMO* mask, bool equip);          // player
    void EquipNearbyNpcMasks(bool equip);                              // mask/unmask high-process NPCs
    RE::BGSListForm* _radWeatherList = nullptr;                    // ESL 0x802 — weathers that count as "rad"
    int  _wasInRadWeather   = -1;       // -1 = just loaded (sync silently), 0 = no, 1 = yes
    std::uint32_t _lastRadWeatherFormID = 0;
    bool _autoMaskEquipped  = false;    // we auto-donned the PLAYER's mask (so we may auto-remove it)
    int  _npcMaskScanTick   = 0;        // throttle countdown for the mid-storm NPC re-scan
    std::unordered_set<std::uint32_t> _autoMaskedNpcs;  // NPC formIDs we auto-masked (to remove on clear)

    // Gas-mask FILTER system. The filter drains while the mask is worn; replacing it consumes a
    // carried filter item (ESL RadOverhaul_GasMaskFilter) and refills it; a depleted filter chokes
    // the player (rads) while in rad weather. Charge persists in the co-save.
    float _filterCharge      = 100.0f;  // 0..100; drains while masked
    int   _filterDurationSec = 300;     // [Filter] DurationSeconds — full->empty wear time
    int   _filterReplaceKey  = 0;       // [Filter] ReplaceKey VK code (0 = disabled)
    bool  _filterAutoReplace = false;   // [Filter] AutoReplace — auto-consume a filter at 0%
    float _filterChokeRads   = 8.0f;    // [Filter] ChokeRadsPerSec when masked w/ a dead filter in rad weather
    RE::TESBoundObject* _filterItem = nullptr;  // ESL 0x803 — the consumable filter
    bool  _filterReplaceWasDown = false;        // edge-detect for the replace hotkey
    int   _filterLowWarned   = 0;       // 0 none / 1 low(<=20) / 2 depleted — reset on refill
    int   _filterSparesCache = 0;       // cached count of the filter item (HUD "xN filters" text only)
    int   _filterCountTick   = 0;       // throttle countdown for refreshing _filterSparesCache
    void  LoadFilterIni();              // read the [Filter] + [Condition] INI sections
    void  LoadMaskConfig();             // read the [Masks] INI section (extra keywords / listed armors)
    void  ReplaceFilter(bool silent);   // consume one filter item -> charge 100
    int   FilterItemCount() const;      // count of the filter item in the player's inventory

    // Gas-mask CONDITION system. The mask takes damage when the player is struck; at 0 it is "broken"
    // and stops protecting (same choke as a dead filter) until repaired with a repair kit. Persists.
    float _maskCondition       = 100.0f;  // 0..100; drops on player hits while masked
    bool  _conditionEnabled    = true;    // [Condition] Enabled
    float _conditionHitDamage  = 6.0f;    // [Condition] DamagePerHit
    int   _conditionRepairKey  = 0;       // [Condition] RepairKey VK code (0 = disabled)
    bool  _conditionAutoRepair = false;   // [Condition] AutoRepair
    RE::TESBoundObject* _repairKitItem = nullptr;  // ESL 0x804 — repair kit consumable
    bool  _conditionRepairWasDown = false;         // edge-detect for the repair hotkey
    int   _conditionLowWarned  = 0;       // 0 none / 1 damaged(<=25) / 2 broken — reset on repair
    void  RepairMask(bool silent);       // consume one repair kit -> condition 100
    int   RepairKitCount() const;        // count of the repair kit in the player's inventory
    // CHOKING system (GMW-style): breathing contaminated air without working protection damages
    // HEALTH, not just rads. Applies while exposed (not sheltered/PA/powered-shelter) in rad
    // weather: full rate with no mask worn, a reduced rate when masked but the filter is dead or
    // the mask broken. INI [Choking]. Choking one-shot VO plays on a randomized throttle.
    bool  _chokingEnabled    = true;    // [Choking] Enabled
    float _chokeHealthPerSec = 1.5f;    // [Choking] HealthPerSecond (no mask)
    float _chokeFailedFactor = 0.5f;    // [Choking] FailedMaskFactor (dead filter / broken mask)
    bool  _chokeSounds       = true;    // [Choking] Sounds
    int   _chokeSoundTick    = 0;       // countdown to the next choking one-shot
    bool  _chokeNotified     = false;   // one notify per exposure episode
    int   _chokeTicks        = 0;       // consecutive choking ticks (grace: no VO/notify for ~2s
                                        // so the rad-weather auto-equip can mask up without a gasp)
    int   _chokeCalmTicks    = 0;       // consecutive safe ticks (hysteresis: cover-edge flicker
                                        // must stay safe ~10s before the notify/VO re-arm)
    int   _chokeDiagTick     = 0;       // throttle for the shelter-debug choke-gate diagnostic log

    // Put-on / take-off screen fade (pure engine, no Prisma, no quest). Tracks the worn-state
    // transition and arms a tick countdown for the fade-back half of the blink.
    int   _maskWasOn        = -1;       // -1 = just loaded (sync silently), 0 = off, 1 = on
    // Upward roof-fan raycast (sky occlusion) to detect overhead cover, incl. player-built roofs.
    // Throttled by Update (not per tick); result cached in _playerEnclosed, used by IsPlayerSheltered().
    bool ComputeEnclosedByStructure() const;
    bool _playerEnclosed    = false;   // cached result of the roof-fan raycast (storms only)
    int  _enclosedCheckTick = 0;       // countdown to the next raycast re-evaluation
    bool _debugShelter      = false;   // debug: live-Notify the roof-fan result + run the raycast even with no storm
    void ApplyStormExposure();         // per-tick rads + HP damage from the active storm
    void UpdateAmbientAudio();         // per-tick: storm bed (by current weather) + general exterior bed
    void EnsureBed(RE::BSSoundHandle& handle, const char*& curPath,
                   const char* wantPath, std::uint16_t atten);  // (re)loop/switch/stop a looping bed
    void UpdateStormFXVisibility();    // show/hide the storm dome + IMAD by player shelter state (no fog indoors)
    void ApplyStormPeakToNPCs();       // storm peak: heal exterior ghoul NPCs, protect all others
    bool IsGhoulActor(RE::Actor* a) const;  // true if the actor's race carries ActorTypeGhoul
    void MakeNearbyNPCsFlee();         // lethal storms: panic nearby non-ghoul NPCs (engine flee)
    int  _fleeTick = 0;                // throttle for re-issuing the flee order
    void CaptureWeatherReturnTarget(); // remember pre-Rad/pre-debug weather before taking sky control
    void UpdateWeatherHandoff();       // release the temporary return override after the blend is stably settled
    void ReleaseStormWeather();        // smoothly restore remembered weather, then return control to the region system
    void Notify(const char* message, bool warning) const;  // vanilla HUD corner message

    // Fire hazard refs spawned around the player during a Firestorm (themeId==1).
    // Resolved once in EnsureEslLoaded. Spawned at Active phase, cleared at Aftermath.
    RE::BGSHazard* _fireHazardForm = nullptr;       // DN121FireHazard (0x1A7C6A, Fallout4.esm)
    RE::BGSExplosion* _nukeExplosionForm = nullptr;
    std::vector<RE::ObjectRefHandle> _fireRefs;    // live handles so we can disable on cleanup
    bool SpawnDistantNuke();                        // one distant firestorm strike; true = detonated (false = transient failure)
    void SpawnFireRing();                           // place a ring of fire hazards around player
    void ClearFireRefs();                           // disable + release all live fire refs

    // STORM DOME (KORs CloudArena, bundled) — spawned NATIVELY, not via the RadStormFX Papyrus quest.
    // SetStage()/OnStageSet historically proved unreliable (returned false, the Papyrus event never fired
    // even with the stage table rebuilt), so the dome is placed the same way the fire ring is —
    // TESDataHandler::CreateReferenceAtLocation. The whole engine tick now runs on the verified game-main
    // thread (marshaled by Hooks.cpp); the rotating cloud animation is baked into the NIFs, so a placed
    // ref just plays it.
    RE::TESForm* _stormDomeForms[4] = {};          // FARO_StormDome01..04 (scriptless ACTI copies)
    RE::TESBoundObject* _stormDomeLight = nullptr; // FARO_StormDomeLight (TopLightNode) — the cloud shapes are
                                                   // effect-LIT, so this light IS the glowing-vortex look.
    RE::TESBoundObject* _stormDomeFlare = nullptr; // FARO_StormDomeFlare (LensFlareNode) — the bright eye/glare
    struct StormDomeLayer
    {
        RE::ObjectRefHandle handle;
        float zOffset;
        bool animStarted = false;
        bool manualSeeded = false;
    };
    std::vector<StormDomeLayer> _stormDomeRefs;
    RE::TESObjectCELL* _stormDomeCell = nullptr;
    RE::TESWorldSpace* _stormDomeWorld = nullptr;
    int _stormDomeRespawnRetryTicks = 0;
    bool _stormDomeUp = false;
    bool _stormDomeSeekAudit = false;
    bool _stormDomeManualDrive = false;
    float  _stormDomeBlend = 0.0f;
    double _stormDomeBlendSec = 0.0;
    bool _stormDomeAnimStarted = false;             // native-placed ACTI refs don't auto-start their
                                                    // NiController animation (KORs' STATIC records do), so
                                                    // UpdateDomeAnimation force-activates it once 3D loads
    void SpawnStormDome();
    void ClearStormDome();                          // disable + release the dome layers
    void UpdateStormDomeLifecycle();                // 4 Hz spawn/retry/worldspace ownership only
    void UpdateDomeAnimation();                     // start the cloud rotation/shader animation post-3D-load
    // Native camera shake (replaces the RadStormFX Papyrus shake, which never fired — SetStage
    // returned false every time). -1 = inactive for both counters.
    int  _shakeBurstStep  = -1;                     // >=0: decaying burst step in progress
    int  _shakeRumbleTick = -1;                     // >=0: ticks until the next sustained rumble
    bool NativeShakeCamera(float strength, float duration);  // true = shake actually submitted (false = invalid 3D/cell/runtime)
    void UpdateStormShake();                        // per-tick burst/rumble driver

    // Continuous camera dust for the Dust Storm. The weather referenceEffect
    // (FXCameraAttachExplosionDustVE) produced no visible motes in practice, so we re-apply the dust
    // ArtObject to the camera on a short overlapping cadence instead — the same ApplyArtObject path as
    // the wall-arrival burst, which DOES render — so there is always blowing dust around the player.
    int  _dustMoteTick = 0;
    void UpdateDustMotes();

    // Storm atmosphere IMADs — engine color grading per storm type (0x880-0x884).
    // Index matches kStormTypes[]: 0=Ash, 1=Fire, 2=Sulfur, 3=Acid Rain, 4=Dust.
    RE::TESImageSpaceModifier* _stormIMADs[kStormTypeCount] = {};
    int _activeStormImadIndex = -1;  // index of the currently-running storm IMAD, -1 = none
    RE::NiPointer<RE::ImageSpaceModifierInstanceForm> _stormImadInstance;
    float _stormImadTargetStrength = 0.0f;
    float _stormImadFadeRate = 0.0f;
    bool _stormImadStopWhenFaded = false;
    void ApplyStormIMAD(int typeIndex);
    void SetStormIMADTarget(int typeIndex, float strength, float fadeSeconds);
    void FadeOutStormIMAD(float fadeSeconds);
    void ClearStormIMAD();

    // Feral ghoul spawns — ghouls emerge from the radiation during lethal storms.
    // Leveled lists from Fallout4.esm (LCharFeralGhoul 0x0758AE, Glowing One 0x0D39EE).
    static constexpr float kDefaultStormGhoulSpawnChance = 0.40f;
    static constexpr std::uint32_t kStormGhoulPendingTimeoutTicks = 120;
    RE::TESBoundObject* _stormGhoulForm = nullptr;
    RE::TESBoundObject* _stormGlowingOneForm = nullptr;
    std::vector<RE::ObjectRefHandle> _stormGhoulRefs;
    std::vector<RE::ObjectRefHandle> _stormGhoulPendingRefs;
    std::vector<RE::ObjectRefHandle> _stormGhoulCleanupRefs;
    std::uint32_t _stormGhoulPendingTicks = 0;
    std::unordered_map<std::uint32_t, std::uint32_t> _stormGhoulUnresolvedTicksByHandle;
    bool _stormGhoulsEnabled = true;
    float _stormGhoulSpawnChance = kDefaultStormGhoulSpawnChance;
    void RetryPendingStormGhouls();
    void RetryStormGhoulCleanup();
    void RetireUnresolvedStormGhouls();
    void ResetStormGhoulStateForLoad();
    void ResetStormDomeForLoad();
    void SpawnStormGhouls();   // % chance to unleash a 5-20 strong persistent hunter pack (lethal storms)
    void ClearStormGhouls();   // disable + delete all spawned ghoul refs
    void CheckGhoulPackCleanup();  // despawn the pack once the player leaves the area
    RE::NiPoint3 _ghoulPackCenter{};   // where the current pack was unleashed
    std::uint32_t _ghoulPackWorldID = 0;   // worldspace the pack was unleashed in (0 = none/interior)
    bool _ghoulPackInterior = false;
    int  _ghoulCleanupTick = 0;

    // WorkshopNPC faction (Fallout4.esm 0x000337F3) — settlers in it are spared the storm peak.
    RE::TESFaction* _workshopNPCFaction = nullptr;
    // ActorTypeGhoul keyword (Fallout4.esm 0x00013794) — marks ghoul races so storms heal, not harm them.
    RE::BGSKeyword* _ghoulKeyword = nullptr;

    // Last stored game time (days) for sleep-healing on PostLoad.
    // -1.0f = not yet set (first session).
    float _lastGameTime = -1.0f;

    // Apply tissue healing from a named source, clamped to current pool, and persisted.
    void HealTissue(float amount, const char* source);

    // Bulk-heal reducible tissue for game-time spent sleeping/waiting/fast-travelling (the 4Hz hook
    // pauses during those, so the per-tick regression can't run). Used by the in-game time-jump path
    // and the save-load path.
    void ApplyRestTissueHeal(float gameHoursElapsed, float currentRads);

    // Keeps the bounded RadAway radiation-delta calculation. The window is armed exclusively by
    // NotifyVerifiedCureUse after a source-identifying item-use adapter has verified the item.
    void CheckActiveCures(float currentRads, float currentHP, bool deltaValid);
    static constexpr int kRadAwayWindowTicks = 80;  // ~20 s at the 4 Hz engine cadence
    int _radAwayWindowTicks = 0;

    // Radstorm warning: play a custom sound once when a radstorm weather rolls in.
    bool _radstormWarned = false;
    // Vanilla radstorms (Fallout4.esm) are scheduled by the region/weather system, not by us, and
    // can chain every few minutes. To "thin them out" we let one through, then suppress any further
    // vanilla radstorm that tries to roll in within kRadstormMinGapSec of play, by forcing the
    // Commonwealth clear weather. -1e30 so the very first radstorm of a session is always allowed.
    double _lastRadstormSec = -1.0e30;   // _playSeconds at which the last allowed radstorm rolled in
    // Cell-exit radstorm grace: stepping from a shelter (interior load-door OR a roofed exterior base)
    // out into the open frequently lands the player in an ALREADY fully-rendered radstorm, which the
    // start-of-transition veto (pct < veto max) below misses — the "storm the moment I leave Sanctuary"
    // case. We track the player's sheltered state per tick; on a sheltered->exposed transition we arm a
    // short grace during which any vanilla radstorm is force-cleared regardless of its transition pct.
    bool _prevSheltered      = true;     // IsPlayerSheltered() last tick (start sheltered: see comment)
    int  _cellExitGraceTicks = 0;        // >0: veto vanilla radstorms regardless of transition pct
    void CheckRadstormWarning();

    void EnsureEslLoaded();
    void UpdateStagePerk(int stageIndex);        // stageIndex -1 = clear
    void ApplyStatModification(int stageIndex);   // -1 = no radiation stage (clears penalties)
    void RemoveStatModification();
    // Apply a floor-guarded SPECIAL penalty: drives the AV down by up to `wantPenalty`, but NEVER
    // below 1 (FO4 corrupts the base when a modifier floors a SPECIAL, esp. with gear equipped).
    // Re-evaluated each tick so gear/chem changes are safe. `applied` tracks our current offset.
    void ApplyClampedSpecial(RE::PlayerCharacter* player, RE::ActorValueInfo* av,
                             int wantPenalty, std::int32_t& applied);

    // ---- CHRONIC TISSUE DAMAGE — real consequences -------------------------------------------
    // Tissue used to have NO effect of its own: it only inflated "effective severity" (rads + tissue),
    // which is mathematically identical to just carrying more rads. And PERMANENT scarring did
    // literally nothing at all — accumulated, saved, shown on the HUD, and intended for a future
    // verified chelation adapter, with zero gameplay consequence. These give both an identity.
    //
    // Because _tissueDamage can never heal BELOW _permanentTissueDamage, every penalty that scales
    // with tissue automatically FLOORS at the scar level — scarring is exactly "the damage you can
    // never heal", with no extra bookkeeping.
    void  ApplyTissueEffects();                    // per-tick: wasting penalties + symptoms
    void  UpdateTissueSymptoms(float severity01);  // cough / nausea / vomiting, layered by severity
    // Floor-guarded tracked penalty for a non-SPECIAL float AV (carry weight, heal rate, speed, AP).
    void  ApplyTrackedAV(RE::PlayerCharacter* player, RE::ActorValueInfo* av,
                         float wantPenalty, float& applied);

    float _appliedCarryPen = 0.0f;   // current carry-weight penalty we hold (muscle wasting)
    float _appliedHealPen  = 0.0f;   // current healRateMult penalty (failing regeneration)
    float _appliedSpeedPen = 0.0f;   // current speedMult penalty (sluggish, high tissue only)
    float _appliedApPen    = 0.0f;   // current actionPoints penalty (fatigue, high tissue only)
    int   _symptomTick     = 0;      // throttle to the next cough / nausea / vomiting fit
    int   _symptomWarned   = 0;      // 0 none / 1 nausea / 2 vomiting — one notify per escalation
};
