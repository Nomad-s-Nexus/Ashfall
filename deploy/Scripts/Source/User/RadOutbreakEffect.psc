ScriptName RadOutbreakEffect extends ActiveMagicEffect

Sound Property CoughSound Auto
Sound Property SneezeSound Auto
Keyword Property SleepFurnitureKeyword Auto
float Property Stage2AfterHours = 6.0 Auto
float Property RecoverAfterHours = 24.0 Auto
float Property SignIntervalSeconds = 20.0 Auto
float Property BedSearchRadius = 3000.0 Auto
bool Property LogProbe = true Auto Const

int Stage = 1
float StartedAt = 0.0
Actor Patient = None
ObjectReference Bed = None

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Patient = akTarget
    if Patient == None || Patient == Game.GetPlayer() || Patient.IsDead() || Patient.IsEssential()
        Dispel()
        return
    endif
    StartedAt = Utility.GetCurrentGameTime()
    Stage = 1
    Probe("start")
    StartTimer(SignIntervalSeconds, 1)
    StartTimerGameTime(1.0, 2)
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
    CancelTimer(1)
    CancelTimerGameTime(2)
    Bed = None
    Probe("finish")
EndEvent

Event OnTimer(int aiTimerID)
    if aiTimerID != 1 || Patient == None
        return
    endif
    Sign()
    StartTimer(SignIntervalSeconds, 1)
EndEvent

Event OnTimerGameTime(int aiTimerID)
    if aiTimerID != 2 || Patient == None
        return
    endif
    float hours = (Utility.GetCurrentGameTime() - StartedAt) * 24.0
    if hours >= RecoverAfterHours - 0.01
        Dispel()
        return
    endif
    if hours >= Stage2AfterHours - 0.01 && Stage < 2
        Stage = 2
        Probe("stage2")
    endif
    if Stage >= 2
        EnsureInBed()
    endif
    StartTimerGameTime(1.0, 2)
EndEvent

Function Sign()
    Sound s = CoughSound
    if Utility.RandomInt(0, 1) == 0 && SneezeSound != None
        s = SneezeSound
    endif
    if s != None
        s.Play(Patient)
    endif
EndFunction

Function EnsureInBed()
    if Patient.GetSleepState() == 3
        return
    endif
    if Bed == None || Bed.IsFurnitureInUse()
        Bed = NearestFreeBed()
    endif
    if Bed != None
        Bed.Activate(Patient)
        Probe("bed")
    endif
EndFunction

ObjectReference Function NearestFreeBed()
    if SleepFurnitureKeyword == None
        return None
    endif
    ObjectReference[] beds = Patient.FindAllReferencesWithKeyword(SleepFurnitureKeyword, BedSearchRadius)
    if beds == None
        return None
    endif
    ObjectReference best = None
    float bestDist = 99999999.0
    int i = 0
    while i < beds.Length
        if !beds[i].IsFurnitureInUse()
            float d = Patient.GetDistance(beds[i])
            if d < bestDist
                best = beds[i]
                bestDist = d
            endif
        endif
        i += 1
    endwhile
    return best
EndFunction

Function Probe(string asTag)
    if LogProbe && Patient != None
        Debug.Trace("RadOutbreak " + asTag + " actor=" + Patient + " stage=" + Stage + " bed=" + Bed + " sleep=" + Patient.GetSleepState())
    endif
EndFunction
