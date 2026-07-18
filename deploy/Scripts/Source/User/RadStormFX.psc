ScriptName RadStormFX extends Quest

Int Property BurstSteps = 6 Auto

Float Property RumbleStrength = 0.12 Auto

explosion Property NukeExplosion Auto

Float Property NukeMaxDistance = 20000.0 Auto

Float Property RumbleInterval = 0.6 Auto

Bool Property EnableCloudDome = false Auto

Float Property DomeFollowInterval = 2.5 Auto

form Property NukeMarker Auto

Float Property DomeZOffset = 6000.0 Auto

Bool Property DebugNotify = true Auto

Float Property NukeZOffset = 22000.0 Auto

Float Property BurstStrength = 0.75 Auto

Float Property NukeMinDistance = 12000.0 Auto

Int _kDomeTimer = 2
Bool _shaking = false
objectreference[] _domeRefs
Int _kRumbleTimer = 1
Bool _domeUp = false

Function _RemoveDome()
	Int removed
	Int I
	If ((!_domeUp))
		Return
	EndIf
	removed = 0
	If ((!(_domeRefs == None)))
		I = 0
		While ((I < _domeRefs.Length))
			If (_domeRefs[I])
				; Fade the dome OUT as the storm clears rather than snapping it out of existence,
				; then delete the ref once it has faded.
				_domeRefs[I].Disable(true)
				_domeRefs[I].Delete()
				removed = (removed + 1)
			EndIf
			I = (I + 1)
		EndWhile
	EndIf
	_domeUp = false
	_Note((("DOME faded out (" + (removed as String)) + " refs)"))
EndFunction

Event OnStageSet(Int aiStage, Int aiItem)
	_Note(("OnStageSet stage=" + (aiStage as String)))
	If ((aiStage == 10))
		_SpawnDome()
		_StartShake()
	Else
		If ((aiStage == 11))
			_SpawnNuke()
			_StartShake()
		Else
			If ((aiStage == 12))
				_SpawnDome()
			Else
				If ((aiStage == 20))
					_StopShake()
					_RemoveDome()
				Else
					_Log(("OnStageSet: unhandled stage " + (aiStage as String)))
				EndIf
			EndIf
		EndIf
	EndIf
EndEvent

Function _Note(String msg)
	debug.Trace(("[RadStormFX] " + msg), 0)
	If (DebugNotify)
		debug.Notification(("[StormFX] " + msg))
	EndIf
EndFunction

Function _StopShake()
	_shaking = false
	_Log("_StopShake")
EndFunction

Function _SpawnDome()
	actor player
	form[] layers
	Int placed
	Int I
	objectreference r
	If ((!EnableCloudDome))
		_Log("_SpawnDome: cloud dome disabled (using weather fog instead)")
		Return
	EndIf
	If (_domeUp)
		_Log("_SpawnDome: already up, skipping")
		Return
	EndIf
	player = Game.GetPlayer()
	; Storm dome layers now live in OUR OWN plugin (assets bundled with permission), so the dome no
	; longer depends on "KORsAnomaly Stripped.esp" being installed.
	;   0x805 FARO_StormDome01 (CloudArena01_4)  0x806 FARO_StormDome02 (CloudArena01)
	;   0x807 FARO_StormDome03 (CloudArena03)    0x808 FARO_StormDome04 (CloudArena04)
	layers = new form[4]
	layers[0] = Game.GetFormFromFile(2053, "RadiationOverhaul.esp")
	layers[1] = Game.GetFormFromFile(2054, "RadiationOverhaul.esp")
	layers[2] = Game.GetFormFromFile(2055, "RadiationOverhaul.esp")
	layers[3] = Game.GetFormFromFile(2056, "RadiationOverhaul.esp")
	If ((layers[0] == None))
		_Note("DOME FAILED: FARO_StormDome01 (0x805) not found in RadiationOverhaul.esp")
		Return
	EndIf
	_domeRefs = new objectreference[4]
	placed = 0
	I = 0
	While ((I < 4))
		If ((!(layers[I] == None)))
			; Place each layer DISABLED so it never pops into existence, move it overhead, then
			; Enable(abFadeIn = true) so the engine FADES the dome in as the storm rolls in.
			r = player.PlaceAtMe(layers[I], 1, false, true, false)
			If (r)
				r.MoveTo((player as objectreference), 0.0, 0.0, DomeZOffset, true)
				r.Enable(true)
				_domeRefs[I] = r
				placed = (placed + 1)
				_Log(((("_SpawnDome: layer " + (I as String)) + " placed+fading ref=") + (r as String)))
			Else
				_Log((("_SpawnDome: layer " + (I as String)) + " PlaceAtMe returned None"))
			EndIf
		Else
			_Log((("_SpawnDome: layer " + (I as String)) + " form lookup = None"))
		EndIf
		I = (I + 1)
	EndWhile
	_Note(((("DOME fading in: " + (placed as String)) + "/4 layers at z+") + (DomeZOffset as String)))
	_domeUp = true
	StartTimer(DomeFollowInterval, _kDomeTimer)
EndFunction

Event OnTimer(Int aiTimerID)
	actor player
	Int I
	If ((aiTimerID == _kRumbleTimer))
		If (_shaking)
			Game.ShakeCamera(None, RumbleStrength, (RumbleInterval + 0.1))
			Game.ShakeController((RumbleStrength * 0.5), RumbleStrength, (RumbleInterval + 0.1))
			StartTimer(RumbleInterval, _kRumbleTimer)
		EndIf
	Else
		If ((aiTimerID == _kDomeTimer))
			If ((_domeUp as Bool))
			EndIf
			If (((!(_domeRefs == None)) as Bool))
				player = Game.GetPlayer()
				I = 0
				While ((I < _domeRefs.Length))
					If (_domeRefs[I])
						_domeRefs[I].MoveTo((player as objectreference), 0.0, 0.0, DomeZOffset, true)
					EndIf
					I = (I + 1)
				EndWhile
				StartTimer(DomeFollowInterval, _kDomeTimer)
			EndIf
		EndIf
	EndIf
EndEvent

Function _Log(String msg)
	debug.Trace(("[RadStormFX] " + msg), 0)
EndFunction

Function _SpawnNuke()
	actor player
	Float ang
	Float dist
	Float ox
	Float oy
	objectreference marker
	objectreference boom
	; Use OUR nuke explosion: FARO_EmissionStrikeAlpha (RadiationOverhaul.esp 0x885 = 2181). It carries
	; Effects\CG_Nuke.nif (the mushroom), the vanilla CGPreWarNukeLight01 and a 20200 outer radius, so it
	; reads as a horizon-scale detonation. Falls back to the vanilla CG nuke if ours is somehow missing.
	If ((NukeExplosion == None))
		NukeExplosion = (Game.GetFormFromFile(2181, "RadiationOverhaul.esp") as explosion)
	EndIf
	If ((NukeExplosion == None))
		NukeExplosion = (Game.GetFormFromFile(1186408, "Fallout4.esm") as explosion)
	EndIf
	If ((NukeMarker == None))
		NukeMarker = Game.GetFormFromFile(59, "Fallout4.esm")
	EndIf
	_Log(((("_SpawnNuke: explosion=" + (NukeExplosion as String)) + " marker=") + (NukeMarker as String)))
	; Both forms are required -- the old code only ever checked the marker, so a missing explosion
	; silently placed nothing.
	If (((NukeExplosion == None) as Bool) || ((NukeMarker == None) as Bool))
		_Note("NUKE FAILED: explosion or marker form is None")
		Return
	EndIf
	player = Game.GetPlayer()
	ang = utility.RandomFloat(0.0, 360.0)
	dist = utility.RandomFloat(NukeMinDistance, NukeMaxDistance)
	ox = (math.Sin(ang) * dist)
	oy = (math.Cos(ang) * dist)
	_Log((((((((((("_SpawnNuke: ang=" + (ang as String)) + " dist=") + (dist as String)) + " offset=(") + (ox as String)) + ",") + (oy as String)) + ",") + (NukeZOffset as String)) + ")"))
	marker = player.PlaceAtMe(NukeMarker, 1, false, false, true)
	If (marker)
		marker.MoveTo((player as objectreference), ox, oy, NukeZOffset, true)
		boom = marker.PlaceAtMe((NukeExplosion as form), 1, false, false, true)
		_Log(("_SpawnNuke: detonation ref=" + (boom as String)))
		marker.Delete()
		_Note(("NUKE detonated at dist " + (dist as String)))
	Else
		_Note("NUKE FAILED: marker PlaceAtMe returned None")
	EndIf
EndFunction

Function _StartShake()
	Int I
	Float t
	Float mag
	_shaking = true
	_Note("SHAKE burst start")
	I = 0
	If (((I < BurstSteps) as Bool))
	EndIf
	While ((_shaking as Bool))
		t = (((BurstSteps - I) as Float) / (BurstSteps as Float))
		mag = (BurstStrength * t)
		Game.ShakeCamera(None, mag, 0.4)
		Game.ShakeController(mag, mag, 0.4)
		utility.Wait(0.35)
		I = (I + 1)
	EndWhile
	If (_shaking)
		StartTimer(RumbleInterval, _kRumbleTimer)
		_Log("_StartShake: burst done, rumble loop started")
	EndIf
EndFunction
