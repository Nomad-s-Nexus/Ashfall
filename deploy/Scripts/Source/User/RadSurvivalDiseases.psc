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
Spell AppliedDiseaseSpell = None

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
    if AppliedDiseaseSpell == akWanted
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
    return Game.GetFormFromFile(first + aiStage - 1, DiseasePlugin) as Spell
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
