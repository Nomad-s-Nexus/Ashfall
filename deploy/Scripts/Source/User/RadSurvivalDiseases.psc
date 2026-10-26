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

float Property HourlyRisk = 0.012 Auto Const
float Property RiskPerWeakStage = 0.008 Auto Const
float Property RiskPerHit = 0.01 Auto Const
float Property RiskPerSwim = 0.03 Auto Const
float Property RiskPerRadHour = 0.08 Auto Const
float Property RiskPerRainHour = 0.02 Auto Const
float Property SwimIntervalGameDays = 0.1 Auto Const
float Property HitCooldownSeconds = 2.0 Auto Const
float Property IncubationHours = 4.0 Auto Const
float Property Stage2Hours = 10.0 Auto Const
float Property Stage3Hours = 24.0 Auto Const
float Property RestCureBaseHours = 4.0 Auto Const
float Property ImmunityHours = 24.0 Auto Const
bool Property DiseasesEnabled = true Auto Const
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
Form[] RadWeatherForms

; -----------------------------------------------------------------------------
; entry points - RadSurvivalQuest must be the only caller (it owns the events)
; -----------------------------------------------------------------------------

Function Tick(float afHours, bool abActive, int aiHungerStage, int aiThirstStage, int aiSleepStage)
    AccrueRisk(afHours, abActive, aiHungerStage, aiThirstStage, aiSleepStage)
    AdvanceDisease(afHours, abActive)
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
EndFunction

Function Reconcile(bool abActive)
    Spell wanted = None
    if abActive && DiseasesEnabled && DiseaseId > 0 && DiseaseStage > 0
        wanted = DiseaseSpellFor(DiseaseId, DiseaseStage)
    endif
    ApplySpell(wanted)
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
    if !abActive || !DiseasesEnabled || DiseaseId > 0 || afHours <= 0.0
        return
    endif

    if IsImmune()
        ; Constraint: immunity drains the pool instead of pausing it.
        RiskDebt = Clamp01(RiskDebt - HourlyRisk * afHours)
        MirrorRisk()
        return
    endif

    float hourly = HourlyRisk + RiskPerWeakStage * ((aiHungerStage + aiThirstStage + aiSleepStage) as float)
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
    AddRisk(RiskPerHit, abActive)
EndFunction

Function ExposureSwim(bool abActive)
    ; Constraint: the swim event repeats while in water, so it is throttled on game time.
    float now = Utility.GetCurrentGameTime()
    if NextSwimAllowed > 0.0 && now < NextSwimAllowed
        return
    endif
    NextSwimAllowed = now + SwimIntervalGameDays
    AddRisk(RiskPerSwim, abActive)
EndFunction

Function ExposureWeather(float afHours, bool abActive)
    ; Constraint: the sky is sampled on the pool tick, so an interval that never elapsed adds nothing.
    if afHours <= 0.0 || !abActive || !DiseasesEnabled || DiseaseId > 0
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
        AddRisk(RiskPerRadHour * afHours, abActive)
        return
    endif
    Weather current = sky as Weather
    if current && current.GetClassification() >= RainClassification
        AddRisk(RiskPerRainHour * afHours, abActive)
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
    if afAmount <= 0.0 || !abActive || !DiseasesEnabled || DiseaseId > 0
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
    if !abActive || !DiseasesEnabled || DiseaseId <= 0 || afHours <= 0.0
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
