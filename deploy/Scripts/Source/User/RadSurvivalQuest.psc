ScriptName RadSurvivalQuest extends Quest

; =============================================================================
; Rad survival core - vanilla Papyrus quest script.
;
; Event source is vanilla Papyrus only, so no script extender is required:
;   Actor.OnItemEquipped      - ingestible use (vanilla HC_Manager uses it too)
;   OnPlayerSleepStart/Stop   - real sleep, including abInterrupted
;   OnPlayerWaitStart/Stop    - waiting is explicitly not rest
;   OnTimerGameTime           - progression in game hours, pauses with the game
;
; HC_Manager and every HC_* form are read and written nowhere: this script
; listens to the same engine events and keeps its own pools. Vanilla Survival is
; left to own its own state, and the double-apply gate below keeps both systems
; from running at once by default.
;
; Consumption model (the part native could not prove):
;   equipping an ingestible arms a candidate -> the player's inventory count is
;   re-checked on a later tick. Relief is applied exactly once, only when the
;   count actually decreased. Dropping, storing, selling, crafting and failed
;   use therefore cannot grant relief.
;
; Shipped base sources have no OnItemAdded/OnItemRemoved event on
; ObjectReference/Actor, so a quest cannot register for them remotely; the tick
; check is the confirmation mechanism, not a fallback.
;
; State lives in this quest instance and is saved with the quest. The three
; GlobalVariable properties are mirrors so the native HUD can read the pools
; through TESGlobal without a Papyrus-to-native bridge.
; =============================================================================

; -----------------------------------------------------------------------------
; tuning - mirrors deploy/F4SE/Plugins/FalloutAnomaly/needs.json
; -----------------------------------------------------------------------------

float Property HourlyHunger = 0.05 Auto Const
float Property HourlyThirst = 0.10 Auto Const
float Property HourlySleepDebt = 0.025 Auto Const
float Property SleepRecoveryPerHour = 0.12 Auto Const
float Property InterruptedScale = 0.5 Auto Const
float Property FoodScale = 0.20 Auto Const
float Property DrinkScale = 0.25 Auto Const
float Property AlcoholHydrationScale = 0.25 Auto Const
float Property TickGameHours = 0.25 Auto Const
float Property MaxElapsedHours = 240.0 Auto Const
float Property PendingExpirySeconds = 2.0 Auto Const
int Property kTimerID = 10 Auto Const
int Property kVanillaSurvivalDifficulty = 6 Auto Const

bool Property Enabled = true Auto Const
bool Property SkipWhenVanillaSurvival = true Auto Const
bool Property ShowNotifications = true Auto Const

; -----------------------------------------------------------------------------
; pool mirrors for the native HUD (assigned when the GLOB records exist)
; -----------------------------------------------------------------------------

GlobalVariable Property RadSurvival_Hunger Auto
GlobalVariable Property RadSurvival_Thirst Auto
GlobalVariable Property RadSurvival_Sleep Auto

; -----------------------------------------------------------------------------
; state - saved with the quest instance
; -----------------------------------------------------------------------------

float HungerDebt = 0.0
float ThirstDebt = 0.0
float SleepDebt = 0.0
float LastGameTime = -1.0
float SleepStartTime = -1.0
float WaitStartTime = -1.0
Form PendingForm = None
int PendingCount = 0
float PendingRealTime = -1.0
int HungerStage = 0
int ThirstStage = 0
int SleepStage = 0

; -----------------------------------------------------------------------------
; lifecycle
; -----------------------------------------------------------------------------

Event OnInit()
    Actor player = Game.GetPlayer()
    if !player
        return
    endif

    LastGameTime = Utility.GetCurrentGameTime()
    ArmSleepWaitEvents()
    UnregisterForRemoteEvent(player, "OnItemEquipped")
    RegisterForRemoteEvent(player, "OnItemEquipped")
    EnsureGlobals()
    MirrorAll()
    RestartTimer()
    Debug.Trace("[RadSurvivalQuest] ready at game day " + LastGameTime)
EndEvent

Function ArmSleepWaitEvents()
    ; Unregister first: a script instance that survived a load can already be
    ; registered, and a duplicate registration delivers every sleep/wait event
    ; twice. This runs on init only; OnPlayerLoadGame does not re-arm it.
    UnregisterForPlayerSleep()
    UnregisterForPlayerWait()
    RegisterForPlayerSleep()
    RegisterForPlayerWait()
EndFunction

Event Actor.OnPlayerLoadGame(Actor akSender)
    Actor player = Game.GetPlayer()
    if !player
        return
    endif

    ; A save restore moves the clock; re-anchor so the loaded interval is not
    ; read as elapsed play time.
    LastGameTime = Utility.GetCurrentGameTime()
    SleepStartTime = -1.0
    WaitStartTime = -1.0
    ClearPending()

    ; Only the remote item event is re-registered here.
    UnregisterForRemoteEvent(player, "OnItemEquipped")
    RegisterForRemoteEvent(player, "OnItemEquipped")
    RestartTimer()
    EnsureGlobals()
    MirrorAll()
    Debug.Trace("[RadSurvivalQuest] post-load anchor H/T/S=" + HungerDebt + "/" + ThirstDebt + "/" + SleepDebt)
EndEvent

; -----------------------------------------------------------------------------
; timer - StartTimerGameTime is one-shot, so the handler re-arms it
; -----------------------------------------------------------------------------

Event OnTimerGameTime(int aiTimerID)
    if aiTimerID != kTimerID
        return
    endif

    if IsActive()
        AdvanceTo(Utility.GetCurrentGameTime())
        ExpirePending()
    else
        ; Suspended: keep the clock anchored so turning vanilla Survival off
        ; mid-save cannot be read as hours of skipped play.
        LastGameTime = Utility.GetCurrentGameTime()
    endif

    StartTimerGameTime(TickGameHours, kTimerID)
EndEvent

Function RestartTimer()
    CancelTimerGameTime(kTimerID)
    StartTimerGameTime(TickGameHours, kTimerID)
EndFunction

; -----------------------------------------------------------------------------
; consumption
; -----------------------------------------------------------------------------

Event Actor.OnItemEquipped(Actor akSender, Form akBaseObject, ObjectReference akReference)
    if !IsActive()
        return
    endif
    Actor player = Game.GetPlayer()
    if !player || akSender != player
        return
    endif
    if !akBaseObject || !IsFoodOrDrink(akBaseObject)
        return
    endif

    ; Arm: remember the live form and the count at equip time. The live form is
    ; held as a script variable so the confirm step never re-resolves a form id.
    PendingForm = akBaseObject
    PendingCount = player.GetItemCount(akBaseObject)
    PendingRealTime = Utility.GetCurrentRealTime()
EndEvent

Function ExpirePending()
    if !PendingForm
        return
    endif
    if PendingRealTime < 0.0
        ClearPending()
        return
    endif
    ; Give the engine a moment to finish the use, then decide from the count.
    if Utility.GetCurrentRealTime() - PendingRealTime < PendingExpirySeconds
        return
    endif
    ConfirmConsumption(PendingForm)
EndFunction

Function ConfirmConsumption(Form akItem)
    Actor player = Game.GetPlayer()
    if !player || !akItem
        ClearPending()
        return
    endif
    int armed = PendingCount
    int now = player.GetItemCount(akItem)
    if armed >= 0 && now < armed
        ApplyRelief(akItem, armed - now)
    endif
    ClearPending()
EndFunction

Function ClearPending()
    PendingForm = None
    PendingCount = 0
    PendingRealTime = -1.0
EndFunction

Function ApplyRelief(Form akItem, int aiCount)
    if aiCount <= 0
        return
    endif

    bool ate = akItem.HasKeyword(KeywordFood())
    bool drank = akItem.HasKeyword(KeywordNukaCola()) || akItem.HasKeyword(KeywordDrink()) || akItem.HasKeyword(KeywordWater())

    ; Alcohol hydrates far less; chems never feed the survival pools.
    float hydration = 1.0
    if akItem.HasKeyword(KeywordAlcohol())
        hydration = AlcoholHydrationScale
    endif

    float portion = PortionFor(akItem)
    if ate
        AddDebt("hunger", -1.0 * portion * FoodScale)
    endif
    if drank
        AddDebt("thirst", -1.0 * portion * DrinkScale * hydration)
    endif
    Debug.Trace("[RadSurvivalQuest] consumed " + akItem + " x" + aiCount + " ate=" + ate + " drank=" + drank)
EndFunction

float Function PortionFor(Form akItem)
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

Event OnPlayerSleepStart(float afSleepStartTime, float afDesiredSleepEndTime, ObjectReference akBed)
    SleepStartTime = Utility.GetCurrentGameTime()
EndEvent

Event OnPlayerSleepStop(bool abInterrupted, ObjectReference akBed)
    float now = Utility.GetCurrentGameTime()
    float started = SleepStartTime
    SleepStartTime = -1.0

    if !IsActive()
        LastGameTime = now
        RestartTimer()
        return
    endif

    float hours = 0.0
    if started >= 0.0
        hours = (now - started) * 24.0
    endif
    if hours < 0.0
        hours = 0.0
    endif

    float recovery = hours * SleepRecoveryPerHour
    if abInterrupted
        recovery = recovery * InterruptedScale
    endif
    AddDebt("sleep", -1.0 * recovery)

    ; Sleep advances game time without ticks: accrue it once, then restart the
    ; timer so the next interval is measured from the wake-up.
    AccrueElapsed(hours)
    RestartTimer()
    Debug.Trace("[RadSurvivalQuest] sleep ended hours=" + hours + " interrupted=" + abInterrupted + " recovery=" + recovery)
EndEvent

Event OnPlayerWaitStart(float afWaitStartTime, float afDesiredWaitEndTime)
    ; The stop event carries no start time, so record it from the game clock.
    WaitStartTime = Utility.GetCurrentGameTime()
EndEvent

Event OnPlayerWaitStop(bool abInterrupted)
    float now = Utility.GetCurrentGameTime()
    float started = WaitStartTime
    WaitStartTime = -1.0

    if !IsActive()
        LastGameTime = now
        RestartTimer()
        return
    endif

    float hours = 0.0
    if started >= 0.0
        hours = (now - started) * 24.0
    endif
    if hours < 0.0
        hours = 0.0
    endif

    ; Waiting is not rest: game time accrues, sleep debt does not recover.
    AccrueElapsed(hours)
    RestartTimer()
    Debug.Trace("[RadSurvivalQuest] wait ended hours=" + hours + " sleep=" + SleepDebt)
EndEvent

; -----------------------------------------------------------------------------
; progression
; -----------------------------------------------------------------------------

Function AdvanceTo(float nowDays)
    if LastGameTime < 0.0
        LastGameTime = nowDays
        return
    endif
    float hours = (nowDays - LastGameTime) * 24.0
    if hours <= 0.0
        LastGameTime = nowDays
        return
    endif
    if hours > MaxElapsedHours
        hours = MaxElapsedHours
    endif
    LastGameTime = nowDays
    Accrue(hours)
EndFunction

Function AccrueElapsed(float afHours)
    if afHours <= 0.0
        return
    endif
    if afHours > MaxElapsedHours
        afHours = MaxElapsedHours
    endif
    LastGameTime = Utility.GetCurrentGameTime()
    Accrue(afHours)
EndFunction

Function Accrue(float afHours)
    AddDebt("hunger", HourlyHunger * afHours)
    AddDebt("thirst", HourlyThirst * afHours)
    AddDebt("sleep", HourlySleepDebt * afHours)
EndFunction

Function AddDebt(string asPool, float afDelta)
    if asPool == "hunger"
        HungerDebt = Clamp01(HungerDebt + afDelta)
        MirrorToGlobal(RadSurvival_Hunger, HungerDebt)
    elseif asPool == "thirst"
        ThirstDebt = Clamp01(ThirstDebt + afDelta)
        MirrorToGlobal(RadSurvival_Thirst, ThirstDebt)
    elseif asPool == "sleep"
        SleepDebt = Clamp01(SleepDebt + afDelta)
        MirrorToGlobal(RadSurvival_Sleep, SleepDebt)
    endif
    UpdatePenaltyStages()
EndFunction

float Function Clamp01(float afValue)
    if afValue < 0.0
        return 0.0
    elseif afValue > 1.0
        return 1.0
    endif
    return afValue
EndFunction

; -----------------------------------------------------------------------------
; GLOB mirrors - the native HUD reads these through TESGlobal
; -----------------------------------------------------------------------------

Function EnsureGlobals()
    ; The ESP binds these properties once the record set is authored. Until that
    ; binding exists (or if a VMAD value is missing) resolve the records by the
    ; documented editor contract so the HUD mirror still works.
    if !RadSurvival_Hunger
        RadSurvival_Hunger = Game.GetFormFromFile(0x01000801, "RadSurvival.esp") as GlobalVariable
    endif
    if !RadSurvival_Thirst
        RadSurvival_Thirst = Game.GetFormFromFile(0x01000802, "RadSurvival.esp") as GlobalVariable
    endif
    if !RadSurvival_Sleep
        RadSurvival_Sleep = Game.GetFormFromFile(0x01000803, "RadSurvival.esp") as GlobalVariable
    endif
EndFunction

Function MirrorAll()
    MirrorToGlobal(RadSurvival_Hunger, HungerDebt)
    MirrorToGlobal(RadSurvival_Thirst, ThirstDebt)
    MirrorToGlobal(RadSurvival_Sleep, SleepDebt)
EndFunction

Function MirrorToGlobal(GlobalVariable akTarget, float afValue)
    if akTarget
        akTarget.SetValue(afValue)
    endif
EndFunction

; -----------------------------------------------------------------------------
; penalties - stage policy only, inert until the SPEL records exist
; -----------------------------------------------------------------------------

Function UpdatePenaltyStages()
    if !IsActive()
        return
    endif

    int hunger = StageFor(HungerDebt)
    if hunger != HungerStage
        int previous = HungerStage
        HungerStage = hunger
        ApplyStagePenalty("hunger", hunger)
        if hunger > previous
            NotifyStage("hunger", hunger)
        endif
    endif

    int thirst = StageFor(ThirstDebt)
    if thirst != ThirstStage
        int previousThirst = ThirstStage
        ThirstStage = thirst
        ApplyStagePenalty("thirst", thirst)
        if thirst > previousThirst
            NotifyStage("thirst", thirst)
        endif
    endif

    int sleep = StageFor(SleepDebt)
    if sleep != SleepStage
        int previousSleep = SleepStage
        SleepStage = sleep
        ApplyStagePenalty("sleep", sleep)
        if sleep > previousSleep
            NotifyStage("sleep", sleep)
        endif
    endif
EndFunction

int Function StageFor(float afDebt)
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

Function ApplyStagePenalty(string asPool, int aiStage)
    ; Single hook for staged penalties. Stays inert until the SPEL records
    ; exist; nothing here mutates actor values or adds spells on its own.
EndFunction

Function NotifyStage(string asPool, int aiStage)
    if !ShowNotifications
        return
    endif
    string label = "HUNGER"
    if asPool == "thirst"
        label = "THIRST"
    elseif asPool == "sleep"
        label = "EXHAUSTION"
    endif
    string text = "Rad survival: " + label + " stage " + aiStage
    if aiStage >= 3
        text = text + " (critical)"
    endif
    Debug.Notification(text)
EndFunction

; -----------------------------------------------------------------------------
; gating
; -----------------------------------------------------------------------------

bool Function IsActive()
    if !Enabled
        return false
    endif
    ; 6 is the vanilla Survival difficulty; HC_ManagerScript gates on it too.
    if SkipWhenVanillaSurvival && Game.GetDifficulty() == kVanillaSurvivalDifficulty
        return false
    endif
    return true
EndFunction

; -----------------------------------------------------------------------------
; classification keywords (Fallout4.esm)
; -----------------------------------------------------------------------------

Keyword Function KeywordFood()
    return Game.GetFormFromFile(0x00055ECC, "Fallout4.esm") as Keyword
EndFunction

Keyword Function KeywordNukaCola()
    return Game.GetFormFromFile(0x00249F30, "Fallout4.esm") as Keyword
EndFunction

Keyword Function KeywordDrink()
    return Game.GetFormFromFile(0x000F4AEC, "Fallout4.esm") as Keyword
EndFunction

Keyword Function KeywordWater()
    return Game.GetFormFromFile(0x000F4AED, "Fallout4.esm") as Keyword
EndFunction

Keyword Function KeywordAlcohol()
    return Game.GetFormFromFile(0x0010C416, "Fallout4.esm") as Keyword
EndFunction

bool Function IsFoodOrDrink(Form akItem)
    if !akItem
        return false
    endif
    if akItem.HasKeyword(KeywordFood())
        return true
    endif
    if akItem.HasKeyword(KeywordNukaCola())
        return true
    endif
    if akItem.HasKeyword(KeywordDrink())
        return true
    endif
    if akItem.HasKeyword(KeywordWater())
        return true
    endif
    return false
EndFunction
