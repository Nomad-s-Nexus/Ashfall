#include "RadEngine_Internal.h"

using namespace RadInternal;

// Debug menu: forced storms/weather, rads/tissue tools, SPECIAL repair, diagnostics.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


// ---------------------------------------------------------------------------
// DEBUG controls (hotkeys + PrismaUI debug menu).
// ---------------------------------------------------------------------------

// Silent dome-only lead used by DEBUG-forced storms only (10 s at 4 Hz). Real gameplay storms keep
// the full [Storm] DomeLeadMinutes. Long enough to see the dome come up alone before the siren.
constexpr int kDebugLeadTicks = 10 * 4;

void RadiationEngine::DebugForceStorm(int typeIndex)
{
    Log()->warn("[DIAG] DebugForceStorm entry typeIndex={}.", typeIndex);
    if (typeIndex < 0 || typeIndex >= kStormTypeTableSize) return;
    // Fully tear down any in-progress storm first (fire ring, ghoul pack, IMAD, shake/dome) so a
    // force-storm mid-firestorm can't leak the previous storm's FX into the new one.
    ClearFireRefs();
    ClearStormGhouls();
    ClearStormIMAD();
    TriggerStormFX(false);
    ClearStormDome();
    _stormFXVisible = false;
    _stormNukeFired = false;
    ReleaseStormWeather();                  // drop any in-progress storm's forced sky
    _stormPhase      = StormPhase::Idle;
    // Direct phase reset bypasses EnterStormPhase, so disarm the shake counters here too — otherwise a
    // storm force-started mid-shake leaves them armed and the next Active phase drives phantom rumble.
    _shakeBurstStep  = -1;
    _shakeRumbleTick = -1;
    _shakeBurstRetries = 0;
    _firestormAwaitingNuke = false;
    _nukeAttempts    = 0;
    _nukeWaitTicks   = 0;
    _debugSkipToActive = false;
    _activeStormType = -1;
    _forcedStormType   = typeIndex;
    _forceStormNow     = true;              // next UpdateStorm Idle tick begins it
    // Debug storms cut the silent dome-only lead to kDebugLeadTicks. The full DomeLeadMinutes lead
    // is correct for real gameplay but made every test a ~4.5 min wait (lead + warning), which is
    // why the siren at the end of the lead went unobserved for weeks. Warning/Active/Aftermath below
    // still run at real timings — only the lead is shortened.
    _forcedLeadTicks   = kDebugLeadTicks;
    // A debug-forced storm now runs the REAL timings — the full warning, so the onset actually plays
    // out the way it does in-game (dome fades in overhead -> siren goes up -> the storm hits) instead
    // of a compressed 10 s window that made the sequence impossible to judge. It used to force a short
    // warning purely so it was quick to test; that made the debug storm a poor stand-in for the thing
    // it was meant to be testing.
    Log()->warn("[DEBUG] Force storm '{}' — lead {} s (debug-shortened) + warning {} s = {} s to impact.",
        kStormTypes[typeIndex].name, kDebugLeadTicks / 4,
        kStormTypes[typeIndex].warningTicks / 4,
        (kDebugLeadTicks + kStormTypes[typeIndex].warningTicks) / 4);
}


void RadiationEngine::DebugEndStorm()
{
    ClearFireRefs();
    ClearStormGhouls();
    ClearStormIMAD();
    TriggerStormFX(false);  // stop shake when a storm is force-ended
    ClearStormDome();       // despawn the native cloud dome
    ForceClearAndSilence(); // silence all storm audio + force Commonwealth clear sky
    _stormPhase       = StormPhase::Idle;
    _shakeBurstStep   = -1;   // disarm shake (direct reset bypasses EnterStormPhase teardown)
    _shakeRumbleTick  = -1;
    _shakeBurstRetries = 0;
    _firestormAwaitingNuke = false;
    _nukeAttempts     = 0;
    _nukeWaitTicks    = 0;
    _debugSkipToActive = false;
    _activeStormType  = -1;
    _stormTicksLeft   = 0;
    _stormRollInTicks = 0;     // cancel any pending firestorm roll-in
    _stormFXVisible   = false;
    _stormNukeFired   = false;
    _forceStormNow    = false; // don't let a queued force re-fire next tick
    _forcedStormType  = -1;
    ScheduleNextStorm();
    RadHud::GetSingleton().PushStorm(static_cast<int>(StormPhase::Idle), 0, 0.0f, true, false);
    Log()->info("[DEBUG] Storm force-ended; weather released, phase reset to Idle.");
    Notify("[DEBUG] Storm ended.", false);
}


void RadiationEngine::DebugAddTissue(float tissue, float permanent)
{
    _tissueDamage          = std::min(kTissueMax, _tissueDamage + tissue);
    _permanentTissueDamage = std::min(_tissueDamage, _permanentTissueDamage + permanent);
    if (_tissueGlobal)          _tissueGlobal->value = _tissueDamage;
    if (_permanentTissueGlobal) _permanentTissueGlobal->value = _permanentTissueDamage;
    char msg[64];
    std::snprintf(msg, sizeof(msg), "[DEBUG] tissue %.0f (perm %.0f)", _tissueDamage, _permanentTissueDamage);
    Notify(msg, false);
}


void RadiationEngine::DebugAddRads(float rads)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* av = RE::ActorValue::GetSingleton();
    if (player && av && av->rads)
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->rads, rads);
}


void RadiationEngine::DebugClearAll()
{
    _tissueDamage = 0.0f;
    _permanentTissueDamage = 0.0f;
    if (_tissueGlobal)          _tissueGlobal->value = _tissueDamage;
    if (_permanentTissueGlobal) _permanentTissueGlobal->value = _permanentTissueDamage;
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* av = RE::ActorValue::GetSingleton();
    if (player && av && av->rads)
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->rads, -100000.0f);
    Notify("[DEBUG] Cleared rads + tissue.", false);
}


void RadiationEngine::HandleDebugCommand(const char* action)
{
    // Runs on the Ultralight/JS thread — DO NOT touch the engine here. Just enqueue; the work
    // is dispatched on the main thread in Update() (DrainDebugCommands). Engine calls made off
    // the main thread (e.g. End Storm's weather/IMAD/VM ops) crash the game.
    if (!action || !action[0]) return;
    // DIAG (flushed): first C++ contact from the JS thread. If this logs but [dispatch] never does,
    // the main-thread Update loop stopped before draining (UI/render-side hang, not storm code).
    Log()->warn("[DIAG] HandleDebugCommand recv (JS thread): '{}'", action);
    std::lock_guard<std::mutex> lock(_debugCmdMutex);
    _pendingDebugCmds.emplace_back(action);
}

void RadiationEngine::PushGhoulSettingsToHud()
{
    RadHud::GetSingleton().PushGhoulSettings(_stormGhoulsEnabled, _stormGhoulSpawnChance);
}


void RadiationEngine::DrainDebugCommands()
{
    std::vector<std::string> cmds;
    {
        std::lock_guard<std::mutex> lock(_debugCmdMutex);
        if (_prevRads < 0.0f)
        {
            _maskOverride = -1;
            _debugShelter = false;
            _filterSparesCache = 0;
            _filterCountTick = 0;
            _enclosedCheckTick = 0;
            _weatherReturnTarget = nullptr;
            _weatherReturnCaptured = false;
            _releaseWeatherOverrideWhenSettled = false;
            _weatherHandoffSettleTicks = 0;
            _pendingDebugCmds.clear();
            return;
        }
        if (_pendingDebugCmds.empty()) return;
        cmds.swap(_pendingDebugCmds);
    }
    for (const auto& c : cmds)
        DispatchDebugCommand(c);
}


void RadiationEngine::DispatchDebugCommand(const std::string& action)
{
    if (action.empty()) return;
    std::string a(action);
    // DIAG (flushed): main-thread dispatch reached. Pinpoints the exact command that hangs.
    Log()->warn("[DIAG] DispatchDebugCommand (main thread): '{}'", a);

    // Commands: "storm:0".."storm:4", "endstorm", "tissue:150", "perm:40",
    auto numAfter = [&](const char* prefix) -> float {
        return static_cast<float>(std::atof(a.c_str() + std::strlen(prefix)));
    };

    if (a.rfind("storm:", 0) == 0)        DebugForceStorm(static_cast<int>(numAfter("storm:")));
    else if (a == "endstorm")             DebugEndStorm();
    else if (a == "skipwarn" || a == "skiptoactive")
    {
        // Fast-forward to the Active phase for testing. The old skipwarn only collapsed the CURRENT
        // phase to one tick, so from Approach it still made the tester sit through the whole Warning.
        // Set the fast-forward flag: EnterStormPhase then collapses each remaining Approach/Warning
        // transition to a single tick, so Active is reached in ~2 ticks — WITHOUT duplicating any
        // transition logic (every real transition and the Active onset still run).
        if (_stormPhase == StormPhase::Approach || _stormPhase == StormPhase::Warning)
        {
            _debugSkipToActive = true;
            _stormTicksLeft    = 1;   // end the current phase next tick; the flag collapses the rest
            Notify("[DEBUG] Fast-forwarding to Active...", false);
        }
        else
        {
            Notify("[DEBUG] skipwarn/skiptoactive only works during Approach or Warning.", false);
        }
    }
    else if (a.rfind("tissue:", 0) == 0)  DebugAddTissue(numAfter("tissue:"), 0.0f);
    else if (a.rfind("perm:", 0) == 0)    DebugAddTissue(numAfter("perm:"), numAfter("perm:"));
    else if (a.rfind("rads:", 0) == 0)    DebugAddRads(numAfter("rads:"));
    else if (a == "clear")                DebugClearAll();
    else if (a == "heal")                 DebugHeal();
    else if (a == "weather:clear")        DebugClearWeather();
    else if (a.rfind("forceweather:", 0) == 0) DebugForceWeatherByForm(a.substr(std::strlen("forceweather:")));
    else if (a.rfind("weather:", 0) == 0) DebugPreviewWeather(static_cast<int>(numAfter("weather:")));
    else if (a == "ghouls:on")
    {
        _stormGhoulsEnabled = true;
        PushGhoulSettingsToHud();
        Notify("[DEBUG] Storm ghoul packs enabled for future storms.", false);
    }
    else if (a == "ghouls:off")
    {
        _stormGhoulsEnabled = false;
        ClearStormGhouls();
        PushGhoulSettingsToHud();
        Notify("[DEBUG] Storm ghoul packs disabled and the current pack cleared.", false);
    }
    else if (a == "ghouls:clear")
    {
        ClearStormGhouls();
        Notify("[DEBUG] Current storm ghoul pack cleared.", false);
    }
    else if (a == "ghouls:reset")
    {
        _stormGhoulsEnabled = true;
        _stormGhoulSpawnChance = kDefaultStormGhoulSpawnChance;
        PushGhoulSettingsToHud();
        Notify("[DEBUG] Storm ghoul packs reset: enabled, 40% chance.", false);
    }
    else if (a.rfind("ghouls:chance:", 0) == 0)
    {
        const float percent = std::clamp(numAfter("ghouls:chance:"), 0.0f, 100.0f);
        _stormGhoulSpawnChance = percent / 100.0f;
        PushGhoulSettingsToHud();
        char m[96];
        std::snprintf(m, sizeof(m), "[DEBUG] Storm ghoul spawn chance = %.0f%%.", percent);
        Notify(m, false);
    }
    else if (a == "give:chelation")       DebugGiveItem(0x860, kEslName, 1);
    else if (a == "give:radaway")         DebugGiveItem(0x023742, "Fallout4.esm", 5);
    else if (a == "give:radx")            DebugGiveItem(0x024057, "Fallout4.esm", 5);
    else if (a == "fixstats")             DebugFixSpecial();
    else if (a == "shelter")
    {
        _debugShelter = !_debugShelter;
        _enclosedCheckTick = 0;  // run the roof-fan right away so the first readout is immediate
        char m[64];
        std::snprintf(m, sizeof(m), "[DEBUG] Shelter readout %s.", _debugShelter ? "ON" : "OFF");
        Notify(m, false);
    }
    else if (a == "mask:scan")            DebugScanWornMask();
    else if (a == "mask:on")              { _maskOverride = 1;  Notify("[DEBUG] Gas-mask overlay forced ON.",  false); }
    else if (a == "mask:off")             { _maskOverride = 0;  Notify("[DEBUG] Gas-mask overlay forced OFF.", false); }
    else if (a == "mask:auto")            { _maskOverride = -1; Notify("[DEBUG] Gas-mask overlay = auto (worn detection).", false); }
    else if (a == "give:filter")          DebugGiveItem(kGasMaskFilterID, kEslName, 5);
    else if (a == "filter:full")          { _filterCharge = 100.0f; _filterLowWarned = 0; Notify("[DEBUG] Filter charge = 100%.", false); }
    else if (a == "filter:low")           { _filterCharge = 12.0f;  _filterLowWarned = 0; Notify("[DEBUG] Filter charge = 12%.", false); }
    else if (a == "filter:dead")          { _filterCharge = 0.0f;   _filterLowWarned = 0; Notify("[DEBUG] Filter charge = 0%.", false); }
    else if (a == "filter:replace")       ReplaceFilter(false);
    else if (a == "give:repairkit")       DebugGiveItem(kGasMaskRepairKitID, kEslName, 5);
    else if (a == "cond:full")            { _maskCondition = 100.0f; _conditionLowWarned = 0; Notify("[DEBUG] Mask condition = 100%.", false); }
    else if (a == "cond:low")             { _maskCondition = 18.0f; _conditionLowWarned = 0; Notify("[DEBUG] Mask condition = 18%.", false); }
    else if (a == "cond:broken")          { _maskCondition = 0.0f; _conditionLowWarned = 0; Notify("[DEBUG] Mask condition = 0%.", false); }
    else if (a == "cond:repair")          RepairMask(false);
    else if (a == "hud:open")             RadHud::GetSingleton().OpenHudLayoutEditor();
    else if (a.rfind("hudlayout:", 0) == 0) RadHud::GetSingleton().ApplyLocalHudLayout(a.substr(std::strlen("hudlayout:")));
    else if (a == "close")                RadHud::GetSingleton().SetDebugMenu(false);
}


void RadiationEngine::DebugFixSpecial()
{
    // Fix Stats button. Each press:
    //   1. Fully removes THIS mod's own radiation SPECIAL penalty and suspends it re-applying.
    //   2. Claws back up to kFixStatsReliefPerPress (5) points of any REMAINING negative on each
    //      SPECIAL — foreign debuffs we don't own (hunger/thirst/fatigue/disease/other mods). Repeated
    //      presses chip a heavy debuff down 5 at a time.
    //   3. Names the active magic effects still lowering a SPECIAL so the player knows what to fix
    //      (eat / drink / sleep / cure) when the cause is NOT this mod.
    // Only ever raises a stat toward its base — never past it, never touches the permanent base.
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* av     = RE::ActorValue::GetSingleton();
    if (!player || !av) return;
    auto logger = Log();

    // ---- Step 1: remove THIS mod's radiation penalty and stop it re-applying this stage ----
    const bool hadOurPenalty = (_appliedEndPenalty != 0 || _appliedStrPenalty != 0);
    RemoveStatModification();   // adds our END/STR penalty back to 0 and clears the trackers
    // Suspend re-application at the current radiation stage until the player's rad level changes.
    // _currentStageLevel is the rad THRESHOLD (0,100,...); convert to the stage INDEX the apply path
    // compares against. Below 100 rads -> -1 (penalty want is already 0 there).
    _specialSuspendStage = (_currentStageLevel >= 100)
        ? static_cast<int32_t>(_currentStageLevel / 100) - 1 : -1;
    logger->info("[FixStats] Our radiation penalty {}; re-application suspended at stage index {}.",
        hadOurPenalty ? "removed" : "was already clear", _specialSuspendStage);

    // ---- Step 2: relieve up to 5 points of any remaining (foreign) deficit on each SPECIAL ----
    struct AV { RE::ActorValueInfo* info; const char* name; };
    const AV specials[] = {
        { av->strength, "Strength" }, { av->perception, "Perception" }, { av->endurance, "Endurance" },
        { av->charisma, "Charisma" }, { av->intelligence, "Intelligence" }, { av->agility, "Agility" },
        { av->luck, "Luck" },
    };

    int  totalRelieved = 0;   // visible points restored this press
    bool stillFloored  = false;   // a debuffed stat is still pinned below its base after this press
    for (const auto& s : specials)
    {
        if (!s.info) continue;
        const float perm = player->GetPermanentActorValue(*s.info);
        if (perm < 1.0f) continue;   // bogus base read — never guess, never damage the stat
        const float before = player->GetActorValue(*s.info);
        if (before >= perm - 0.01f) continue;   // not debuffed — preserve any gear/chem bonus, skip

        // Reduce the negative modifier by 5. This works even when the stat is FLOORED at 1 by a
        // negative larger than the visible gap (GetActorValue clamps, but the underlying temporary
        // modifier is not clamped, so +5 genuinely shrinks it — heavy debuffs just take a few presses
        // to surface). Then cap: if we overshot the base, trim back so we never exceed it or strip gear.
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, *s.info,
            static_cast<float>(kFixStatsReliefPerPress));
        const float after0 = player->GetActorValue(*s.info);
        if (after0 > perm)
            player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, *s.info, perm - after0);
        const float after  = player->GetActorValue(*s.info);
        const int   gained = static_cast<int>(after - before + 0.5f);
        if (gained > 0) totalRelieved += gained;
        if (after < perm - 0.5f) stillFloored = true;   // still debuffed after this press
        logger->info("[FixStats] {}: base {:.0f}, {:.0f} -> {:.0f} (+{}){}.",
            s.name, perm, before, after, gained, (after < perm - 0.5f) ? " [still down]" : "");
    }

    // ---- Step 3: diagnose foreign sources — list active magic effects lowering any SPECIAL ----
    // Our own radiation penalty is a direct ModActorValue (not an ActiveEffect), so it never appears
    // here — this list is purely external causes (needs/disease/gear/other mods).
    std::vector<std::string> causes;
    auto addCause = [&](const char* nm)
    {
        if (!nm || !*nm || causes.size() >= 6) return;
        for (const auto& c : causes) if (c == nm) return;   // de-dup
        causes.emplace_back(nm);
    };
    if (auto* list = static_cast<RE::MagicTarget*>(player)->GetActiveEffectList())
    {
        for (auto& handle : list->data)
        {
            RE::ActiveEffect* ae = handle.get();
            if (!ae || !ae->effect) continue;
            RE::EffectSetting* es = ae->effect->effectSetting;
            if (!es || !es->data.primaryAV) continue;
            RE::ActorValueInfo* pav = es->data.primaryAV;
            const bool isSpecial =
                pav == av->strength || pav == av->perception || pav == av->endurance ||
                pav == av->charisma || pav == av->intelligence || pav == av->agility || pav == av->luck;
            if (!isSpecial) continue;
            // Lowers the stat if flagged detrimental, or its magnitude is negative.
            const bool lowers =
                es->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kDetrimental) ||
                ae->magnitude < 0.0f;
            if (!lowers) continue;
            // Prefer the ABILITY/spell name ("Starving", "Incapacitated", "Severely Dehydrated") over
            // the generic base-effect name ("Reduce Strength") — the ability name tells the player what
            // need to address (eat / sleep / drink). Fall back to the base-effect name if unnamed.
            const char* nm = ae->spell ? ae->spell->GetFullName() : nullptr;
            if (!nm || !*nm) nm = es->GetFullName();
            addCause(nm);
        }
    }

    // ---- Build the report (full detail also goes to the log). Shown in a PrismaUI popup so long
    // multi-line reports are not truncated by the vanilla HUD corner; falls back to the corner if
    // PrismaUI is unavailable. ----
    std::vector<std::string> report;
    if (hadOurPenalty)
        report.emplace_back("Radiation sickness penalty cleared. It won't return until your radiation level changes.");

    if (totalRelieved > 0)
    {
        char m[96];
        std::snprintf(m, sizeof(m), "Restored +%d to your SPECIAL stats.%s", totalRelieved,
            stillFloored ? " Press again to recover more." : "");
        report.emplace_back(m);
    }
    else if (stillFloored)
    {
        report.emplace_back("Chipping away at a heavy SPECIAL debuff - keep pressing to recover it.");
    }

    if (!causes.empty())
    {
        std::string msg = "Lowered by: ";
        for (size_t i = 0; i < causes.size(); ++i) { if (i) msg += ", "; msg += causes[i]; }
        report.push_back(msg);
        report.emplace_back("These are NOT from this mod - eat, drink, sleep, or treat illness to clear them.");
        logger->warn("[FixStats] Foreign SPECIAL debuffs active -> {}", msg);
    }
    else if (totalRelieved == 0 && !stillFloored && !hadOurPenalty)
    {
        report.emplace_back("Your SPECIAL stats look healthy - nothing to fix.");
    }

    if (report.empty())
        report.emplace_back("Your SPECIAL stats look healthy - nothing to fix.");

    // Popup (full text, no corner truncation); fall back to corner messages if PrismaUI is absent.
    if (!RadHud::GetSingleton().ShowSpecialInfo(report))
        for (const auto& line : report) Notify(line.c_str(), false);
}


void RadiationEngine::DebugGiveItem(uint32_t formID, const char* plugin, int count)
{
    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!dh || !player) return;
    RE::TESForm* form = dh->LookupForm(formID, plugin);
    // Our debug items are all AlchemyItem (ALCH) -> TESBoundObject (single-inheritance), so a
    // static_cast is valid. (TESForm::As<TESBoundObject> isn't linkable in this CommonLibF4.) Guard the
    // form type first: a regenerated/mis-resolved FormID landing on a non-bound record would otherwise
    // hand AddObjectToContainer a bad pointer and crash.
    const bool bound = form && (form->GetFormType() == RE::ENUM_FORM_ID::kALCH ||
                                form->GetFormType() == RE::ENUM_FORM_ID::kMISC ||
                                form->GetFormType() == RE::ENUM_FORM_ID::kARMO ||
                                form->GetFormType() == RE::ENUM_FORM_ID::kWEAP ||
                                form->GetFormType() == RE::ENUM_FORM_ID::kAMMO);
    auto* obj = bound ? static_cast<RE::TESBoundObject*>(form) : nullptr;
    if (!obj) { Notify("[DEBUG] item not found.", false); return; }
    player->AddObjectToContainer(obj, nullptr, count, nullptr, RE::ITEM_REMOVE_REASON::kNone);
    Notify("[DEBUG] item(s) added to inventory.", false);
}


// Force ANY weather in the load order by "Plugin.esp|hexObjectID" (the debug menu's WEATHER tab).
// FO4 strips EditorIDs at runtime, so the menu carries an authored name list and sends plugin+objectID;
// we resolve it here via LookupForm (which also survives load-order changes, unlike a raw FormID).
void RadiationEngine::DebugForceWeatherByForm(const std::string& spec)
{
    auto dlog = Log();
    const auto bar = spec.find('|');
    if (bar == std::string::npos) { dlog->warn("[DIAG] forceweather: bad spec '{}'.", spec); return; }
    const std::string plugin = spec.substr(0, bar);
    const std::uint32_t id = static_cast<std::uint32_t>(std::strtoul(spec.c_str() + bar + 1, nullptr, 16));

    auto* dh  = RE::TESDataHandler::GetSingleton();
    auto* sky = RE::Sky::GetSingleton();
    if (!dh || !sky) { dlog->warn("[DIAG] forceweather: no data handler / sky."); return; }

    RE::TESForm* f = dh->LookupForm(id, plugin.c_str());
    if (!f || f->GetFormType() != RE::ENUM_FORM_ID::kWTHR)
    {
        dlog->warn("[DIAG] forceweather: '{}' 0x{:06X} not found or not a weather.", plugin, id);
        Notify("[DEBUG] weather not found (mod not loaded?).", false);
        return;
    }
    // SNAP, don't ramp. This is an inspector: BeginSmoothWeather sets pct=0 and lets the engine ramp over
    // the target's Trans-Delta (tens of seconds), so clicking through the list showed a blend still
    // dominated by the PREVIOUS weather ("Clear looks like Overcast") and precipitation never started
    // (rain only renders at full pct). ForceWeather(override=true) applies it immediately at full pct.
    // The smooth path stays for real storms, where the gradual roll-in is the point.
    CaptureWeatherReturnTarget();
    _releaseWeatherOverrideWhenSettled = false;
    _weatherHandoffSettleTicks = 0;
    _weatherHandoffWaitTicks = 0;
    _debugWeatherHold = true;   // hold the vetoes down or CheckRadstormWarning reverts this
    if (auto* sky2 = RE::Sky::GetSingleton())
        sky2->ForceWeather(static_cast<RE::TESWeather*>(f), true);
    _weatherForced = true;
    dlog->warn("[DIAG] forceweather: {} 0x{:06X} applied INSTANTLY (veto hold ON).", plugin, id);
    Notify("[DEBUG] weather forced (instant, held).", false);
}


void RadiationEngine::DebugPreviewWeather(int idx)
{
    auto dlog = Log();
    dlog->warn("[DIAG] DebugPreviewWeather idx={} (count={}).", idx, kStormTypeCount);
    if (idx < 0 || idx >= kStormTypeCount) { dlog->warn("[DIAG]   rejected (out of range)."); return; }
    RE::TESWeather* w = _stormWeathers[idx];
    auto* sky = RE::Sky::GetSingleton();
    dlog->warn("[DIAG]   weather={} sky={}.", w ? "ok" : "NULL", sky ? "ok" : "NULL");
    if (w && sky)
    {
        // Preview via the ENGINE'S OWN smooth transition (vanilla path) so the debug menu shows the TRUE
        // in-game behaviour — a gradual roll-in, no jarring snap, no purple flash.
        RE::TESWeather* prev = sky->currentWeather;
        dlog->warn("[DIAG]   BeginSmoothWeather...");
        CaptureWeatherReturnTarget();
        _debugWeatherHold = true;     // stand the radstorm/dust vetoes down (they'd revert this to clear)
        BeginSmoothWeather(w);        // visual only — does NOT run the storm gameplay
        dlog->warn("[DIAG]   transition armed.");
        if (idx == kDustStormIndex && prev && prev != w)
        {
            _dustBuildupActive = true;   // watch for arrival -> wall-arrival dust burst
            _dustRollElapsed   = 0;
            dlog->warn("[DIAG]   Dust preview armed (engine-native smooth transition, from 0x{:06X}).",
                prev->GetFormID());
        }
        Notify("[DEBUG] weather transition started (visual).", false);
    }
}


void RadiationEngine::DebugScanWornMask()
{
    // Walk the player's worn biped and log every occupied ARMO slot. The mask-worn detection keys
    // ONLY on biped slot 44 (kMouth), so this reveals whether the player's gas mask actually fills
    // that slot — the decisive check when the mask features read as dead but the mask "looks" worn.
    auto dlog = Log();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->biped) { dlog->warn("[MaskScan] no player/biped."); Notify("[DEBUG] mask:scan - no biped.", false); return; }
    auto& biped = *player->biped;
    const int mouth = static_cast<int>(RE::BIPED_OBJECT::kMouth);
    dlog->warn("[MaskScan] ==== worn biped scan (mask detection wants slot {} = kMouth) ====", mouth);
    int count = 0;
    for (std::size_t i = 0; i < std::size(biped.object); ++i)
    {
        RE::TESForm* form = biped.object[i].parent.object;
        if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO) continue;
        ++count;
        const bool fillsMouth = static_cast<RE::TESObjectARMO*>(form)->FillsBipedSlot(mouth);
        dlog->warn("[MaskScan]   slot[{}] ARMO 0x{:08X}{}", i, form->GetFormID(),
                   fillsMouth ? "  <-- fills kMouth (WOULD be detected as mask)" : "");
    }
    dlog->warn("[MaskScan] ==== {} worn armor(s); IsWearingGasMask()={} ====", count, IsWearingGasMask());
    Notify("[DEBUG] mask:scan - see FalloutAnomaly.log ([MaskScan]).", false);
}


void RadiationEngine::DebugClearWeather()
{
    // ForceWeather, which is interior-safe — NOT Sky::ResetWeather, which CTDs in interiors.)
    _debugWeatherHold = false;   // release the veto hold — the region system takes back over
    ForceClearAndSilence();
    Notify("[DEBUG] weather released (vetoes back on).", false);
}


void RadiationEngine::DebugHeal()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* av = RE::ActorValue::GetSingleton();
    if (!player || !av) return;

    // Rads cap MAX health, so healing HP alone does nothing while rads are high. Clear rads
    // AND the tissue pool first, then restore health to full.
    if (av->rads)
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->rads, -100000.0f);
    _tissueDamage = 0.0f;
    _permanentTissueDamage = 0.0f;
    if (_tissueGlobal)          _tissueGlobal->value = 0.0f;
    if (_permanentTissueGlobal) _permanentTissueGlobal->value = 0.0f;
    if (av->health)
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->health, 100000.0f);
    Notify("[DEBUG] healed (rads + tissue cleared, HP restored).", false);
}
