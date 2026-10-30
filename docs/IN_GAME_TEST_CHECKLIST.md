# In-game test checklist (Rad Overhaul, Very Hard)

Player setup: difficulty VERY HARD (not vanilla Survival), Papyrus logging enabled so
`Documents\My Games\Fallout4\Logs\Script\Papyrus.0.log` records the `[RadSurvivalQuest]` traces below.

This branch adds this file because the full checklist lives on the unmerged needs/disease branch
(`nomad/sweet-perlman-a88784`), which also adds the same path with sections 1-9. If both land, that file is the
superset and this section 1 is its relief step; otherwise this file is the relief checklist on its own.

## 1. Food/drink relief (Very Hard)

- **Eat a cheap meal** (e.g. Cram 25 caps, Blamco 10, Pork n Beans 10). Expected: hunger falls by 0.18-0.21, i.e. at
  least 15 HUD percentage points (>=0.15 in the 0..1 GLOB), and the stage/penalty changes on that use if the debt was
  within one band of a threshold. Proof: a `[RadSurvivalQuest] consumed <form> x1 ate=true drank=false` trace, one
  "Hunger eased" notification, and the hunger GLOB dropping by >=0.15.
- **Drink a cheap drink** (Dirty Water 5, Nuka-Cola 20). Expected: thirst falls by 0.21-0.25 (>=18 HUD points,
  >=0.18 GLOB), one "Thirst quenched" notification, one `consumed ... drank=true` trace. From stage 1, one cheap
  drink plus one more item must drop the stage visibly (the old build needed about three).
- **Eat a high-value meal** (steakBrahmin 40, SteakDeathclaw 130): 0.24-0.30 hunger; the 1.5-portion cap applies.
- **Drink a high-value drink** (Purified Water 75): up to 0.38 thirst.
- **Alcohol is the documented exception:** a beer should relieve only about 0.06 thirst because
  `AlcoholHydrationScale` is 0.25. Record it, do not mark it a failure.
- **Record the confirmed-use count for each item.** The count-drop confirmation on this branch is expected to give
  exactly one `consumed` line per use, but 0 or 2 is the open question this test exists to answer, not a FAIL by
  itself.
- **Use a chem (e.g. Jet).** Expected: no `consumed` line from this script, no notification, no pool change.
- **Open the Pip-Boy after each step** and compare the mirrored GLOBs with the HUD to confirm the pool actually moved.

Record which build the log came from: this branch traces `consumed <form> xN ate=... drank=...`; the needs/disease
branch traces `consume-arm` / `consume-confirm` / `consume-apply` instead.

Not covered here (other branches own those sections): fountain bottles, mask overlay, auto-don, hotkeys, needs
persistence.
