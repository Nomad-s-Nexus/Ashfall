# In-game test checklist (Rad Overhaul, Very Hard)

Player setup: difficulty VERY HARD (not vanilla Survival), Papyrus logging enabled
(`[Papyrus] bEnableLogging=1` in Fallout4Custom.ini) so `Documents\My Games\Fallout4\Logs\Script\Papyrus.0.log`
records the `[RadSurvivalQuest]` traces below. Run from a save that already has the RadSurvival quest running and
at least one empty bottle, one food item, one drink item and a gas mask.

Do not mark an item PASS unless the expected log line is in Papyrus.0.log / FalloutAnomaly.log and the game state
matches. Mark everything else NOT RUN.

## 1. Food/drink relief (Very Hard)
- **Eat one food item** (e.g. a Mole Rat steak). Expected: hunger falls by the item's portion, and Papyrus.0.log
  shows `consume-arm equip <form>` and `consume-confirm remove <form> x1` in either order, then exactly one
  `consume-apply <form> x1 ate=true`.
  Proof: hunger value in the HUD drops; there is exactly one apply line for that item.
- **Drink one drink item.** Expected: thirst falls, `consume-apply ... drank=true`.
- **Eat the last item of a stack.** Expected: same single `consume-apply x1`, no double credit.
- **Drop a food item, then (after waiting more than 2 s) eat another.** Expected: the drop alone produces no
  `consume-apply`; the later eat produces exactly one. Proof: count `consume-apply` lines.
- **Use a chem (e.g. Jet).** Expected: no `consume-apply` (chems are not food) and no hunger/thirst change.
- **Open the Pip-Boy after each step and re-check the pools** (they are mirrored GLOBs) to see the value actually
  moved, not just the trace.

Expected relief amounts (computed from `PortionFor` with the 0.8 floor and 1.5 cap, **not verified in game**):
- **Cheap meal** (Cram 25 caps, Blamco 10, Pork n Beans 10): hunger falls by 0.18-0.21 (at least 0.15 in the 0..1 GLOB),
  one "Hunger eased" notification, and the stage changes on that use if the debt was within one band of a threshold.
- **Cheap drink** (Dirty Water 5, Nuka-Cola 20): thirst falls by 0.21-0.25 (at least 0.18 GLOB), one "Thirst quenched"
  notification. From stage 1, one cheap drink plus one more item must drop the stage visibly.
- **High-value meal** (steakBrahmin 40, SteakDeathclaw 130): 0.24-0.30 hunger; the 1.5 portion cap applies.
- **High-value drink** (Purified Water 75): up to 0.38 thirst.
- **Alcohol is the documented exception:** a beer relieves only about 0.06 thirst because `AlcoholHydrationScale` is
  0.25, and shows "Thirst eased". Record it, do not mark it a failure.

## 2. Fountain sneak-activation
- **Sneak + activate a drinking fountain holding an empty bottle.** Expected: one HUD "Filled a bottle with dirty
  water", one empty bottle consumed, one Dirty Water added, and (open question, record both outcomes) whether the
  drink's rads also applied in the same press. Proof: FalloutAnomaly.log `[Water] filled one ...` line; bottle count.
- **Activate the same fountain without sneaking.** Expected: drinks normally, no bottle consumed, no `[Water] filled`
  line.
- **Turn SNEAK TO FILL off in the SETTINGS tab, then activate the fountain without sneaking.** Expected: one bottle
  filled (HUD message, `[Water] filled` line); record whether the fountain's drink also applied in the same press.
  Turn it back on: plain activation drinks only again.
- **Turn AUTO-EQUIP MASK off, walk into rad weather.** Expected: the mask stays off and the AUTO chip reads OFF. With
  AUTO-EQUIP MASK on and EQUIP IN COMBAT off, enter rad weather during combat: chip reads HELD, mask goes on after combat.

## 3. Power-armor overlay
- **Wear a power-armor helmet and a gas mask.** Expected: no visor view (PNG overlay is off, see section 12; the 3D view is the visor); foley/filter values still update.
  Proof: no overlay on screen; FalloutAnomaly.log `[ChokeDiag] realMasked=true` while `maskOn` is false is not logged,
  so record a screenshot state instead.
- **Leave the PA helmet on, debug `mask:on`.** Expected: the 3D mask view shows (override wins).

## 4. Visor vs HUD (3D mask view; the PNG overlay is off)
- **Wear the mask in a settlement and accept a notification / gain XP / open a loot list.** Expected: the top-left
  message text, the bottom-centre XP bar and the centre loot/prompt are readable through the visor; the corners and
  edges keep the visor look. Proof: visual check (no log line).
- **Low filter.** Expected: condensation fog appears only at the edges; HUD areas stay clear.

## 5. Mask auto-don / hysteresis
- **Step out of shelter into rad weather.** Expected: after ~3 s of stable exposure the mask is donned; log shows the
  mask equip and no repeated equip/unequip. Proof: count mask foley/equip events over 30 s.
- **Stand in a doorway or under a roof edge.** Expected: no rapid equip/unequip flapping (3 s hysteresis).
- **Enter a building while auto-masked.** Expected: the mask we auto-equipped is removed (manual mask is not).
- **Load a save made while auto-masked, indoors.** Expected: the mask is removed after load.
- **Auto-donned mask, Gasmask Overhaul installed: let it play its mask draw animation, then go indoors.** Expected: the
  mask is still removed by us (ownership survives the unequip/re-equip within 2.5 s).
- **Enter a storm unmasked and wait.** Expected: no "breathing contaminated air" gasp before the auto-don lands.

## 6. Foreign mask + hotkeys
- **Wear a Gasmask Overhaul mask and press the replace/repair keys.** Expected: `[Filter] replace refused: no spare
  filters` / `[Condition] repair refused: no repair kits` (the keys consume OUR items only), and with our filter/kit in
  the inventory `[Filter] replaced` appears. Proof: FalloutAnomaly.log `[Input]`/`[Filter]`/`[Condition]` lines.
- **Plain gameplay, no menu open, press F6/F7 with a mask on.** Expected: the keys work every time (hotkeys must not
  depend on a menu being open or a cursor being visible).
- **Hold F6 while closing the Pip-Boy.** Expected: no replace fires on release of the menu (no phantom press).
- **Alt-tab to the desktop and press the keys.** Expected: nothing happens (foreground gate); no `[Filter] replaced`.
- **Open the Pip-Boy / pause menu and press the keys.** Expected: nothing happens (blocking-menu gate).
- **Bind a duplicate or F8 in the INI, reopen the debug menu.** Expected: `[Input] ... both mask hotkeys disabled` or
  `rejected` warning lines; keys do nothing.

## 7. Vault 81 water
- **Sneak-fill a bottle in Vault 81 (main cell, CELL Vault81 0x02A728) and in Vault81Entry (0x000A9B62).** Expected:
  Purified Water + "Filled a bottle with purified water."
- **Same in Vault81Secret (Secret Vault 81 lab), Vault81Ext (exterior) and the holding cell.** Expected: Dirty Water.
  Proof: FalloutAnomaly.log `[Water] filled one` line names the output; HUD message.

## 8. Roofless interior rain (ConcordMuseum01 is in the shipped whitelist)
- **Stand under the open sky in ConcordMuseum01 (Museum of Freedom) while it rains outside.** Expected: the player's
  wetness rises; the climate layer changes only the wetness/thermal response, it does not add its own rain particles
  (proof: `[Climate] Roofless interior roof cover changed: cell=0x..., roofed=false, rainCover=0%` in
  FalloutAnomaly.log, and the wetness value climbs).
- **Stand under a roofed part of the same cell while it rains.** Expected: no wetness gain (proof:
  `roofed=true, rainCover=100%` for the same cell and wetness does not rise).
- **Stand in an unlisted interior (any other building) while it rains.** Expected: dry as before (no roofless log line
  for that cell).
- **Probe throttling in ConcordMuseum01:** the probe logs a debug line on every run
  (`[Climate] Roofless interior probe: cell=0x..., roofed=...`). Move less than 96 units without crossing a roof
  boundary: no new probe line. Then move 96 units or more, or leave and re-enter ConcordMuseum01: exactly one new
  probe line. The probe record is flushed immediately, so tailing the log live works; if in doubt, read the log after
  quitting. The throttle is
  `RadClimateShelterPolicy::ShouldProbe` (`>= kProbeMoveDistance` squared, or a cell change); its in-game firing rate
  is unverified until this step runs.

## 9. Needs persistence
- **New character:** hunger/thirst/sleep start at 0 (GLOB defaults). Log: `[RadSurvivalQuest] ready at game day ...`.
- **Load a save:** log `post-load anchor H/T/S=...` matches the HUD; no stale jump.

## 10. NPC outbreak, milestone 1 probe (RadOutbreak.esp, off unless the spell is added by hand)
- **Select a settler in the console and run `addspell XX000801`** (XX = RadOutbreak.esp load-order prefix). Expected log line
  `RadOutbreak start actor=... stage=1` in the Papyrus log.
- **Wait about 6 game hours (Wait menu or `wait`).** Expected: `RadOutbreak stage2`, then `RadOutbreak bed` and the settler
  lies in the nearest bed that is not in use. If the settler keeps working or stands still, the `Bed.Activate(Patient)` route is beaten by the
  workshop AI package: that is the answer this probe exists to give, report it.
- **After 24 game hours** the effect dispels (`RadOutbreak finish`) and the settler returns to work.
- Sounds are not wired yet (CoughSound/SneezeSound empty): silence is expected.

## 11. Empty bottle returned after drinking
- **Fill a bottle at a water source, then drink the Dirty or Purified Water.** Expected: the water is consumed, thirst drops, and one Empty Bottle (BottleEmpty01) is added back to the inventory. Log: `consume-apply ... drank=true`. Drinking other drinks adds no bottle. Unverified in game.

## 12. M50 mask view (3D visor) and PNG overlay off
- **Equip a gas mask in first person.** Expected: the 3D M50 interior (rubber seal, valve housings, glass) is drawn and no flat `gasmask_overlay.png` appears. Unverified in game.
- **Look for** missing-texture pink or white shapes, a black glass pane, the seal clipping the cheeks, and a frame-rate drop (278k tris).
