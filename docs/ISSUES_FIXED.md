# Issues Fixed - Storm System

Fallen World Rad Overhaul (FalloutAnomalyRadiation F4SE plugin). Log of bugs found, fixed,
built, and deployed during the storm-system hardening sessions.

## 2026-07-19 — "Alarm cue is silent" is a TEST-HARNESS artifact, not an audio bug

- **Symptom:** the incoming-storm siren was reported as never playing, believed broken since the
  mod's early days.
- **Not the cause (all ruled out):** the asset exists and is deployed
  (`Sound\FalloutAnomaly\ambient\psi_storm_siren.xwm`, 598,766 bytes, identical in project `deploy/`
  and the live modlist); the mod is enabled (`+Fallen World Rad Overhaul` in the MO2 profile);
  `PlayStormCue()` and its `BSAudioManager::GetSoundHandleByFile` path are intact.
- **Root cause:** `PlayStormCue()` is only reached from `EnterWarningPhase()`, which runs when the
  `Approach` (silent dome-only lead) phase elapses. That lead is `_domeLeadTicks` = **180 s** by
  default. `FalloutAnomaly.log` contains **many** `[StormSeq] t=0 APPROACH begin` lines and **zero**
  `APPROACH complete -> WARNING` lines: every logged storm was cut short before 180 s, either by the
  play session ending or by another `storm:N` debug force overwriting it. Longest uninterrupted run
  observed was 105 s (2026-07-18 01:51:18 -> 01:53:05). The alarm has therefore never been *reached*,
  so it has never been observed to fail.
- **Aggravating factor:** `DebugForceStorm` logs `full warning (75 s)` / `(90 s)` — that is the
  `warningTicks` figure only. It does not mention the 180 s `Approach` lead that now runs *first*,
  so a debug storm is silent for ~4.5 min total. The comment in `RadEngine_Debug.cpp` shows the
  "real timings" choice was deliberate, but it predates the `Approach` phase being added in front.
- **Fix applied:** `PlayStormCue()` now logs `[StormCue] Siren started (...)` on success. Previously
  it logged **only** on failure, so "cue never fired" and "cue fired fine" were indistinguishable in
  the log — which is what left this open for weeks.
- **Second fix (R. approved 2026-07-19): debug-forced storms now use a 10 s lead.**
  `DebugForceStorm` sets `_forcedLeadTicks = kDebugLeadTicks` (10 s), consumed by the next
  `BeginStorm`, which then resets it to -1 so the following *natural* storm is back to the configured
  `DomeLeadMinutes`. Only the silent lead is shortened — `warningTicks`, Active and Aftermath still
  run at real timings. Real gameplay storms are unchanged at 180 s.
  `_activeLeadTicks` records what the in-flight storm actually used, so the `APPROACH complete`
  line reports the true elapsed lead instead of always printing `_domeLeadTicks`.
- **`skipwarn` already handled `Approach`** (`RadEngine_Debug.cpp`) — it was never Warning-only. It
  remains the way to skip the *warning* phase; the short debug lead just removes the need to use it
  for the lead.
- **`DebugForceStorm`'s log line was actively misleading** and is fixed: it printed
  `full warning (75 s)` — the `warningTicks` figure only — while silently running a 180 s lead first.
  It now prints lead + warning + total-to-impact.
- **VERIFICATION STILL OWED:** confirm `[StormCue] Siren started` actually appears in
  `FalloutAnomaly.log` on a fresh game session. Cannot be confirmed from the toolchain — needs an
  in-game run. Diagnosis is evidence-backed; the siren firing is NOT yet observed.

## 2026-06-27 (later 2) — HUD vitals appeared as empty grey bars (some users)

- **Symptom:** the radiation HUD's vitals (rads/tissue/health/AP) rendered as blank grey bars that
  never filled — most visible with the debug menu open (which force-shows the view).
- **Root cause:** `RadHud::Push` returns early unless `IsPlayerInWorld()` is true, and that gate only
  latched when a Pip-Boy was found in the `kPipboy` biped slot. Alternate-start mods, Pip-Boy
  replacers, and holster frameworks never trip that scan, so `updateRad` was never called and the
  bars stayed at their 0% (empty) default.
- **Fixes:**
  - `IsPlayerInWorld()` now also latches when `Calendar::gameYear > 2100` (postwar — intro is over),
    independent of the Pip-Boy scan. Prewar Vault 111 / chargen (2077) still keeps the HUD hidden.
  - `Update()` clamps the HUD max: if `GetPermanentActorValue(health/actionPoints)` reads 0 while the
    live value is > 0 (derived-HP overhauls / odd starts), fall back to the live value so the bar
    never renders empty for a living player and `hudLoaded` still trips.
  - `radhud.js`: `hPct/apPct` show full when max is 0 but current > 0; `hudLoaded = (hMax>0 || h>0)`.

## 2026-06-27 (later) — Fix Stats button reworked (SPECIAL debuff relief + diagnosis)

- **"Fix Stats" appeared to do nothing.** It early-returned while the player was a ghoul
  (`[DEBUG] Fix Stats unavailable while ghoulified`) — and the reporting player was ghoulified —
  so the button silently refused. With ghoulification removed it runs normally again.
- **Reworked the button to relieve + diagnose** rather than blanket-reset all 7 temp slots:
  1. Fully removes this mod's radiation END/STR penalty and suspends it until rad level changes.
  2. Restores up to 5 points (`kFixStatsReliefPerPress`) of any remaining SPECIAL deficit per press,
     capped at the base value (never strips gear bonuses, never overshoots base).
  3. Enumerates the player's active magic effects (`MagicTarget::GetActiveEffectList`) and names any
     that are lowering a SPECIAL (detrimental flag or negative magnitude), so the player is told what
     external system (hunger/thirst/fatigue/disease/other mod) to address.
- **END/STR "stuck at 1, base climbing to ~22 with ~-21 modifier" is NOT this mod.** Our penalty
  caps at END -7 / STR -5 and only uses temporary modifiers (verified: no `kPermanent` writes
  anywhere in the engine). That signature is the vanilla "unequip gear while a SPECIAL is floored
  bakes the bonus into the permanent base" bug, driven by a large persistent SPECIAL debuff from
  elsewhere in the load order. The button surfaces the source; it does not (and safely cannot)
  rewrite the corrupted permanent base.
- **Confirmed source: Survival Mode Overhaul.esp** (inspected via FO4RecordEditor). Its constant-effect
  need abilities apply stacking SPECIAL reductions that worsen by stage:
  - Hunger (`SMO_HC_HungerEffectsAb_*`): Endurance (Famished `abReduceEndurance` -4 ... Starving -10),
    plus Charisma/Luck.
  - Sleep deprivation (`SMO_HC_SleepEffectsAb_*`): Strength (Incapacitated `abReduceStrength` -6),
    plus Agility/Luck.
  - Thirst (`SMO_HC_ThirstEffectsAb_*`): Intelligence/Perception/Luck (Severely Dehydrated INT -10, PER -7).
  These use vanilla `abReduce<SPECIAL>` base effects (PeakValueModifier, primaryAV set), so the Fix
  Stats runtime scan catches them; the button now prefers the ability name ("Starving",
  "Incapacitated", "Severely Dehydrated") over the generic "Reduce X" so the player knows to eat/sleep/
  drink. Disease perks (`HC_Disease*Perk`) are damage/resist modifiers, not the SPECIAL drain.

## 2026-06-27 — Ghoulification removed + leaving-shelter storm spam

- **Player stuck as a ghoul; Shift+F8 unghoul kept reverting.** Once permanent tissue hit 800
  the player ghoulified (rad-immune), which made the whole overhaul stop mattering. DebugUnghoul
  (Shift+F8) cleared it, but `OnPostLoad` re-flagged the player as a ghoul on the next load
  whenever the ghoul quest was at stage ≥ 10 — and DebugUnghoul leaves the quest at stage **20**
  (still ≥ 10), so it re-ghoulified every reload. **Fix:** added master switch
  `kGhoulificationEnabled` (now `false`). The `Ghoulify()` trigger and debug action are gated on
  it, and existing ghoul saves are force-reverted to human on every load (idempotent race swap;
  quest pushed to stage 20 so the stale re-detect can't fire). Re-enable by flipping the flag.

- **Leaving the Sanctuary shelter triggered back-to-back radstorms / rad death.** Vanilla
  radstorms were only vetoed at the very start of their sky transition (`pct < kRadstormVetoMaxPct`).
  Stepping from a shelter into the open lands the player in a radstorm that is already at full
  `pct` (the exterior re-renders the queued weather instantly), so the veto missed it and the
  storm — plus its rad damage — landed the moment the player left cover. **Fix:** a sheltered→
  exposed transition (interior load-door OR roofed exterior base, via `IsPlayerSheltered`) arms a
  ~60s grace (`kCellExitGraceTicks`) during which any vanilla radstorm is force-cleared regardless
  of pct. Radstorm cooldown also raised 30 → 60 min (`kRadstormMinGapSec`).

- **Deployed DLL was a 1-byte placeholder.** The live modlist copy under
  `Fallen World Rad Overhaul/F4SE/Plugins/` was empty; redeployed the real 805 KB build.

## Storm ending and notifications

- **Storm never reported as ending.** The HUD re-showed the storm banner at the Aftermath
  phase instead of an "ending" message. Now the Aftermath banner reads "<theme> - PASSING"
  with the subtext "The storm is clearing - radiation subsiding" (radhud.js), and the engine
  fires plain notifications: "The radiation storm is passing." (Active to Aftermath, all storm
  types, not just lethal) and "The radiation storm has cleared." (Aftermath to Idle).

- **Storm never ended in interiors.** A forced storm weather lingered as the "current" weather
  while the player was sheltered/indoors, so its weather-conditioned rad effect re-fired the
  instant they stepped back outside and the storm appeared to never end. ReleaseStormWeather
  now hard-snaps to the Commonwealth clear weather when the player is sheltered (ForceWeather
  is interior-safe), instead of only releasing the override. Sky::ResetWeather is deliberately
  avoided because it CTDs in interiors.

## Fog / visual atmosphere

- **Fog rendered as a sphere around the player.** The "fog" was the RadStormFX cloud-dome mesh
  (cloud-arena layers placed overhead and re-centered on the player), which read as a bubble
  wrapped around the player and clipped through walls indoors. The dome is now OFF by default
  (RadStormFX.psc property EnableCloudDome = false, gates _SpawnDome). Storm atmosphere now
  relies on the weather's own engine volumetric fog (FogDistance Near/Far/Power in the storm
  weathers), which fills the world properly. Set EnableCloudDome = true to restore the old
  cloud sphere.

- **Storm fog/IMAD bled into interiors.** Screen FX (cloud dome plus full-screen atmosphere
  IMAD color grade) are exterior-only. UpdateStormFXVisibility edge-triggers on shelter change:
  it applies the dome plus IMAD when the player is exposed and removes both when sheltered, so
  nothing shows on-screen while indoors.

## Firestorm

- **Firestorm screen-shake stuck indoors (peak).** The roll-in block hand-set _stormFXVisible
  when sheltered, desyncing it from the real shake state so the visibility manager never called
  TriggerStormFX(false). Removed the manual flag writes and let the visibility manager reconcile
  the FX state each tick.

- **Firestorm camera shake not reconciled during the nuke roll-in window.** During the ~12s
  between the nuke detonation and the blast wave arriving, the Active phase returned early
  before UpdateStormFXVisibility ran, so ducking indoors kept the rumble shaking the camera
  until the roll-in ended. Added an off-only reconciliation in the roll-in branch: if the
  player shelters while the wave is inbound, the shake/dome stop immediately. It only ever turns
  FX off, so the deferred fog cannot roll in early.

## Exterior protection (shelter detection)

- **Player took rads under a roof with four walls.** The roof-fan raycast missed real roofs.
  Widened the overhead reach (kRoofRayDist 600 to 768, about 38 ft), tightened the angled fan
  to 20 degrees off vertical, and lowered the cover threshold (kRoofFanMinHits 3 to 2) so the
  up-ray plus one angled ray is enough for small, seamed, or edged roofs. Raised the ray origin
  toward head height (kShelterHeadZ 96). Re-check cadence is about 1s while a storm is active.

- **No way to diagnose shelter coverage.** Added a "shelter" debug toggle (Shelter Debug button
  in raddebug.html plus the shelter command). When on, the roof-fan runs continuously even with
  no storm and prints a live on-screen readout of which rays connect (up / +X / -X / +Y / -Y)
  and COVERED vs EXPOSED.

## Hunter ghoul pack

- **Pack could fail to despawn across worldspaces.** CheckGhoulPackCleanup relied only on
  GetDistanceFromPoint, which is computed in raw cell coordinates and is meaningless across
  worldspaces. Fast travel to a different exterior with overlapping coordinates could read a
  small distance and leave the pack alive (accumulating on further travel). Added
  _ghoulPackWorldID (captured at spawn from cachedWorldspace); cleanup now checks worldspace
  change first and only trusts the distance test within the same worldspace. Area trackers are
  cleared in ClearStormGhouls so stale state cannot mislead the next check. Required including
  RE/T/TESWorldSpace.h (the type was incomplete).

## State hygiene

- **_stormRollInTicks not reset on load.** It was the only storm field OnPostLoad did not reset.
  The HUD theme-override (if _stormRollInTicks > 0, force theme 0) runs on every UpdateStorm
  regardless of phase, so a save taken mid-firestorm-roll-in carried a stale value until the
  next storm. Added the reset alongside the other storm fields.

## Audio

- **Storm sounds were OGG (not engine-playable).** Fallout 4 plays WAV PCM and XWM, not OGG.
  Converted the storm sound set to XWM (ffmpeg to WAV s16le 44.1kHz stereo, then xWMAEncode) via
  convert_sounds.ps1 and wired key events: warning siren (incoming storm), ambient loop (exposed
  bed), nuke impact (firestorm detonation), and blast wave (firestorm roll-in).

- **Storm audio crash-safety.** Verified the audio path cannot crash the game: BSSoundHandle has
  a trivial destructor (fire-and-forget locals are safe), a missing file yields an invalid handle
  (silence, not a crash), GetSoundHandleByFile uses the correct signature with 2D usage flags, and
  all playback runs on the main thread. The incoming siren is tracked in _stormSirenHandle so a
  load or cancel can silence it.

## Dead code removal

- **RadiationState module deleted.** The class (exposure history, item-consumption log,
  alcohol-debuff timer, peaks bitfield) was acquired once in Initialize (_state =
  &RadiationState::GetSingleton()) and never used again - none of its methods were ever called.
  It belonged to the original C++-simulation design that was replaced by the game/ESL approach
  (environmental rads from vanilla rad regions, item effects from magic effects, weather rads
  from weather-conditioned effects). Removed RadiationState.h, RadiationState.cpp, the _state
  member, the forward declaration, and the include. It also carried two latent defects (now
  gone): StartAlcoholDebuff stored the duration as if it were an absolute end-time, and the
  exposure/item vectors only ever grew. The alcohol mechanic it was meant to drive (drink
  alcohol to drain rads, 0.5 rad/sec for 10 min per items.json and ARCHITECTURE.md) was never
  finished and is now fully removed rather than left as a broken stub.

- **RadiationEngine_Improved.cpp deleted.** Orphaned old monolithic engine that lived outside
  the compiled src tree (xmake globs the inner src/**.cpp), so it was never built.

- **Unused config halves removed.** RadiationConfig loaded zones.json, items.json, and
  perks.json into ZoneConfig/ItemConfig/PerkConfig maps, but only GetStageConfig (thresholds.json)
  is ever called by the engine. Removed those three structs, their maps, the three dead accessors,
  and their loading; kept StageConfig and the stage table. The data files themselves were left in
  place (they may still feed the ESL-generation scripts). DLL shrank from 802,816 to 791,552 bytes.

## Defensive hardening

- **Chelation form lookup is now type-checked.** EnsureEslLoaded type-checks GetFormType before
  casting on every form lookup except the chelation item (0x860), which cast blind to
  TESBoundObject. That pointer feeds GetInventoryObjectCount in CheckActiveCures, so a mis-typed
  form (e.g. an ESL regen that changed 0x860) would have handed the engine a bad pointer. Added
  the kALCH type check to match the rest of the function (chelation is an ALCH by design).

## Notes

- Build: xmake from the plugin root. Output build/release/FalloutAnomalyRadiation.dll, deployed
  to the Fallen World Rad Overhaul MO2 mod folder (F4SE/Plugins).
- Papyrus: RadStormFX.psc compiled with the official Fallout 4 PapyrusCompiler against the
  workspace base scripts, deployed as Scripts/RadStormFX.pex.
- Remaining warnings C4244 (SetStage int to uint16_t) and C4463 (raceSwitchPending bit-field) are
  pre-existing and benign; the values always fit.
- In-game testing is still required: a build cannot verify audio playback or the on-screen fog.
