
# Fallout Anomaly – Radiation Overhaul — Changelog

## 2026-10-29: roofless-interior rain (Concord Museum of Freedom, build-verified only)

**Rain can now reach the player inside one approved roofless interior.** `climate_weather.json` gained a
`rooflessInteriorCells` list with exactly one entry, `Fallout4.esm:0x017DF7` (ConcordMuseum01), resolved through
`TESDataHandler::LookupForm` with the weather overrides. A listed interior is treated like an exterior for
precipitation only when the existing five-ray roof probe finds open sky; a roofed result gives zero, every unlisted
or unresolved interior keeps the old dry rule, and the interior keeps its indoor thermal shelter. `wetness` uses the
list too, so the exposed part can actually wet the player (`[Climate] Roofless interior roof cover changed` logs
`rainCover=0%` for open sky and `rainCover=100%` for a covered spot). Policy covered by `tests/climate_policy_tests.cpp`
(listed+open, listed+roofed, unlisted interior, exterior, unresolved entry) and a new
`tests/verify_climate_roofless.py`; this branch's build and release workflows in
`.github/workflows/rad-overhaul-build.yml` / `-release.yml` run it. Not played in game: whether the museum's play space is
genuinely open to the sky still needs the in-game check in `docs/IN_GAME_TEST_CHECKLIST.md`.

## 2026-10-28: review-round fixes (consumption arrays, relief INI bridge, overlay alpha, hysteresis, hotkeys)

**Consumption reliability.** The candidate arrays are now initialised in `OnInit` and re-initialised by the prune
guard, so the first meal on a new game or a mid-save install credits relief instead of writing to None arrays. The
removal handler does the cheap form/ammo/consumable checks before the player and difficulty lookups, because every
shot fires ammo removal events. A world drop is documented as rejected only through the equip pairing; the code no
longer implies `akItemReference` proves it. **Generic relief INI bridge.** `[Relief] ExtraFoodKeywords`,
`ExtraThirstKeywords`, `ExtraFoodItems` and `ExtraThirstItems` add other mods' food/drink without code changes
(`src/RadReliefConfig.cpp`, native Papyrus functions); the shipped defaults cover the two vanilla drinks that only
carry `HC_SustenanceType_QuenchesThirst` (RefreshingBeverage 0x058AB8, MS17DeezerLemonade 0x0E9284), and spoiled food is
deliberately not credited. **Visor overlay.** The visor and its fog no longer rely on CSS `mask-image`: the HUD clear
zones are baked into the PNG alpha (top-left messages, centre readouts, bottom-centre XP) and the HUD test decodes the
PNGs and checks real alpha. **Auto-don.** A 3 s stable-exposure requirement stops doorway flapping, a null parent cell
is skipped instead of read as exterior, and an auto-donned mask saved before a load is still removed after it.
**Hotkeys.** INI values accept decimal or 0x-hex, invalid values are logged and disabled, mouse buttons/Shift/Esc/F8 are
rejected, a duplicate disables both keys, and the keys only fire while the game window is foreground with no blocking
menu open. **Interior rain.** A roofless-interior whitelist is proposed in the code-notes with the FO4IDE-verified
`ConcordMuseum01` cell; it is not implemented until the lead approves the table. Nothing here was played in game.

## 2026-10-28: gas-mask overlay, auto-don and hotkey fixes

**Power-armor overlay.** The Prisma visor overlay is now hidden while a power-armor helmet is worn
(any ArmorTypePower piece that fills the helmet slot, frame or full suit), because the PA HUD replaces
it; the debug `mask:on` override still forces it and the real worn state still drives foley, filter and
choking. **HUD-safe visor art.** `radhud.css` masks `#maskOverlay` and `#visorFog` with a centre-clear
radial gradient, so the vanilla message area, XP bar and loot/prompt readouts stay readable; the
assumption is stated in the code-notes and `tests/status_hud_tests.js` asserts the rule. **Auto-don.**
The mod no longer dons a mask just because the sky switched to rad weather: interiors, shelter, a
powered shelter and a sealed suit no longer count as exposure, the mask is donned only when exposure
starts and removed only if we equipped it, so manual unequip while exposed is respected. **Hotkeys.**
`ReplaceKey`/`RepairKey` are validated (F8 and duplicates rejected with a log line) and re-read when
the debug menu opens, so an INI edit applies without a restart; there is still no in-game rebinding
screen. The keys consume only our own filter/repair items, which is why another mask mod's mask and
filters do nothing on those keys - documented in the code-notes, not changed.

## 2026-10-28: tester follow-up audit (penalties, interior rain, not-ours items)

**Penalties.** Verified with FO4IDE that the hunger/thirst/sleep penalty spells modify only CarryWeight,
ActionPoints and HealRateMult; the tester's SPECIAL drops are the radiation stage perks (END/STR in the
plugin log), and leaving vanilla Survival clears our penalty spells through `ClearAllPenalties`. Vanilla
`HC_*` spells left on a save moved off Survival are vanilla state we do not touch. **Interior rain.**
Interior precipitation is zero by design and the tester log shows no rainy interior ticks; a whitelist of
roofless interiors is a documented follow-up, not changed without FO4IDE-verified cell data. **Not ours.**
Bullet weight and the power-armor harness/vest carry weight come from Fallen World's
`FWHardcore_Weapons/CarryWeight/Difficulty` plugins: our source never touches ammo weight, and our
CarryWeight writes are additive actor-value modifiers that cannot cancel another effect's bonus.


## 2026-10-28: hunger/thirst relief confirmed by the engine's item-removal event

**Food and drink now relieve the Rad survival pools.** The quest script used to arm a candidate on
`Actor.OnItemEquipped` with `GetItemCount` and confirm on a later tick by watching the count fall.
That is timing-dependent: if the engine consumes the aid item before the Papyrus event is delivered,
the armed count is already the post-consumption count and relief never fires — which matches the
tester's "eating and drinking never change hunger/thirst". Relief now runs on `ObjectReference.OnItemRemoved`
(registered remotely with `AddInventoryEventFilter(None)`), only when the destination container is
None and an `OnItemEquipped` for the same form arrived within 2 s, so exactly one credit is given per
confirmed consumption whichever event arrives first, and a drop or a failed equip cannot credit
anything. `Debug.Trace` now writes `consume-arm equip`, `consume-confirm remove` and `consume-apply`
so the next Papyrus.0.log proves the path. The keyword classifier was also cached (no more five
`GetFormFromFile` calls per event). Coverage against the tester's `FWHardcore_Food.esp`: 113 of 123
ALCH records carry a covered keyword; two drinks that carry only the vanilla
`HC_SustenanceType_QuenchesThirst` keyword are still not credited because the script must not read
`HC_*` forms (owner decision). Not played in game.

## 2026-10-28: empty bottles fill at water sources (build-verified only)

**Empty bottles at water sources.** Sneak-activating a drinking water source while holding an empty
bottle now consumes one bottle and adds one water item, through a native `TESActivateEvent` sink that
runs independently of the vanilla `HC_*` Survival path — no `HC_*` record is read or written, and the
activated source's `spellOnDrink` is never consulted. Built-in tables cover the 26 vanilla empty-bottle
items and the ten vanilla/DLC drinking activators, plus the `AnimFurnWaterPump` keyword for mod-added
pumps. **Output rule: Dirty Water everywhere except inside the maintained Vault 81 cells (`Vault81`,
`Vault81Entry`), which give Purified Water.** The decision uses only the player's current parent cell
— the activated source's type and `spellOnDrink` are never consulted. Everything outside those two
cells is therefore dirty: the other vaults (75, 95, 111, 114, 118, 88, the Vault-Tec Workshop and
DLC03 vaults), the Vault-Tec lab (`Vault81Secret`), the Vault 81 holding cell and the Vault 81
exterior. Settlement pumps and sinks, DLC and vault fountains and the irradiated
`Vault_WaterFountain01_Rads` follow the same cell-only rule — the source type never decides by itself.
The clean check keys on the parent cell, never on the location, because `Vault81Ext` shares
`Vault81Location`; anything unresolved or exterior reads as dirty, and the code never fails open to
clean. Every fill shows a HUD message, so a consumed bottle is never silent, and a bottle the engine
refuses to remove produces no water.
`[Water] RequireModifier=1` (default) requires sneaking: a normal activation just drinks and never
consumes a bottle. `[Water] Enabled=1` controls the feature and the default
`SkipWhenVanillaSurvival=1` leaves filling to vanilla in Survival difficulty, so a bottle is never
filled twice; on Very Hard the vanilla perk does not fill, so this hook is the only filler there. The
hook builds and its policy unit test runs, but it has not been played in game yet, and whether the
activate event fires once or twice when entering/exiting pump furniture is unverified.
Follow-ups on the same feature: the same-target debounce is stamped only after a confirmed fill, so a
refused removal does not arm it; the clean-water wording now states that only `Vault81` and
`Vault81Entry` are maintained cells (the lab, holding cell and exterior are dirty); and the sneak gate
is called out as deliberately different from vanilla Survival, which fills a bottle on a plain
activation without sneaking.

## 2026-10-28: bounded RadStormFX shake burst

**RadStormFX shake burst.** `_StartShake` looped `While (_shaking)` and only `_StopShake` cleared the
flag, so after `BurstSteps` iterations the magnitude went negative and the rumble timer never started.
The loop is now bounded by `I < BurstSteps`, which lets the existing `If (_shaking) StartTimer(...)`
run. `DebugNotify` now defaults to false so the on-screen `[StormFX]` spam stops. This repairs the
script only: the Papyrus shake has never run on any save (`SetStage` fails — see `docs/ISSUES_FIXED.md`),
and the native `NativeShakeCamera` path is what actually shakes the camera.

## 2026-07-19: black storm dome FIXED (confirmed in-game); storm siren logging + debug lead

**Black dome [FIXED — confirmed in-game 2026-07-19].** Closes the Issue 1 left open by pt13 below.
Three changes, landed across `fe6e2aacf` and `147ed6689`:
- **Arena light restored.** pt12 removed it as "pointless" after concluding the dome was fogged out
  rather than unlit. That was half right: the fog fix was real, but the cloud layers ship with
  own-emit OFF and effect-lighting ON, so with no light they still render as a dark blob. The light
  is back at the NIF's `TopLightNode` height, aimed straight down, radius 20000.
- **Dome animation force-started.** A natively-placed ACTI ref leaves the clouds' shader float stuck
  at its initial value; each layer's controller sequence is now activated once its 3D attaches
  (async, so polled; one-shot per dome).
- **Black-hole apex card.** NASA Goddard / Schnittman texture (SVS #13326, credited in `NOTICE.md`),
  card switched from `BSLightingShaderProperty` to an emissive shader.

None of the three candidate causes pt13 listed (loose sky-mod DDS override, CommonwealthDusty
imagespace, backface culling) turned out to be it.

**Storm siren.** `PlayStormCue` now logs on success, not only on failure — the siren was reported
silent for weeks, but the log could not tell "never fired" from "fired fine". It had never been
*reached*: every logged storm was cut short before the 180 s silent Approach lead elapsed (longest
uninterrupted run 105 s). Debug-forced storms now use a 10 s lead so testing does not take ~4.5 min;
natural storms keep `DomeLeadMinutes`. `DebugForceStorm`'s log line printed `full warning (75 s)`
while silently running the 180 s lead first, and now reports lead + warning + total.

**Docs.** New `STATUS.md` is the single authoritative list of unfinished work; superseded planning
docs moved to `archive/`. Ghoulification removed from `README.md` and the top stage relabelled
"Systemic Failure" (the feature's code was deleted earlier but was still advertised).

## 2026-07-15 (pt13): staged storm onset + dome ObjectBounds; SkyrimCloudLUT diagnosis CORRECTED

**LUT diagnosis corrected (Issue 1 still open).** pt12 blamed the black dome on a "missing"
`textures\sky\SkyrimCloudLUT.dds`. WRONG — that was a loose-file-only search. A BA2 grep found
`Sky\SkyrimCloudLUT.DDS` **and** `SkyrimCloudSheet01.DDS` inside the vanilla `Fallout4 - Textures3.ba2`
(SkyrimCloud* are Skyrim-origin textures shipped in FO4's base archives). The LUT is present and loadable,
so the black dome has a DIFFERENT cause — re-diagnosis pending (leading candidates: the loose sky-mod
`SkyrimCloudSheet01.dds` override being incompatible with the vanilla LUT, the CommonwealthDusty storm
imagespace crushing the emissive, or inside-the-dome backface culling). No LUT synthesis needed.

**Staged storm onset (Issue 2) [DONE].** Replaced the pt12 "all in one go" onset with R.'s intended
sequence via a new `StormPhase::Approach` (appended to the enum to keep HUD int values stable):
- `BeginStorm` now spawns ONLY the dome and enters `Approach` for a silent lead — `[Storm] DomeLeadMinutes`
  (default 3 min, INI-configurable; set 0.25 for fast testing). No siren, alert, weather, or HUD banner
  during the lead.
- On lead expiry, `EnterWarningPhase()` fires the alarm + alert and starts the dust-sky roll-in; the
  existing Warning->Active effects proceed unchanged.
- `[StormSeq]` log lines timestamp each transition (APPROACH begin -> WARNING/alarm -> dust roll-in ->
  Active). `skipwarn` debug also skips the lead.

**Dome ObjectBounds (Task 3) [DONE].** FARO_StormDome01-04 had zero ObjectBounds (KORS originals are
+/-11000/+/-8800); zero bounds cull the huge overhead dome as a point, popping it out when looking
off-center. Set all four to (-11000,-11000,-1042)-(11000,11000,11040) via a surgical in-place OBND binary
overwrite — master-order-safe by construction (TES4 MAST order byte-for-byte unchanged, verified
before+after; validated re-parse in fo4editor).

## 2026-07-14 (pt12): dome "still dark" = FOGGED OUT, not unlit; onset "all in one go" [FIXED]

Two reports: dome still dark (user theorised imagespace conflict), and the onset staggers — "the dome
comes in, then a few minutes later the alert; it should be all in one go."

**Dark dome — root cause was the fog, not lighting.** `nif_inspect` on `CloudArena01_4.nif` shows the
bright parts are UNLIT `BSEffectShaderProperty` layers (ArenaLightning + SkyrimCloudSheet/LUT emissive);
the one lit layer is a `BSLightingShaderProperty` with a `black.dds` diffuse (light × black = black by
design). So my pt11 arena LIGHT was pointless — it lit a black sphere; the log even showed `light on`
with the dome still dark. The actual killer: the Dust Storm fog was **near=150 far=2000 hiDensity=0.97**
(a near-whiteout), but the dome sits at **z+3800 — beyond far=2000**, so it was radially fogged to flat
brown. It was hidden, not unlit — the user's imagespace instinct was the right neighbourhood (fog/grade
crushing it).
- Removed the useless arena light (spawn + resolution + member + constants).
- Retuned the Dust Storm fog to height-based so the ground stays engulfed but the dome clears above:
  `far 2000 -> 5000` (dome distance no longer fully fogged), `heightRange 15000 -> 3000` (fog concentrated
  low, thins above ~3000 where the dome is), `hiDensity 0.97 -> 0.90`. near=150 kept for close brownout.
- Note: engulf-vs-visible-dome is a balance; these are first-pass tuning values, may need a nudge.

**Onset "all in one go."** The dome, the incoming-storm siren, and the alert now all fire together at
`BeginStorm` (removed the `kSirenAfterDomeTicks` stagger that delayed the siren ~8 s after the dome). The
Warning->Active shelter lead is unchanged (that gap is the "storm has hit" beat / seek-shelter time).

## 2026-07-14 (pt11): pt10 dome crash — scripted activator placed off the worker thread [FIXED]

pt10 crashed the game on load: `EXCEPTION_ACCESS_VIOLATION` in `BSFixedString::BSFixedString(&)` on a
Havok/Physics **Job/Task thread**, with `BSScript::Internal::VirtualMachine`,
`GameScript::ObjectBindPolicy`, and Havok behavior-graph objects (`BShkbHkxDB`, `SubgraphAndOffsetDBData`)
on the stack. No `Native storm dome spawned` line printed before the crash → it died on the FIRST
`CreateReferenceAtLocation` of the loop.

Root cause: pt10 switched the dome to the REAL KORs records, layer 0 of which is
`KORs_Anomaly_CloudArena01` — an Activator carrying `KORsCloudArenaActScript` + `defaultdisablehavokonload`.
Placing a **scripted** ref natively from the 4Hz worker-thread hook forces BSScript object-binding +
Havok behavior-graph load on that thread, which is not safe — the same worker-thread VM hazard that made
`SetStage` fail. (The save had a storm active across load, so the first tick re-spawned the dome and hit
it immediately.) Our own bare FARO ACTI copies placed fine in pt10's screenshots precisely because they
carry no script.

Fix:
- Dome back to the scriptless FARO_StormDome01..04 ACTI copies (mesh `CloudArena01_4.nif` etc., bundled).
  No script → no VM bind → no crash. Their baked NiController rotation still auto-plays.
- Light the black cloud mesh with a **separately-spawned scriptless LIGH**: KORs `ArenaLight01`
  (radius-20000 shadow spotlight, 0x025334) placed above the apex pitched straight down (+pi/2),
  backlighting the clouds toward the player the way the KORs `TopLightNode` light does. A LIGH has no
  script/behavior-graph, so it is safe off the worker hook. Tracked in `_stormDomeRefs` (torn down by
  `ClearStormDome`).
- Rule confirmed: **never place a scripted/behavior-graph ref (Activator w/ script, Actor) from the 4Hz
  worker hook** — only scriptless bound objects (Static, scriptless Activator, Light, Explosion).

## 2026-07-14 (pt10): Storm dome black/static/too-high + no rolling dust [FIXED]

Two reports, with a reference screenshot of the intended look (a bright rotating vortex dome with an
emissive glowing core + lightning), plus "no rolling dust around me."

**Dome — black, static, too high.** Our own FARO_StormDome ACTI records were inert copies: no
light-spawning script and no auto-rotation, so they placed as a static, unlit black dome at z+6000.
Fix: place the REAL KORs CloudArena records natively instead of our copies.
- Layer 0 = `KORs_Anomaly_CloudArena01` (Activator, 0x026269). Its `KORsCloudArenaActScript` spawns the
  two arena lights (`ArenaLight01` radius-20000 illuminator + `ArenaLight02` lens flare) via
  `PlaceAtNode` in **`OnLoad`** — which fires on the MAIN thread when the ref's 3D attaches, regardless
  of the worker thread that placed it. That is what lights the otherwise-black cloud mesh.
- Layers 1-3 = `KORsCloudArena02/03/04` (Statics, 0x02626B/0x026A07/0x026A08). Their baked NiController
  rotation auto-plays — that is the spin.
- Lowered `kStormDomeZOffset` 6000 -> 3800 so the ceiling sits closer and reads as enveloping.
- The placement code was already form-agnostic (casts to `TESBoundObject`), so it takes ACTI + STAT
  unchanged; only the form resolution + a STAT type-accept changed.
- Depends on KORsAnomaly being in the load order (it is). A fully standalone version needs STAT/LIGH
  records authored in xEdit — the record editor can't create those types. Follow-up.

**No rolling dust.** The weather `referenceEffect` (FXCameraAttachExplosionDustVE) attaches (log
confirms) but produced no visible motes in practice. Drive it the reliable way instead: new
`UpdateDustMotes()` re-applies the heavy dust ArtObject (`_dustBurstArt`) to the camera every ~4.5 s for
5 s (slight overlap = steady, no pulse) whenever the dust sky is current + player exposed, in Warning or
Active. Same `ApplyArtObject` path as the wall-arrival burst, which DOES render.

Good news from the same test: the pt9 re-transition fix landed — log shows `'Dust Storm' sky already
current at Active — no re-transition`, and the fog now engulfs (thick brown low-vis haze).

## 2026-07-14 (pt9): Dust Storm re-transitioned at Active hit -> tore down its own camera dust [FIXED]

Report: end Ash, start Dust; the dust "blew through for a second and never engulfed." The log shows the
dust storm RE-transitioning at the Active hit (`'Dust Storm' armed ... from 0xA2000844` = dust->dust).

Root cause: the Active-hit "don't re-force" branch was gated on `_dustBuildupActive && currentWeather==w`.
But with the pt4 Trans-Delta authored, the warning SKY transition often finishes BEFORE the 75 s warning
ends — and completing it clears `_dustBuildupActive`. So by the Active hit the guard was false, it
fell through, and called `BeginSmoothWeather(dust)` AGAIN. That redundant dust->dust transition resets
pct and, via the pt6 camera-VE end threshold (`kEndEffect`, tears the outgoing effect down at pct 0.39),
**despawns the blowing-dust camera VE mid-storm** — exactly the "vanished after a second" symptom.

Fix: gate the skip on `sky->currentWeather == w` alone. If the storm sky is already current (dust
warning already transitioned it in), never re-force — regardless of the `_dustBuildupActive` timing.
Non-dust storms don't force their sky during the warning, so `currentWeather != w` there and they still
transition normally at Active.

## 2026-07-14 (pt8): Storm dome spawned NATIVELY — the Papyrus quest never delivered it [FIXED]

In-game the dome never appeared. The log shows exactly why: even after `InitSearchTables` rebuilt the
stage table (dump #2 shows all 5 stages populated, quest enabled `flags=0x8019`), `SetStage(10)` and
`SetStage(12)` **still return false** and Papyrus `OnStageSet` never fires (`bEnableTrace=1`, yet zero
`RadStormFX` traces in Papyrus.0.log). Root cause: the 4Hz Update hook is a **worker thread** — native
cell insertion works off it (the fire ring proves this), but the VM stage-set + event dispatch does not.

Fix: stop routing the dome through the quest. Spawn it **natively** with
`TESDataHandler::CreateReferenceAtLocation` (the exact call `SpawnFireRing` uses), from our own records
`FARO_StormDome01..04` (`0x805`-`0x808`, the bundled KORs CloudArena layers). Rotation is baked into the
NIFs, so a placed ref just plays it. Placed at player `z+6000`; the ±8,000-11,000 span keeps the player
inside for the whole storm with no per-tick reposition. `SpawnStormDome`/`ClearStormDome` mirror the
fire-ring lifecycle and are wired into every path: onset, shelter in/out (`UpdateStormFXVisibility`),
firestorm duck, storm end, debug end/force, and load-cleanup (temp refs don't persist a save).

Known follow-up: native `Enable()` has no fade param (that bool is resetInventory), so the dome pops in
rather than fading. It is 6,000 units up and masked by the sky roll-in at onset, and it never appeared
at all before — fade is polish, not a blocker.

Also clarified from the log: the test used `storm:0` = **Ash Fallout** (near=1200), a lighter haze BY
DESIGN, not the thick brownout the Dust Storm (near=150) produces — so part of "never engulfed" was Ash
being Ash. The `SetStage(...) result=false` lines that remain are the (now-vestigial) Papyrus shake path.

## 2026-07-14 (pt7): Final weather RE sweep reviewed — validated our approach, no risky changes

Reviewed the final weather RE sweep (Sky/TESWeather/Clouds/Precipitation/Atmosphere/Stars/SkySound/
Region, 103 decompiled functions). The headline finding is a suggestion, not a bug, and the honest
outcome is that our current code is already correct — so this pass validates and documents rather than
rewrites.

**`Sky::SetWeatherExternal` (RVA 0x651A60) — the real public smooth-transition API.** The report flags
our hand-rolled `BeginSmoothWeather()` as "may be replaceable" by a direct call. Checked the integration:
it is NOT a safe drop-in. `SetWeatherExternal` sets overrideWeather/defaultWeather and DEFERS the actual
`currentWeather` swap to the next `UpdateWeather` tick — but our per-tick storm logic reads
`sky->currentWeather == <storm weather>` (the Active-hit handoff at RadEngine_Storm.cpp:967 and
UpdateDustFogRoll's arrival watch) to know the storm sky is live, so it needs `currentWeather` set
IMMEDIATELY, which our direct-swap version guarantees and SetWeatherExternal does not. Replacing it is a
real, testable integration change to code that took the whole purple-sky saga to stabilise — not worth
the regression risk for the one edge it improves. Documented the relationship, the engine RVA, and the
one thing we forgo (the restart-anchor for mid-transition interrupts, Sky+0x394 bit 8 / Sky+0x390 —
a rare cosmetic one-frame jump) in a `BeginSmoothWeather` comment so it is a recorded choice, not an
oversight.

**Other findings — noted, no action (correctly):**
- `Precipitation::Update` -> `TESWaterSystem::ToggleWaterRipples`, rain-% gated: automatic for weathers
  with real precipitation. Our storms use fog + camera VE, not precip particles (the known-open
  dusty-precipitation item), so nothing to call — it would come for free IF/when Acid Rain gets real
  precipitation.
- The three `GetRandomWeather` (climate/weatherlist/region) being one shared weighted-random-with-
  live-global-override algorithm: architectural, informational — our storms are DLL-scheduled, not
  climate-rolled, so N/A.
- The `UpdateColorsFromTemplate`/`UpdateFogFromTemplate` = interior room-lighting (not weather-blend)
  self-correction: already respected — we have never touched template functions for our weather.

## 2026-07-14 (pt6): Camera-effect spawn timing authored (Sky::UpdateEffects finding)

Three more RE findings reviewed; acted on the one that was a real, verifiable win.

**[USED] Camera-VE spawn/despawn thresholds.** `Sky::UpdateEffects` (RVA 0x655280) confirmed the Dust
Storm's camera-attached dust VE (`referenceEffect` = FXCameraAttachExplosionDustVE) does NOT spawn at
transition start — the engine waits until `currentWeatherPct >= weatherData[kBeginEffect]/255`
(`weatherData[0xF]`, CommonLibF4-named), and tears the outgoing one down at
`>= weatherData[kEndEffect]/255` (`[0x10]`). Both are per-record CK bytes our generator left
unauthored, so the blowing dust appeared at a random point in the roll-in (or, if the byte was high,
barely at all). `EnsureEslLoaded` now authors them per storm (begin=40 ~0.16, end=100 ~0.39) so the
camera dust rolls in EARLY with the storm and clears promptly when it ends. Harmless on storms with no
referenceEffect (nothing to gate). Indices CommonLibF4-named + decompile-confirmed; magnitudes are a
design knob, logged per storm.

**[NOT USED, deliberately] `Sky::UpdateAurora` bit + `Sky::SkipWeatherTransition`.** Recorded but no
action:
- The new `weatherData[kFlags]` bit `0x10` (`WeatherDataFlags::kPermAurora`, the aurora time-window
  gate; the byte now has three confirmed bits: 0x4 rainy, 0x10 aurora-window, 0x40 rain-occlusion) and
  the same-aurora identity-skip are moot for us — we already CLEAR the aurora model on every storm
  (the pt2 purple-sky fix), so there is no aurora to gate or crossfade.
- `Sky::SkipWeatherTransition` is a genuine lighter-than-ForceWeather primitive worth knowing, but we
  deliberately want SMOOTH transitions everywhere now (`BeginSmoothWeather`), so there is nowhere it
  improves current behaviour. Noted for future use.

## 2026-07-14 (pt5): Storms now have WIND (decompile-verified pipeline) — they were dead calm

The RE `README-Weather.md` round-3 pass mapped the full wind pipeline (`Sky::UpdateWind`, RVA
0x650190), and it exposed a real hole: our storms had **no authored wind at all**, so they were dead
calm — clouds didn't scroll and there was no shader wind, which is wrong for what are literally
wind-driven dust / ash / chem storms.

Four `weatherData[]` bytes drive the global shader wind (indices confirmed against CommonLibF4's
`WeatherData` enum AND the decompile; all normalized 0-255, all weather-transition-blended like fog):
- `[0]` `kWindSpeed` -> `BSShaderManager::fWindMagnitude` (cloud scroll + shader wind)
- `[0x13]` `kWindTurbulence` -> `fWindFrequency` / `fWindMinSpeed` / `fWindMaxSpeed` (gust frequency + speed variance)
- `[0x11]` `kWindDir` -> `fWindAngle` (base direction)
- `[0x12]` `kWindDirRange` -> direction jitter (samples 4 offsets in range, keeps the one nearest the base)

`EnsureEslLoaded` now authors all four per storm, by theme (alongside the ThunderFreq clamp and the
pt4 TransDelta): Dust Storm howls (speed 220 / turbulence 200), Ash 200/180, Firestorm 200/175, Sulfur
150/140, Acid Rain 135/130; base heading 64, jitter 96 for all. `weatherData` is `int8_t` but the
engine reads each byte UNSIGNED, so the values are written as the byte pattern of the 0-255 number.
The indices are decompile-verified; the magnitudes are a design knob (the transMin/transMax engine
constants that convert byte -> real units are still unnamed, so tuned by feel — but consistent and
tunable now, which they were not before). Logged per storm.

## 2026-07-14 (pt4): Author the weather TRANSITION-SPEED byte (decompile-verified from RE)

Reviewed `ReverseEngineering\Organized\1.10.163\README-Weather.md` (decompile-verified `Sky::` engine
truth) against our weather code and acted on the one solid, safe win.

**Transition speed is now deterministic.** `Sky::UpdateWeather` (RVA 0x64E530) ramps `currentWeatherPct`
0->1 over a duration of `weatherData[kTransDelta]/255 * (transMax - transMin) + transMin` in-game hours —
`weatherData[3]` (`TESWeather+0xA6F`, `WeatherData::kTransDelta`) is a normalized 0-255 knob for how long
our `BeginSmoothWeather` cross-fade takes. We had **never authored it**, so the five storms rolled their
skies in at whatever random, inconsistent byte the ESL generator left. `EnsureEslLoaded` now sets it to
a uniform `24` for every storm (alongside the existing ThunderFreq clamp), so every storm's sky rolls in
at the same deliberate pace and it lines up with the ~8 s dome/siren onset sequence. It's a single named
constant (`kStormTransDelta`) — one-line to retune. Logged per storm (old -> new byte).

**Deliberately NOT changed (would have been a guess):**
- fogData[6]/[7] = fog CLAMP (Day/Night, confirmed to exist by the `Sky::UpdateFog` decompile). Our
  `ApplyWeatherFog` doesn't touch it. The dust storm already renders correctly with the record's value,
  and the clamp's zero-vs-no-clamp semantics are not decompile-verified in the README, so writing a
  guessed value could silently cap the storm fog. Left to the record; commented so the next pass knows.
- The transMin/transMax engine constants that convert the byte to real hours are still unnamed, so `24`
  is tuned by feel, not derived from a target duration — but it is now CONSISTENT and controllable,
  which it was not before.

Other README findings noted for later (not acted on): the live-global weather-odds override (`chanceVar`
on climate/region entries — not relevant while our storms are DLL-scheduled, not climate-rolled), and
`cloudLayerDisabledBits` for per-weather cloud-layer hiding. Both recorded in the README if wanted.

## 2026-07-14 (pt3): Storm dome is now OURS + fades in, and the storm onset is sequenced

**1. The dome is standalone.** Assets bundled with permission from `mods\Anomaly` and given our OWN
records, so the dome no longer requires `KORsAnomaly Stripped.esp` to be installed:
- Bundled `meshes\KORsMeshes\Anomaly\CloudArena0*.nif`, `textures\KORsTextures\Anomalies\Arena*.dds`,
  `materials\Knundrum\Anomaly\CloudArenaLightning0*.bgem` (into the mod folder AND `deploy\`).
- New records in `RadiationOverhaul.esp`: `FARO_StormDome01..04` (`000805`-`000808`) carrying
  `CloudArena01_4` / `CloudArena01` / `CloudArena03` / `CloudArena04`. Created as ACTI (the editor
  can't author STAT, and an activator renders identically — the original main layer was one anyway;
  at 8,000+ units overhead there is no interaction concern). Rotation and the lightning are baked into
  the animated NIFs and their materials, so no script/light/godray dependency chain was needed.
- **Master order verified byte-for-byte before and after the save** — still
  `[0] DLCCoast, [1] DLCNukaWorld, [2] Fallout4.esm` (Fallout4.esm is NOT index 0 in this plugin; a
  re-sort would have remapped every Fallout4.esm reference and silently corrupted it). A `.bak_predome`
  backup was taken first.

**2. The dome FADES in and out.** `_SpawnDome` now places each layer with
`PlaceAtMe(..., abInitiallyDisabled = true)` and then `Enable(abFadeIn = true)`, so the engine fades the
dome in instead of popping it into existence. `_RemoveDome` mirrors it with `Disable(abFadeOut = true)`
before deleting.

**3. Storm onset is now a sequence**, so it builds instead of everything landing at once:
1. `BeginStorm` -> the dome starts **fading in overhead** (stage 12: dome only, no shake/nuke; exterior
   only, since a dome mesh would clip through a roof indoors).
2. ~8 s later (`kSirenAfterDomeTicks`) -> the **incoming-storm siren** goes up.
3. Warning ends -> the storm **hits** (shake / nuke / fog / IMAD as before).
The countdown is cleared in `ReleaseStormWeather` so a storm ending inside the fade window can't start
the siren after the fact.

**Papyrus toolchain gotcha (cost three attempts):** `PapyrusCompiler.exe` ALWAYS implicitly imports the
**current working directory**. Run from the workspace root it tries to enumerate the whole tree (dies on
a deep `node_modules` path); run from `%TEMP%` it trips the 260-char limit. It must be run from a clean,
shallow, empty CWD. See `scratchpad\compile_radstormfx.bat`.

## 2026-07-14 (pt2): Storm dome enabled (KORs CloudArena) [FIXED]

The rotating storm dome now actually spawns with a storm. It was blocked by two things, both now gone.

**1. The quest was dead** (fixed earlier today — `InitSearchTables`, see pt1). `_SpawnDome` runs from
`OnStageSet` stages 10/12, which never fired.

**2. The script ships the dome OFF.** `RadStormFX` declares `Bool Property EnableCloudDome = false`, so
`_SpawnDome()` bailed out immediately with "cloud dome disabled (using weather fog instead)". Now flipped
ON at runtime from the DLL.

**The dome itself** (assets used with permission, from `mods\Anomaly`, `KORsAnomaly Stripped.esp`):
- `KORs_Anomaly_CloudArena01` (**Activator**, `026269`) — the main dome, `CloudArena01_4.nif`, bounds
  ±11,000 wide x 11,000 tall. Its `KORsCloudArenaActScript` spawns the arena light + lens flare on
  `OnLoad`, and the rotation/cloud motion is baked into the animated NIF.
- `KORsCloudArena02/03/04` (**Statics**, `02626B` / `026A07` / `026A08`) — the additional layers.

The Papyrus `_SpawnDome` already placed all four, `MoveTo`'d them onto the player, and ran a 2.5 s timer
to keep them centred, with `_RemoveDome` on stage 20 — that code was correct all along, just unreachable.
Its `Game.GetFormFromFile` lookups resolve by plugin NAME, so **no new master is required** (verified all
four FormIDs match, and `KORsAnomaly Stripped.esp` is active in the load order).

**Why the property is set from C++ and NOT by editing the ESP:** `RadiationOverhaul.esp`'s raw TES4
master order is `[0] DLCCoast, [1] DLCNukaWorld, [2] Fallout4.esm` — **Fallout4.esm is not index 0**. The
plugin's FormIDs are built against that mapping so it is self-consistent and works, but a `save_plugin`
that re-sorts masters to load order would remap every Fallout4.esm reference and silently corrupt the
plugin (hang on load, no crash log). Not worth that risk for one boolean.

Instead `EnsureQuestRunning` writes the property on the bound script object under `attachedScriptsLock`
— a locked data read/write, **not** a VM dispatch, so it is safe off the worker-thread Update hook (only
`DispatchMethodCall` / latent natives are unsafe there).

## 2026-07-14: RadStormFX quest was dead — its STAGE TABLE was never built [FIXED]

Root cause of the firestorm nuke and camera shake never once firing, found and fixed.

`TESQuest::SetStage` kept returning **false**. The pt2 theory (dormant quest, `QUEST_DATA.flags` bit 0
clear) was WRONG — the bit was already set. Re-reading the SetStage decompile (RVA 0x5D71E0), past that
bit check it looks the stage up in the quest's **runtime STAGE TABLE**, a scatter map at `TESQuest+0x130`
(probed via `+0x138` / `+0x144`):

```
if ((quest+0xF4 & 1) != 0) {              // enabled -> we already set this
    entry = stageTable[ mask & stage ];   // <-- MISSES HERE
    if ((entry & 0xFFFF0000) != 0xFFFF0000) { ...set stage, notify... }
}
return false;
```

That table is built by **`TESQuest::InitSearchTables()`** (OG RVA `0x5E07D0`). A quest whose ESL was
added to an **existing save** never gets properly started, so the engine never builds it: the `stages`
array loads from the record (0/10/11/12/20 all present — verified in the plugin), but the scatter map is
empty, every lookup misses, and SetStage silently fails. Nothing downstream of `OnStageSet` ever ran.

**Fix:** `EnsureQuestRunning` now calls `InitSearchTables()` once per session (native, void, no args — no
Papyrus, no latent call, safe off the worker-thread hook). Bound by raw module offset since it is not in
CommonLibF4's `IDs.h` and no Address Library id was to hand, so it is guarded by
`REX::FModule::IsRuntimeOG()` — on NG/AE that offset would point at a different function entirely.
`DumpQuestStageTable` now dumps twice (before/after) so the log proves the table goes from empty to
populated.

This unblocks the **camera shake** and the **firestorm nuke** together, since both route through
`SetStage` -> `OnStageSet`. (The cloud dome is deliberately off — the Papyrus script ships
`EnableCloudDome = false`, using weather fog instead.)

**Also learned:** the native `SpawnDistantNuke` path was a dead end and is now understood — it places an
EXPL via `CreateReferenceAtLocation`, which creates a *reference* but never runs the engine's detonation
logic (hence `spawned=true` yet `immediate3D=false` and nothing on screen). A hazard/static renders that
way (the fire ring works), but an explosion must be *fired*. Papyrus `PlaceAtMe` on an EXPL detonates it
properly, which is exactly what the quest's `_SpawnNuke` does — another reason fixing the quest is the
right unlock rather than reinventing the nuke natively. No vanilla ArtObject or placeable carries
`Effects\CG_Nuke.nif`, so there was no shortcut.

## 2026-07-13 (pt6): RadiationEngine.cpp split by subsystem (was 4,277 lines)

Pure code motion — **no behaviour change**. `RadiationEngine.cpp` had grown to 4,277 lines and ~85
member functions covering storms, weather, the gas mask, shelter, tissue, audio and the debug menu all
in one translation unit.

**New `RadEngine_Internal.h`** holds what used to be the file's anonymous namespace: the tuning
constants, the `kStormTypes` table, the co-save record tags, and the small shared helpers (`Log`,
`IsRadstormWeather`, `IsRainyWeather`, `Play2DSound`, `IsPlayerFemale`, `ApplyWeatherFog`,
`RayHitsStructure`, `PerkName`). It uses a named `RadInternal` namespace with `inline` helpers; every
engine .cpp does `#include "RadEngine_Internal.h"` + `using namespace RadInternal;`, so all existing
unqualified references keep working untouched.

| File | Lines | Owns |
|---|---:|---|
| `RadiationEngine.cpp` | 1108 | Core: Initialize, the 4Hz Update tick, PollInput, ESL form resolution, co-save persistence, new-game/post-load reset |
| `RadEngine_Storm.cpp` | 923 | Storm scheduling + phases, fire ring, ghouls, IMAD, quest FX stages, NPC effects, exposure |
| `RadEngine_Tissue.cpp` | 410 | Tissue accrual, permanent scarring, cures, radiation stages, SPECIAL/AV penalties, symptoms |
| `RadEngine_Debug.cpp` | 363 | Debug menu: forced storms/weather, rads/tissue tools, SPECIAL repair, diagnostics |
| `RadEngine_Mask.cpp` | 302 | Gas mask: detection, auto-equip (player + NPCs), filter, condition, put-on fade |
| `RadEngine_Weather.cpp` | 192 | Vanilla-radstorm thinning, engine-native smooth transitions, the dust roll-in |
| `RadEngine_Shelter.cpp` | 98 | Roof-fan cover raycast, powered Pulowski shelters, sealed power armour |
| `RadEngine_Audio.cpp` | 94 | Incoming-storm siren, looping ambient/storm beds |
| `RadEngine_Internal.h` | 443 | Shared constants, `kStormTypes`, inline helpers |

`RadiationEngine.h` (the class declaration) is unchanged, and xmake already globs `src/**.cpp` so no
build-file edit was needed. The link is the proof of a clean split: every declared member resolves to
exactly one definition (a dropped function would fail at its call site, a duplicate would be a
multiple-definition error).

## 2026-07-13 (pt5): Tissue damage now has REAL consequences (was effectively inert)

**The problem.** Tissue damage's only effect was to inflate "effective severity" (`effective = rads +
tissue`), which is mathematically identical to just carrying more rads — so tissue had no identity of
its own, and its only teeth were the small stage END/STR penalties it nudged you into. Worse,
**PERMANENT scarring did literally nothing**: it accumulated, saved to a global, displayed on the HUD,
and, at the time, was described as cured by chelation, yet carried **zero** gameplay consequence (it was
only a floor on tissue healing). That historical cure claim is no longer active: item cures now fail
closed pending a verified source-identifying adapter. That's why chelation felt pointless and tissue felt
weightless.

**Layer 1 — chronic wasting** (always on, scales with `t = tissue / 1000`). Applied as floor-guarded,
tracked `kTemporary` AV modifiers (new `ApplyTrackedAV`, same contract as `ApplyClampedSpecial`: clamped
against the PERMANENT base so it can never drive an AV negative in any gear state, re-derived each tick
rather than stacked, with a dead-band to stop per-tick churn):
- **Carry weight** -120 at full tissue (muscle wasting).
- **healRateMult** -50 at full tissue (stimpaks, food and regen all heal far less).
- **speedMult** -15, ramping in past `t = 0.50` (sluggishness).
- **actionPoints** -40, ramping in past `t = 0.40` (fatigue).

**Death spiral.** Tissue accrual is now multiplied by `(1 + t * 1.5)` — a body already wrecked by
radiation absorbs further damage faster, so letting tissue run away genuinely compounds instead of
being linear.

**Permanent scarring finally bites.** Full scarring costs up to **4 ENDURANCE = lost MAX HP** that you
cannot heal, rest off, or RadAway away. This 2026-07-13 entry originally planned chelation and doctor
restoration, but neither path is active now: no verified doctor or item cure adapter exists. Folded into
the existing floor-guarded END penalty (rather than a second modifier) so the two can never combine to
floor the stat.

Note that because tissue can never heal below the permanent scar, **every** wasting penalty above
automatically floors at the scar level too — scarring is exactly "the damage you can never heal", with
no extra bookkeeping.

**Layer 2 — visceral symptoms**, layered by severity, fits recurring faster the sicker you are (~10-35 s):
- `t >= 0.30` — occasional coughing.
- `t >= 0.55` — nausea (warning + gagging).
- `t >= 0.75` — vomiting fits that cost **6 HP** each.
All native (sound + notify + HP), so it is safe off the worker-thread Update hook — no Papyrus.

Tracked penalties are reset in `OnPostLoad` alongside the SPECIAL trackers (the engine drops
`kTemporary` modifiers on load, so a stale tracker would silently skip re-applying the penalty).

## 2026-07-13 (pt4): Cover system too permissive + Power Armor bug review

**[FIXED][HIGH] Cover: standing in an open street read as "covered".** The roof-fan fires 5 rays (one
straight up, four angled 20 deg off vertical). Cover required only `hits >= 2` and the **straight-up ray
was not required**. The angled rays travel ~263 units SIDEWAYS on the way up, so standing in a street
beside a building, the two rays leaning toward it hit the building's **WALL** (not a roof), scored 2/5
and reported COVERED with completely open sky overhead. Cover from SKY radiation must be OVERHEAD:
the straight-up ray is now MANDATORY (`covered = up && hits >= 3`). Real roofs are unaffected — for a
roof ~200 units up the angled rays cross it only ~73 units out, so any genuine roof (including a small
player-built shack) still lights all four and reads 5/5. Threshold raised 2 -> 3 so a lone pipe/beam
directly overhead plus one wall clip no longer counts. The shelter-debug overlay now prints
"NO ROOF OVERHEAD" when the up-ray misses.

**[FIXED][HIGH] Power Armor granted PERMANENT, TOTAL radiation immunity from every source.** The
sheltered rad-block is a plain rads DELTA refund — it cannot tell where the rads came from. The
sheltered case was correctly gated on `needsShelterEval` (a storm actually being up), but **power armour
and the powered Pulowski shelter were UNGATED**: `blockRadGain = IsPlayerInPoweredShelter() ||
inPowerArmor`. So while suited, the player was rad-immune forever, to everything — eating radioactive
food, drinking dirty water, swimming in irradiated water, standing in the Glowing Sea all refunded to
zero. Fixed: all three blockers are now gated on `needsShelterEval`, so PA/shelter seal you against
STORM radiation (their actual job) while consumables and rad zones behave normally. Vanilla PA already
carries a large rad-resist for zones, and `ApplyStormExposure` separately honours rad-exposure resistance.

**[FIXED][HIGH] PA rad protection now requires the COMPLETE, SEALED rig — helmet mandatory.** The check
required only 4 distinct ArmorTypePower pieces (the comment claimed 5), so a rig missing the helmet AND
a limb still read as "sealed" and got full storm immunity. A suit only shields against radiation if it
is actually sealed, and the **helmet is the crucial piece — it seals the player's breathing**, so an
open-headed frame is not protection regardless of how many limb pieces are on. Now requires a PA piece
filling the helmet slot (FO4 slot 30 / `BIPED_OBJECT::kHairTop`) **and** all 6 distinct pieces
(helmet + torso + 2 arms + 2 legs, `kPowerArmorFullPieces`). Lose the helmet in a fight and you are
exposed — as intended.

**Audited and OK:** `ApplyStormExposure` correctly returns early when sheltered/in PA and otherwise
scales both rads and HP by the player's rad-exposure resistance (hazmat/gas masks meaningfully reduce
storm damage); PA detection via the vanilla `ArmorTypePower` keyword de-dupes FormIDs across slots;
NPC masking correctly skips PA-sealed and ghoul actors.

## 2026-07-13 (pt3): Gas-mask system bug review — filter burned while sheltered [FIXED]

Full review of the gas-mask system (detection, auto-equip, filter, condition, choking, persistence).

**[FIXED][HIGH] The filter drained while the player was SHELTERED / in power armour.** The drain was
gated only on `realMasked`, with no exposure check. Combined with the auto-equip (which dons the mask on
rad-weather entry **regardless of cover**), a player who correctly took shelter for a ~4 min storm still
burned ~80% of a 300 s filter while completely safe indoors — i.e. the system punished correct play, and
players had no way to avoid it short of manually unequipping. Fixed: the filter is now only consumed
while the mask is worn AND the player is actually exposed (`!sheltered && !poweredShelter && !powerArmor`
— the exact exemption the CHOKING block already used).

**[FIXED][MED] The failed-mask rad choke ignored cover.** `(filterDead || maskBroken) && inRadWeather`
applied `ChokeRadsPerSec` with no shelter/PA check (`inRadWeather` is pure FLST weather membership, not
exposure). It was masked in practice because the sheltered rad-block refunded the gain on the NEXT tick,
so it only produced a per-tick rads flicker rather than net damage — but it was inconsistent with the
choking block and depended on the refund to stay correct. Now gated on the same `maskExposed`.

**Audited and found OK:** filter drain math (exactly 100% over `DurationSeconds`); `DurationSeconds`
is already clamped to >= 1 so a `0` in the INI can't divide-by-zero; filter charge + mask condition DO
persist in the co-save (versioned record, v1 saves degrade gracefully); mask detection via biped mouth
slot is mask-agnostic and correct; equip/unequip is native `ActorEquipManager` (worker-thread safe, no
Papyrus); `OnPlayerHit` correctly no-ops when unmasked or already broken.

**Noted, not fixed (lower severity, no code change yet):**
- Auto-remove uses `GetWornMask()`, so if the player swaps to a different mask mid-storm we unequip
  *that* one rather than the one we auto-equipped.
- `_autoMaskedNpcs` tracks NPCs by FormID; NPCs that unload before the storm ends are never unmasked
  and keep the mask indefinitely.
- During the dust WARNING phase `inRadWeather` is forced false, so a mask auto-equipped by a preceding
  rad weather would be auto-removed and then re-equipped at Active (on/off flicker in that edge case).

## 2026-07-13 (pt2): Storms cluster around interior/exterior transitions — veto hole CLOSED

Report: a streamer on a **brand new save** (no reloads) got storms ~10 min apart, correlated with going
between interior and exterior cells. The past-due-reload fix (pt1) can't explain that. Traced it to a
real hole in the **vanilla-radstorm** thinning:

`CheckRadstormWarning` only vetoed a radstorm if it caught it EARLY in its transition
(`currentWeatherPct < kRadstormVetoMaxPct`); if it caught it already-visible it deliberately **fell
through and ACCEPTED** it ("better one storm than a jarring cut"). But **our 4Hz hook does not tick
during a loading screen** — so a radstorm queued by the region system arrives at **FULL pct the instant
the exterior loads**. Outside the 60s cell-exit grace, every such storm was accepted *even though the
60-minute cooldown said veto* — and accepting it set `_lastRadstormSec = _playSeconds`, **restarting**
the cooldown, so the next one came through as well. Net effect: vanilla radstorms cluster on every
interior/exterior transition, which is exactly the reported symptom.

**Fix:** veto at ANY pct. The only reason for the pct gate was avoiding a jarring sky snap — and we now
have the engine-native smooth transition, so the veto reverts SMOOTHLY instead. `BeginSmoothWeather`
gained a `lockOverride` parameter (default true); the veto passes `false` so it does NOT set
`overrideWeather`, leaving the region system in control exactly as the old non-locking `ForceWeather`
did. Hole closed, no visual regression.

Managed storms were audited and cleared: the 4Hz throttle is wall-clock, `_playSeconds` can never run
fast, and every path back to Idle either reschedules or is a debug path — so our own storms cannot fire
faster than the configured `[Storm]` gap.

## 2026-07-13: Storm frequency — the REAL cause of "back-to-back storms" (past-due reload re-arm)

A stream showed storms ~10 minutes apart. Two distinct causes, both now handled:

**1. Released build reads GAME-HOURS, not minutes.** It scheduled on a fixed 4-14 **game-hour** gap.
Fallen World runs a fast timescale, so a game-hour is ~2 real minutes -> 4 game-hours lands at ~10 real
minutes. That is exactly the observed spacing. Dev already schedules on a real-play-minute clock
(`_playSeconds`, wall-clock 4Hz throttle in `Hooks.cpp`, persisted in the co-save) — verified correct,
and there is no game-time path left (`_nextEmissionGlobal` is now only an on/off gate). **Needs a release.**

**2. [FIXED] Past-due reload re-arm was 4-8 minutes — a storm death-spiral.** On loading a save whose
storm schedule was already past due (i.e. ANY save made during or just after a storm), `OnPostLoad`
re-armed the next storm only **4-8 real minutes** out. Storms are lethal, so: player dies -> reloads ->
save is past-due -> storm hits 4-8 min later -> dies -> reloads... Every reload of that save reproduces
it. This is the most likely thing users were actually reporting, and it is present in the CURRENT code,
not just the old build. Fixed: a past-due schedule now re-arms the **full configured gap**
(`ScheduleNextStorm()`, honouring `[Storm] MinMinutes/MaxMinutes`), so a reload can never produce a
short-gap storm.

## 2026-07-12 (pt4): Storm frequency — now player-tuneable via INI ([Storm] section)

Players on the **released build** keep reporting "storms happen too much". Two parts to this:

**1. The released build has the real bug.** It scheduled storms on a fixed **4-14 GAME-hour** gap.
Under a fast (Hardcore) timescale that collapses to almost nothing — storms fire back-to-back, an hour
of non-stop storms. This was **already fixed in dev**: `ScheduleNextStorm` now schedules on a
**real-play-minute** clock (`_playSeconds`, persisted in the co-save), so frequency no longer drifts
with the modlist's timescale. **That fix has never shipped** — it needs a release.

**2. Frequency was hardcoded, so players couldn't fix it themselves.** Added a **`[Storm]`** section to
`FalloutAnomalyRadiation.ini` (the mod already had Filter/Condition/Choking/Ambient sections):
- `Enabled` — `0` turns the mod's own storms off entirely (keeps radiation / gas-mask / filter /
  choking; the debug-force still works).
- `MinMinutes` / `MaxMinutes` — the gap between storms in REAL minutes of play (default `35`-`90`).
  Suggested in the INI comments: `90`-`180` for much rarer storms.
- `VanillaRadstormGapMinutes` — the base game chains its own radstorms every few minutes; we thin them
  by vetoing any that roll in within this window (default `60`).
Values are sanity-clamped so a bad/zero INI entry can't spam storms back-to-back, and the resolved
settings are logged at load. Defaults are unchanged, so existing behaviour is identical out of the box.

## 2026-07-12 (pt9): Firestorm strike overload fixed [DEPLOYED]

- The 22:25 crash was an `XAudio2` audio-worker access violation immediately after the third huge
  strike. The custom explosion records contain no explicit sound links, but stacking several giant
  vanilla explosion effects still overloaded Fallout 4's sound/effect work.
- Firestorm now spawns exactly one zero-damage original Rad Mod strike: `FARO_EmissionStrikeAlpha`
  (0x885), using the vanilla `Effects\CG_Nuke.nif` visual. The unused Beta record remains in the
  plugin for later independent tuning but is not spawned.
- The one strike now lands 6,000–7,000 units ahead of the player, instead of 3,000–5,000, and all
  staggered follow-up strikes were removed. This preserves the sky-engulfing fire while preventing
  overlapping blast/audio load and player-side detonation framing.

## 2026-07-12 (pt8): NAC study converted to clean-room implementation; no NAC assets ship

- NAC was inspected only to understand its high-level behavior: forward-facing randomized strikes,
  two visual variants, and event-driven staggering. No NAC mesh, sound, script, texture, or record is
  included in Rad Mod.
- The briefly copied research NIFs were removed from both package and live mod before this build.
- Added original Rad Mod explosion records `FARO_EmissionStrikeAlpha` (0x885) and Beta (0x886),
  referencing only vanilla Fallout 4 `Effects\CG_Nuke.nif` and `Effects\Inst_Nuke01.nif`.
  Both are zero-damage/zero-force visual records. Native C++ independently implements one forward
  strike followed by four randomized strikes at 6–15 second intervals.

## 2026-07-12 (pt7): Broken cinematic NIF removed; stable multi-Fat-Man blast ring [DEPLOYED]

- In-game screenshot proved the four-way cinematic NIF renders as intersecting hemispheres/sheets,
  not a mushroom cloud. Removed `FARO_FirestormHorizonBurst.nif` from package and live mod.
- Form 0x885 now uses vanilla `Effects\ExplosionNuke01.nif` and `ExplosionNukeImod`, with Damage=0
  and Force=0. Firestorm spawns eight effects evenly around the player at radius 3,600, producing a
  large surrounding detonation from stable vanilla visuals while storm exposure remains the only
  gameplay-damage path.

## 2026-07-12 (pt6): SLBM reference spawned but did not render — origin moved into loaded grid [DEPLOYED]

- Live log proved form 0x885 resolved and `CreateReferenceAtLocation` returned a valid handle at
  13,633 units, but no nuke appeared. That position is outside the loaded exterior-cell grid: a valid
  unloaded reference is not evidence that its 3D was instantiated.
- Detonation origin moved to 3,500–5,000 units. The 360-degree NIF is roughly 9,500 units across, so
  it remains horizon-scale. Z now matches the player's terrain level instead of floating +500.
- Spawn log now includes destination cell FormID and `immediate3D`; this distinguishes cell-streaming
  failure from explosion-object instantiation on the next test.
- Separate confirmed result: quest stages array/table are now fully populated, but `SetStage(10)`
  still returns false and Papyrus receives no `RadStormFX.OnStageSet`; quest initialization remains
  unresolved and is not the cause of the native nuke visual failure.

## 2026-07-12 (pt5): Firestorm blast upgraded to SLBM-scale 360-degree mushroom cloud [DEPLOYED]

- Broke down `YangtzeRedux.esp`: its missile hits an invisible `FatManExplosion2` primary
  (20,000 damage / 3,000 radius), which places `CGPrewarNuke12`, the cinematic prewar mushroom
  cloud with a 1,250,000 imagespace radius, custom long-range audio, and a persistent deadly-radiation
  hazard. We do **not** inherit those lethal gameplay values; Rad Mod's storm exposure remains the
  sole damage authority.
- Inspected `CG_Nuke360.nif` with `niftool`: 88 effect shapes, no collision, vanilla effect textures.
  It is four rotated copies of the 22-part cinematic nuke (fireball, pillar, ground smoke, wave,
  atmosphere), fixing the vanilla `CG_Nuke.nif` half-model that only reads correctly from one side.
- Added `RadOverhaul_FirestormSLBMExplosion` (`0x885`): uses
  `Effects\FalloutAnomaly\FARO_FirestormHorizonBurst.nif`, `ExplosionCarNukeImod`, and the nuke
  light, but has
  Damage=0, Force=0, no placed radiation hazard, and no embedded sound (the existing blowout impact
  audio remains authoritative). The DLL now spawns this form instead of `FatManExplosion`.
- NIF copied into both package and live mod. ESP rebuilt/deployed; `fo4recordedit` confirms 198
  records and reports 0 deleted, 0 undeclared-master references, 0 type collisions. DLL build clean,
  deployed at 856,576 bytes.

## 2026-07-12 (pt4): Safe distant nuke + quest stage-table repair candidate [DEPLOYED, NEEDS GAME TEST]

- Chose **no missile yet**. Firestorm now spawns vanilla `FatManExplosion` (`Fallout4.esm`
  `0x1A7FF2`) directly with the already-proven `CreateReferenceAtLocation` path, 12,000–16,000
  units from the player as a small airburst. No `Projectile::Launch`, hardcoded RVA, or hidden-return
  ABI is involved. Stage 11 is no longer requested, so repairing the quest cannot double-detonate
  the old Papyrus nuke; firestorm requests stage 10 for shake only and rolls the dome separately.
- `fo4recordedit` MCP was installed in Codex and used to compare quest `0x871` against vanilla
  `MS02MiscObjective`. The RadStormFX quest had five stage shells but **zero log entries on every
  stage**; the working vanilla stage has a real `LogEntries[0]`. Added inert note-only anchor entries
  to stages 0/10/11/12/20 in both Spriggit source and `gen_esl.py`, forcing the loader to materialize
  real stage records without adding objectives or fragments.
- Added one-shot `[StormFX/StageDiag]` logging of the live `TESQuest::stages` array and
  `BSTSmallIndexScatterTable` (pointer/size/mask/entries). This will prove whether the repaired ESP
  populates the runtime table before the first `SetStage` call.
- Rebuilt ESP: 197 records; `fo4recordedit check_plugin` reports 0 deleted, 0 undeclared-master refs,
  0 type collisions. Build/package/live ESP hashes match. DLL built clean and auto-deployed
  (856,576 bytes).
- Test: load outdoors, force Firestorm. Expected log: non-empty StageDiag array/table,
  `[Storm/Nuke] ... succeeded`, then `SetStage(10) result=true`. Visually: distant FatMan blast,
  quest camera shake, then the existing 12-second blast-wave/fog/fire roll-in. No missile yet.

## 2026-07-12 (pt3): Firestorm nuke STILL not firing — Papyrus quest is a dead end [OPEN]

**Shipped and confirmed working this session:** engine-native smooth weather transitions for all five
storms (user: "transition was better", "pink sky is done"), aurora cleared on all storms (cleaner skies),
smooth debug end/clear.

**STILL BROKEN — the firestorm nuke never appears.** Status after the pt2 quest fix:

- The pt2 theory (dormant quest, `QUEST_DATA.flags` bit 0 clear) was **WRONG**. The runtime log shows
  NO `"Quest 0x871 was dormant -> enabled natively"` line, which means the enabled bit was **already
  set** — yet `SetStage(11)` **still returns false**. So the failure is NOT the enabled bit.
- Per the `TESQuest::SetStage` decompile (RVA 0x5D71E0), the only other way it returns false is the
  **runtime stage-table lookup** (`quest+0x138` scatter table, mask at `+0x144`) failing to find the
  stage — even though the quest record defines stages 0/10/11/12/20 and the script handles them. The
  quest's stage table appears not to be populated at runtime (suspected ESL quest-init issue).
- Consequence: **every** `SetStage` fails, so the Papyrus `RadStormFX` quest never runs ANY of its FX —
  the nuke, the cloud dome, and the camera shake have all been silently dead. (The fog/IMAD do work;
  those are native, in the DLL.)

**The effect the user actually wants** (ref: `Yangtze Submarine Tactical Nuke Redux - True SLBM`): a
missile flies over the player's head and detonates a nuke in the distance. Researched — it is entirely
**vanilla forms**; the Yangtze mod only ships one NIF and overrides these:
- Projectile **`MS02NukeMissileProjectileFar`** (`Fallout4.esm 0x065DF4`) — model `Ammo\Warhead\Warhead01.nif`,
  speed 5000, gravity 5.2, range 15000, flags Explosion + PenetratesGeometry.
- Its impact explosion: **`FatManExplosion`** (`Fallout4.esm 0x1A7FF2`).
- Yangtze overrides the projectile with long-flight "True SLBM" tuning, so launching the vanilla form
  while Yangtze is installed inherits the long-range flight for free.

**Next step — OPEN DECISION (user was asked, not yet answered).** Three paths:
1. **Native `Projectile::Launch`** (static, OG RVA `0x140FCA260`, takes `ProjectileLaunchData` which IS
   exposed in CommonLibF4, 0xA0). Gives the full flyover + distant nuke. Blockers: not in CommonLib's
   `IDs.h` (no Address Library id found → would need a hardcoded OG offset + a runtime/version guard),
   and the return is `BSPointerHandle<Projectile>` which is **non-trivial** (`~BSUntypedPointerHandle`
   calls `reset()`), so it uses the MSVC **hidden-return-pointer ABI**:
   `ProjHandle* Launch(ProjHandle* ret /*RCX*/, const ProjectileLaunchData& /*RDX*/)`. Untested → CTD risk.
   Off-thread safety looks fine (the fire ring already does native `CreateReferenceAtLocation` from the
   same 4Hz worker hook).
2. **Distant nuke blast only** (no flyover) using only proven-safe primitives
   (`CreateReferenceAtLocation` + `ApplyArtObject`, both already working here). Low risk, lesser effect.
3. **Keep debugging the quest** — add a runtime diag dumping the quest's `stages` array / stage table to
   find why the lookup fails; if fixed, Papyrus `_SpawnNuke` runs on the main thread (safest place to
   fire a projectile).

## 2026-07-12 (pt2): Firestorm nuke never fired (dormant quest) + cleaner storm skies

Two firestorm issues:

**1. Nuke/dome/shake never showed — dormant FX quest.** All storm screen FX route through the
RadStormFX quest (`0x871`) via `SetStage(10/11/12/20)`. The log showed `SetStage(11) result=false`.
Decompiled `TESQuest::SetStage` (RVA 0x5D71E0): it returns false and does nothing unless the quest's
runtime ENABLED bit is set — `if ((quest+0xF4 & 1) != 0)`, i.e. `QUEST_DATA.flags` bit 0. The quest is
flagged `StartGameEnabled`, but that only auto-fires on a NEW GAME; added to an existing save via the
ESL it stayed **dormant** (bit clear), so every SetStage silently failed and the dome/shake/nuke never
ran. `EnsureQuestRunning` was a no-op (the Papyrus `Quest.Start()` path is a latent native that CTDs
from the worker-thread hook). Fix: `EnsureQuestRunning` now sets the enabled bit **natively**
(`quest->data.flags |= 1`) — a plain memory write, safe off-thread — so SetStage succeeds and queues
`OnStageSet` for the VM to run the nuke/dome/shake on the main thread.

**2. Firestorm sky less clean than dust — inherited aurora.** All five storms inherited the radstorm
template's aurora `Sky\RadStormSkyEffect.nif` (a full-sky-dome shimmer that doesn't fit fire/ash/
sulfur/acid/dust). Dust already cleared it (that was half the old "purple"); the others kept it, making
their transitions read muddier. Moved the aurora clear out of the dust-only branch so **all storms**
clear it — each storm's sky is now just its own colours + fog + IMAD, matching dust's clean look.

## 2026-07-12: Smooth transition extended to all storms + storm-clear

Dust Storm's engine-native smooth transition confirmed working in-game (no snap, no pink/purple).
Extended `BeginSmoothWeather` coverage to the rest of the weather cycle:
- **All five storm onsets** (Ash / Firestorm / Sulfur / Acid / Dust) already route through
  `BeginSmoothWeather` at the Active hit (single shared arm point) — verified. The four non-dust
  storms transition in smoothly like dust now, no `ForceWeather` snap.
- **Debug end / clear** (`DebugEndStorm`, `DebugClearWeather` → `ForceClearAndSilence`) now transitions
  to the Commonwealth clear sky via `BeginSmoothWeather(clear)` instead of a hard `ForceWeather` snap —
  sets currentWeather=clear immediately (storm rad effect stops, no lingering) while the sky blends back
  smoothly. Falls back to `ReleaseWeatherOverride` if the clear weather form is unresolved.
- Left as intentional snaps (invisible/corrective, not player-facing transitions): the radstorm
  startup/cooldown **veto** reverts (cut at pct~0, previous sky still on screen), the **indoor**
  storm-end snap (sky not simulated while sheltered), and the **new-game** clear-on-entry force.

Natural outdoor storm-end already used the smooth `Sky::ReleaseWeatherOverride` (engine reverts to the
region weather on its own).

## 2026-07-11: Weather transition — use the ENGINE'S OWN smooth path (RE-derived), not a snap

The snap fix (pt4) killed the sustained purple but was jarring. Decompiled the vanilla weather
transition from `Sky::UpdateWeather` (RVA 0x64E530), `Sky::SetWeatherExternal` (0x651A60) and
`Sky::Update` (0x64DA60):

- The console `fw` command routes through `QueueForceWeather` → `Sky::ForceWeather` (0x651BE0) which
  **snaps** (pct=1, no transition) — that's why forcing any vanilla weather flashed purple.
- Natural/region weather changes use the smooth path: set the target weather and let the engine ramp
  `currentWeatherPct` (Sky+0x360) 0→1 on its own **whenever both `currentWeather` and `lastWeather`
  are set**, blending sky/colours/fog/imagespace over the target's Trans-Delta byte (weatherData[3] @
  +0xA6F). The imagespace blend (`ImageSpaceBaseData::Blend`, gated on Sky+0x38) is the thing that can
  read purple mid-blend — but the engine drives it cleanly with the correct source/target/timer, so a
  proper native transition doesn't smear. My earlier manual `currentWeatherPct` writes **every tick**
  fought that ramp → an invalid held blend → the sustained purple.

**Fix:** new `BeginSmoothWeather(target)` replicates the vanilla setup exactly — `overrideWeather =
target` (lock), `lastWeather = currentWeather` (source), `currentWeather = target`, `lastWeatherUpdate
= currentGameHour` (start clock), `currentWeatherPct = 0` — then **never touches it again**. The engine
ramps it. All storm arm points (natural warning, Active hit, debug preview) now use it instead of
`ForceWeather` + manual pct. `UpdateDustFogRoll` only WATCHES for arrival (pct≈1 / lastWeather cleared)
to fire the wall-arrival dust burst. Result: a smooth, gradual, vanilla-identical roll-in — no snap,
and no held-blend purple. Fog rolls in as part of the engine transition (prev→dust). If the roll feels
too fast/slow, tune the dust weather's Trans-Delta (weatherData[3]).

## 2026-07-10 (pt4): PURPLE SKY — actual root cause found (modlist-wide transition flash)

After five failed asset-side fixes (aurora, imagespaces, IMAD, ENB weatherlist, fog), an in-game
bisection nailed it: forcing **any vanilla weather via console** (`fw 1F61A1` CommonwealthDusty,
`fw 1C3D5E` CommonwealthGSRadstorm) flashes the sky **purple for ~0.5 s** — with **ENB off**. The
purple is a **modlist-wide weather-TRANSITION flash**, not our data. Exhaustive proof that our data is
innocent: every weather colour (deployed record) is brown; all 8 CommonwealthDusty imagespaces are
neutral (tint 0, cinematic 1); the ENB-overridden `LUT_CW_ClearDAY.dds` maps our brown sky/fog inputs
to brown (sampled the actual 256×16 cube); Radiant's `GS01_d.dds` clouds are white; all sky textures
exist in the BA2s. The blend BETWEEN two weathers is what renders purple.

**Why only our Dust Storm looked bad:** the old roll-in held the sky in a weather BLEND
(`currentWeatherPct` ramp + `lastWeather`) for ~10 s, stretching the 0.5 s flash into sustained purple.

**Fix:** stop blending the sky entirely. `ForceWeather` already snaps the dust sky in one frame
(pct=1, brown, no worse than any vanilla forced change); the roll now touches neither
`currentWeatherPct` nor `lastWeather`. Only the FOG rolls in, via direct per-tick `fogData` writes
(engine re-reads fogData live every frame) — a fog write is not a weather blend, so it can't flash
purple. Result: instant brown sky + dust wall closing in over the full duration, and our Dust Storm
is now no worse than any other weather change in the modlist. The residual ~0.5 s flash on ANY weather
transition is a pre-existing modlist issue (affects vanilla weathers, ENB-independent) and is out of
scope for our mod — flagged for separate investigation if desired.

## 2026-07-09 (pt3): Decoupled dust roll — fast sky colour settle + independent fog ramp

The pt2 fixes (aurora clear + imagespace repoint) made the SETTLED dust sky brown (runtime-proven:
`[PurpleDiag] all 8 imagespace slots CommonwealthDusty (brown) = YES` at pct=1.0). What remained was
the several-second **roll-in cross-fade**: `Sky::currentWeatherPct` 0→1 drove the WHOLE sky (colour,
clouds, imagespace) AND the fog on one clock, so the colour couldn't settle fast without snapping the
fog to whiteout — and the transient blend read as purple/pink before settling brown ("fixes itself
after several seconds").

**Fix — decouple the two clocks** in `UpdateDustFogRoll`:
- **Sky colour: fast.** `currentWeatherPct` ramps 0→1 over `kSkySettleTicks=6` (~1.5 s) then HOLDS at
  1.0. Once 1.0 the sky is 100% dust (brown) with no blend → purple-free for the rest of the roll. The
  1.5 s settle blends from a NEUTRAL sky (`_clearWeather`) whenever the outgoing weather is rad-storm/
  rain (purple-prone), so even the settle window can't flash purple.
- **Fog density: slow, independent.** Per-tick `fogData` thin→target lerp over `_dustRollTotal`
  (`kDustFogRollTicks`~10 s debug / `warningTicks`~75 s natural). Engine re-reads fogData live every
  frame, so visibility closes far→near while the sky is already brown = the dust wall rolling in.

New proof log `[RollDiag] t=E/T pct=X fogNear=… fogFar=… hiDens=…` every 8 ticks: pct hits 1.00 by
t=6 while fogNear/hiDens keep climbing — demonstrates both requirements (no purple dwell + density
ramps, not snapped). Applied to all arm points (natural warning, Active handoff, debug preview,
mid-storm). `ReleaseStormWeather` restores the full fog target so an interrupted roll can't leave a
stale thin profile.

## 2026-07-09: Purple sky pt2 — the AURORA was the dome

User test: purple persisted after the pt7 imagespace repoint, and "the actual SKY is purple".
Spriggit dump proves the dust weather's own sky colours are already brown (`#403524`/`#4A3D28`/
`#8A6E42`), so the remaining source is the inherited **Aurora: `Sky\RadStormSkyEffect.nif`**
(`TESWeather+0xF78`) — the radstorm's full-sky-dome shimmer NIF, which renders the purple dome over
the sky colours and is immune to imagespace grading. Fixed by clearing the aurora model at runtime
in `EnsureEslLoaded` (dust branch): logs `[PurpleDiag] Dust weather aurora was 'Sky\RadStormSkyEffect.nif'
-> clearing.` Secondary note: the dust weather's cloud layers still use Glowing Sea textures
(`GS01/GS03/GS04`) — candidates for the (held) CommonwealthDusty cloud copy if any off-colour remains.

### pt2b: purple STILL there after aurora clear — the ENB weather list was the layer nobody owned

Log-verified the aurora clear ran and imagespaces were 8/8, sky/cloud colours are all brown in the
record — yet the sky rendered purple. Remaining cause: the Fallen World **ENB preset grades per
weather** via `enbseries\_weatherlist.ini` (weather FormID -> Weathers/*.ini profile). All five
RadOverhaul storm weathers (`000840`-`000844`) were absent from the list, so ENB applied its base
profile to them (purple night grading). Fix: added `000844` (Dust Storm; plugin is NOT ESL-flagged,
so lower-24-bit match is exact) to `[WEATHER008]` Fog_2 — the same profile as CommonwealthDusty.
Edited both live copies (Stock Folder + Fallen World Optional Mods\Root). If confirmed, map the
other four storms (`000840`-`000843`) to the RadStorm/Fog profiles the same way.

## 2026-07-07 (pt7): Dust Storm real purple-sky cause found — radstorm imagespaces

The purple sky survived disabling the IMAD (pt4) because that was never the (whole) cause. Log +
record inspection found it: the generated dust weather inherited the **CommonwealthGSRadstorm base
template's per-time IMAGESPACES** (`0x224586`-`0x224593`, blue/purple rad-storm colour grades). An
imagespace tints the ENTIRE scene, so it rendered purple regardless of the (correct, brown) sky
colours. Fixed by repointing all 8 time-of-day imagespace slots to vanilla **CommonwealthDusty**'s
brown dust grades (`CW_Dusty*`, `0x1F61A2`-`0x1F61A9`) at runtime in `EnsureEslLoaded` (native
`TESWeather::imageSpace[8]` writes, no ESP surgery). The weather also carries a rad-storm `Aurora`
(`RadStormSkyEffect.nif`) — noted as a secondary suspect if any glow remains.

Also confirmed (log): the Option-B build-up + roll-in mechanically work end to end (`build-up armed`
-> `build-up -> Active roll-in` -> `roll-in complete` -> `heavy-dust burst`).

The storm now visibly builds on the horizon during the Warning phase before the Active roll-in, so
it reads as approaching rather than appearing. Prototype for R. to evaluate before deciding on the
larger Option A discrete-wall feature.

- **Mechanism:** `BeginStorm` force-fades the dust sky in at pct 0, and `UpdateDustFogRoll` ramps
  `currentWeatherPct` **0 -> kWarningBuildupMaxPct (0.35)** across the whole Warning phase (sky darkens
  + horizon hazes, near stays clear), then the Active roll-in continues **0.35 -> 1.0** over ~10 s
  (the arrival + wall-arrival dust burst). One continuous cross-fade; reuses the pct-ramp engine work.
- **Warning-phase timing (what to test):** natural storms = each type's `warningTicks` (Dust = 75 s;
  Ash/Fire 90 s, Sulfur/Acid 60 s); **debug storms = 10 s** (`storm:4`). So a debug Dust Storm builds
  over ~10 s then rolls in over ~10 s; a natural one builds over ~75 s.
- **Gameplay unchanged during the build-up:** the FLST-driven choke / CHOKING health / gas-mask
  auto-equip and the loud storm wind bed are all suppressed while the build-up is in the Warning phase
  (`inRadWeather` + ambient-bed gates), so they still begin only when the storm HITS (Active) — exactly
  as before. Only the sky visuals build during the approach. Dust-only; other storms unchanged.

Layered a short heavier camera-dust gust on top of the steady dust VE at the moment the roll-in wall
reaches full, so the storm "hits" rather than just settling in.

- Engine research (decompiled `Sky::UpdateEffects`, RVA 0x655280) showed the weather's camera VE is
  instantiated ONCE and only re-created when its slot is null, so a live `referenceEffect` swap does
  NOT re-trigger. Instead the burst is fired directly on the player via
  `TESObjectREFR::ApplyArtObject` (REL 357908) with attach-to-camera + inherit-rotation and a 4 s
  self-expiring duration -> reverts to the steady `FXCameraAttachExplosionDustVE` on its own.
- Art: Nuka World's `DLC04CameraAttachDustHeavyAO` (0x0156CB), falling back to vanilla
  `FXCameraAttachExplosionDust` (0x1EB2D5) if Nuka World is absent. Resolved in `EnsureEslLoaded`.
- Trigger hook: `UpdateDustFogRoll()` at roll completion (`_dustFogRollTick` hits 0), exterior-only
  (skipped while sheltered). ApplyArtObject is a native temp-effect spawn, same class as the storm's
  existing off-thread ghoul spawns, so it's safe from the worker-thread Update hook.

In-game test after pt3: the roll-in + camera-dust VE work (log confirms both), but the sky went
purple and the fog was too light / read as a bubble around the player.

- **Purple sky = the IMAD, disabled for dust.** The dust weather's own sky colours are all correctly
  brown, so they weren't the cause. `RadOverhaul_DustStormIMAD` was applied on top and its brown
  `TintColor #553D22` added over the dark-blue NIGHT sky renders purple (brown's high-R + night's
  high-B). Vanilla CommonwealthDusty uses no IMAD at all, so `ApplyStormIMAD` now skips the Dust Storm
  (index 4) entirely and clears any running dust IMAD. This also removes the flat "painted" grade.
- **Far heavier fog.** Dust distance fog tightened Far 4500 -> 2000 (near-whiteout by ~28 m), Near
  200 -> 150, HighDensityScale 0.90 -> 0.97 — a dense brownout with only a ~2 m clear bubble, so it
  reads as all-encompassing rather than a light haze centred on the player. Camera-dust particles +
  brown weather colours carry the colour now that the IMAD is gone.

Follow-up after in-game testing. Three issues, each root-caused with decompile/log evidence:

- **Debug/real weather change snapped the sky (Issue 3).** `Sky::ForceWeather` (RVA 0x651BE0) was
  decompiled: it hard-sets `currentWeatherPct = 1.0` in BOTH the `true` and `false` branches (the bool
  only picks overrideWeather-lock vs defaultWeather) — so it ALWAYS snaps; there is no "transition"
  arg. The old fog-value ramp only moved fog while the sky/imagespace popped. Fixed: at the hit we now
  capture the outgoing sky as `lastWeather`, `ForceWeather(dust,true)` to lock, then drive
  `currentWeatherPct` 0->1 over ~10 s in `UpdateDustFogRoll`, cross-fading the WHOLE weather (sky,
  clouds, imagespace, fog). `Sky::Update` never rewrites `currentWeatherPct` (verified) and the
  override lock suppresses the region system, so nothing fights the ramp. Applied to both the storm
  path AND the `weather:4` debug preview, so both debug buttons show the same roll-in.
- **Incoming-storm siren no longer played (Issue 2).** The cue only ever fired for VANILLA radstorms
  (`IsRadstormWeather` matches only vanilla FormIDs 0x1C3D5E..., not our managed 0x84x weathers), and
  the managed cue had been dropped in the ambient refactor. Restored: `BeginStorm` now plays it
  (`PlayStormCue`), and it fades out when the storm HITS.
- **Siren rang long after the storm ended.** On weather-clear the vanilla path reset its flag but never
  stopped the looping cue. Added a short fall-off (`StopStormCue`, 2 s fade) on weather-clear, at the
  hit, at storm end (`ReleaseStormWeather`), and on the spawn-in-storm intercept. Cue play/stop is now
  a shared `PlayStormCue`/`StopStormCue` pair used by both the vanilla and managed paths.
- **"Not blinding" (Issue 1) — addressed.** Log confirmed the roll reaches full target, so it wasn't a
  roll bug — fog-alone at Far=12000/Power=0.25 is only ~68% at 40 m (a haze). Fixed two ways: (1)
  tightened the dust distance fog to Far=4500 / Power=0.30 (~86% opacity by 40 m = a real wall), and
  (2) attached the vanilla camera-dust `VisualEffect` (`FXCameraAttachExplosionDustVE`, Fallout4.esm
  0x1EB2D6 -> `TESWeather::referenceEffect` at 0xFC0) at runtime — the screen-filling blowing dust that
  actually reads as blinding. Both runtime (no ESP/master-order surgery). IMAD double-tint audit still
  pending an in-game look now that fog + particles do the work.

## 2026-07-07 (pt2): Storm height-fog fixed + Dust Storm rolls in like a real dust wall

The storms rendered as a flat "painted" tint with no depth. Diagnosed via the serialized weather
records + decompiling the engine fog consumer:

- **Root cause:** the generated storm weathers set FogHeightMid but left **HeightRange and
  HighDensityScale at 0** (gen_esl.py:388 never wrote them). With HeightRange 0 the vertical fog
  gradient collapses -> flat uniform fog, so the IMAD tint did all the visible work.
- **Offsets decompile-verified** against `Sky::UpdateFog` (RVA 0x64F950) and `TESWeather::GetFogData`
  (0x658300): `fogData[8..17]` = near/far HeightMid, HeightRange, and HighDensityScale (day/night).
  Near and far height are independent absolute (mid, range) pairs, not base+delta.
- **Reference-learned tuning.** Studied the shipped "Dust Storm and Heat" mod — it adds no weather at
  all (just no-sprint + heat gameplay); the dust *visual* is vanilla **CommonwealthDusty** (0x1F61A1):
  Near 100 / Far 20000 / **Power 0.2** / NearHeightRange **25000** / **HighDensityScale 0.975** +
  camera-dust VisualEffect + weather imagespaces (no IMAD). Huge range + high density = a full-height
  dust wall, NOT a low band.
- **Fix (runtime, all in `EnsureEslLoaded` via new `ApplyWeatherFog` helper):** every storm weather now
  gets real height fog. `StormType` gained `fogHeightMid/Range/HiDensity`. Dust Storm adopts the
  CommonwealthDusty profile (Near 200, Far 12000, Power 0.25, HeightRange 15000, HiDensity 0.90);
  ash/sulfur/acid/fire get a moderate ground-fog band (Range 2000, HiDensity scaled by severity
  0.20-0.30-0.45).
- **Dust Storm roll-in.** New `UpdateDustFogRoll()` (per-tick) ramps the dust fog from near-clear to
  the full wall over ~10 s at Active-entry, so the dust **rolls in toward the player** instead of
  popping to full. The engine re-reads fogData live each frame, so native per-tick writes animate it
  (safe off the worker thread). `ReleaseStormWeather` stops the roll and restores full fog.
- **Still open (reported, not done):** add the camera-dust `VisualEffect` (blowing motes) to the Dust
  weather (ESP edit) and audit whether `RadOverhaul_DustStormIMAD` still over-tints now that fog does
  real work (test-first per plan).

## 2026-07-07: Dust Storm CTD fixed — engine div-by-zero on ThunderFreq=0 (real root cause)

The long-standing "Dust Storm freezes / turns pink" bug is **fixed** — and the previously-recorded
root cause (astral emoji in the HUD banner, 2026-06-30) was **wrong**. Diagnosed properly this pass:

- Instrumented the storm hit with flushing `[HIT] N/4` probes. In-game they showed all four steps
  completing (ForceWeather / TriggerStormFX / ApplyStormIMAD / PushStorm) then a CTD ~1s later — so
  it was never the banner, the records, or the JS bridge.
- The crash log (`crash-2026-07-07-18-21-18`) pinned it: `EXCEPTION_INT_DIVIDE_BY_ZERO` in
  `Sky::UpdateSound` (`Fallout4.exe+0x650917`, `div r8d`), with `R9 = TESWeather* 0xAC000844` (our
  Dust Storm) on `BSJobs::JobThread`. Decompiling `Sky::UpdateSound` (RVA 0x650510): the thunder
  branch computes `BSRandom() % (weather->weatherData[0xA] * SkySound::ucThunderCount)`.
  `weatherData[0xA]` = **kThunderFreq**. Our generated storm weathers carry a Thunder-type sound but
  **ThunderFreq == 0**, so the modulo divides by zero the instant the storm sky goes current. The
  "pink / black-and-white frozen frame" was just the IMAD grade on the last frame before the crash.
- **Fix:** in `EnsureEslLoaded` (where the DLL already runtime-patches each storm weather's fog),
  clamp `weatherData[kThunderFreq]` from 0 -> 100 for every storm weather. Divisor is now safe; the
  high frequency means real thunder ~never fires. Runtime patch, so no ESP/master-order surgery. All
  five storm weathers carried the same bad byte — ash/sulfur/acid/firestorm were equally at risk and
  are fixed too. Diagnostic probes removed after confirmation.

## 2026-07-06 (pt3): Storm ambient beds + general background ambient (debug-force now audible)

Debug-forced storms were silent (`DebugPreviewWeather` = `ForceWeather`, visual only) and the storm
ambient played the loose `My Audio.wav` (a stereo PCM wav the engine can't stream — same gotcha as
the mask foley). New layered ambient-audio system, all `.xwm`:

- **Per-storm bed** — each `kStormTypes` entry gets an `ambient` loop (Firestorm -> `psi_storm_01`,
  Ash/Acid/Dust -> `blowout_wind_01/02/03`, Sulfur -> `blowout_bkgrnd`). Played by the new
  `UpdateAmbientAudio()` **driven by the current weather**, so a debug `weather:N` force plays the
  bed exactly like a real storm (no special-casing). Exterior + exposed only.
- **General background bed** — `envcreepywind.xwm`, quiet, plays in any exterior when no storm is up.
- **Looping** via `EnsureBed()` (re-triggers when the clip ends; switches/fades on change). Owns
  `_stormSoundHandle`/`_ambientBedHandle` entirely — the old per-phase start/fade calls were removed
  so they don't fight the weather-driven manager.
- **`[Ambient]` INI** — `Enabled` (general bed), `Volume`, `StormVolume` (0..100 -> attenuation).
- Radstorm warning cue repointed off `My Audio.wav` to `psi_storm_siren.xwm`.

`My Audio.wav` is now unused (can be pruned). To test: debug menu -> `weather:1` outdoors -> the
firestorm bed plays; `weather:clear` -> back to the general ambient.

## 2026-07-06 (pt2): CTD on putting a gas mask on — Papyrus VM dispatched from the worker-thread hook

Once the foley fix let a mask actually register as worn, equipping one **crashed**
(`crash-2026-07-06-21-28-51`: EXCEPTION_ACCESS_VIOLATION in `VirtualMachine::DispatchStaticCall`,
`call [rax+0x10]` rax=0, R11="FadeOutGame", on `JobListManager::ServingThread`). Root cause: the
put-on screen fade calls `Game.FadeOutGame` through the Papyrus VM, but the 4Hz
`PlayerCharacter::Update` hook runs on a **JobListManager worker thread** — dispatching a Papyrus
call from off the main thread corrupts the VM's per-thread scrap state and null-derefs. Latent since
the fade was added; only triggered now because no prior session ever got a mask detected as worn.

**Attempt 1 (INSUFFICIENT):** deferred both VM dispatches via `F4SE::GetTaskInterface()->AddTask`.
It crashed identically (`crash-...-22-08-14`) — the stack showed the task running under
`Main::UpdateNonRenderSafeAITasks` on a `BSJobs::JobThread`, i.e. **in this runtime the F4SE task
queue also drains on a worker thread**, so AddTask did NOT move the Papyrus call to the main thread.

**Fix (attempt 2, shipped):** eliminate Papyrus from the mask path entirely.
- `CallFadeOutGame` — **removed** (no-op). The `Game.FadeOutGame` put-on/off blink was cosmetic; the
  goggle overlay still appears via `RadHud::PushMask`. If the blink is wanted back, do it as a JS
  black-flash in the PrismaUI radhud view, never through the script VM.
- `SetActorMaskEquipped` — rewritten to the **native `ActorEquipManager::EquipObject/UnequipObject`**
  (the engine's own equip path, the same one AI actor updates use), which is safe on the worker
  thread. Powers the storm auto-equip + NPC masking without a VM call.

Verified: `FadeOutGame` and `EquipItem` string literals are both gone from the built DLL.

Rule going forward: **never dispatch Papyrus (DispatchStaticCall/DispatchMethodCall) from the 4Hz
Update hook — and AddTask does NOT make it safe here (the task queue runs on a worker thread too).**
Use native engine APIs (ActorEquipManager, ModActorValue, ForceWeather, BSAudioManager) instead; the
script VM is the only thread-fragile piece.

## 2026-07-06: Gas-mask foley was silent — loose stereo WAV doesn't play; converted to XWM + instrumentation

In-game the whole gas-mask slice read as dead (no foley, no choke VO, no fog). Diagnosed from the
live log + binary, NOT by guessing: ruled out a stale DLL (md5-identical, feature strings present),
INI parse (`[Choking]` parses every load), FLST membership (0x802 correctly contains 0x840-0x844 +
vanilla radstorms), and shelter-stuck (`Shelter->exposed` transitions fire). Root cause:

- **Loose stereo PCM `.wav` never plays through `BSAudioManager::GetSoundHandleByFile`.** The engine
  streams `.xwm` fine in the *identical* code path (every storm sound is xwm), but the handle comes
  back invalid for the 12 mask WAVs (44.1kHz/16-bit/**stereo**) — silent, no crash. **Fix:** converted
  all 12 to `.xwm` with `xWMAEncode` (repo + live), pointed the path constants at `.xwm`, removed the
  wavs. `Play2DSound` now logs a `[Sound]` warning on an invalid handle instead of swallowing it.

Two of the four reported symptoms were **not bugs**, just untestable in the short test sessions:
the **visor fog** needs <30% filter (drains over 300s; sessions were 38-85s of outdoor play), and
the **managed storms** (Firestorm/Sulfur/Acid/Dust) never auto-fire because the storm clock never
reaches `next=5377s` (~90 min) — only vanilla radstorms occurred.

Instrumentation added (priority: make it testable):
- `[ChokeDiag]` — 1 Hz log of every mask/choke gate (current-weather FormID, in-FLST, stormActive,
  realMasked, enclosed/sheltered/PA/poweredShelter, filter/cond, chokeTicks) while the `shelter`
  debug toggle is on. Reveals *why* choke does/doesn't fire (e.g. whether `ForceWeather` actually
  flips `sky->currentWeather` into the FLST).
- `mask:scan` debug command — dumps every worn ARMO and the biped slots it fills, flagging slot 44
  (kMouth). Decides whether the player's gas mask is even detectable (detection keys ONLY on kMouth).

## 2026-07-03 (pt2): Gas-mask foley + choking health damage + low-filter visor fog

First slice of the GMW port (gap analysis in GMW_GAP_ANALYSIS.md; remaining items tracked in
GASMASK_ROADMAP.md).

- **METRO EXODUS mask foley** copied into `Sound\FalloutAnomaly\mask\` (self-contained; 12 WAVs
  from the enabled GMW tweaked-audio mod) and wired natively: draw/withdraw on every worn-state
  transition (male/female), filter-replace takes (full on manual, fast on auto, alternating),
  choking one-shots (M/F x masked/unmasked) on a ~6-11 s randomized throttle.
- **Choking health damage** (new INI `[Choking]`, default on): breathing rad weather while exposed
  now damages HEALTH - 1.5/s with no mask, half rate when masked but the filter is dead or the
  mask broken (rads choke unchanged on top). Sheltered / powered shelter / full power armor exempt.
  One corner notify per exposure episode.
- **Low-filter visor fog**: condensation layer in radhud.html driven by the existing filter push -
  builds below 30% charge, maxes at 0%, fixed haze when the mask is broken, clears when unmasked.
- Deployed: views + INI + sounds live; DLL build clean but the live copy was FILE-LOCKED (FO4
  running) - close the game and re-run `xmake` (or copy `build\release\FalloutAnomalyRadiation.dll`)
  to finish the deploy.

Same-day bug pass on this slice (all fixed + rebuilt):
- Roof-shelter raycast now also refreshes for FLST rad weather (`inRadWeather` folded into
  `needsShelterEval`, hoisted before it) - previously `_playerEnclosed` could be stale/false for
  FLST weathers outside a managed storm or vanilla radstorm, so a player under a built roof could
  wrongly choke (and miss the sheltered rad-gain refund).
- Choking grace + hysteresis: no notify/VO for the first ~2s of an episode (lets the rad-weather
  auto-equip mask up without a spurious gasp on storm entry), and ~10s of sustained safety before
  they re-arm (no spam when flickering at a cover edge). Damage still applies from the first tick.
- Choking trackers reset in OnPostLoad so a different save doesn't inherit grace/hysteresis state.
- Verified: all 12 copied WAVs are engine-compatible PCM 16-bit 44.1kHz; visor-fog JS guards the
  -1 "system off" sentinels; fog z-order sits above the mask overlay and below all widgets.

## 2026-07-03: Full review + same-day fix pass; ghoul-race remnants purged end to end

Two-agent review (engine core; HUD/lifecycle/data) + hand-verified ghoul audit, then every finding
fixed same-day (issue list + OUTCOME in docs/CODE_REVIEW_2026-07-03.md). Build clean; DLL, views,
INI, and stripped ESP deployed to the live modlist.

- **HIGH fix - rads erased on save-load:** OnPostLoad reset `_prevRads` to 0 so the sheltered
  rad-gain refund read the whole saved rad pool as "gain since last tick" and wiped it whenever the
  player loaded in power armor / a powered shelter / sheltered rad weather. Now -1 sentinels skip
  all delta logic on the first tick.
- **Storm save-load fixes:** first-load FX stop latched until forms resolve (persisted Papyrus
  rumble loop from a mid-storm save now actually stops); a past-due storm schedule re-arms 4-8 min
  out on load instead of guaranteeing an instant new storm every reload.
- **Ghoul-race system fully purged:** QUST 0x870 + GLOB 0x854 stripped from the shipped ESP
  (backup kept), gen_esl.py no longer generates them (FormIDs reserved), esl_src records deleted,
  stub RadGhoulQuest.pex + optional/tooling RadOverhaul_GhoulRace.esp deleted. Stage 10 keeps only
  its "Ghoulification" label (plain END-7/STR-5 debuff).
- **Dead vitals pipeline excised** (the deferred H1 follow-up): RadHud::Push/updateRad, prefs
  save/load, ToggleHud + ToggleKey INI + PollInput branch, ~250 lines of JS, the CSS vitals block,
  and 4 orphan PNGs. Resolves M8 (sync INI writes on the JS thread) by removal.
- **Logger moved to the standard path:** Documents\My Games\Fallout4\F4SE\FalloutAnomaly.log
  (protected game dirs no longer silently null-sink all logging).
- **Deploy package now reproduces the live mod:** + RadStormFX.pex and a recovered decompiled
  RadStormFX.psc (source existed nowhere), + GearPatch/SurPatch ESPs, + RobCo loot ini, + full
  storm sound library; removed the stale RadiationOverhaul.esl, the orphan radstorm_warning.wav,
  and the superseded FW_GasMaskFade script (the C++ fade replaced it). data\thresholds.json
  re-synced from the canonical deploy copy (it was the pre-rebalance Jun 5 table).
- **Smaller fixes:** NPC auto-mask leak (unmask walks the tracked set), DebugForceStorm now tears
  down the previous storm's FX, null/missing thresholds.json is loud and clears stage effects,
  RadHud DOM-ready Show/Hide race guarded, `_domReady` flags atomic, wait-thread 10-min cap, dead
  siren/IMAD-tint/`_ghoulPackCellID`/config-field code removed, INI rewritten (Shift+F8 documented,
  dead [HUD] section dropped), stale comments corrected.

## 2026-06-30 (pt2): Full code-review pass - fixes, cleanup, ghoul-migration removal

Four-pass full-codebase review (tracked issue list in docs/CODE_REVIEW_2026-06-30.md). Build is
clean with zero warnings after this pass.

- **Removed the dead player-ghoulification migration** now that the feature is gone:
  `RevertGhoulIfDisabled` + the `_ghoulQuest`/`_ghoulGlobal` resolution + `kHumanRaceFormID` + both
  call sites. Eliminates the C4463 signed-bitfield warning (`raceSwitchPending = 1`). The separate
  live storm-ghoul-spawn feature (feral ghouls emerge in storms, ghoul NPCs heal in rads) is kept.
- **Performance (4Hz hot path).** Cached the spdlog logger once (`Log()` helper) instead of a
  global-mutex registry lookup on every call across ~40 sites; throttled the per-tick gas-mask
  filter inventory scan to at most every 8 ticks and only while masked.
- **Build.** Added an `after_build` auto-deploy to the live modlist (honours `XSE_FO4_MODS_PATH`),
  so a build can no longer leave a stale DLL in place.
- **HUD JS robustness.** The vitals HUD moved to PrismaHUD_F4 and `#hud` was deleted from the view,
  but `radhud.js` still dereferenced it unguarded (debug-menu open, load-time layout apply). All
  `#hud` access is now null-guarded and the dead per-tick `updateRad` early-returns. (Real bug, but
  not the storm freeze - it fires at menu open, which completes.)
- **Truth-up.** `Initialize()` no longer logs "10 stages / Items 10+ / Perks 10+ / Zones 10" (the
  DLL loads only `thresholds.json`, 6 stages); removed the dead `items/perks/zones.json` from the
  shipped folders; repointed stale `Non Prisma Projects\Rad Mod` paths in the ESP generator, the
  sound-convert script, and the README to `Projects\Rad Mod`.
- **Small correctness/clarity.** `static_assert(kStormTypeCount == kStormTypeTableSize)`; explicit
  `SetStage` cast (C4244 gone); `DebugAddTissue` uses `kTissueMax`; dust `fogNear` 900 -> 960 (off
  the fog-dome threshold); logger init hardened with a null-sink fallback; stale storm comments fixed.
- **Correction to the prior entry below:** swapping the `🌫` emoji for `☁` did NOT resolve the Dust
  Storm freeze - it still hangs. Investigation now localizes the freeze to the PrismaUI JS-to-native
  bridge at button-click time (the storm C++ code provably never runs); a DIAG-probe build is
  deployed to confirm. See docs/CODE_REVIEW_2026-06-30.md.

## 2026-06-30 — Dust Storm blowout + proper rolling fog (5th storm type)

Researched NAC, Fog of War, and the CK wiki for FO4 weather/fog. Findings: Fog of War's "dust
storms" are just Climate/Region re-rolls of a True Storms weather (a dependency); NAC's heavy fog
uses short far-distance + brown tint + its own SPGD precipitation. Built a self-contained version.

- **New 5th storm type — Dust Storm** (`RadOverhaul_DustStorm` 0x844, theme 5). GMW-style blinding
  dust blowout: warm tan/brown fog, near-zero visibility wall (DLL fog near 900 / far 2600 / pow
  0.55 — the densest of all storms), driving wind (92%), no lightning, brown darkening IMAD
  (`RadOverhaul_DustStormIMAD` 0x884). Counts as rad weather (added to FLST 0x802) so the gas mask
  auto-equips. Ambient hazard (no shelter-death). HUD banner "DUST STORM — BLINDING DUST · MASK UP".
- **Proper rolling fog across all storms.** The DLL previously stomped every storm with one uniform
  fog (near 2000 / far 10000) — far too wispy to read as a storm. Now fog is **per storm type**
  (`kStormTypes[].fogNear/Far/Power`), each a real visibility wall.
- **Hard-freeze root cause: an astral-plane emoji in the HUD storm banner.** The Dust Storm froze the
  game on trigger ("purple sky" = frozen mid-frame). Bisected via the debug buttons: the weather
  PREVIEW (weather:4) and the Ash/Sulfur storms were all fine — only the full Dust *Storm* froze. The
  dust weather + IMAD records are byte-identical in structure to the working Ash storm, so they were
  ruled out. The only thing unique to the dust storm at runtime is `PushStorm(theme=5)` -> the HUD
  banner, whose label used `🌫` (U+1F32B, an astral-plane surrogate pair). Every other theme uses BMP
  symbols (⚡☣⚗☢); astral-plane chars can hang Ultralight's text shaper, and PrismaUI composites that
  surface into the game frame -> whole-game freeze. Fixed by swapping `🌫` for the BMP `☁`. **Rule:
  never put astral-plane (4-byte UTF-8 / surrogate-pair) characters in a PrismaUI/Ultralight view.**
- **Blowing-particle precipitation was also removed (separate, precautionary).** An earlier attempt
  wired the weather `Precipitation` to SPGD particles using vanilla `CameraAttachWeatherParticlesDusty.BGEM`
  — a camera-attach EFFECT material, not a precipitation material. This was NOT the freeze cause
  (removing it didn't fix the freeze) but it's still the wrong material type, so the SPGD records were
  removed. Real blowing dust needs a shipped weather-particle BGEM+DDS — deferred. The Dust Storm
  blowout is delivered by heavy brown fog + the darkening IMAD (same stable mechanism as the Ash storm).
- **Build:** spriggit serialize of the live ESP (captures the MCP-authored FLST/MISC the old
  `esl_src` lacked) -> add dust Weather/IMAD/SPGD + FLST entry -> deserialize. `esl_src` promoted to
  the complete tree; `gen_esl.py` updated with the dust storm + a divergence warning. check_plugin
  clean (201 records, 0 issues). DLL rebuilt (kStormTypeCount 4->5). Debug menu: "Dust Storm"
  (storm:4) + "Dust" weather-preview (weather:4).

## 2026-06-28 (later 2) — Full Power Armor suit fully blocks radiation

- Storms and ambient rad sources were still chipping rads/HP off the player while in Power Armor.
  Vanilla rad-resist piped through the existing `kStormResistHalf` (80) shelter formula only blocks
  ~93% at PA's ~1000 resist, leaving a constant bleed-through.
- Now: if the player has >= 4 distinct armor pieces equipped that carry the vanilla `ArmorTypePower`
  keyword (Fallout4.esm 0x4D8A1), they're considered to be in a full PA suit. While in PA:
  - All rad GAIN is blocked at source (same path as Pulowski powered shelter) — storms, rad zones,
    irradiated food/water. Existing rads are not healed; use RadAway.
  - Storm HP damage (`ApplyStormExposure`) is also fully suppressed.
- Threshold of 4 (not 5/6) means losing the helmet for visibility still leaves you protected; only
  taking off 2+ pieces drops you out of "full suit" coverage.
- New: `IsPlayerInPowerArmor()` helper + `_armorTypePowerKeyword` resolved on ESL load.

## 2026-06-28 (later) — Code review pass: ghoul removal cleanup + actor-identity fix

Full code review of the radiation engine after the player-ghoulification removal. Findings applied:

- **HIGH:** `Hooks.cpp` actor-identity check used pointer compare (`a_this != GetSingleton()`),
  which can false-negative under multiple inheritance on PlayerCharacter — same class of bug that
  bit Permadeath. Replaced with `GetFormID() != 0x14`.
- **Player-ghoulification dead code removed:** `Ghoulify`, `UpdateGhoul`, `DebugUnghoul`, the
  Update() ghoul branch, the `_isGhoul` guards in `ApplyStormExposure` / `ApplyDebuffs` /
  `DebugFixSpecial` / `DispelNegativeSpecials`, the threshold check in `UpdateTissueDamage`,
  the OnPostLoad re-flag block, the `"ghoul"` / `"unghoul"` debug commands, `RadHud::SetGhoul`
  + `window.ghoulMode` JS stub, the "Turn Ghoul" / "Revert" debug-menu buttons. All ghoul-tuning
  constants (`kGhoul*`) dropped. Engine source ~150 lines lighter.
- **Migration preserved:** `RevertGhoulIfDisabled()` is kept as a one-time migration that runs on
  every load — clears the legacy `RadOverhaul_IsGhoul` global, advances the legacy quest to its
  reverted stage, and swaps GhoulRace -> HumanRace if a save still loads with the player ghoulified.
  Idempotent for human saves (one form lookup). Safe to delete in a future release.
- **ESP records:** `RadOverhaul_IsGhoul` (0x854) and `RadOverhaul_GhoulQuest` (0x870) are now
  orphans in the ESL. Left in place; rebuilding the ESP just for this is not worth it.
- **Storm feral-ghoul packs unaffected** (`SpawnStormGhouls`, `CheckGhoulPackCleanup`,
  `IsGhoulActor`, `ApplyStormPeakToNPCs`). That is a separate enemy-NPC system.
- **Debug menu HTML/JS:** GHOULIFICATION section removed in both `deploy\` and the live MO2 mod.

## 2026-06-28 — Pip-Boy menu no longer pauses radiation / storm damage

- **You could cheese radstorms by sitting in the Pip-Boy.** Our 4Hz throttle in
  `PlayerCharacter::Update` accumulated the engine-supplied `a_delta`, which FO4 zeroes for actor
  ticks while a fullscreen menu (Pip-Boy / map / console) is open. The accumulator never advanced,
  so `RadiationEngine::Update()` never ran and `ApplyStormExposure` stopped applying rad / HP
  damage. NPC hits still hurt you in menus (those come from hit events, not the actor tick), which
  matches Fallen World's "menus do not pause time" behavior — radiation should match it too.
- Fix in `Hooks.cpp`: throttle off `std::chrono::high_resolution_clock` wall-clock instead of
  `a_delta`. Engine ticks at 4Hz regardless of menu pause-state. Storm pacing already advances by a
  fixed 0.25 per tick (`_playSeconds`), so schedule timing is unchanged.
- No new tunables. Live-tested: build + deploy clean; needs in-game verification that storm rads /
  HP loss continue while the Pip-Boy is open.

## 2026-06-27 (later 2) — HUD vitals empty-grey-bars fix

- **HUD vitals showed as empty grey bars for some users.** `RadHud::Push` gates all vitals data on
  `IsPlayerInWorld()`, which only latched once a Pip-Boy was detected in the `kPipboy` biped slot.
  Alternate-start mods / Pip-Boy replacers never tripped it, so `updateRad` was never called and the
  bars sat at their 0% default — visible (empty) whenever the debug menu force-showed the view.
  - **Fallback in-world latch:** also latch when the Calendar reads a postwar year (> 2100), so the
    HUD populates even when the Pip-Boy slot scan misses. Prewar intro (2077) still stays hidden.
  - **Max-value guard:** `GetPermanentActorValue(health/actionPoints)` can read 0 (derived-HP
    overhauls / odd starts), pushing a 0 max that renders an empty bar and never sets `hudLoaded`.
    Now falls back to the live value so a living player never shows an empty health/AP bar.
  - **JS defense (radhud.js):** if max is 0 but the current value is > 0, render the bar full and
    treat the HUD as loaded.

## 2026-06-27 (later) — Fix Stats button reworked

### "Fix Stats" SPECIAL button — relieve + diagnose
- **Reworked the Fix Stats debug button.** Each press now:
  1. Fully removes THIS mod's own radiation SPECIAL penalty (END/STR) and suspends it until the player's rad level changes.
  2. Claws back up to **5 points** of any remaining negative on each SPECIAL (`kFixStatsReliefPerPress`) — foreign debuffs we don't own. Repeated presses chip a heavy debuff down 5 at a time.
  3. **Names the active magic effects still lowering a SPECIAL** (hunger / thirst / fatigue / disease / other mods) via the player's active-effect list, and tells the player to eat / drink / sleep / cure — so they know what is NOT this mod.
- **Why it looked broken before:** the old button refused entirely while the player was a ghoul (`[DEBUG] Fix Stats unavailable while ghoulified`), and the player was ghoulified — so it appeared to do nothing. With ghoulification removed it now runs normally.
- **Diagnosis of the END/STR "stuck at 1" report:** this mod's radiation penalty caps at END -7 / STR -5 and only ever uses temporary modifiers (never the permanent base), so a base inflating to ~22 with a matching ~-21 modifier is NOT from this mod. That signature is the vanilla "unequip gear while a SPECIAL is floored bakes the bonus into the base" bug, driven by a large persistent SPECIAL debuff from elsewhere in the load order (typically a survival-needs or disease system). The button now surfaces that source by name.

## 2026-06-27

### Ghoulification — removed (temporary, player request)
- **Added a master kill-switch `kGhoulificationEnabled` (currently `false`).** The player no longer transforms into a ghoul when permanent tissue maxes out — the rad-immunity capstone ruined the experience (a ghoul takes no rad damage, so the overhaul stops mattering).
  - The `Ghoulify()` trigger in `UpdateTissueDamage` and the debug `ghoul` action are both gated on the flag.
  - **Existing ghoul saves are force-reverted to human on every load.** Fixes the bug where Shift+F8 (DebugUnghoul) only worked until the next load: the old `OnPostLoad` re-flagged the player as a ghoul whenever the ghoul quest was at stage ≥ 10, and DebugUnghoul leaves it at stage 20 (still ≥ 10), so it re-ghoulified on reload. The revert is idempotent (the race swap no-ops once the player is human) and pushes the quest to stage 20 so the stale re-detect can never fire again.
  - Flip `kGhoulificationEnabled` back to `true` to restore the endgame capstone.

### Storms — leaving shelter no longer triggers back-to-back radstorms
- **Cell-exit radstorm grace.** Stepping from a shelter (interior load-door *or* a roofed exterior base) into the open frequently landed the player in an already fully-rendered radstorm, which the start-of-transition veto (`pct < kRadstormVetoMaxPct`) missed. Now a sheltered→exposed transition arms a ~60s grace (`kCellExitGraceTicks`) during which any vanilla radstorm is force-cleared **regardless of its transition pct**. This is the "storm the moment I leave Sanctuary" fix.
- **Radstorm cooldown raised 30 → 60 min** (`kRadstormMinGapSec`) to further thin back-to-back chains.
- **Fixed broken deployed DLL.** The live modlist copy (`Fallen World Rad Overhaul/F4SE/Plugins/FalloutAnomalyRadiation.dll`) was a 1-byte placeholder; redeployed the real 805 KB build.

## 2026-06-13

### Storms — frequency & first impression
- **Thinned out radstorms.** Vanilla radstorms are now allowed but repeats are suppressed within a cooldown (`kRadstormMinGapSec` = 30 min), so they stop coming back-to-back.
- **Early veto** of a vanilla radstorm that rolls in too soon (within `kRadstormVetoMaxPct` ≈ 10% of the transition) — reverted to the prior weather instead of snapping to clear, so it no longer visually shocks the player.
- **Softer storm alert** — quieter, faded-in cue (`kRadstormCueFadeMs` = 2500, `kRadstormCueAttenuation` = 1200) instead of a hard shock.

### Storms — persistence
- **Play-clock now survives save/load.** Added F4SE co-save serialization (record `SCLK`, unique ID `FARO`): persists `_playSeconds`, `_nextStormPlaySec`, `_lastRadstormSec`, `_stormScheduled` via Save/Load/Revert callbacks. The mod's own scheduled storms now actually fire across sessions.

### Radiation & shelter
- **Fixed rads leaking indoors.** Radstorm radiation comes from a weather-conditioned magic effect that bypassed the shelter check. Storm rads are now neutralized whenever the player is sheltered during *any* storm (powered shelter, or roof overhead).
- **Roof-based shelter detection for player-built structures.** Added a "roof fan" raycast from the player's head (1 up + 4 angled rays); ≥3 hits = covered. Uses raw game-world coords for `TESObjectCELL::Pick` (fixed an earlier bug where coords were wrongly pre-scaled by `BS_TO_HK_SCALE`, which made the test always miss). Throttled so it isn't cast every frame.

### NPCs
- **Ghoul NPCs are immune to storm rads and heal in them** (+`kStormGhoulHeal` 150/peak). Detection via the `ActorTypeGhoul` keyword on the actor's race.
- **All outside NPCs are protected from storm damage** — Diamond City guards, patrols, caravans, etc. no longer take storm damage (`ApplyStormPeakToNPCs`).
- **NPCs flee during storms** (`MakeNearbyNPCsFlee`, within `kFleeRadius` 5000), rechecked periodically. *(Experimental — needs in-game verification.)*

### Storm ghoul packs
- **% chance for a storm to spawn a hunter pack** (`kStormGhoulSpawnChance` ≈ 40%), **5–20 persistent ghouls** that hunt the player.
- **They do NOT despawn when the storm ends** — but the pack is cleaned up if the player travels far (`kGhoulDespawnDist` ≈ 140 m) or moves indoors / into a new cell.

### Storm dome (visual)
- **Fixed the dome appearing as a ring on top of the player.** Root cause: cloud-layer activators were placed at `DomeZOffset = 0.0` (player height). Set `DomeZOffset = 6000` (sky height) so the stacked cloud layers read as a sky dome that follows the player.
  - Applied to the deployed `RadiationOverhaul.esp` as a script property via a verified Spriggit round-trip (record count unchanged 196=196, value confirmed after re-parse; original backed up as `RadiationOverhaul.esp.bak_predome`).
  - Also updated the `RadStormFX.psc` source default and `gen_esl.py` so future regenerations keep the fix.
  - *(Height is a best estimate — still needs in-game verification/tuning.)*

### Build / deployment hygiene
- **Single DLL load.** Removed a stale duplicate DLL that was loading alongside the current one and causing double storms. Canonical location: `…\Fallen World Rad Overhaul\F4SE\Plugins\FalloutAnomalyRadiation.dll`.

### Gear patch — NEW sister plugin `RadOverhaul_GearPatch.esp`
- **Hazmat suit buffed to "super" rad resistance.** Overrides vanilla `Armor_HazmatSuit` (`Fallout4.esm` `0CEAC4`): radiation resist **1000 → 2500**. The worn `HazmatSuitPerk` enchantment, model, and keywords are preserved (full faithful record copy via Mutagen).
  - Deployed to `…\Fallen World Rad Overhaul\RadOverhaul_GearPatch.esp` (masters: `Fallout4.esm`).
  - Reproducible generator saved at `Rad Mod\GearPatch\` (`dotnet run -c Release`).
  - **ACTION REQUIRED:** enable `RadOverhaul_GearPatch.esp` in MO2; load it after armor mods.
- **Gas mask intentionally NOT patched.** Investigation of `Gas Masks of the Wasteland.esp` (all armors, object-mods, spells, magic effects, globals, MCM config, and the extracted `FilteredAir.pex`/`Radiation.pex` scripts) found **no static rad-resist record** — protection is fully Papyrus-scripted (dispels the rad spell while a filter is worn) and the mod's own rad spell uses `IgnoreResistance`. There is no "800 rad resist" field to override; tune via the mod's MCM instead.

### Untouched (per request)
- The tissue-curing item recipe was left alone (user fixed it separately).
