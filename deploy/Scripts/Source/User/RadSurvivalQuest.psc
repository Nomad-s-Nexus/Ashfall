ScriptName RadSurvivalQuest extends Quest
; Constraint: HC_Manager and every HC_* form are read and written nowhere here.
; Constraint: relief is confirmed by the engine's own item-removal event, never by an inventory count.

; tuning - the only source of the pool rates; the three GLOBs are the HUD contract

float Property HourlyHunger = 0.05 Auto
float Property HourlyThirst = 0.10 Auto
float Property HourlySleepDebt = 0.025 Auto
float Property SleepRecoveryPerHour = 0.12 Auto
float Property InterruptedScale = 0.5 Auto
float Property FoodScale = 0.20 Auto
float Property DrinkScale = 0.25 Auto
float Property AlcoholHydrationScale = 0.25 Auto
float Property TickGameHours = 0.25 Auto
float Property MaxElapsedHours = 240.0 Auto
float Property PairWindowSeconds = 2.0 Auto
float Property StageHysteresis = 0.04 Auto
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
; Invariant: a use is only credited when the equip and the removal pair up inside PairWindowSeconds.
bool ConsumptionArraysReady = false
Form[] EquipCandidateForms
float[] EquipCandidateTimes
Form[] RemoveCandidateForms
int[] RemoveCandidateCounts
float[] RemoveCandidateTimes

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
    ClearConsumptionCandidates()
    ArmSleepWaitEvents()
    ArmPlayerEvents(player)
    MirrorAll()
    ResyncPenalties()
    ResyncDiseases()
    RestartTimer()
    Debug.Trace("[RadSurvivalQuest] ready at game day " + LastGameTime)
EndEvent

Function ArmSleepWaitEvents()
    ; Constraint: unregister first; a surviving instance would double-deliver every sleep/wait event.
    UnregisterForPlayerSleep()
    UnregisterForPlayerWait()
    RegisterForPlayerSleep()
    RegisterForPlayerWait()
EndFunction

Function ArmPlayerEvents(Actor player)
    UnregisterForRemoteEvent(player, "OnItemEquipped")
    RegisterForRemoteEvent(player, "OnItemEquipped")
    ; Invariant: inventory events need a filter on this script or the engine sends none.
    RemoveAllInventoryEventFilters()
    AddInventoryEventFilter(None)
    UnregisterForRemoteEvent(player, "OnItemRemoved")
    RegisterForRemoteEvent(player, "OnItemRemoved")
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
    ; Constraint: re-anchor the clock so a loaded interval is not read as elapsed play.
    LastGameTime = Utility.GetCurrentGameTime()
    SleepStartTime = -1.0
    WaitStartTime = -1.0
    ClearConsumptionCandidates()
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
    else
        ; Constraint: keep the clock anchored while suspended, so a gating flip is not read as skipped play.
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
    ; Constraint: cheap form guards first; vanilla credits a use only with no live reference or a non-quest one.
    if !akBaseObject || (akReference != None && akReference.IsQuestItem())
        return
    endif
    if !IsConsumable(akBaseObject)
        return
    endif
    if akSender != Game.GetPlayer() || !IsActive()
        return
    endif
    Debug.Trace("[RadSurvivalQuest] consume-arm equip " + akBaseObject)
    PairEquip(akBaseObject)
EndEvent

Event ObjectReference.OnItemRemoved(ObjectReference akSender, Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akDestContainer)
    ; Constraint: this fires for every removal (every shot fires ammo), so cheap guards run first.
    if !akBaseItem || akDestContainer != None || aiItemCount <= 0
        return
    endif
    if (akBaseItem as Ammo)
        return
    endif
    if !IsConsumable(akBaseItem)
        return
    endif
    if akSender != Game.GetPlayer() || !IsActive()
        return
    endif
    Debug.Trace("[RadSurvivalQuest] consume-confirm remove " + akBaseItem + " x" + aiItemCount)
    PairRemove(akBaseItem, aiItemCount)
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

bool Function IsConsumable(Form akItem)
    if IsFoodOrDrink(akItem) || IsExtraReliefItem(akItem)
        return true
    endif
    return Diseases != None && Diseases.IsCureItem(akItem)
EndFunction
; Invariant: whichever of the equip/removal pair arrives second applies the relief exactly once.
Function PairEquip(Form akItem)
    PruneConsumptionCandidates()
    int match = RemoveCandidateForms.Find(akItem)
    if match >= 0
        int count = RemoveCandidateCounts[match]
        RemoveCandidateForms.Remove(match)
        RemoveCandidateCounts.Remove(match)
        RemoveCandidateTimes.Remove(match)
        ApplyRelief(akItem, count)
        return
    endif
    EquipCandidateForms.Add(akItem)
    EquipCandidateTimes.Add(Utility.GetCurrentRealTime())
EndFunction

Function PairRemove(Form akItem, int aiCount)
    PruneConsumptionCandidates()
    int match = EquipCandidateForms.Find(akItem)
    if match >= 0
        EquipCandidateForms.Remove(match)
        EquipCandidateTimes.Remove(match)
        ApplyRelief(akItem, aiCount)
        return
    endif
    RemoveCandidateForms.Add(akItem)
    RemoveCandidateCounts.Add(aiCount)
    RemoveCandidateTimes.Add(Utility.GetCurrentRealTime())
EndFunction

Function PruneConsumptionCandidates()
    ; Constraint: a mid-save install can deliver an event before OnInit initialises the candidate arrays.
    if !ConsumptionArraysReady
        ClearConsumptionCandidates()
    endif
    float now = Utility.GetCurrentRealTime()
    int i = EquipCandidateTimes.Length - 1
    while i >= 0
        if now - EquipCandidateTimes[i] > PairWindowSeconds
            EquipCandidateForms.Remove(i)
            EquipCandidateTimes.Remove(i)
        endif
        i -= 1
    endwhile
    i = RemoveCandidateTimes.Length - 1
    while i >= 0
        if now - RemoveCandidateTimes[i] > PairWindowSeconds
            RemoveCandidateForms.Remove(i)
            RemoveCandidateCounts.Remove(i)
            RemoveCandidateTimes.Remove(i)
        endif
        i -= 1
    endwhile
EndFunction

Function ClearConsumptionCandidates()
    EquipCandidateForms = new Form[0]
    EquipCandidateTimes = new float[0]
    RemoveCandidateForms = new Form[0]
    RemoveCandidateCounts = new int[0]
    RemoveCandidateTimes = new float[0]
    ConsumptionArraysReady = true
EndFunction

; Constraint: extra relief entries are resolved by the native INI bridge; no HC_* form is read.
bool Function IsExtraFoodItem(Form akItem) native
bool Function IsExtraThirstItem(Form akItem) native

bool Function IsExtraReliefItem(Form akItem)
    return IsExtraFoodItem(akItem) || IsExtraThirstItem(akItem)
EndFunction

Function ApplyRelief(Form akItem, int aiCount)
    if aiCount <= 0
        return
    endif
    ; Constraint: taint and cure route through the disease script on a confirmed removal.
    if Diseases != None
        Diseases.ConsumeItem(akItem, aiCount, IsActive())
    endif
    ResolveKeywords()
    bool ate = (_FoodKeyword != None && akItem.HasKeyword(_FoodKeyword)) || IsExtraFoodItem(akItem)
    bool drank = (_NukaKeyword != None && akItem.HasKeyword(_NukaKeyword)) || (_DrinkKeyword != None && akItem.HasKeyword(_DrinkKeyword)) || (_WaterKeyword != None && akItem.HasKeyword(_WaterKeyword)) || IsExtraThirstItem(akItem)
    ; Alcohol hydrates far less; chems never feed the survival pools.
    float hydration = 1.0
    if _AlcoholKeyword != None && akItem.HasKeyword(_AlcoholKeyword)
        hydration = AlcoholHydrationScale
    endif
    float portion = PortionFor(akItem)
    if ate
        AddDebt("hunger", -1.0 * portion * FoodScale)
    endif
    if drank
        AddDebt("thirst", -1.0 * portion * DrinkScale * hydration)
    endif
    Debug.Trace("[RadSurvivalQuest] consume-apply " + akItem + " x" + aiCount + " ate=" + ate + " drank=" + drank)
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
    ; Constraint: sleep advances game time without ticks, so accrue it once and restart the timer.
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
; penalties - one staged ability spell per pool per stage; relief removes it, stage 0 means none.

Function UpdatePenaltyStages()
    if !IsActive()
        ; Constraint: no Rad penalty survives the gate; the pools keep their values and resume.
        ClearAllPenalties()
        return
    endif
    ; Invariant: reconcile all three pools every call; the trackers gate notifications only.
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
    ; Constraint: drop the penalty cache on every load so a retuned table is picked up without doubling.
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
; classification keywords (Fallout4.esm), cached so per-event classification never hits the form table

Keyword _FoodKeyword
Keyword _NukaKeyword
Keyword _DrinkKeyword
Keyword _WaterKeyword
Keyword _AlcoholKeyword

Function ResolveKeywords()
    ; Constraint: cache only when all five Fallout4.esm keywords resolved, otherwise classification retries.
    if _FoodKeyword && _NukaKeyword && _DrinkKeyword && _WaterKeyword && _AlcoholKeyword
        return
    endif
    _FoodKeyword = Game.GetFormFromFile(0x00055ECC, "Fallout4.esm") as Keyword
    _NukaKeyword = Game.GetFormFromFile(0x00249F30, "Fallout4.esm") as Keyword
    _DrinkKeyword = Game.GetFormFromFile(0x000F4AEC, "Fallout4.esm") as Keyword
    _WaterKeyword = Game.GetFormFromFile(0x000F4AED, "Fallout4.esm") as Keyword
    _AlcoholKeyword = Game.GetFormFromFile(0x0010C416, "Fallout4.esm") as Keyword
EndFunction

bool Function IsFoodOrDrink(Form akItem)
    if !akItem
        return false
    endif
    ResolveKeywords()
    if _FoodKeyword && akItem.HasKeyword(_FoodKeyword)
        return true
    endif
    if _NukaKeyword && akItem.HasKeyword(_NukaKeyword)
        return true
    endif
    if _DrinkKeyword && akItem.HasKeyword(_DrinkKeyword)
        return true
    endif
    if _WaterKeyword && akItem.HasKeyword(_WaterKeyword)
        return true
    endif
    return false
EndFunction
