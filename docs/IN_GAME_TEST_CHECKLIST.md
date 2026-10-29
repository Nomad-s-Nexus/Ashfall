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

## 2. Fountain sneak-activation
- **Sneak + activate a drinking fountain holding an empty bottle.** Expected: one HUD "Filled a bottle with dirty
  water", one empty bottle consumed, one Dirty Water added, and (open question, record both outcomes) whether the
  drink's rads also applied in the same press. Proof: FalloutAnomaly.log `[Water] filled one ...` line; bottle count.
- **Activate the same fountain without sneaking.** Expected: drinks normally, no bottle consumed, no `[Water] filled`
  line.

## 3. Power-armor overlay
- **Wear a power-armor helmet and a gas mask.** Expected: no visor overlay; foley/filter values still update.
  Proof: no overlay on screen; FalloutAnomaly.log `[ChokeDiag] realMasked=true` while `maskOn` is false is not logged,
  so record a screenshot state instead.
- **Leave the PA helmet on, debug `mask:on`.** Expected: overlay shows (override wins).

## 4. Overlay vs HUD
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
