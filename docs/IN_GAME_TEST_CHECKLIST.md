# In-game test checklist (Rad Overhaul, Very Hard)

Player setup: difficulty VERY HARD (not vanilla Survival), Papyrus logging enabled so
`Documents\My Games\Fallout4\Logs\Script\Papyrus.0.log` records the `[RadSurvivalQuest]` traces below.

## 1. Food/drink relief (Very Hard)

- **Eat a cheap meal** (e.g. Cram, Blamco, Pork n Beans; 10-25 caps). Expected: hunger drops by 0.18-0.21 and, if the
  debt was within one band of a threshold, the stage/penalty changes on that use. Proof: one
  `[RadSurvivalQuest] consumed <form> x1 ate=true drank=false` line, one "Hunger eased" notification, and the HUD
  hunger value drops by >= 0.15.
- **Drink a cheap drink** (Dirty Water, Nuka-Cola). Expected: thirst drops by 0.21-0.25, one "Thirst quenched"
  notification, and one `consumed ... drank=true` line. From stage 1, one cheap drink plus one more item must drop the
  stage visibly (the old build needed about three).
- **Eat a high-value meal** (steakBrahmin, SteakDeathclaw, Purified Water). Expected: 0.24-0.38 relief; the cap is 1.5
  portions, not unlimited.
- **Use a chem (e.g. Jet).** Expected: no `consumed` line for it from this script, no notification, no pool change.
- **Open the Pip-Boy after each step** and compare the mirrored GLOBs with the HUD to confirm the pool actually moved.

Record which build the log came from: this branch traces `consumed <form> xN ate=... drank=...`; the needs/disease
branch traces `consume-arm` / `consume-confirm` / `consume-apply` instead. Both should produce exactly one confirmed
use per item.

Not covered here (other branches own those sections): fountain bottles, mask overlay, auto-don, hotkeys, needs
persistence.
