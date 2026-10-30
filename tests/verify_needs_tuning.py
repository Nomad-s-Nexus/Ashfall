#!/usr/bin/env python3
"""Contract for the live survival-rate tuning: policy keys, quest script keys and defaults, debug command, panel."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
VIEW = ROOT / "deploy/PrismaUI_F4/views/Interface/FalloutAnomaly"
failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8") if path.is_file() else ""


policy = text(SRC / "RadNeedsTuningPolicy.h")
runtime = text(SRC / "RadNeedsTuning.cpp")
quest = text(ROOT / "deploy/Scripts/Source/User/RadSurvivalQuest.psc")
ini = text(ROOT / "deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini")
js = text(VIEW / "radhud.js")
html = text(VIEW / "radhud.html")

specs = re.findall(r'\{\s*"(\w+)",\s*([0-9.]+)f,\s*([0-9.]+)f,\s*([0-9.]+)f\s*\}', policy)
keys = [s[0] for s in specs]
check(keys == ["RateMultiplier", "HourlyHunger", "HourlyThirst", "HourlySleepDebt", "FoodScale", "DrinkScale"],
      f"policy keys changed order or content: {keys}")

# Constraint: the quest passes the policy index as the key and the script default as the fallback.
indexes = dict(zip(("kTuneRate", "kTuneHunger", "kTuneThirst", "kTuneSleep", "kTuneFood", "kTuneDrink"), range(6)))
for name, index in indexes.items():
    check(re.search(rf"int Property {name} = {index} Auto Const", quest) is not None,
          f"{name} must equal policy index {index}")

reference = {key: float(ref) for key, ref, _, _ in specs}
for prop, key in (("HourlyHunger", "HourlyHunger"), ("HourlyThirst", "HourlyThirst"),
                  ("HourlySleepDebt", "HourlySleepDebt"), ("FoodScale", "FoodScale"), ("DrinkScale", "DrinkScale")):
    match = re.search(rf"float Property {prop} = ([0-9.]+) Auto", quest)
    check(match is not None and abs(float(match.group(1)) - reference.get(key, -1)) < 1e-9,
          f"{prop} script default must match the policy reference for the panel")

for token in ("Tuned(kTuneRate, 1.0)", "Tuned(kTuneHunger, HourlyHunger)", "Tuned(kTuneThirst, HourlyThirst)",
              "Tuned(kTuneSleep, HourlySleepDebt)", "Tuned(kTuneFood, FoodScale)", "Tuned(kTuneDrink, DrinkScale)"):
    check(token in quest, f"the quest must read {token}")
check("float Function TuningOverride(int aiKey) native" in quest, "the quest must declare the native override")
check("if raw > 0.0" in quest and "return raw - 1.0" in quest, "Tuned must decode the +1.0 override encoding")
check("value + 1.0f : 0.0f" in runtime, "the native must encode an override as value + 1.0 and unset as 0.0")
check('"TuningOverride", PapyrusTuningOverride' in runtime, "the native must be bound on RadSurvivalQuest")

check("RadNeedsTuning::Install()" in text(SRC / "main.cpp"), "plugin load must install the tuning bridge")
check('a.rfind("tune:", 0) == 0' in text(SRC / "RadEngine_Debug.cpp") and "RadNeedsTuning::Handle(" in text(SRC / "RadEngine_Debug.cpp"),
      "the debug dispatcher must route tune: commands")
check("setNeedsTuning" in text(SRC / "RadHud.cpp") and "PushNeedsTuning(RadNeedsTuning::Json())" in text(SRC / "RadHud.cpp"),
      "opening the debug menu must push the current tuning")

for key in keys:
    check(f'["{key}"' in js, f"the panel must list {key}")
    check(re.search(rf"^{key}=", ini, re.M) is not None, f"the INI must document [Survival] {key}")
check("[Survival]" in ini, "the INI needs a [Survival] section")
check('id="tuneRows"' in html and "tune:reset" in html, "the SURVIVAL tab needs the slider host and reset button")
check("window.setNeedsTuning" in js, "the view must render the tuning payload")

if failures:
    print("needs tuning contract failures:")
    for failure in failures:
        print(f"  FAIL {failure}")
    sys.exit(1)

print("Needs tuning verified: policy keys, quest decode, debug command, panel and INI")
