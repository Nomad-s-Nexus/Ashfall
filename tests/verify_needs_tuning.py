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


def code_only(papyrus: str) -> str:
    return "\n".join(line.split(";", 1)[0] for line in papyrus.splitlines())


policy = text(SRC / "RadNeedsTuningPolicy.h")
runtime = text(SRC / "RadNeedsTuning.cpp")
quest = text(ROOT / "deploy/Scripts/Source/User/RadSurvivalQuest.psc")
diseases = text(ROOT / "deploy/Scripts/Source/User/RadSurvivalDiseases.psc")
ini = text(ROOT / "deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini")
js = text(VIEW / "radhud.js")
html = text(VIEW / "radhud.html")
debug = text(SRC / "RadEngine_Debug.cpp")
mask = text(SRC / "RadEngine_Mask.cpp")
climate = text(SRC / "ClimateRuntime.cpp")
needs_hud = text(SRC / "RadHudNeeds.cpp")
needs_view = text(VIEW / "hud_needs.html")

specs = re.findall(r'\{\s*"(\w+)",\s*([0-9.]+)f,\s*([0-9.]+)f,\s*([0-9.]+)f,\s*(true|false)\s*\}', policy)
keys = [s[0] for s in specs]
check(len(keys) == 42, f"policy key count changed: {len(keys)}")

# Constraint: the enum order is the table order; the Papyrus keys come first.
enum_body = re.search(r"enum Key : std::size_t\s*\{(.*?)\};", policy, re.S)
enum_names = []
if enum_body:
    for name in re.findall(r"\b(k[A-Z]\w*)\b", enum_body.group(1)):
        if name not in ("kPapyrusKeys", "kKeyCount"):
            enum_names.append(name)
check([n[1:] for n in enum_names] == keys, "the Key enum must list the same names in the same order as kSpecs")

papyrus_keys = keys[:16]
quest_indexes = {"kTuneRate": 0, "kTuneHunger": 1, "kTuneThirst": 2, "kTuneSleep": 3, "kTuneFood": 4,
                 "kTuneDrink": 5, "kTuneSleepRecovery": 6}
disease_indexes = {"kTuneDiseases": 7, "kTuneBaseRisk": 8, "kTuneRadWeather": 9, "kTuneRain": 10, "kTuneWound": 11,
                   "kTuneSwim": 12, "kTuneTaintedFood": 13, "kTuneTaintedWater": 14, "kTuneContaminated": 15}
for source, table in ((quest, quest_indexes), (diseases, disease_indexes)):
    for name, index in table.items():
        check(re.search(rf"int Property {name} = {index} Auto Const", source) is not None,
              f"{name} must equal policy index {index}")
check(papyrus_keys[7] == "DiseasesEnabled" and papyrus_keys[15] == "RiskContaminated", "the Papyrus key block moved")

reference = {key: float(ref) for key, ref, _, _, _ in specs}
for source, prop, key in (
        (quest, "HourlyHunger", "HourlyHunger"), (quest, "HourlyThirst", "HourlyThirst"),
        (quest, "HourlySleepDebt", "HourlySleepDebt"), (quest, "FoodScale", "FoodScale"),
        (quest, "DrinkScale", "DrinkScale"), (quest, "SleepRecoveryPerHour", "SleepRecovery"),
        (diseases, "HourlyRisk", "DiseaseBaseRisk"), (diseases, "RiskPerRadHour", "RiskRadWeather"),
        (diseases, "RiskPerRainHour", "RiskRain"), (diseases, "RiskPerHit", "RiskWound"),
        (diseases, "RiskPerSwim", "RiskSwim"), (diseases, "RiskPerTaintedFood", "RiskTaintedFood"),
        (diseases, "RiskPerTaintedWater", "RiskTaintedWater"), (diseases, "RiskPerContaminated", "RiskContaminated")):
    match = re.search(rf"float Property {prop} = ([0-9.]+) Auto", source)
    check(match is not None and abs(float(match.group(1)) - reference.get(key, -1)) < 1e-9,
          f"{prop} script default must match the policy reference for {key}")

for token in ("Tuned(kTuneRate, 1.0)", "Tuned(kTuneHunger, HourlyHunger)", "Tuned(kTuneThirst, HourlyThirst)",
              "Tuned(kTuneSleep, HourlySleepDebt)", "Tuned(kTuneFood, FoodScale)", "Tuned(kTuneDrink, DrinkScale)",
              "Tuned(kTuneSleepRecovery, SleepRecoveryPerHour)"):
    check(token in quest, f"the quest must read {token}")
for token in ("Tuned(kTuneBaseRisk, HourlyRisk)", "Tuned(kTuneWound, RiskPerHit)", "Tuned(kTuneSwim, RiskPerSwim)",
              "Tuned(kTuneRadWeather, RiskPerRadHour)", "Tuned(kTuneRain, RiskPerRainHour)",
              "Tuned(kTuneTaintedFood, RiskPerTaintedFood)", "Tuned(kTuneTaintedWater, RiskPerTaintedWater)",
              "Tuned(kTuneContaminated, RiskPerContaminated)"):
    check(token in diseases, f"the diseases script must read {token}")
check("DiseasesEnabled &&" in diseases and "Tuned(kTuneDiseases, 1.0) >= 0.5" in diseases,
      "DiseasesOn must combine the script property with the tuned toggle")
check(not re.search(r"if[^\n]*(?<!\w)DiseasesEnabled(?!\s*&&)", diseases.replace("DiseasesEnabled && Tuned", "")),
      "every disease gate must go through DiseasesOn()")
for source, name in ((quest, "the quest"), (diseases, "the diseases script")):
    check("RadNatives.TuningOverride(aiKey)" in source, f"{name} must read the override through RadNatives")
    check(not re.search(r"\bnative\b", code_only(source)), f"{name} must not declare any native function")
    check(not re.search(r"(?<!RadNatives\.)\bTuningOverride\(", code_only(source)), f"{name} must not call TuningOverride outside RadNatives")
    check("if raw > 0.0" in source and "return raw - 1.0" in source, f"{name} Tuned must decode the +1.0 encoding")
check("value + 1.0f : 0.0f" in runtime, "the native must encode an override as value + 1.0 and unset as 0.0")
check('"RadNatives", "TuningOverride"' in runtime and runtime.count('BindNativeMethod(') == 1,
      "the native must be bound once, as a global on RadNatives")
check('"RadSurvivalQuest"' not in runtime and '"RadSurvivalDiseases"' not in runtime, "no per-quest binding may remain")
check(re.search(r"PapyrusTuningOverride\(std::monostate,", runtime) is not None, "the native must take no self parameter")
natives = text(ROOT / "deploy/Scripts/Source/User/RadNatives.psc")
relief_cpp = text(SRC / "RadReliefConfig.cpp")
natives_code = code_only(natives)
check(re.match(r"\s*ScriptName RadNatives native hidden\b", natives_code) is not None, "RadNatives must be a native hidden script")
declared = re.findall(r"^(\w+) Function (\w+)\([^)]*\) global native\s*$", natives_code, re.M)
check(sorted(declared) == [("float", "TuningOverride"), ("int", "GetReliefFlags")],
      "RadNatives must declare exactly the two global natives")
check(natives_code.count("Function") == 2, "RadNatives must contain no other function")
check('"RadNatives", "GetReliefFlags"' in relief_cpp and relief_cpp.count('BindNativeMethod(') == 1 and
      "PapyrusGetReliefFlags(std::monostate," in relief_cpp, "the relief native must be bound once as a global on RadNatives")
for sibling in (ROOT / "deploy/Scripts/Source/User").glob("*.psc"):
    if sibling.name != "RadNatives.psc":
        check(not re.search(r"\bnative\b", code_only(text(sibling))), f"{sibling.name} must not declare a native function")
check("kPapyrusKeys" in runtime, "the native must reject non-Papyrus keys")
check('constexpr const char* kSection = "Tuning"' in runtime, "overrides persist in [Tuning]")

check("RadNeedsTuning::Install()" in text(SRC / "main.cpp"), "plugin load must install the tuning bridge")
check('a.rfind("tune:", 0) == 0' in debug and "RadNeedsTuning::Handle(" in debug, "the debug dispatcher must route tune: commands")
check("ApplyTuning()" in debug and "LoadFilterIni()" in debug, "tune: commands must re-apply the engine overlay")
check("void RadiationEngine::ApplyTuning()" in mask and "ApplyTuning();" in mask, "LoadFilterIni must apply the overlay")
check("setNeedsTuning" in text(SRC / "RadHud.cpp") and "PushNeedsTuning(RadNeedsTuning::Json())" in text(SRC / "RadHud.cpp"),
      "opening the debug menu must push the current tuning")
for token in ("coolingScale", "wetnessGainScale", "wetnessDryScale"):
    check(token in climate, f"the climate runtime must feed {token}")
src_engine = text(SRC / "RadiationEngine.cpp")
audio = text(SRC / "RadEngine_Audio.cpp")
storm = text(SRC / "RadEngine_Storm.cpp")
check("kAutoEquipMask" in src_engine and "kAutoEquipInCombat" in src_engine and "IsInCombat()" in src_engine,
      "the auto-don must honour the toggles and combat")
check("_wasRadExposed = 0;" in src_engine, "a blocked auto-don must re-arm so it fires after combat")
check("SetLive(" in mask and "SetLive(Key::kStormMinMinutes" in mask, "ApplyTuning must publish the values in force")
check("applied != atten" in audio, "a volume slider must retune the playing bed")
check("EffectiveStormGap" in storm and "_stormMaxRealMin = _stormMinRealMin;" not in mask,
      "the storm gap must be computed on read and never written back")
check("_stormScheduled = false" in mask, "a moved storm gap must reschedule")
check("kAlwaysShowNeeds" in needs_hud and '"always"' in needs_hud, "the needs HUD payload must carry the always-show flag")
check("e.hidden=stage===0&&!always" in needs_view, "the needs view must honour the always-show flag")

tuning_js = js[js.index("var TUNING = ["):js.index("function tuneText")]
js_keys = set(re.findall(r'\["([A-Z]\w+)", "[A-Z0-9 /\-]+"', tuning_js))
for key in keys:
    check(key in js_keys, f"the panel must list {key}")
    check(re.search(rf"^{key}=", ini, re.M) is not None, f"the INI must document [Tuning] {key}")
check(js_keys <= set(keys), f"the panel lists unknown keys: {sorted(js_keys - set(keys))}")
check("[Tuning]" in ini and "[Survival]" not in ini, "the INI needs a [Tuning] section")
check('id="tuneRows"' in html and "tune:reset" in html, "the SETTINGS tab needs the host and reset button")
check("window.setNeedsTuning" in js, "the view must render the tuning payload")

if failures:
    print("needs tuning contract failures:")
    for failure in failures:
        print(f"  FAIL {failure}")
    sys.exit(1)

print("Needs tuning verified: policy keys, quest decode, debug command, panel and INI")
