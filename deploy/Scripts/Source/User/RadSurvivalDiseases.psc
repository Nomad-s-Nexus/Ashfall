ScriptName RadSurvivalDiseases extends Quest

; =============================================================================
; Rad survival diseases - invariant rules for risk, incubation, escalation, immunity, cure.
;
; Invariant: no event registration lives here. RadSurvivalQuest owns every player
; event and calls in, so a save cannot deliver one event to two scripts.
; Invariant: the ability on the player is derived from DiseaseId/DiseaseStage through
; AppliedDiseaseSpell, so a stage change replaces the spell exactly once.
; Constraint: DiseaseStage 0 is incubating - no ability until the illness takes hold.
; =============================================================================

; -----------------------------------------------------------------------------
; tuning - must mirror tools/rad_diseases.json
; -----------------------------------------------------------------------------

float Property HourlyRisk = 0.012 Auto
float Property RiskPerWeakStage = 0.008 Auto
float Property RiskPerHit = 0.01 Auto
float Property RiskPerSwim = 0.03 Auto
float Property RiskPerRadHour = 0.08 Auto
float Property RiskPerRainHour = 0.02 Auto
float Property SwimIntervalGameDays = 0.1 Auto
float Property HitCooldownSeconds = 2.0 Auto
float Property IncubationHours = 4.0 Auto
float Property Stage2Hours = 10.0 Auto
float Property Stage3Hours = 24.0 Auto
float Property RestCureBaseHours = 4.0 Auto
float Property ImmunityHours = 24.0 Auto
bool Property DiseasesEnabled = true Auto Const
; Constraint: Tuned() keys match RadNeedsTuningPolicy::kSpecs order.
int Property kTuneDiseases = 7 Auto Const
int Property kTuneBaseRisk = 8 Auto Const
int Property kTuneRadWeather = 9 Auto Const
int Property kTuneRain = 10 Auto Const
int Property kTuneWound = 11 Auto Const
int Property kTuneSwim = 12 Auto Const
int Property kTuneTaintedFood = 13 Auto Const
int Property kTuneTaintedWater = 14 Auto Const
int Property kTuneContaminated = 15 Auto Const
bool Property ShowNotifications = true Auto Const
int Property CurrentStateVersion = 2 Auto Const
string Property DiseasePlugin = "RadSurvival.esp" Auto Const
; 0x81C / 0x81F: first-stage ability of each illness; stages must run +0..+2
int Property FeverSpellBase = 2076 Auto Const
int Property WastingSpellBase = 2079 Auto Const
; Constraint: weathers resolve by file name, because one master forbids a cross-plugin VMAD binding.
string Property StormWeatherPlugin = "RadiationOverhaul.esp" Auto Const
; 0x840..0x844: the five storm weathers the DLL forces; 0x1C3D5E is vanilla CommonwealthGSRadstorm
int Property StormWeatherBase = 2112 Auto Const
int Property StormWeatherCount = 5 Auto Const
int Property VanillaRadStormFormId = 1850718 Auto Const
int Property RainClassification = 2 Auto Const

; -----------------------------------------------------------------------------
; pool mirrors for the native HUD - Risk 0..1, Disease packs id*10+stage
; -----------------------------------------------------------------------------

GlobalVariable Property RadSurvival_Risk Auto
GlobalVariable Property RadSurvival_Disease Auto

; Constraint: the taint records are authored; the lists decide membership, the keywords opt in.
FormList Property TaintedFoodList Auto
FormList Property TaintedWaterList Auto
FormList Property ContaminatedList Auto
Keyword Property KeywordTaintedFood Auto
Keyword Property KeywordTaintedWater Auto
Keyword Property KeywordContaminated Auto
Keyword Property KeywordCureItem Auto
; tuning - must mirror the taint block of tools/rad_diseases.json
float Property RiskPerTaintedFood = 0.10 Auto
float Property RiskPerTaintedWater = 0.15 Auto
float Property RiskPerContaminated = 0.05 Auto
; 0x82C: the shipped cure ingestible; the record tool has no keyword write path
int Property CureItemFormId = 2092 Auto Const
; 0x82D..0x832: the six disease perks, so the Pip-Boy PERKS tab names what the player has
int Property FeverPerkBase = 2093 Auto Const
int Property WastingPerkBase = 2096 Auto Const
; 0x023742 Fallout4.esm: vanilla RadAway, the named and obtainable cure
int Property RadAwayFormId = 145218 Auto Const

; -----------------------------------------------------------------------------
; state - saved with the quest, must stay versioned by SavedStateVersion
; -----------------------------------------------------------------------------

int DiseaseId = 0
int DiseaseStage = 0
int DiseaseCount = 0
int SavedStateVersion = 0
float DiseaseProgress = 0.0
float RiskDebt = 0.0
float ImmunityUntil = -1.0
float LastHitRealTime = -1.0
float NextSwimAllowed = 0.0
Spell AppliedDiseaseSpell = None
Perk AppliedDiseasePerk = None
Form[] RadWeatherForms

; -----------------------------------------------------------------------------
; entry points - RadSurvivalQuest must be the only caller (it owns the events)
; -----------------------------------------------------------------------------

; Constraint: an override is encoded as value + 1.0 and unset as 0.0, so a missing native reads as unset.
float Function TuningOverride(int aiKey) native

float Function Tuned(int aiKey, float afDefault)
    float raw = TuningOverride(aiKey)
    if raw > 0.0
        return raw - 1.0
    endif
    return afDefault
EndFunction

bool Function DiseasesOn()
    return DiseasesEnabled && Tuned(kTuneDiseases, 1.0) >= 0.5
EndFunction

Function Tick(float afHours, bool abActive, int aiHungerStage, int aiThirstStage, int aiSleepStage)
    AccrueRisk(afHours, abActive, aiHungerStage, aiThirstStage, aiSleepStage)
    AdvanceDisease(afHours, abActive)
EndFunction

; Constraint: showcase entry, called only by RadSurvivalQuest.DebugSetNeeds; disease 0 clears without granting immunity.
Function DebugSetDisease(int aiDisease, int aiStage, bool abActive)
    if aiDisease < 0 || aiDisease > 2
        return
    endif
    DiseaseId = aiDisease
    DiseaseStage = 0
    DiseaseProgress = 0.0
    if aiDisease > 0
        DiseaseStage = aiStage
        if DiseaseStage < 0
            DiseaseStage = 0
        elseif DiseaseStage > 3
            DiseaseStage = 3
        endif
        ; Constraint: progress sits at the stage's entry hour so AdvanceDisease neither skips nor undoes it.
        if DiseaseStage == 1
            DiseaseProgress = IncubationHours
        elseif DiseaseStage == 2
            DiseaseProgress = Stage2Hours
        elseif DiseaseStage == 3
            DiseaseProgress = Stage3Hours
        endif
    endif
    RiskDebt = 0.0
    Reconcile(abActive)
    MirrorRisk()
    MirrorDisease()
EndFunction

Function Resync(bool abActive)
    EnsureStateVersion()
    if DiseaseId < 0 || DiseaseId > 2
        DiseaseId = 0
    endif
    if DiseaseStage < 0
        DiseaseStage = 0
    elseif DiseaseStage > 3
        DiseaseStage = 3
    endif
    if DiseaseProgress < 0.0
        DiseaseProgress = 0.0
    endif
    RiskDebt = Clamp01(RiskDebt)
    RadWeatherForms = new Form[0]
    if NextSwimAllowed < 0.0
        NextSwimAllowed = 0.0
    endif
    Reconcile(abActive)
    MirrorRisk()
    MirrorDisease()
    TraceTaintLists()
EndFunction

Function Reconcile(bool abActive)
    Spell wanted = None
    if abActive && DiseasesOn() && DiseaseId > 0 && DiseaseStage > 0
        wanted = DiseaseSpellFor(DiseaseId, DiseaseStage)
    endif
    Perk wantedPerk = None
    if wanted && DiseaseId > 0 && DiseaseStage > 0
        wantedPerk = DiseasePerkFor(DiseaseId, DiseaseStage)
    endif
    ApplySpell(wanted)
    ApplyPerk(wantedPerk)
EndFunction

Function TryRestCure(float afHours, bool abInterrupted, bool abActive)
    ; Constraint: an interrupted night cures nothing; a full rest of 4h + stage cures.
    if DiseaseId <= 0 || abInterrupted || !abActive
        return
    endif
    if afHours < RestCureBaseHours + (DiseaseStage as float)
        return
    endif
    CureDisease()
EndFunction

; -----------------------------------------------------------------------------
; risk - precondition: nothing here runs unless the caller reports an active layer
; -----------------------------------------------------------------------------

Function AccrueRisk(float afHours, bool abActive, int aiHungerStage, int aiThirstStage, int aiSleepStage)
    if !abActive || !DiseasesOn() || DiseaseId > 0 || afHours <= 0.0
        return
    endif

    if IsImmune()
        ; Constraint: immunity drains the pool instead of pausing it.
        RiskDebt = Clamp01(RiskDebt - Tuned(kTuneBaseRisk, HourlyRisk) * afHours)
        MirrorRisk()
        return
    endif

    float hourly = Tuned(kTuneBaseRisk, HourlyRisk) + RiskPerWeakStage * ((aiHungerStage + aiThirstStage + aiSleepStage) as float)
    RiskDebt = Clamp01(RiskDebt + hourly * afHours)
    MirrorRisk()
    if RiskDebt >= 1.0
        StartDisease(abActive)
    endif
EndFunction

bool Function QualifiesAsWound(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Actor akPlayer)
    ; Constraint: only a wound from another actor counts; self-inflicted and hazard hits do not.
    Actor hurt = akTarget as Actor
    if !akPlayer || !hurt || hurt != akPlayer
        return false
    endif
    Actor attacker = akAggressor as Actor
    if !attacker || attacker == akPlayer
        return false
    endif
    ; Constraint: a cloak or radiation spell is a hazard field, not a bite or a bullet.
    if akSource as Spell
        return false
    endif
    return true
EndFunction

Function ExposureHit(bool abActive)
    ; Constraint: a real-time cooldown, or an automatic burst floods the pool in one burst.
    float now = Utility.GetCurrentRealTime()
    if LastHitRealTime > 0.0 && now - LastHitRealTime < HitCooldownSeconds
        return
    endif
    LastHitRealTime = now
    AddRisk(Tuned(kTuneWound, RiskPerHit), abActive)
EndFunction

Function ExposureSwim(bool abActive)
    ; Constraint: the swim event repeats while in water, so it is throttled on game time.
    float now = Utility.GetCurrentGameTime()
    if NextSwimAllowed > 0.0 && now < NextSwimAllowed
        return
    endif
    NextSwimAllowed = now + SwimIntervalGameDays
    AddRisk(Tuned(kTuneSwim, RiskPerSwim), abActive)
EndFunction

Function ExposureWeather(float afHours, bool abActive)
    ; Constraint: the sky is sampled on the pool tick, so an interval that never elapsed adds nothing.
    if afHours <= 0.0 || !abActive || !DiseasesOn() || DiseaseId > 0
        return
    endif
    Actor player = Game.GetPlayer()
    if !player || player.IsInInterior()
        return
    endif
    Form sky = Weather.GetCurrentWeather() as Form
    if !sky
        return
    endif
    if IsRadWeather(sky)
        AddRisk(Tuned(kTuneRadWeather, RiskPerRadHour) * afHours, abActive)
        return
    endif
    Weather current = sky as Weather
    if current && current.GetClassification() >= RainClassification
        AddRisk(Tuned(kTuneRain, RiskPerRainHour) * afHours, abActive)
    endif
EndFunction

Function ConsumeItem(Form akItem, int aiCount, bool abActive)
    if !akItem || aiCount <= 0
        return
    endif
    ; Constraint: the cure item clears the same state a rest cure clears.
    if IsCureItem(akItem)
        ; Constraint: a cure taken while healthy must say so instead of vanishing silently.
        if DiseaseId <= 0
            NotifyDisease("nothing to cure")
            return
        endif
        CureDisease()
        return
    endif
    ExposureConsume(akItem, aiCount, abActive)
EndFunction

bool Function IsCureItem(Form akItem)
    if !akItem
        return false
    endif
    ; Constraint: only an ingestible can be the cure, so a weapon or armour equip pays no lookup.
    Potion potion = akItem as Potion
    if !potion
        return false
    endif
    ; Constraint: the shipped item carries no KWDA, so it resolves by form id first.
    Form cure = Game.GetFormFromFile(CureItemFormId, DiseasePlugin)
    if cure && akItem == cure
        return true
    endif
    ; Constraint: RadAway is the obtainable cure - nameless records cannot be crafted, bought or picked up.
    Form radAway = Game.GetFormFromFile(RadAwayFormId, "Fallout4.esm")
    if radAway && akItem == radAway
        return true
    endif
    return akItem.HasKeyword(KeywordCureItem)
EndFunction

int Function ExposureCategory(Form akItem)
    if ListHas(TaintedFoodList, akItem) || akItem.HasKeyword(KeywordTaintedFood)
        return 1
    endif
    if ListHas(TaintedWaterList, akItem) || akItem.HasKeyword(KeywordTaintedWater)
        return 2
    endif
    if ListHas(ContaminatedList, akItem) || akItem.HasKeyword(KeywordContaminated)
        return 3
    endif
    return 0
EndFunction

bool Function ListHas(FormList akList, Form akItem)
    ; Constraint: a missing list charges nothing, so a short load order is fail-open.
    if !akList || !akItem
        return false
    endif
    return akList.HasForm(akItem)
EndFunction

float Function CategoryRisk(int aiCategory)
    if aiCategory == 1
        return Tuned(kTuneTaintedFood, RiskPerTaintedFood)
    elseif aiCategory == 2
        return Tuned(kTuneTaintedWater, RiskPerTaintedWater)
    elseif aiCategory == 3
        return Tuned(kTuneContaminated, RiskPerContaminated)
    endif
    return 0.0
EndFunction

Function ExposureConsume(Form akItem, int aiCount, bool abActive)
    if aiCount <= 0 || !abActive || !DiseasesOn() || DiseaseId > 0
        return
    endif
    int category = ExposureCategory(akItem)
    if category <= 0
        return
    endif
    AddRisk(CategoryRisk(category) * (aiCount as float), abActive)
    Debug.Trace("[RadSurvivalDiseases] tainted " + akItem + " x" + aiCount + " category " + category)
EndFunction

Function TraceTaintLists()
    if !TaintedFoodList || !TaintedWaterList || !ContaminatedList
        Debug.Trace("[RadSurvivalDiseases] a taint form list is unbound; that category charges nothing")
    endif
EndFunction

bool Function IsRadWeather(Form akWeather)
    if !akWeather
        return false
    endif
    Form[] weathers = RadWeatherForms
    if !weathers || weathers.Length == 0
        weathers = ResolveRadWeather()
    endif
    int i = 0
    while i < weathers.Length
        if weathers[i] == akWeather
            return true
        endif
        i += 1
    endwhile
    return false
EndFunction

Form[] Function ResolveRadWeather()
    ; Constraint: a failed resolve is never cached, so a missing plugin is retried, not remembered.
    Form[] resolved = new Form[0]
    Weather vanilla = Game.GetFormFromFile(VanillaRadStormFormId, "Fallout4.esm") as Weather
    if vanilla
        resolved.Add(vanilla)
    else
        Debug.Trace("[RadSurvivalDiseases] missing vanilla rad weather " + VanillaRadStormFormId)
    endif
    int i = 0
    while i < StormWeatherCount
        Weather storm = Game.GetFormFromFile(StormWeatherBase + i, StormWeatherPlugin) as Weather
        if storm
            resolved.Add(storm)
        endif
        i += 1
    endwhile
    if resolved.Length == 0
        Debug.Trace("[RadSurvivalDiseases] no rad weather resolved from " + StormWeatherPlugin)
        return resolved
    endif
    RadWeatherForms = resolved
    return resolved
EndFunction

Function AddRisk(float afAmount, bool abActive)
    ; Constraint: exposure is one rule, shared, and never runs while the layer is suspended.
    if afAmount <= 0.0 || !abActive || !DiseasesOn() || DiseaseId > 0
        return
    endif
    if IsImmune()
        return
    endif
    RiskDebt = Clamp01(RiskDebt + afAmount)
    MirrorRisk()
    if RiskDebt >= 1.0
        StartDisease(abActive)
    endif
EndFunction

bool Function IsImmune()
    if ImmunityUntil < 0.0
        return false
    endif
    return Utility.GetCurrentGameTime() < ImmunityUntil
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
; illness lifecycle - constraint: one ability at a time, replaced on escalate
; -----------------------------------------------------------------------------

Function StartDisease(bool abActive)
    ; Constraint: illnesses alternate so one save cannot roll the same one twice.
    DiseaseId = 1
    if DiseaseCount % 2 == 1
        DiseaseId = 2
    endif
    DiseaseCount += 1
    DiseaseStage = 0
    DiseaseProgress = 0.0
    RiskDebt = 0.0
    MirrorRisk()
    MirrorDisease()
    Reconcile(abActive)
    NotifyDisease(DiseaseLabel(DiseaseId) + ": something is wrong, rest before it takes hold")
EndFunction

Function AdvanceDisease(float afHours, bool abActive)
    if !abActive || !DiseasesOn() || DiseaseId <= 0 || afHours <= 0.0
        return
    endif

    DiseaseProgress += afHours
    int before = DiseaseStage
    if DiseaseStage == 0 && DiseaseProgress >= IncubationHours
        DiseaseStage = 1
    elseif DiseaseStage == 1 && DiseaseProgress >= Stage2Hours
        DiseaseStage = 2
    elseif DiseaseStage == 2 && DiseaseProgress >= Stage3Hours
        DiseaseStage = 3
    endif

    if DiseaseStage != before
        Reconcile(abActive)
        MirrorDisease()
        NotifyDisease(DiseaseLabel(DiseaseId) + " stage " + DiseaseStage)
        Debug.Trace("[RadSurvivalDiseases] stage " + DiseaseStage + " progress " + DiseaseProgress)
    endif
EndFunction

Function CureDisease()
    if DiseaseId <= 0
        return
    endif
    string label = DiseaseLabel(DiseaseId)
    ImmunityUntil = Utility.GetCurrentGameTime() + ImmunityHours / 24.0
    DiseaseId = 0
    DiseaseStage = 0
    DiseaseProgress = 0.0
    RiskDebt = 0.0
    ApplySpell(None)
    ApplyPerk(None)
    MirrorRisk()
    MirrorDisease()
    NotifyDisease("recovered from " + label + ", but vulnerable for a while")
EndFunction

Function ApplySpell(Spell akWanted)
    Actor player = Game.GetPlayer()
    if !player
        return
    endif
    ; Constraint: an ability lost to a dispel must be re-applied on the next reconcile.
    if AppliedDiseaseSpell == akWanted && (!akWanted || player.HasSpell(akWanted))
        return
    endif
    if AppliedDiseaseSpell
        player.RemoveSpell(AppliedDiseaseSpell)
    endif
    if akWanted
        player.AddSpell(akWanted, false)
    endif
    AppliedDiseaseSpell = akWanted
    Debug.Trace("[RadSurvivalDiseases] ability -> " + akWanted)
EndFunction

Function ApplyPerk(Perk akWanted)
    Actor player = Game.GetPlayer()
    if !player
        return
    endif
    ; Constraint: a perk lost to a dispel must be re-applied on the next reconcile.
    if AppliedDiseasePerk == akWanted && (!akWanted || player.HasPerk(akWanted))
        return
    endif
    if AppliedDiseasePerk
        player.RemovePerk(AppliedDiseasePerk)
    endif
    if akWanted
        player.AddPerk(akWanted, false)
    endif
    AppliedDiseasePerk = akWanted
EndFunction

Perk Function DiseasePerkFor(int aiDisease, int aiStage)
    int first = 0
    if aiDisease == 1
        first = FeverPerkBase
    elseif aiDisease == 2
        first = WastingPerkBase
    else
        return None
    endif
    if aiStage < 1 || aiStage > 3
        return None
    endif
    Form perkForm = Game.GetFormFromFile(first + aiStage - 1, DiseasePlugin)
    return perkForm as Perk
EndFunction

Spell Function DiseaseSpellFor(int aiDisease, int aiStage)
    int first = 0
    if aiDisease == 1
        first = FeverSpellBase
    elseif aiDisease == 2
        first = WastingSpellBase
    else
        return None
    endif
    if aiStage < 1
        return None
    endif
    if aiStage > 3
        aiStage = 3
    endif
    Spell resolved = Game.GetFormFromFile(first + aiStage - 1, DiseasePlugin) as Spell
    if !resolved
        Debug.Trace("[RadSurvivalDiseases] missing ability " + (first + aiStage - 1) + " in " + DiseasePlugin)
    endif
    return resolved
EndFunction

string Function DiseaseLabel(int aiDisease)
    if aiDisease == 1
        return "fever"
    elseif aiDisease == 2
        return "wasting"
    endif
    return "illness"
EndFunction

Function NotifyDisease(string asText)
    if !ShowNotifications
        return
    endif
    Debug.Notification("Rad survival: " + asText)
EndFunction

; -----------------------------------------------------------------------------
; save migration and HUD mirrors - must run on every load before use
; -----------------------------------------------------------------------------

Function EnsureStateVersion()
    if SavedStateVersion >= CurrentStateVersion
        return
    endif
    ; Constraint: an ability already on the actor must come off before the state is reset.
    if AppliedDiseaseSpell
        Actor player = Game.GetPlayer()
        if player
            player.RemoveSpell(AppliedDiseaseSpell)
        endif
    endif
    ; Constraint: a save from the penalty-only build has no disease state; initialise it once.
    DiseaseId = 0
    DiseaseStage = 0
    DiseaseCount = 0
    DiseaseProgress = 0.0
    RiskDebt = 0.0
    ImmunityUntil = -1.0
    AppliedDiseaseSpell = None
    SavedStateVersion = CurrentStateVersion
EndFunction

Function MirrorRisk()
    MirrorToGlobal(RadSurvival_Risk, RiskDebt)
EndFunction

Function MirrorDisease()
    float packed = 0.0
    if DiseaseId > 0
        packed = (DiseaseId * 10 + DiseaseStage) as float
    endif
    MirrorToGlobal(RadSurvival_Disease, packed)
EndFunction

Function MirrorToGlobal(GlobalVariable akTarget, float afValue)
    if akTarget
        akTarget.SetValue(afValue)
    endif
EndFunction
