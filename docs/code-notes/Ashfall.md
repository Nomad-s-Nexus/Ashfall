# Ashfall code notes

## Storm dome: the vortex tube is never drawn; the portal follows its baked windows

Reference: `src/RadCorrectnessPolicy.h:DomeHiddenShapeMask`, `src/RadCorrectnessPolicy.h:VisibleShapeMask`, `src/RadCorrectnessPolicy.h:kDomeHiddenShapes`, `src/RadEngine_Storm_Dome.cpp:RadiationEngine::ApplyDomeShapeMask`, and `deploy/Meshes/KORsMeshes/Anomaly/CloudArena01_4.nif`.

Symptom: during the Ash Fallout event (a forced `storm:0` in the 2026-11-06 log, "Native storm dome spawned: 4/4 layers", "dome spin: Hole4=found Vortex2=found Vortex3=found") a glassy tube with a flared dark base and a glowing ring inside floats in a flat grey sky, instead of ash filling the air. The first fix (PR #53) hid the tube and the portal group together, so the storm portal disappeared and only the clouds remained. The portal is wanted back; only the tube stays hidden.

Cause, read from the shipped NIF (byte-identical to the file installed in the Fallen World modlist, sha1 77e8a795):

- `StormRoot` scales the whole mesh by 0.1. The `Vortex2` and `Vortex3` nodes sit at (1575, 1725, 3281) world units from the dome origin, which follows the player, so the structure hangs about 4000 units away at roughly 54 degrees elevation. `Hole4` sits at the same spot as a sibling of the vortex nodes (see Parenting below).
- `Vortex2` holds `CoreBack` and `Vortex3` holds `CoreFront`. Both are an open-topped cylinder of about 330 units radius that flares to about 440 units over a height of about 450 units. Their vertex alpha is 117 to 255 along the straight wall and fades to 0 only at the flared bottom, so the wall and the open rim read as a hard-edged glass tube. The effect shader flags are double sided, depth tested, no depth write, alpha blended.
- `Hole4` holds `PortalA` and `PortalB` (the `city.dds` portal), `DiskOuter`, `DiskInner`, `DiskHalo` and `PhotonRing`. Together they are the glowing ring inside the tube.
- The baked `Storm` sequence shows the portal shapes (`PortalA`, `PortalB`, `DiskOuter`, `DiskInner`, `DiskHalo`, `PhotonRing`) from about sequence second 128 to 320 (`RadDomeVisibilityTable.h`), and the tube shapes (`CoreBack`, `CoreFront`) from about 147 to 297. A natural storm reaches that range before the siren and holds it through Active; a forced storm seeks to about 170 s, so the portal is on screen from the first frame.

Parenting, checked in the NIF graph (`CloudArena01_4.nif`, read with `tools/bake_dome_visibility.py`'s parser): `StormRoot` has one child, `OrderedRenderingNode` (`BSOrderedNode`), and `Vortex0`, `Vortex1`, `Vortex2`, `Vortex3` and `Hole4` are all direct siblings under it. `Hole4` is not a child of `Vortex2` or `Vortex3`. `Vortex2` holds only `CoreBack` and `Vortex3` only `CoreFront`, and both tube shapes are `BSTriShape` leaves with no children. So culling them cannot hide the portal, and `ApplyDomeShapeMask` culls by name with `SetAppCulled` on the shape only, never on a node.

Fix: `kDomeHiddenShapes` lists only `CoreBack` and `CoreFront`. `VisibleShapeMask` clears those two bits at every sequence time, including the invalid-time path, so `ApplyDomeShapeMask` calls `SetAppCulled(true)` on them and never un-culls them. The six portal shapes, the cloud decks (`Plain*`, `Overcast`, `Outer*`, `FogSkirt`, `HorizonBand`), `EyeGlow`, the bolts and the flashes all follow their baked alpha windows, so the overcast sky, lightning and sky fade are unchanged. The baked table, the baker and the NIF are untouched; whether a culled shape keeps animating (`RadiationEngine::DriveDomeAnimation`) is unverified in game and does not matter for shapes that are never shown.

Names are resolved with `DomeShapeIndex`, so a shape missing from the table is skipped. `tests/correctness_policy_tests.cpp` requires exactly two hidden bits, the portal bits set at 200 s and cleared at 100 s and 330 s. `tests/verify_dome_visibility.py` checks the same from the baked table and the header.

Not verified in game. Whether the portal and the remaining cloud layers read correctly is a visual judgement that needs a run with the rebuilt DLL.

Related: PR #49 (`feat/rad-storm-rad-weather-no-dome`) removes the dome entirely. It is 85 commits behind `main` and conflicts in 14 files, so this change is separate and smaller; if #49 is rebased and merged later, this change is moot.

## Storm dome: portal placement, deck floor off by default, debug-menu evidence (v1.2.3 test feedback)

Reference: `src/RadEngine_Storm_Dome.cpp:RadiationEngine::ApplyDomeVortexLayout`, `src/RadEngine_Storm_Dome.cpp:RadiationEngine::ApplyDomeDeckFloor`, `src/RadEngine_Storm_Dome.cpp:RadiationEngine::SpawnStormDome`, `src/RadEngine_Storm_Dome.cpp:ReadDomeTunable`, `src/RadCorrectnessPolicy.h:DomeShapeVisibility`, `src/RadCorrectnessPolicy.h:DomeDeckFloorFor`, `src/RadCorrectnessPolicy.h:kDomeVortexDistance`, and `src/RadiationEngine.cpp:RadiationEngine::PollInput`.

What the player saw after the first build (2026-11-06 22:57 session, `FalloutAnomaly.log`): the sky showed a dark overcast with a large pinwheel of grey radial streaks converging on a point above the crosshair. No portal and no vortex colour.

Cause of the pinwheel, from the code and the log:

- The outer cloud discs are authored under `Vortex0` (`OuterHigh`) and `Vortex1` (`OuterBack`, `OuterFront`), not under the portal group. The first build applied the same node shift and the x1.5 scale to all five vortex nodes, so these discs moved off the player and their radial texture centres came into view.
- The deck floor raised `OuterHigh`, `OuterBack`, `OuterFront`, `FogSkirt` and `HorizonBand` to 0.6 to 0.8 alpha, and `DomeShapeVisibility` forced them visible from 128 to 320 s. Their authored alpha is faint, so the baked disc geometry showed at full strength.
- The portal group (`Hole4`, `Vortex2`, `Vortex3`) was moved, but at 2600 units its shapes (136 to 312 world units) subtend about 3 degrees, so it did not read as a portal.
- The log line `[StormFX] dome vortex placed ... node shift (-29530,-39298,-20812)` is the shift that was applied to all five nodes.

Changes:

- Only `Hole4`, `Vortex2` and `Vortex3` are shifted and scaled (`RadiationEngine::ResolveDomeLayerNodes` and `RadiationEngine::ApplyDomeVortexLayout`). `Vortex0` and `Vortex1` keep their authored position and scale, so the cloud decks stay centred on the player.
- Portal placement defaults: 1800 units ahead, 900 up, scale x3.0 (`kDomeVortexDistance`, `kDomeVortexHeight`, `kDomeVortexScale`). The bearing is still fixed at spawn.
- The deck floor is off by default. It is switched on with `[StormDome] DeckFloor=1`. When on, only `PlainHigh`, `PlainMid` and `PlainLow` are floored to 0.6 and forced visible 128 to 320 s (`DomeDeckFloorFor`, `DomeShapeVisibility(seq, deckFloorOn)`). `Outer*`, `FogSkirt` and `HorizonBand` are never floored or forced; they follow their baked windows.
- Runtime tunables are read once at spawn from the `[StormDome]` section of `Data\F4SE\Plugins\FalloutAnomalyRadiation.ini`. Missing or out-of-range values fall back to the defaults (`ReadDomeTunable`, `DomeTunableOr`):

```
[StormDome]
DeckFloor=0
VortexDistance=1800
VortexHeight=900
VortexScale=3.0
```

- The depth flags (`ZBufferTest`, `ZBufferWrite`) are not changed. Their diagnostic logs were removed.

Debug menu (Shift+F8) evidence from the same session:

- `[HUD] Debug menu opened` at 22:57:48.476. `storm:0` arrived at 22:57:50.046. `[HUD] Debug menu closed` at 22:57:52.248, which is `RadHud::SetDebugMenu(false)`. Its callers are `RadHud::ResetDebugMenuForSession` (load and new game only, not at 52.2), `RadHud::OnBlockingMenuOpened` (pause or message box sink), the `close` debug command, and `RadHudLayout`. The sink logs nothing, so the exact trigger at 52.2 is not in the log.
- Older sessions show the same open-then-close during a forced storm (2026-11-06 20:50 and 21:59, 2026-11-01 00:02). This is pre-existing and not caused by the dome change.
- No Shift+F8 press registered after 22:57:52: there is no further `Debug menu opened` line. The PrismaUI log shows `WebInput window deactivated` at 22:58:15 and 22:58:25, and `RadiationEngine::PollInput` ignores the key unless the game window is foreground.
- The PrismaUI compositor reuse (`present-lease-unavailable`) starts at 22:57:07.554, at view creation, about 40 seconds before the dome spawned. The dome did not cause it.
- The session ends at 22:58:35 with the player at 705 rad, and no crash log was written.

Conclusion: the debug-menu problem is not caused by the dome change. The 22:57:52.248 close matches PrismaUI `FocusMenu::Close`/`Unfocus` to the millisecond, so it was our own `SetDebugMenu(false)`. No `close` command reached `HandleDebugCommand`, and no Shift+F8 after 52.248 reached `ToggleDebugMenu`. Most likely cause: the `GameWindowForeground()` gate in `RadiationEngine::PollInput`, which the focused PrismaUI view can make false. Not proven from the log.

Fix (no logging added):

- `src/RadiationEngine.cpp:RadiationEngine::PollInput`: Shift+F8 is accepted while the menu is open (`GameWindowForeground() || RadHud::DebugMenuOpen()`), so an open menu always closes.
- `src/RadHud.cpp:OnRadDebugCommand`: `close` bypasses the engine queue (which drains only inside `Update()`), calls `RadHud::RequestDebugClose`, and `RadHud::PumpDeferredDebug` applies it every frame from `PollInput`.
- `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:closeDebugMenu`: the CLOSE button hides the panel locally, then sends `close`. Escape closes the panel when it is open.
- `src/RadHud.cpp:SetDebugMenu`: Escape reaches the view's JS only when the view owns it. Opening the menu claims Escape through PrismaUI V9 `SetViewOwnsEscape`, and closing or a rejected focus releases it, so the pause menu works again. Without V9 Escape stays with the game.
- `src/RadHud.cpp:SetDebugMenu`: with the DOM still loading there is no JS to hide the panel, so `SetDebugMenu(false)` still releases view focus and Escape ownership.
- Contract: `tests/verify_debug_menu_close.py`.

Log lines to look for:

- `[StormFX] dome portal placed: yaw ..., eye 1800 units ahead and 900 up, scale x3.00, node shift (...) NIF units, deck floor off.`
- `[StormFX] dome sequence milestone: approach|warning|active|aftermath begins at seq ...`

Not verified in game.

## God mode: plugin drains cannot kill a TGM player

Status: TODO-verify. Runtime god-mode detection is not verified in game.

- Drain sites: choking (`RadiationEngine.cpp:RadiationEngine::Update`) and storm HP loss (`RadEngine_Storm_FX.cpp:RadiationEngine::ApplyStormExposure`). The vomiting fit (`RadEngine_Tissue.cpp:RadiationEngine::UpdateTissueSymptoms`) is already floored at `kSymVomitFloorHp` and is unchanged.
- Gate: `RadEngine_Storm_FX.cpp:RadiationEngine::GodModeHealthDrain` checks `[Survival] RespectGodMode` first, then `IsPlayerGodMode`, which runs only on the OG runtime. TODO-verify: the `RE::ID::PlayerCharacter::IsGodMode` address entry in `IDs.h`; NG and AE are untested.
- Clamp: `RadCorrectnessPolicy.h:GodModeClampedDrain` keeps health at or above `kGodModeFloorHealth` (1). Heals pass through. Rads and the HUD are not clamped.
- Config: `RadEngine_Mask.cpp:RadiationEngine::LoadFilterIni` reads `[Survival] RespectGodMode` (default 1).

## Pip-Boy redesign (PR #59) on the debug-menu stack

Reference: `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.html`, `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:closeDebugMenu`, `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:RadShowcase.startTour`, and `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud_interactive.js`.

- #59's markup, CSS and JS are taken as-is. Its module split (`radhud_interactive.js`, loaded by `radhud.html`) is kept.
- #58's close behaviour is re-applied: the footer CLOSE button calls `closeDebugMenu()` (local hide, then native `close`). Escape closes the debug panel through the single keydown handler. #59's duplicate Escape branch was removed. `startTour()` closes through `closeDebugMenu()`.
- `tests/verify_hud_layout_bridge.py` now checks #59's armor markup (`armor-region-card`, `armorInsulation`, `armorEffective`).
- `tests/verify_survival_menu.py` asserts the redesign's `.diagnostics dl` font size (11px) instead of the old 15px. This weakens the readability check and is flagged for a decision.
- Not verified in game.

## Phosphor HUD core rings

- **Why arc paths.** Ultralight (PrismaUI) has no SVG masks and no `pathLength`, so a ring cannot be a dash-array circle. Each ring layer is a set of arc `<path>`s over a 64-unit viewBox (ring r=28, disc r=24.5). `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_core.js:build` writes n empty `<path d="">` elements per layer (trk, ghost, val) into the core markup, as the Phosphor reference does. `hud_core.js:ring` then sets each path's `d` for the range lo..hi (clockwise from the top) and leaves it empty when the segment is outside the range. Every core has 20 segments with a 3.2 gap; there is no small variant (the old 10-segment `sm` core was removed 2026-11-06). Setting `d` on existing elements avoids writing SVG markup at runtime, which Ultralight has not been verified to parse.
- **Contract.** `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_core.js:PhosCore.build` fills an element with the core markup, adds `core` to it, and returns a handle. The signature is `build(el, icon, label, solid)`. `hud_core.js:PhosCore.set` takes `frac` 0..1 (ring value), `valueText` (the `<b>` under the label), `fillFrac` 0..1 (default 1) and `state` ('', 'warn', 'low' or 'crit'). `state` is applied as a class on the core element. `fillFrac` moves the clip rect top edge so the icon is lit from the bottom, as Phosphor does. Each handle gets its own clipPath id. The part that was lost lingers as a pale ghost for 450 ms.
- **Colour.** The ring colour is the inline `--c` on each core element (set in the view markup). `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_status.css:.core` supplies the rest. The 'warn' state draws the ring amber. 'low' and 'crit' draw it rad red and blink the lit icon.
- **Needs mapping.** `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_needs.html:renderNeeds` takes fullness `f = 1 - need/100` as both the ring value and the icon fill. The core state is 'crit' below 0.25, 'warn' up to 0.5, and empty otherwise. The core value text is the `PHOS_WORDS` caption. The percentage is kept only in the chip's `aria-label` and `title`. The chip holds only its core: there is no classic ring, icon, label or value element.
- **Look is not switchable.** There is no classic look. `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_needs.html` starts with `<body class="theme-phosphor">` and `hud_needs.html:setTheme` does nothing. `src/RadHudCompanion.cpp:RadHud::PushTheme` still sends "phosphor" so older view copies keep working. `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:setTheme` is also a no-op and `radhud.html` starts with the same body class for the emission banner styling. `[Hud] Mode` only decides whether our duplicate vitals are dropped (`src/RadHudCompanion.cpp:ResolveCompanionMode`).
- **Other views.** Filter and toxic use `hud_core.js` now (see the filter and toxic bullets below). Climate is covered by the climate bullets.
- **Test harness.** `tests/status_hud_tests.js:load` runs `hud_core.js` as an external script, and its fake element's `querySelector` returns an element with 20 fake `children`, which `hud_core.js:ring` writes `d` into. Without these the needs page cannot build its cores.
- **Provenance.** The food, water and rest icon paths, the ring geometry and the core CSS are copied from Phosphor HUD, MIT License, Copyright (c) 2026 Phosphor HUD contributors. See `THIRD_PARTY_NOTICES.md`.
- **Climate cores.** The WARM, WET, TEMP and WIND chips in `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_climate.html:renderClimate` each get a small core (`#warmCore`, `#wetCore`, `#airCore`, `#windCore`). `hud_climate.html:cores` builds them once at script load from `hud_climate.html:PHOS_ICONS`, which holds the chip's existing 24-unit icon paths and labels (TEMP for the air chip). The chips carry no `data-ring` attribute.
- **Climate mapping.** `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_climate.html:status` passes its `frac` as both the ring value and the icon fill, and its `value` as the core's value text. Severity 'danger' maps to state 'crit' and 'warn' to 'warn'; 'dim' maps to no state. The inline `--c` is pip green for WARM, TEMP and WIND and cyan for WET. The value text goes only to the core's `<b>` via `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_climate.html:status`, so the chips have no value element any more.
- **Climate theme.** This view has no classic look. `hud_climate.html` starts with `<body class="theme-phosphor">`, and `hud_climate.html:setTheme` ignores its argument and keeps that class on. The core is the only chip markup, so the classic svg.ring, icon and label were removed.
- **Climate CSS.** `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_status.css:#warm` strips the chip frame for the four climate chips. Id selectors beat the generic `.status` rule.
- **Climate test harness.** `tests/status_hud_tests.js:load` has a `clearTimeout` stub, because `hud_core.js:set` calls it when a ring value drops. The climate test drops `warmth` from 100 to 0 and would otherwise throw inside `renderClimate`. Climate value text is read as `climate.context.cores.<id>.num` in `tests/status_hud_tests.js:load`, because the chips have no value element any more.
- **Filter cores.** `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_filter.html:renderFilter` drives four cores (`#breathCore`, `#filterCore`, `#maskCore`, `#autoCore`), built once at script load from `hud_filter.html:PHOS_ICONS`, which holds the chip's existing 24-unit icon paths. The filter and mask cores use the charge and condition percentage as both ring and icon fill, and that percentage as value text. The breath core is a full ring whose value is the air state. The auto core is a full ring whose value is ON, OFF or HELD. Chip danger maps to state 'crit' and warn to 'warn'.
- **Filter theme and markup.** `hud_filter.html` starts with `<body class="theme-phosphor">`, and `hud_filter.html:setTheme` ignores its argument, so no classic svg, svg.ring or value `<b>` remains. The `breathValue`, `filterValue`, `maskValue`, `autoValue`, `filterRing` and `maskRing` ids were removed with that markup; the value text lives in each core's `<b>`. The chip ids, `aria-label`, `title`, className, the key hints (`filterKey`, `maskKey`) and the hide logic are kept.
- **Toxic cores.** `hud_toxic.html:renderToxic` drives the tissue core (ring `tissue/1000`, state 'warn' from 150 and 'crit' from 600) and the large health, AP and rads cores built from `hud_toxic.html:PHOS_CORES`. Health ring is `health/healthMax` with state 'crit' at 25% or less; AP ring is `ap/apMax` with state 'warn' at 20% or less; rads ring is `rads/radsMax` with a 0.03 floor and fill at the same fraction, state 'crit' at 600 or more, copying Phosphor's RAD core. Phosphor uses 'low' for health below 25%; this view uses 'crit' as specified.
- **Toxic fallback vitals.** `hud_toxic.html:renderToxic` shows `#health`, `#stamina` and `#rads` only when `d.fallback` is true (`hidden=!fallback`), in every theme, so the Phosphor look is the default without Phosphor HUD installed. Phosphor HUD draws them itself and sends `fallback` false.
- **Toxic theme and scar ring.** `hud_toxic.html` starts with `<body class="theme-phosphor">` and `hud_toxic.html:setTheme` ignores its argument. The classic scar ring has no core equivalent, so the permanent-tissue number lives only in the tissue chip's `aria-label` and `title` (`hud_toxic.html:renderToxic`, description `Tissue damage; permanent scarring N`).
- **Toxic icons.** The HP, AP and RAD icons and labels in `hud_toxic.html:PHOS_CORES` are copied from the Phosphor HUD `hud.js` KICON and KLABEL tables (MIT, see `THIRD_PARTY_NOTICES.md`). The RAD icon has three wedges that the reference builds with a generator; they were expanded by hand at -90, 30 and 150 degrees, radii 3.4 to 9.2, plus or minus 30 degrees.
- **Phosphor chip CSS.** `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_status.css:#breath` resets the frame of the filter and toxic chips. Id selectors beat the generic `.status` rule. `hud_status.css:.status` is now the only chip style (used by the risk, illness and key-hint chips); the old classic base rules, `svg.ring`, `.cap` and `[data-ring]` rules were deleted.
- **Filter and toxic test harness.** `tests/status_hud_tests.js:load` has the same `clearTimeout` stub as the climate note. The filter and toxic assertions read `context.cores.<id>` as `.num.textContent`, `.v` and `.state`, because `hud_core.js:PhosCore.build` returns those handles as the global `cores`. The body-class check is a regex on `phosToxic.html` and `phosFilter.html`, since the fake DOM does not parse `<body>`.
- **Solid lit icon and value colour.** `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_core.js:PhosCore.build` takes an optional fourth `solid` argument that adds class `solid` to the lit icon layer, and `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_status.css:.core .ic.lit.solid` gives it Phosphor's faint fill. `hud_toxic.html:PHOS_CORES` passes it for health, AP and rads, as the reference `hud.js` does for hp, ap and rad. `hud_status.css:.core small b` uses the bright ink colour; the warn/danger value colours apply only to direct-child `b` of a chip (`.status.warn>b`), i.e. the risk and illness chips, not to core values.
- **Core grid and default rows (2026-11-06, not verified in game).** Every core is one cell: 84 x 98 px at the 1920x1080 reference (64 px ring, 4 px gap, 30 px label and value box), with an 8 px gap between cells in a row (`hud_status.css:#root`, `.core`, `.core small`). The ring is 64 px for all cores, so the old 44 px small cores are gone. The label is 11 px and the value 12 px, and a value that does not fit is clipped with an ellipsis rather than wrapping. The four widgets share a left edge and a 108 px row pitch. Non-companion defaults are x 22 with rows at y 638 (toxic), 746 (needs), 854 (climate) and 962 (filter), footprints 360 x 98 (needs 268 x 98). Companion defaults (`src/RadHudCompanion.cpp:CompanionWidgetDefault`) use x 396 with rows at y 538, 646, 754 and 862, which sit clear of the ring and XP text and end above the compass row. The same values are repeated in each widget's `DEF`, `rad_hud_local_editor.js`, `src/RadHudLayout.cpp:LayoutState`, and the `RegisterWidget` descriptors in `RadHud.cpp`, `RadHudClimate.cpp` and `RadHudNeeds.cpp`. `tests/verify_hud_core_layout.py` checks that they agree, the pitch, the left edge and the companion zone; `tests/status_hud_tests.js` checks the 20-segment core and the DEF footprint. A saved `hud_layout.json` still wins over all of these. Anchors for the one-line constraint comments in source: `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_core.js:build` (20 segments, no small variant), `src/RadHudCompanion.cpp:CompanionWidgetDefault` (companion zone: Phosphor rings at x 56..272, XP text at x < 390 and y 820..915, compass row at x >= 660 and y > 970), and `src/RadCorrectnessPolicy.h:kDomeHiddenShapes` (tube only; the portal follows its baked windows).

- **Pip-Boy Diagnostics & Settings Console redesign (2026-11-07).**
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.html`: Replaced dense essay text and irregular debug decks in `01 RAD MOD` and `03 GHOULS` with uniform 2-column `.pip-grid-2` action grids (34px locked buttons). Replaced 3-card thermal summary and verbose rule essays in `04 ARMOR` with a sleek 44px summary bar, collapsible details table, and symmetrical limb cards around the wireframe figure. Added on-demand mechanics guide drawer (`toggleArmorMechanics`). Settings categorized into 4 expandable details folders plus copy ini diff. Weather voids structured into 3 distinct Commonwealth sectors. Added RobCo CRT phosphor boot animation, dynamic ECG pulse oscilloscope with heart-rate tach/bradycardia alerts, dial scrubbers, and simulation stress presets (`RAD BURST`, `FREEZE`, `BREACH`, `NOMINAL`).
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:showRadDeck`: Deck visibility switcher supporting balanced 3-column view (`all`) and focused single-category views (`rads`, `mask`, `storm`) with persistent styling.
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:tuneRows`: Categorized tuning accordion generator mapping survival and gameplay variables into expandable details groups with instant numeric inputs and reset buttons.
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:copyIniDiff`: Generates INI delta format string from modified sliders and copies to clipboard with visual status confirmation.
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud_interactive.js:playRobcoBoot`: Modularized CRT terminal startup sequence rendering diagnostic initialization text and phosphor scanline fades with user skip controls.
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud_interactive.js:toggleGeigerAudio`: Web Audio API synthetic Geiger click generator whose pulse frequency scales dynamically with active radiation dosage, toggled via header pill.
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:focusLimb`: Interactive limb selection highlighting the target body zone on the wireframe schematic and filtering matching equipped apparel records.
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:toggleArmorMechanics`: Toggles on-demand quick-reference drawer explaining insulation, layer stacking rules, and wetness penalties without cluttering the primary diagnostics screen.
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css:.pip-grid-2`: Enforces strict 2-column button geometry with 34px fixed heights, 50% relative widths, uniform border radiuses, and green/amber/danger phosphor color states.
  - `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/rad_diagnostics.js:updateVitalsInfo`: Dispatches live vitals telemetry to circular gauges, ECG pulse canvas, and geiger counter audio synthesizer while maintaining compatibility with headless assertions.

## Radiation HUD push and view-retry throttling (audit items 1-4)

- Item 1, `src/RadHudLifecycle.cpp:StartPrismaViewRetry`: the retry was started from `PushNeeds`, `PushClimate` and `PushToxic` every 4 Hz tick while a view was missing, including before the player was in the world. Each start spawned a thread whose first step un-suppressed then re-suppressed the vanilla HP/AP meters and called `CreateView`, which resets `g_inWorld` and `_maskPushed`. Now the three call sites gate on `PlayerInWorld()`, the retry is rate-limited to one start per second (`_viewRetryNotBeforeMs`), and `StartPrismaViewRetry` with all views present (`RadHud::ViewsExist`) only re-applies vanilla-meter suppression. `CreateRadHudViewOnPrismaThread` takes the same early exit, so a retry attempt never recreates existing views.
- The `_viewCreateRetry` atomic guard still prevents overlapping retry threads.
- Not verified in game.

## Debug menu close: focus is released on the game thread, with a watchdog

Reference: src/RadHud.cpp:RadHud::RequestDebugClose, src/RadHud.cpp:RadHud::ReleaseStaleDebugFocus, src/RadiationEngine.cpp:RadiationEngine::Update.

Symptom risk: the close used to complete only inside PumpDeferredDebug, which runs from PollInput, queued from the PlayerCharacter::Update hook. If that stalled, the panel hid but the PrismaUI view kept focus, so the cursor stayed captured and Escape was swallowed.

Fix: RequestDebugClose still sets the atomic close flag, and also queues PumpDeferredDebug as a main-thread task through Hooks::QueueMainThreadTask, so the close (and its Unfocus plus SetViewOwnsEscape(false)) runs on the next main-thread pump. ReleaseStaleDebugFocus runs at the top of the 4 Hz RadiationEngine::Update and calls Unfocus plus SetViewOwnsEscape(false) only when the menu is closed and the view still reports HasFocus. It never touches a view that is open or that another view has focused.

Not verified in game: the watchdog path itself needs a run where the close hook is forced to stall.

## Debug menu focus: bounded retry, FocusOverlay fallback, kPanel only while open

Reference: src/RadHud.cpp:RadHud::SetDebugMenu, src/RadHud.cpp:RadHud::PumpDeferredDebug, src/RadHud.cpp:FocusDebugOverlayFallback, src/RadHud.cpp:SetDebugViewRole.

Symptom: when PrismaUI rejected Focus() on the debug view, the menu hid and retried every 150 ms with no end.

Fix: the first rejection stamps _debugFocusFailedSinceMs and retries every 150 ms. Before each retry, IVPrismaUI10::FocusOverlay (null-checked) is tried as a fallback. After 2 s of rejection the pending open is cleared, the retry state is reset, and one player message is shown through RE::SendHUDMessage::ShowHUDMessage. The stamp is re-taken whenever the retry state was reset (_debugNextFocusRetryMs == 0), so a fresh open never inherits an old window.

View role: the debug view declares kPanel only after a successful open and returns to kWidget on close and on focus failure. The always-on overlay is never kPanel, because IsAnyPanelVisible() would otherwise be true for other plugins.

Not verified in game: whether FocusOverlay accepts where Focus does not, and whether the 2 s cap is long enough for a slow view.

## Pip-Boy stack UI and perf fixes (this session)

- ECG loop runs only while the debug panel is open, capped at about 30 fps (38d71ec): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud_interactive.js:window.setEcgActive`.
- Geiger click timer stops when the debug panel closes, restarts on FX toggle-on, and is a no-op without AudioContext (432d8b4, eb47770): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud_interactive.js:window.SetDebugMenu`.
- Dial scrubbers start from the live percentage in `data-pct`, and `updateEcgVitals` merges partial vitals instead of zeroing them (c5c639d): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud_interactive.js:window.initDialScrubbers`.
- Dial hover uses an inset/outline box-shadow and no longer translates or scales the box (c5c639d): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css:.overview-dial`.
- RobCo boot plays only from the replay control, and key auto-repeat is ignored (c38f425): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud_interactive.js:window.playRobcoBoot`.
- Unreferenced telemetry and ops helpers are removed, with the dead wetness dial call (9334ed4): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud_interactive.js:window.triggerScenario`.
- Clipboard copy falls back to execCommand and never throws (0cddf5a): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:copyText`.
- Widgets report visibility only on change, and hud_filter skips re-rendering unchanged data (7a2e4a2): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/hud_filter.html:renderFilter`.
- Debug overlay uses explicit inset edges, and its backdrop is a `#debugBackdrop` sibling instead of a 100vmax box-shadow (0790a4c): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css:#debugBackdrop`.
- The overview wetness readout targets the `ov-val-wetness` element (1fdb12c): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/rad_diagnostics.js:ov-val-wetness`.
- Dome follow returns early unless an IMAD fade tick is due or the player moved more than 64 units (c2a1fca): `src/Hooks_DomeFollow.cpp:QueueDomeFollow`.
- Armor piece names pass through `SanitizeUiText`, because 4-byte UTF-8 characters hang the Ultralight shaper (see CHANGELOG): `src/RadCorrectnessPolicy.h:RadCorrectnessPolicy::SanitizeUiText`, `src/RadHud.cpp:RadHud::PushArmor`.

## Pip-Boy stack follow-ups: hudlayout save, view retry, input edges, dome follow, debug queue

- A, `src/RadEngine_Debug.cpp:RadiationEngine::HandleDebugCommand`: a coalesced hudlayout replacement keeps `save:true` from any queued entry it replaces (`HudLayoutRequestsSave` / `WithHudLayoutSave`), so a live-preview drag cannot drop a pending save.
- B, `src/RadHudLifecycle.cpp:RadHud::StartPrismaViewRetry`: the throttled branch returns `_viewCreateRetry.load() || ViewsExist()`, not `true`. `CreateView` is not idempotent, so a caller must create the view when no retry is running and no view exists.
- C, `src/RadiationEngine.cpp:RadiationEngine::PollInput`: one `ReadInputBits()` read is stored in `_polledInputBits`, and every key edge (debug toggle, filter replace, repair) is derived from that one mask. A second `GetAsyncKeyState` read could disagree with the stored bits and fire or miss an edge.
- D, `src/Hooks_DomeFollow.cpp:Hooks::QueueDomeFollow`: the skip threshold is `RadInternal::kStormDomeFollowEpsilon` (64 units, `src/RadEngine_Internal.h`). `lastFollowPos` is stamped inside the main-thread task only after `PlayerUpdatesReady` passes; if the task bails, `followPosValid` is cleared so the next call moves the layer.
- Debug queue, intentional: `src/RadEngine_Debug.cpp:RadiationEngine::DrainDebugCommands` no longer clears `_pendingDebugCmds` on load or not-ready ticks. The queue stays bounded at 32 (`kMaxPendingDebugCmds`, oldest dropped). Commands such as `storm:N` queued before the first tick therefore run later, on purpose.
- Not verified in game.

## Diagnostics console opacity (armor figure see-through fix)

- Panel, card and control backgrounds were 0.4-0.7 alpha, so the world showed through the armor figure and limb cards; raised to 0.92-0.96 (`deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css:pip-card-bg`).
- Console chrome is opaque now (`#debugPanel` gradient alpha 1, header and footer 0.96): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css:debugPanel`.
- `.armor-figure` uses `mix-blend-mode: screen`, so its backdrop `.armor-schematic` is solid (`#07130a`, `isolation: isolate`); a one-line Constraint comment sits above `.armor-figure`: `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css:armor-figure`.
- `#debugBackdrop` stays translucent by design, and the HUD layout tab has no CSS transparency dependency: `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css:debugBackdrop`.
- Not verified in game (no visual check in Ultralight): `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.css:armor-schematic`.

## Debug panel starts closed; JS never self-opens it

- Root cause (in-game symptom: the diagnostics console shows at start, and Esc, CLOSE and Shift+F8 do not close it): the DOMContentLoaded handler in `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js:window` ran `SetDebugMenu(1)` whenever `window.radDebug` was undefined. Native registers `radDebug` with `RegisterJSListener` from the DOM-ready callback in `src/RadHud.cpp:RadHud::CreateView`, which runs after the page's DOMContentLoaded, so the guard was always true in game. The panel opened with native `_debugOpen` false and the view unfocused, so Esc and clicks went to the game. The same block also pushed mock vitals, survival, armor and tuning data into the live HUD.
- Fix: that block now runs only behind an explicit `#preview=1` hash (standalone preview). Every load calls `window.SetDebugMenu(0)` first, so the panel is hidden until native `SetDebugMenu(true)` opens and focuses it.
- Native side: the DOM-ready callback in `src/RadHud.cpp:RadHud::CreateView` pushes `SetDebugMenu("0")`, and a newly created view resets `_debugOpen` to false, so native and page state agree on every (re)created view.
- Contract guards: `tests/verify_debug_menu_close.py` (hidden first paint, single `SetDebugMenu(1)` gate, DOM-ready push) and `tests/showcase_tests.js` (DOMContentLoaded leaves the panel closed with no native bridge).
- Not verified in game: the in-game open, Esc, CLOSE and Shift+F8 sequence after this change.

## Debug panel follows native state on every transition

- Log evidence (02:24 session): native open at 02:24:21.261 came through `src/RadiationEngine.cpp:RadiationEngine::PollInput` to `src/RadHud.cpp:RadHud::ToggleDebugMenu` case 2 (no deferred or pending log line, so not the pending path). Native close at 02:24:25.417 and the JS `close` at 02:24:23.293 and 02:24:27.690 each reached `src/RadHud.cpp:RadHud::SetDebugMenu` and the PrismaUI log shows Unfocus, SetViewOwnsEscape(false) and SetViewRole(kWidget). Native and page state agree after 27.691, so no state mismatch is visible in that trace.
- Old gap: `src/RadHud.cpp:RadHud::SetDebugMenu` had a no-view branch that latched `_debugOpen = true` with no panel to show. It now drops an open request there.
- Every page push goes through `src/RadHud.cpp:RadHud::PushDebugPanelState`, which records the delivered state in `_debugPushedOpen`. `src/RadHud.cpp:RadHud::SyncDebugPanelState` runs on the 4 Hz tick from `src/RadiationEngine.cpp:RadiationEngine::Update` and re-sends only when the delivered state differs. A new view resets the delivered state in `src/RadHud.cpp:RadHud::CreateView`.
- Load and new game call `src/RadiationEngine.cpp:RadiationEngine::ResetDebugKeyBaseline`, which sets the Shift+F8 edge baseline to the key state held at load time. `src/RadHud.cpp:RadHud::ResetDebugMenuForSession` also clears a pending close request.
- Contract guards: `tests/verify_debug_menu_close.py:main`.
- Not verified in game: whether the Ultralight compositor repaints the hidden panel after a native-initiated close. The PrismaUI log shows `committed-frame reuse` with `contentChanged=0` after the 27.691 close, which could mean a stale frame on screen. This is not proven and a page-state fix does not address it.

## Debug menu close destroys and re-creates the overlay view

- Symptom: after the debug menu closed, the compositor kept the last frame of overlay view 5 (radhud.html). The DOM hid the panel, but the pixels stayed.
- Arming: `src/RadHud.cpp:RadHud::SetDebugMenu` arms the refresh only when a close follows an open (`wasOpen`). The focus give-up path never opened, so it does not arm.
- Pump: `src/RadHud.cpp:RadHud::PumpDebugViewRefresh` runs from `src/RadiationEngine.cpp:RadiationEngine::Update` (4 Hz). It waits 100 ms, destroys view 5, and re-creates it through `src/RadHud.cpp:RadHud::CreateView` in the same pass. The re-created view is hidden until its DOM is ready and resets its caches.
- State machine: `src/RadCorrectnessPolicy.h:RadCorrectnessPolicy::DebugViewRefreshStep`. An open request cancels an armed refresh. Tests: `tests/correctness_policy_tests.cpp:main`.
- Test cases in `tests/correctness_policy_tests.cpp:main`: never-opened close and time alone do not refresh; a close after open arms at exactly 100 ms; a second close while armed keeps the first due time; an open while armed, or at the due time, cancels; a destroyed view is always re-created; idle is terminal; a second cycle re-arms from its own close; timing stays correct past 32 bits.
- Load and new game: `src/RadHud.cpp:RadHud::ResetDebugMenuForSession` cancels a pending refresh.
- Trade-off: view 5 also hosts the gas-mask overlay and storm banner, so they blink off for the DOM reload after each close. Toxicity and filter widget views are untouched.
- Contract guards: `tests/verify_debug_menu_close.py:main`.
- Deferred while the built-in HUD editor is open or a pause or message menu is up: `src/RadHud.cpp:RadHud::PumpDebugViewRefresh` checks `src/RadHudLayout.cpp:RadHud::LocalHudEditorPreviewOn` and `RE::UI::GetMenuOpen` before it destroys view 5. The armed refresh stays pending and runs when they close. The editor lives in view 5, so a destroy during the editor would leave `g_layout.preview` true.
- Not verified in game: whether destroy and re-create clears the stale frame, and how long the blink lasts.

## Enclosed shelter: no wind chill, 8 C ambient floor (not built here, not verified in game)

- Roof: `src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::ProbeRoof` casts one straight-up ray and four angled rays from head height. `src/ClimateShelterPolicy.h:RoofCover` is true when the straight-up ray hits and at least 3 of the 5 rays hit in total. This is the radiation roof-fan rule (`src/RadEngine_Shelter.cpp:RadiationEngine::ComputeEnclosedByStructure`, minimum `src/RadEngine_Internal.h:kRoofFanMinHits`). The climate-only rule that accepted 2 hits with the straight-up ray is removed.
- Shared ray helper: `src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::ProbeRoof` and `src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::ProbeWallHits` call `src/RadEngine_Internal.h:RayHitsStructure` unchanged. Radiation uses the same helper. Climate's own copy had the same logic, so this swap does not change the roof query by itself.
- Walls: `src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::ProbeWallHits` casts 8 horizontal rays from 80 units above the player, out to 600 units. `src/ClimateShelterPolicy.h:Classify` needs at least 5 hits for walled. The earlier 24-unit minimum-distance cut (`src/ClimateShelterPolicy.h:WallRayCounts`, now removed) dropped any wall hit closer than 24 units to the ray origin. A wall the player stands against may be that close (not measured). Risk: if the default ray filter hits the player's own body, open ground could read as walled. UNVERIFIED, decide in game.
- Wind: `src/ClimateShelterPolicy.h:FromLevel` sets the wind shelter to 1 when enclosed and 0.75 when only roofed. `src/ClimatePolicy.h:RadClimatePolicy::EffectiveWindShelter01` returns 1 indoors. Open spots keep the raw wind.
- Ambient floor: src/ClimateEnvironmentPolicy.h:ShelteredAmbientC raises cold outdoor air to the floor when enclosed. Partial shelter blends toward the floor. The default floor is 8.0 C. INI [Climate] EnclosedAmbientFloorC (range -10 to 20) is sanitized in src/ClimateRuntime.cpp. INI [Climate] EnclosedShelterEnabled=0 turns the enclosed level off and leaves roofed-open shelter.
- Timing: `src/ClimateShelterPolicy.h:ShouldProbe` re-probes after the player moves at least 96 units. `src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::Sample` and `src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::SampleRooflessInterior` also re-probe when `src/ClimateShelterPolicy.h:ShouldReprobeTimed` finds that the real time since `_lastProbeTime` (`std::chrono::steady_clock`) has passed 1.5 seconds open or 3.0 seconds enclosed. Game days are not used, because game time jumps during waits and time skips, which would make the schedule fire at the wrong pace. `src/ClimateShelterPolicy.h:Observe` requires a second matching probe before the level changes (hysteresis), so one odd sample does not flip the level.
- src/ClimateRuntime.cpp:Update samples the shelter once per tick and feeds shelter01, windShelter01 and the sheltered ambient into the climate step.
- Roofless interiors: `src/ClimateShelterPolicy.h:FromRooflessInterior` sets enclosed01 from the roof, so an open roofless interior no longer reads as enclosed. The earlier version set it to 1 for every roofless interior. Wind shelter stays 1 for every listed roofless interior, because `src/ClimatePolicy.h:RadClimatePolicy::EffectiveWindShelter01` returns 1 for indoor cells. This may be wrong for an open roofless interior. Flagged, not changed.
- Roofed thermal: `src/ClimateShelterPolicy.h:FromLevel` gives a roofed exterior thermal shelter of 0.5 and rain cover of 1.0. The base commit gave 0.25 for the same case (`src/ClimateShelterPolicy.h:FromState`). The CHANGELOG records this change.
- Roof rays in real interiors: root cause not established. For `src/RadEngine_Internal.h:RayHitsStructure`, the zero-filter theory below is UNVERIFIED and not applied. The helper is shared with radiation, so any filter change there also changes the radiation roof fan.
- UNVERIFIED zero-filter theory, not applied: the CommonLibF4 `RE::bhkPickData` default constructor leaves the collision filter at zero, which matches no layer, so `TESObjectCELL::Pick` could never hit. `src/RadEngine_Internal.h:RayHitsStructure` builds its pick with that default constructor. The claimed fix sets the pick's layer to `kDroppingPick` after `SetStartEnd`. No layer change is made in this branch.
- Zero-filter sources, for `src/RadEngine_Internal.h:RayHitsStructure`: (1) PrismaUI `COMMONLIB_FINDINGS.md` in the RE Docs vault (`other-online/github/PRISMA-USER-INTERFACE-FRAMEWORK__Prisma-Matrix/docs/knowledge-base/legacy/Prisma/COMMONLIB_FINDINGS.md`, callout dated 2026-07-16, lines 98-119). This is the only source for the claim. It says the claim was decompile-proven only after three in-game test cycles on camp placement (1085 consecutive ticks with zero hits). That decompile is not in the mirror and was not re-checked here. (2) Garden CommonLib, source 1, `include/RE/B/bhkPickData.h` on its default branch, read in this pass: it declares the no-argument constructor, `SetStartEnd` and `HasHit`, and no filter member. The constructor body is not in the header. `include/RE/C/COL_LAYER.h` declares `kUnidentified` and `kDroppingPick`. (3) RE Docs vault `local/Havok_Research/animation/garden-tools-520/RE-022/README.md`, line 468: describes the object-pair test only, where a zero in either collision info means the pair always collides. It does not cover ray queries, so it neither supports nor refutes the theory. (4) `docs/CHANGELOG.md`, 2026-07-13 (pt4) entry: a radiation roof-fan reading scored 2 of 5 when two angled rays hit a building's wall. A zero filter would not produce a wall hit, so this conflicts with the theory unless the radiation and climate calls differ in a way not yet checked. (5) The in-repo comment above `src/RadEngine_Internal.h:RayHitsStructure` says the constructor's default filter is the one the game's crosshair pick uses, and that this pick hits statics. That comment cites no decompile, and it contradicts the theory. The radiation roof fan relies on this helper. (6) `src/RadEngine_Storm_Weather.cpp:ResolveGroundHeight` builds the same default pick and sets no layer (lines 48-50). If that ground query works in game, it is evidence against the theory. Not checked in game.
- Not a source: the F4RR notes at `RE Docs/local/F4RR-co/RE-Work/Runtime/03-FINDINGS.md` do not contain this claim (checked in this pass), so do not cite them for it. Source 3 (RE server) was not reached in this pass. Source 4 was not checked in this pass. Source 5 was not used. Anchor: `src/RadEngine_Internal.h:RayHitsStructure`.
- NEEDS NOMAD: whether to test the zero-filter fix in game. A fix in the shared helper also changes the radiation roof fan. Until then the roof rays stay as they are. Anchor: `src/RadEngine_Internal.h:RayHitsStructure`.
- Tests, not run here (no builds or scripts on this PC): `tests/climate_policy_tests.cpp:main` covers `src/ClimateShelterPolicy.h:RoofCover`, `ShouldReprobeTimed` and `FromRooflessInterior`. `tests/climate_wetness_policy_tests.cpp:main` covers `src/ClimateEnvironmentPolicy.h:ShelteredAmbientC`. `tests/verify_climate_runtime.py` checks the sampler wiring in `src/ClimateRuntime.cpp:ClimateRuntime::Update` and the probe calls in `src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::ProbeRoof`.
- Not verified in game: the HUD wind readout (`src/ClimatePolicy.h:RadClimatePolicy::VisibleWindMps`), the COLD versus FREEZING label, roof ray results in real interiors (`src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::ProbeRoof`), and wall ray hits in real interiors (`src/ClimateShelterRuntime.cpp:ClimateShelterRuntime::ProbeWallHits`).