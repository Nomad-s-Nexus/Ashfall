ScriptName RadSurvivalQuest extends Quest

; Constraint: HC_Manager and every HC_* form are read and written nowhere here.
; Constraint: relief needs a confirmed inventory count drop, never the equip event.

; tuning - the only source of the pool rates; the three GLOBs are the HUD contract

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
float Property StageHysteresis = 0.04 Auto Const
int Property kTimerID = 10 Auto Const
int Property kVanillaSurvivalDifficulty = 6 Auto Const

bool Property Enabled = true Auto Const
bool Property SkipWhenVanillaSurvival = true Auto Const
bool Property ShowNotifications = true Auto Const

; penalties are staged ability spells; clear this to ship them off without a rebuild
bool Property PenaltiesEnabled = true Auto Const
string Property PenaltyPlugin = "RadSurvival.esp" Auto Const
; 0x80D / 0x810 / 0x813: first-stage ability of each pool, stages run +0..+2
int Property HungerPenaltySpellBase = 2061 Auto Const
int Property ThirstPenaltySpellBase = 2064 Auto Const
int Property SleepPenaltySpellBase = 2067 Auto Const

; pool mirrors for the native HUD (assigned when the GLOB records exist)

GlobalVariable Property RadSurvival_Hunger Auto
GlobalVariable Property RadSurvival_Thirst Auto
GlobalVariable Property RadSurvival_Sleep Auto

; Invariant: the disease layer is a second script on this quest, never a second event owner.
RadSurvivalDiseases Property Diseases Auto

; state - saved with the quest instance

float HungerDebt = 0.0
float ThirstDebt = 0.0
float SleepDebt = 0.0
float LastGameTime = -1.0
float SleepStartTime = -1.0
float WaitStartTime = -1.0
Form PendingForm = None
int PendingCount = 0
float PendingRealTime = -1.0
Form PendingForm2 = None
int PendingCount2 = 0
float PendingRealTime2 = -1.0
int HungerStage = 0
int ThirstStage = 0
int SleepStage = 0

; spell actually on the player per pool, so a stage change can replace it exactly once
Spell AppliedHungerSpell = None
Spell AppliedThirstSpell = None
Spell AppliedSleepSpell = None

; invariant: a reconcile must not resolve the same stage twice
int CachedHungerStage = -1
Spell CachedHungerSpell = None
int CachedThirstStage = -1
Spell CachedThirstSpell = None
int CachedSleepStage = -1
Spell CachedSleepSpell = None

; lifecycle

Event OnInit()
    Actor player = Game.GetPlayer()
    if !player
        return
    endif

    LastGameTime = Utility.GetCurrentGameTime()
    ArmSleepWaitEvents()
    ArmPlayerEvents(player)
    MirrorAll()
    ResyncPenalties()
    ResyncDiseases()
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

Function ArmPlayerEvents(Actor player)
    UnregisterForRemoteEvent(player, "OnItemEquipped")
    RegisterForRemoteEvent(player, "OnItemEquipped")
    UnregisterForRemoteEvent(player, "OnPlayerLoadGame")
    RegisterForRemoteEvent(player, "OnPlayerLoadGame")
    UnRegisterForHitEvent(player)
    RegisterForHitEvent(player)
    UnregisterForRemoteEvent(player, "OnPlayerSwimming")
    RegisterForRemoteEvent(player, "OnPlayerSwimming")
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

    ; Only the remote and hit events are re-armed here; sleep/wait survive a load.
    ArmPlayerEvents(player)
    RestartTimer()
    MirrorAll()
    ResyncPenalties()
    ResyncDiseases()
    Debug.Trace("[RadSurvivalQuest] post-load anchor H/T/S=" + HungerDebt + "/" + ThirstDebt + "/" + SleepDebt)
EndEvent

; timer - StartTimerGameTime is one-shot, so the handler re-arms it

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

    ; Invariant: reconcile every tick, in both branches (a flipped switch must not wait for a stage).
    UpdatePenaltyStages()
    ReconcileDiseases()

    StartTimerGameTime(TickGameHours, kTimerID)
EndEvent

Function RestartTimer()
    CancelTimerGameTime(kTimerID)
    StartTimerGameTime(TickGameHours, kTimerID)
EndFunction

; consumption

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

    ArmPending(akBaseObject, player.GetItemCount(akBaseObject))
EndEvent

Event OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Projectile akProjectile, bool abPowerAttack, bool abSneakAttack, bool abBashAttack, bool abHitBlocked, string apMaterial)
    Actor player = Game.GetPlayer()
    if !player
        return
    endif
    ; Constraint: the registration is consumed by each delivery, so it must be re-armed here.
    RegisterForHitEvent(player)
    if Diseases == None
        return
    endif
    if Diseases.QualifiesAsWound(akTarget, akAggressor, akSource, player)
        Diseases.ExposureHit(IsActive())
    endif
EndEvent

Event Actor.OnPlayerSwimming(Actor akSender)
    if Diseases == None
        return
    endif
    Diseases.ExposureSwim(IsActive())
EndEvent

Function ArmPending(Form akItem, int aiCount)
    ; Constraint: a second item used before the tick must not overwrite the first.
    if PendingForm && PendingForm2
        ConfirmConsumption(PendingForm, PendingCount)
        PendingForm = PendingForm2
        PendingCount = PendingCount2
        PendingRealTime = PendingRealTime2
        PendingForm2 = None
        PendingCount2 = 0
        PendingRealTime2 = -1.0
    endif
    if !PendingForm
        PendingForm = akItem
        PendingCount = aiCount
        PendingRealTime = Utility.GetCurrentRealTime()
    else
        PendingForm2 = akItem
        PendingCount2 = aiCount
        PendingRealTime2 = Utility.GetCurrentRealTime()
    endif
EndFunction

Function ExpirePending()
    if PendingForm && PendingRealTime >= 0.0 && Utility.GetCurrentRealTime() - PendingRealTime >= PendingExpirySeconds
        Form first = PendingForm
        int firstCount = PendingCount
        PendingForm = PendingForm2
        PendingCount = PendingCount2
        PendingRealTime = PendingRealTime2
        PendingForm2 = None
        PendingCount2 = 0
        PendingRealTime2 = -1.0
        ConfirmConsumption(first, firstCount)
    endif
    if PendingForm2 && PendingRealTime2 >= 0.0 && Utility.GetCurrentRealTime() - PendingRealTime2 >= PendingExpirySeconds
        Form second = PendingForm2
        int secondCount = PendingCount2
        ClearPending()
        ConfirmConsumption(second, secondCount)
    endif
EndFunction

Function ConfirmConsumption(Form akItem, int aiArmed)
    Actor player = Game.GetPlayer()
    if !player || !akItem
        return
    endif
    int now = player.GetItemCount(akItem)
    if aiArmed >= 0 && now < aiArmed
        ApplyRelief(akItem, aiArmed - now)
    endif
EndFunction

Function ClearPending()
    PendingForm = None
    PendingCount = 0
    PendingRealTime = -1.0
    PendingForm2 = None
    PendingCount2 = 0
    PendingRealTime2 = -1.0
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

; sleep and wait

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
    RestCureDiseases(hours, abInterrupted)
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

; progression

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
    TickDiseases(afHours)
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

; GLOB mirrors - the native HUD reads these through TESGlobal


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
; penalties - staged ability spells authored in RadSurvival.esp
;
; One spell per pool per stage (1-3), 1:1 with the stage the pools compute. A stage
; change removes the spell that is on the player and adds the new one, so penalties
; never stack and relief takes the spell off. Stage 0 means no spell at all.
; -----------------------------------------------------------------------------

Function UpdatePenaltyStages()
    if !IsActive()
        ; Suspended (disabled, or vanilla Survival owns the player): no Rad penalty may
        ; survive the gate. The pools keep their values and resume with the layer.
        ClearAllPenalties()
        return
    endif

    ; Invariant: reconcile all three pools every call; ApplyStagePenalty is idempotent per pool.
    ; Constraint: the trackers below gate only the notifications, never the reconcile.
    int hunger = StageForHeld(HungerDebt, HungerStage)
    bool hungerRose = hunger > HungerStage
    HungerStage = hunger
    ApplyStagePenalty("hunger", hunger)
    if hungerRose
        NotifyStage("hunger", hunger)
    endif

    int thirst = StageForHeld(ThirstDebt, ThirstStage)
    bool thirstRose = thirst > ThirstStage
    ThirstStage = thirst
    ApplyStagePenalty("thirst", thirst)
    if thirstRose
        NotifyStage("thirst", thirst)
    endif

    int sleep = StageForHeld(SleepDebt, SleepStage)
    bool sleepRose = sleep > SleepStage
    SleepStage = sleep
    ApplyStagePenalty("sleep", sleep)
    if sleepRose
        NotifyStage("sleep", sleep)
    endif
EndFunction

int Function StageForHeld(float afDebt, int aiCurrent)
    ; Constraint: a held stage only steps down after clearing its threshold by the band.
    if aiCurrent >= 3 && afDebt >= 0.90 - StageHysteresis
        return 3
    endif
    if aiCurrent >= 2 && afDebt >= 0.67 - StageHysteresis
        return 2
    endif
    if aiCurrent >= 1 && afDebt >= 0.34 - StageHysteresis
        return 1
    endif
    return StageFor(afDebt)
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
    Actor player = Game.GetPlayer()
    if !player
        return
    endif

    Spell wanted = None
    if PenaltiesEnabled && aiStage > 0
        wanted = PenaltySpellFor(asPool, aiStage)
    endif

    Spell applied = AppliedPenalty(asPool)
    ; Constraint: a spell lost to another mod's dispel must be re-applied on the next reconcile.
    if applied == wanted && (!wanted || player.HasSpell(wanted))
        return
    endif
    if applied
        player.RemoveSpell(applied)
    endif
    if wanted
        player.AddSpell(wanted, false)
    endif
    SetAppliedPenalty(asPool, wanted)
    Debug.Trace("[RadSurvivalQuest] penalty " + asPool + " stage " + aiStage + " -> " + wanted)
EndFunction

Function ClearAllPenalties()
    ApplyStagePenalty("hunger", 0)
    ApplyStagePenalty("thirst", 0)
    ApplyStagePenalty("sleep", 0)
EndFunction

Function ResyncPenalties()
    ; Recount the stages from the pools, then let ApplyStagePenalty reconcile the spells.
    ; Applied*Spell is saved state, so an unchanged stage does nothing and a retuned
    ; table is picked up on the next load instead of doubling a spell.
    ; Constraint: the cache must be dropped on every load so a retuned table is picked up.
    ClearPenaltyCache()
    HungerStage = StageFor(HungerDebt)
    ThirstStage = StageFor(ThirstDebt)
    SleepStage = StageFor(SleepDebt)
    ApplyStagePenalty("hunger", HungerStage)
    ApplyStagePenalty("thirst", ThirstStage)
    ApplyStagePenalty("sleep", SleepStage)
EndFunction

Spell Function PenaltySpellFor(string asPool, int aiStage)
    if aiStage < 1
        return None
    endif
    if aiStage > 3
        aiStage = 3
    endif

    int first = 0
    int cachedStage = -1
    Spell cachedSpell = None
    if asPool == "hunger"
        first = HungerPenaltySpellBase
        cachedStage = CachedHungerStage
        cachedSpell = CachedHungerSpell
    elseif asPool == "thirst"
        first = ThirstPenaltySpellBase
        cachedStage = CachedThirstStage
        cachedSpell = CachedThirstSpell
    elseif asPool == "sleep"
        first = SleepPenaltySpellBase
        cachedStage = CachedSleepStage
        cachedSpell = CachedSleepSpell
    else
        return None
    endif

    ; Invariant: a repeated stage must not hit the form table again.
    if cachedStage == aiStage
        if cachedSpell
            return cachedSpell
        endif
    endif

    ; Constraint: a failed resolve must not be cached, or a missing plugin never retries.
    Spell resolved = Game.GetFormFromFile(first + aiStage - 1, PenaltyPlugin) as Spell
    if !resolved
        Debug.Trace("[RadSurvivalQuest] missing penalty spell " + (first + aiStage - 1) + " in " + PenaltyPlugin)
        return None
    endif
    if asPool == "hunger"
        CachedHungerStage = aiStage
        CachedHungerSpell = resolved
    elseif asPool == "thirst"
        CachedThirstStage = aiStage
        CachedThirstSpell = resolved
    elseif asPool == "sleep"
        CachedSleepStage = aiStage
        CachedSleepSpell = resolved
    endif
    return resolved
EndFunction

Function ClearPenaltyCache()
    CachedHungerStage = -1
    CachedHungerSpell = None
    CachedThirstStage = -1
    CachedThirstSpell = None
    CachedSleepStage = -1
    CachedSleepSpell = None
EndFunction

Spell Function AppliedPenalty(string asPool)
    if asPool == "hunger"
        return AppliedHungerSpell
    elseif asPool == "thirst"
        return AppliedThirstSpell
    elseif asPool == "sleep"
        return AppliedSleepSpell
    endif
    return None
EndFunction

Function SetAppliedPenalty(string asPool, Spell akSpell)
    if asPool == "hunger"
        AppliedHungerSpell = akSpell
    elseif asPool == "thirst"
        AppliedThirstSpell = akSpell
    elseif asPool == "sleep"
        AppliedSleepSpell = akSpell
    endif
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

; invariant: RadSurvivalDiseases owns the state, this script owns the events

Function TickDiseases(float afHours)
    if Diseases == None
        return
    endif
    Diseases.Tick(afHours, IsActive(), StageFor(HungerDebt), StageFor(ThirstDebt), StageFor(SleepDebt))
    ; Invariant: sky exposure rides the same interval, through the same AddRisk rule.
    Diseases.ExposureWeather(afHours, IsActive())
EndFunction

Function RestCureDiseases(float afHours, bool abInterrupted)
    if Diseases == None
        return
    endif
    Diseases.TryRestCure(afHours, abInterrupted, IsActive())
EndFunction

Function ReconcileDiseases()
    if Diseases == None
        return
    endif
    Diseases.Reconcile(IsActive())
EndFunction

Function ResyncDiseases()
    if Diseases == None
        return
    endif
    Diseases.Resync(IsActive())
EndFunction

; gating

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

; classification keywords (Fallout4.esm)

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
