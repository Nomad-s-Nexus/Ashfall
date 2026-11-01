ScriptName RadShowcase

; Constraint: debug showcase entry for the native HUD; the quest script itself must stay free of global functions.
; Constraint: RadSurvival.esp holds the quest at 0x800 and keeps Fallout4.esm as its only master, so it is resolved by plugin name.
; A negative pool value or disease id leaves that part unchanged.
Function Apply(float afHunger, float afThirst, float afSleep, int aiDisease, int aiStage) Global
    RadSurvivalQuest quest = Game.GetFormFromFile(0x00000800, "RadSurvival.esp") as RadSurvivalQuest
    if quest == None
        Debug.Trace("[RadShowcase] RadSurvivalQuest not found")
        return
    endif
    quest.DebugSetNeeds(afHunger, afThirst, afSleep, aiDisease, aiStage)
EndFunction
