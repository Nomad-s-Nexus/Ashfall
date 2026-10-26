Scriptname RadSurvival

; =============================================================================
; Rad Survival Core - hunger, thirst, sleep debt, relief and staged penalties.
;
; Runtime: Hydra (PapyrusScriptExtender). Everything below is vanilla-safe:
;   - no ESP, no quest record, no VMAD, no GLOB/SPEL/MGEF required;
;   - state lives in Hydra:SaveMap (F4SE co-save), so it is save-persistent;
;   - lifecycle/event callbacks are global functions (Hydra:FunctionRefs);
;   - progression uses a repeating game-time timer (Hydra:Timer), which pauses
;     with the game and does not need a quest's OnTimerGameTime re-arm;
;   - HC_Manager and every HC_* form are read/written nowhere.
;
; Consumption model (the part native could not prove):
;   item equip arms a candidate -> player inventory count is re-checked on the
;   next Hydra item add/remove event and on the next tick. Relief is applied
;   exactly once, only when the count actually decreased. Dropping, storing,
;   selling, crafting and failed use therefore cannot grant relief.
;
; Penalties: stage is computed and persisted here, but applying actor-value
;   effects needs the Rad DLL bridge (Papyrus has no SetValue/ModValue; only
;   GetValue/GetBaseValue exist in the shipped base sources). ApplyStagePenalty
;   is the single hook the native bridge will call; it never mutates the player
;   on its own, so this script cannot leak spells or actor values.
;
; Bootstrapped from Data/Hydra/ScriptFunctions/RadSurvival.json.
; =============================================================================

Import Hydra:Events
Import Hydra:FunctionRefs
Import Hydra:Timer
Import Hydra:SaveMap
Import Hydra:MemMap
Import Hydra:IO:Json

; -----------------------------------------------------------------------------
; bootstrap
; -----------------------------------------------------------------------------

Function OnGameDataReady() Global
    if !Hydra:SaveMap.IsNamespaceValid(Namespace())
        Debug.Trace("[RadSurvival] ERROR: save-map namespace '" + Namespace() + "' is not registered; survival inactive.", 2)
        return
    endif
    Migrate()
    ArmEvents()
    ArmTimer()
    if IsDebug()
        Debug.Trace("[RadSurvival] ready; H/T/S=" + DebtText(), 0)
    endif
EndFunction

Function Migrate() Global
    int target = 1
    int have = GetInt("version", 0)
    if have >= target
        return
    endif
    ; v0 -> v1: no persisted state existed before this version.
    SetInt("version", target)
    SetFloat("hunger", 0.0)
    SetFloat("thirst", 0.0)
    SetFloat("sleep", 0.0)
    SetFloat("lastTime", Utility.GetCurrentGameTime())
    SetFloat("sleepStart", -1.0)
    SetFloat("waitStart", -1.0)
    SetInt("pendingForm", 0)
    SetInt("pendingCount", 0)
    SetFloat("pendingReal", -1.0)
    SetInt("stageHunger", 0)
    SetInt("stageThirst", 0)
    SetInt("stageSleep", 0)
    Debug.Trace("[RadSurvival] migrated state to v" + target as string)
EndFunction

Function ArmEvents() Global
    ; Always unregister before registering: Hydra restores persistent
    ; registrations from its own co-save, and a stale registration plus a new one
    ; would deliver every event twice. Unregister-then-register is idempotent.
    Hydra:Events.UnregisterForItemEquipUnequip(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnItemEquipped"))
    Hydra:Events.RegisterForItemEquipUnequip(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnItemEquipped"), None, True)

    Hydra:Events.UnregisterForItemAddRemove(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnItemAddRemove"))
    Hydra:Events.RegisterForItemAddRemove(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnItemAddRemove"), None, True)

    Hydra:Events.UnregisterForSleepStartStop(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnSleepStartStop"))
    Hydra:Events.RegisterForSleepStartStop(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnSleepStartStop"), None, True)

    Hydra:Events.UnregisterForWaitStartStop(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnWaitStartStop"))
    Hydra:Events.RegisterForWaitStartStop(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnWaitStartStop"), None, True)

    Hydra:Events.UnregisterForPostLoadGame(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnPostLoadGame"))
    Hydra:Events.RegisterForPostLoadGame(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnPostLoadGame"), None, True)

    if IsDebug()
        Debug.Trace("[RadSurvival] persistent event handlers armed", 0)
    endif
EndFunction

Function ArmTimer() Global
    RestartTimer()
    if IsDebug()
        Debug.Trace("[RadSurvival] progression timer started (" + TimerIntervalText() + " game-seconds)", 0)
    endif
EndFunction

Function RestartTimer() Global
    ; Hydra restarts an already-active timer with the new arguments, so this is
    ; safe to call after a wait/sleep time jump and never accumulates timers.
    Hydra:Timer.StartRepeatingGameTime(Hydra:FunctionRefs.CreateGlobalRef("RadSurvival", "OnTick"), TimerInterval())
EndFunction

float Function TimerInterval() Global
    float tick = CfgFloat("/tickSeconds", 900.0)
    if tick < 30.0
        tick = 30.0
    endif
    return tick
EndFunction

string Function TimerIntervalText() Global
    return TimerInterval() as string
EndFunction

; -----------------------------------------------------------------------------
; lifecycle
; -----------------------------------------------------------------------------

Function OnPostLoadGame(Hydra:Events:PostLoadGameParams akEvent) Global
    ; A save restore moves the clock; re-anchor so the loaded interval is not
    ; treated as elapsed play time.
    SetFloat("lastTime", Utility.GetCurrentGameTime())
    SetFloat("sleepStart", -1.0)
    SetFloat("waitStart", -1.0)
    SetInt("pendingForm", 0)
    SetFloat("pendingReal", -1.0)
    ArmEvents()
    ArmTimer()
    if IsDebug()
        Debug.Trace("[RadSurvival] post-load anchor; restored H/T/S=" + DebtText(), 0)
    endif
EndFunction

; -----------------------------------------------------------------------------
; consumption
; -----------------------------------------------------------------------------

Function OnItemEquipped(Hydra:Events:ItemEquipUnequipParams akEvent) Global
    if !IsActive()
        return
    endif
    if !akEvent.bEquipped
        return
    endif
    Actor player = Game.GetPlayer()
    if !player || akEvent.kTargetActor != player
        return
    endif
    if !akEvent.kItem
        return
    endif
    if !IsFoodOrDrink(akEvent.kItem)
        return
    endif

    ; Arm: remember the form and the inventory count at equip time. Relief is
    ; only granted after the count is observed to drop.
    SetInt("pendingForm", akEvent.kItem.GetFormID())
    SetInt("pendingCount", player.GetItemCount(akEvent.kItem))
    SetFloat("pendingReal", Utility.GetCurrentRealTime())
    Hydra:MemMap.SetValue(PendingNamespace(), "form", akEvent.kItem)
EndFunction

Function OnItemAddRemove(Hydra:Events:ItemAddRemoveParams akEvent) Global
    if !IsActive()
        return
    endif
    Actor player = Game.GetPlayer()
    if !player || !akEvent.kItem
        return
    endif
    if akEvent.kSourceRef != player && akEvent.kTargetRef != player
        return
    endif
    int pending = GetInt("pendingForm", 0)
    if pending == 0 || akEvent.kItem.GetFormID() != pending
        return
    endif
    ConfirmConsumption(akEvent.kItem)
EndFunction

Function ConfirmConsumption(Form akItem) Global
    Actor player = Game.GetPlayer()
    if !player || !akItem
        ClearPending()
        return
    endif
    int armed = GetInt("pendingCount", -1)
    int now = player.GetItemCount(akItem)
    if armed >= 0 && now < armed
        ApplyRelief(akItem, armed - now)
    endif
    ClearPending()
EndFunction

Function ClearPending() Global
    SetInt("pendingForm", 0)
    SetInt("pendingCount", 0)
    SetFloat("pendingReal", -1.0)
    Hydra:MemMap.RemoveKey(PendingNamespace(), "form")
EndFunction

Function OnTick() Global
    if !IsActive()
        ; Keep the clock anchored while suspended, so turning vanilla Survival
        ; off mid-save cannot be read as hours of skipped play and burst the pools.
        SetFloat("lastTime", Utility.GetCurrentGameTime())
        return
    endif
    AdvanceTo(Utility.GetCurrentGameTime())
    ExpirePending()
EndFunction

Function ExpirePending() Global
    int pending = GetInt("pendingForm", 0)
    if pending == 0
        return
    endif
    float armed = GetFloat("pendingReal", -1.0)
    if armed < 0.0
        ClearPending()
        return
    endif
    ; Give the container event time to arrive, then decide from the count.
    if Utility.GetCurrentRealTime() - armed < 2.0
        return
    endif
    ; The live form is kept in session memory: re-resolving an ESL/light-plugin
    ; form id through Game.GetForm is not reliable.
    Form item = Hydra:MemMap.GetValueOrDefault(PendingNamespace(), "form", None) as Form
    if item
        ConfirmConsumption(item)
    else
        ClearPending()
    endif
EndFunction

Function ApplyRelief(Form akItem, int aiCount) Global
    if aiCount <= 0
        return
    endif
    bool ate = akItem.HasKeyword(KeywordFood())
    bool drank = akItem.HasKeyword(KeywordNukaCola()) || akItem.HasKeyword(KeywordDrink()) || akItem.HasKeyword(KeywordWater())

    ; Alcohol hydrates far less; chems never feed the survival pools.
    float hydra = 1.0
    if akItem.HasKeyword(KeywordAlcohol())
        hydra = 0.25
    endif

    float portion = PortionFor(akItem)
    if ate
        AddDebt("hunger", -1.0 * portion * CfgFloat("/recovery/foodScale", 0.20), True)
    endif
    if drank
        AddDebt("thirst", -1.0 * portion * CfgFloat("/recovery/drinkScale", 0.25) * hydra, True)
    endif
    if IsDebug()
        Debug.Trace("[RadSurvival] consumed " + akItem + " x" + aiCount as string + " ate=" + ate as string + " drank=" + drank as string + " -> H/T=" + GetFloat("hunger", 0.0) as string + "/" + GetFloat("thirst", 0.0) as string, 0)
    endif
    UpdatePenaltyStages()
EndFunction

float Function PortionFor(Form akItem) Global
    float portion = 0.5
    int value = akItem.GetGoldValue()
    if value > 0
        portion = 0.5 + (value as float) / 100.0
    endif
    if portion > 1.0
        portion = 1.0
    endif
    return portion
EndFunction

; -----------------------------------------------------------------------------
; sleep and wait
; -----------------------------------------------------------------------------

Function OnSleepStartStop(Hydra:Events:SleepStartStopParams akEvent) Global
    if !IsActive()
        SetFloat("lastTime", Utility.GetCurrentGameTime())
        SetFloat("sleepStart", -1.0)
        return
    endif
    if akEvent.bStarted
        SetFloat("sleepStart", Utility.GetCurrentGameTime())
        if IsDebug()
            Debug.Trace("[RadSurvival] sleep started", 0)
        endif
        return
    endif

    float started = GetFloat("sleepStart", -1.0)
    float now = Utility.GetCurrentGameTime()
    if started < 0.0
        started = now
    endif
    float hours = (now - started) * 24.0
    if hours < 0.0
        hours = 0.0
    endif

    float recovery = hours * CfgFloat("/recovery/sleepPerHour", 0.12)
    if akEvent.bInterrupted
        recovery = recovery * CfgFloat("/recovery/interruptedScale", 0.5)
    endif
    AddDebt("sleep", -1.0 * recovery, True)
    SetFloat("sleepStart", -1.0)

    ; Sleep advances game time without ticks; accrue it once, then restart the
    ; repeating timer so the next interval is measured from the wake-up.
    AccrueElapsed(hours)
    RestartTimer()
    UpdatePenaltyStages()
    if IsDebug()
        Debug.Trace("[RadSurvival] sleep ended hours=" + hours as string + " interrupted=" + akEvent.bInterrupted as string + " recovery=" + recovery as string + " sleep=" + GetFloat("sleep", 0.0) as string, 0)
    endif
EndFunction

Function OnWaitStartStop(Hydra:Events:WaitStartStopParams akEvent) Global
    if !IsActive()
        SetFloat("lastTime", Utility.GetCurrentGameTime())
        SetFloat("waitStart", -1.0)
        return
    endif
    if akEvent.bStarted
        ; Record the real start: the stop event's fStartTime is zeroed when
        ; bStarted is false, so the delta has to come from the game clock.
        SetFloat("waitStart", Utility.GetCurrentGameTime())
        return
    endif
    ; Waiting is not rest: game time accrues, sleep debt does not recover.
    float started = GetFloat("waitStart", -1.0)
    float now = Utility.GetCurrentGameTime()
    SetFloat("waitStart", -1.0)
    float hours = 0.0
    if started >= 0.0
        hours = (now - started) * 24.0
    endif
    if hours < 0.0
        hours = 0.0
    endif
    AccrueElapsed(hours)
    RestartTimer()
    UpdatePenaltyStages()
    if IsDebug()
        Debug.Trace("[RadSurvival] wait ended hours=" + hours as string + " interrupted=" + akEvent.bInterrupted as string + " sleep=" + GetFloat("sleep", 0.0) as string, 0)
    endif
EndFunction

; -----------------------------------------------------------------------------
; progression
; -----------------------------------------------------------------------------

Function AdvanceTo(float nowDays) Global
    float last = GetFloat("lastTime", -1.0)
    if last < 0.0
        SetFloat("lastTime", nowDays)
        return
    endif
    float hours = (nowDays - last) * 24.0
    if hours <= 0.0
        SetFloat("lastTime", nowDays)
        return
    endif
    if hours > 240.0
        hours = 240.0
    endif
    SetFloat("lastTime", nowDays)
    Accrue(hours)
EndFunction

Function AccrueElapsed(float afHours) Global
    if afHours <= 0.0
        return
    endif
    if afHours > 240.0
        afHours = 240.0
    endif
    SetFloat("lastTime", Utility.GetCurrentGameTime())
    Accrue(afHours)
EndFunction

Function Accrue(float afHours) Global
    ; Rates are fractions of the 0..1 pool per game hour (same file the native
    ; needs runtime reads, so both layers agree when the bridge lands).
    float hunger = CfgFloat("/hourlyRates/hunger", 0.05) * afHours
    float thirst = CfgFloat("/hourlyRates/thirst", 0.10) * afHours
    float sleep = CfgFloat("/hourlyRates/sleepDebt", 0.025) * afHours
    AddDebt("hunger", hunger, False)
    AddDebt("thirst", thirst, False)
    AddDebt("sleep", sleep, False)
    UpdatePenaltyStages()
EndFunction

Function AddDebt(string asPool, float afDelta, bool abRelief) Global
    float value = GetFloat(asPool, 0.0) + afDelta
    if value < 0.0
        value = 0.0
    endif
    if value > 1.0
        value = 1.0
    endif
    SetFloat(asPool, value)
    if abRelief && IsDebug()
        Debug.Trace("[RadSurvival] relief " + asPool + " -> " + value as string, 0)
    endif
EndFunction

; -----------------------------------------------------------------------------
; penalties (stage policy only; application belongs to the Rad DLL bridge)
; -----------------------------------------------------------------------------

Function UpdatePenaltyStages() Global
    if !IsActive()
        return
    endif
    int hunger = StageFor(GetFloat("hunger", 0.0))
    int thirst = StageFor(GetFloat("thirst", 0.0))
    int sleep = StageFor(GetFloat("sleep", 0.0))

    int oldHunger = GetInt("stageHunger", 0)
    if hunger != oldHunger
        SetInt("stageHunger", hunger)
        ApplyStagePenalty("hunger", hunger)
        if hunger > oldHunger
            NotifyStage("hunger", hunger)
        endif
    endif

    int oldThirst = GetInt("stageThirst", 0)
    if thirst != oldThirst
        SetInt("stageThirst", thirst)
        ApplyStagePenalty("thirst", thirst)
        if thirst > oldThirst
            NotifyStage("thirst", thirst)
        endif
    endif

    int oldSleep = GetInt("stageSleep", 0)
    if sleep != oldSleep
        SetInt("stageSleep", sleep)
        ApplyStagePenalty("sleep", sleep)
        if sleep > oldSleep
            NotifyStage("sleep", sleep)
        endif
    endif
EndFunction

Function NotifyStage(string asPool, int aiStage) Global
    if !IsNotify()
        return
    endif
    string label = "HUNGER"
    if asPool == "thirst"
        label = "THIRST"
    elseif asPool == "sleep"
        label = "EXHAUSTION"
    endif
    string text = "Rad survival: " + label + " stage " + aiStage as string
    if aiStage >= 3
        text = text + " (critical)"
    endif
    Debug.Notification(text)
EndFunction

int Function StageFor(float afDebt) Global
    if afDebt < 0.34
        return 0
    endif
    if afDebt < 0.67
        return 1
    endif
    if afDebt < 0.90
        return 2
    endif
    return 3
EndFunction

Function ApplyStagePenalty(string asPool, int aiStage) Global
    ; Single integration point for the native bridge. The bridge calls
    ; RadSurvival.ApplyStagePenalty(pool, stage) only after the DLL has applied
    ; the matching removal, so this function intentionally mutates nothing.
    if IsDebug()
        Debug.Trace("[RadSurvival] stage change " + asPool + " -> " + aiStage as string, 0)
    endif
EndFunction

; -----------------------------------------------------------------------------
; gating / config
; -----------------------------------------------------------------------------

bool Function IsActive() Global
    if !IsEnabled()
        return False
    endif
    if IsSkipVanillaSurvival() && Hydra:Game.GetDifficulty() == Hydra:Game.GetDifficulty_Survival()
        return False
    endif
    return True
EndFunction

bool Function IsEnabled() Global
    return CfgFloat("/enabled", 1.0) > 0.5
EndFunction

bool Function IsSkipVanillaSurvival() Global
    return CfgFloat("/skipWhenVanillaSurvival", 1.0) > 0.5
EndFunction

bool Function IsDebug() Global
    return CfgFloat("/debug", 0.0) > 0.5
EndFunction

bool Function IsNotify() Global
    return CfgFloat("/notify", 1.0) > 0.5
EndFunction

float Function CfgFloat(string asJsonPointer, float afDefault) Global
    string path = ConfigPath()
    if !Hydra:IO:Json.IsCached_MemMap(path)
        if !Hydra:IO:Json.Cache_MemMap(path)
            return afDefault
        endif
    endif
    return Hydra:MemMap.GetValueOrDefault(path, asJsonPointer, afDefault) as float
EndFunction

string Function ConfigPath() Global
    return "Data/F4SE/Plugins/FalloutAnomaly/survival.json"
EndFunction

; -----------------------------------------------------------------------------
; state helpers
; -----------------------------------------------------------------------------

string Function Namespace() Global
    return "RadSurvival"
EndFunction

string Function PendingNamespace() Global
    return "RadSurvivalPending"
EndFunction

float Function GetFloat(string asKey, float afDefault) Global
    return Hydra:SaveMap.GetValueOrDefault(Namespace(), asKey, afDefault) as float
EndFunction

int Function GetInt(string asKey, int aiDefault) Global
    return Hydra:SaveMap.GetValueOrDefault(Namespace(), asKey, aiDefault) as int
EndFunction

Function SetFloat(string asKey, float afValue) Global
    Hydra:SaveMap.SetValue(Namespace(), asKey, afValue)
EndFunction

Function SetInt(string asKey, int aiValue) Global
    Hydra:SaveMap.SetValue(Namespace(), asKey, aiValue)
EndFunction

string Function DebtText() Global
    return GetFloat("hunger", 0.0) as string + "/" + GetFloat("thirst", 0.0) as string + "/" + GetFloat("sleep", 0.0) as string
EndFunction

; -----------------------------------------------------------------------------
; classification keywords (Fallout4.esm, verified against the load order)
; -----------------------------------------------------------------------------

Keyword Function KeywordFood() Global
    return Game.GetFormFromFile(0x00055ECC, "Fallout4.esm") as Keyword
EndFunction

Keyword Function KeywordNukaCola() Global
    return Game.GetFormFromFile(0x00249F30, "Fallout4.esm") as Keyword
EndFunction

Keyword Function KeywordDrink() Global
    return Game.GetFormFromFile(0x000F4AEC, "Fallout4.esm") as Keyword
EndFunction

Keyword Function KeywordWater() Global
    return Game.GetFormFromFile(0x000F4AED, "Fallout4.esm") as Keyword
EndFunction

Keyword Function KeywordAlcohol() Global
    return Game.GetFormFromFile(0x0010C416, "Fallout4.esm") as Keyword
EndFunction

bool Function IsFoodOrDrink(Form akItem) Global
    if !akItem
        return False
    endif
    if akItem.HasKeyword(KeywordFood())
        return True
    endif
    if akItem.HasKeyword(KeywordNukaCola())
        return True
    endif
    if akItem.HasKeyword(KeywordDrink())
        return True
    endif
    if akItem.HasKeyword(KeywordWater())
        return True
    endif
    return False
EndFunction
