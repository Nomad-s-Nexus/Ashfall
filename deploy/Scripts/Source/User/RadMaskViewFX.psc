Scriptname RadMaskViewFX extends Quest

Bool shown = false
VisualEffect fx
Keyword kwRad
Keyword kwGmo

Event OnQuestInit()
    Resolve()
    RegisterForRemoteEvent(Game.GetPlayer(), "OnPlayerLoadGame")
    StartTimer(0.5, 1)
EndEvent

Event Actor.OnPlayerLoadGame(Actor akSender)
    shown = false
    Resolve()
    StartTimer(0.5, 1)
EndEvent

Event OnTimer(int aiTimerID)
    Refresh()
    StartTimer(0.5, 1)
EndEvent

Function Resolve()
    fx = Game.GetFormFromFile(0x801, "RadOverhaul_MaskFX.esp") as VisualEffect
    kwRad = Game.GetFormFromFile(0x809, "RadiationOverhaul.esp") as Keyword
    kwGmo = Game.GetFormFromFile(0x011001, "Gasmask Overhaul.esp") as Keyword
EndFunction

Function Refresh()
    Actor player = Game.GetPlayer()
    Bool want = fx != None && player != None && ShouldShow(player)
    If want && !shown
        fx.Play(player)
        shown = true
    ElseIf !want && shown
        fx.Stop(player)
        shown = false
    EndIf
EndFunction

Bool Function ShouldShow(Actor akPlayer)
    If !akPlayer.GetAnimationVariableBool("IsFirstPerson") || akPlayer.IsInPowerArmor()
        Return false
    EndIf
    Return (kwRad != None && akPlayer.WornHasKeyword(kwRad)) || (kwGmo != None && akPlayer.WornHasKeyword(kwGmo))
EndFunction
