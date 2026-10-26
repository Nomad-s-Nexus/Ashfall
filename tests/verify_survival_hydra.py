"""Survival (Hydra) contract checks."""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "deploy/Scripts/Source/User/RadSurvival.psc"
COMPILED = ROOT / "deploy/Scripts/RadSurvival.pex"
SCRIPT_FUNCTIONS = ROOT / "deploy/Hydra/ScriptFunctions/RadSurvival.json"
SAVE_MAPS = ROOT / "deploy/Hydra/ScriptSaveMaps/RadSurvival.json"
TUNING = ROOT / "deploy/F4SE/Plugins/FalloutAnomaly/survival.json"
NATIVE_NEEDS = ROOT / "deploy/F4SE/Plugins/FalloutAnomaly/needs.json"

PEX_MAGIC = bytes((0xDE, 0xC0, 0x57, 0xFA))
NAMESPACE = "RadSurvival"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def text(path):
    return path.read_text(encoding="utf-8")


def code_only(papyrus):
    lines = []
    for line in papyrus.splitlines():
        cut = line.find(";")
        lines.append(line if cut < 0 else line[:cut])
    return "\n".join(lines)


check(SOURCE.is_file(), f"missing {SOURCE.relative_to(ROOT)}")
check(COMPILED.is_file(), f"missing {COMPILED.relative_to(ROOT)}")
source = text(SOURCE) if SOURCE.is_file() else ""
code = code_only(source)

if COMPILED.is_file():
    data = COMPILED.read_bytes()
    check(data[:4] == PEX_MAGIC, "compiled PEX does not have the FO4 PEX header")
    check(b"OnGameDataReady" in data, "compiled PEX is missing OnGameDataReady")
    check(b"StartRepeatingGameTime" in data, "compiled PEX is missing the repeating timer call")

check("extends Quest" not in code, "survival core must not require a quest record")
for forbidden in ("HC_Manager", "HC_", "GetHardcore", "SurvivalModeEnabled"):
    check(forbidden not in code, f"survival core must not touch vanilla survival state ({forbidden})")
for mutation in (".ModValue(", ".DamageValue(", ".RestoreValue(", ".ForceActorValue("):
    check(mutation not in code, f"Papyrus must not mutate actor values ({mutation})")

required_functions = [
    "Function OnGameDataReady() Global",
    "Function OnPostLoadGame(Hydra:Events:PostLoadGameParams akEvent) Global",
    "Function OnItemEquipped(Hydra:Events:ItemEquipUnequipParams akEvent) Global",
    "Function OnItemAddRemove(Hydra:Events:ItemAddRemoveParams akEvent) Global",
    "Function OnSleepStartStop(Hydra:Events:SleepStartStopParams akEvent) Global",
    "Function OnWaitStartStop(Hydra:Events:WaitStartStopParams akEvent) Global",
    "Function OnTick() Global",
]
for signature in required_functions:
    check(signature in code, f"missing Hydra callback: {signature}")

registrations = re.findall(r"Hydra:Events\.RegisterFor\w+\([^\n]*", code)
unregistrations = re.findall(r"Hydra:Events\.UnregisterFor\w+\([^\n]*", code)
check(len(registrations) >= 5, "expected five persistent event registrations")
for line in registrations:
    check(line.rstrip().endswith(", None, True)"), f"event registration is not persistent: {line.strip()}")
check(len(unregistrations) >= len(registrations),
      "each event must be unregistered before registering, so a Hydra-restored registration cannot double-deliver")

check("StartRepeatingGameTime" in code, "progression must use Hydra's repeating game-time timer")
check("StartTimerGameTime" not in code, "the one-shot Papyrus game-time timer must not be used")
check("Hydra:SaveMap.IsNamespaceValid" in code, "survival must validate its save-map namespace")
check("Hydra:SaveMap.SetValue" in code, "progress must be persisted to the Hydra save map")
check("Hydra:SaveMap.GetValueOrDefault" in code, "progress must be restored from the Hydra save map")
check("Utility.GetCurrentGameTime" in code, "progression must use game time")

check("pendingForm" in code and "pendingCount" in code,
      "consumption must be confirmed by an inventory count change")
check("GetItemCount" in code, "consumption confirmation must read the player inventory count")
check("ExpirePending" in code, "an armed consumption candidate must expire")
check("Game.GetForm(" not in code,
      "pending forms must not be re-resolved with Game.GetForm (ESL/light-plugin ids fail)")
check("Hydra:MemMap.SetValue" in code and " as Form" in code,
      "the pending consumption form must be held as a live form for the confirm step")

keywords = {
    "0x00055ECC": "ObjectTypeFood",
    "0x00249F30": "ObjectTypeNukaCola",
    "0x000F4AEC": "ObjectTypeDrink",
    "0x000F4AED": "ObjectTypeWater",
    "0x0010C416": "ObjectTypeAlcohol",
}
for form_id, editor_id in keywords.items():
    pattern = rf"GetFormFromFile\({form_id},\s*\"Fallout4\.esm\"\)"
    check(re.search(pattern, code, re.IGNORECASE) is not None,
          f"keyword {editor_id} must resolve as {form_id}:Fallout4.esm")

check("GetDifficulty_Survival" in code, "the vanilla-survival double-apply gate is missing")
check("IsActive()" in code, "survival must gate progression behind IsActive")

check("Function ApplyStagePenalty(string asPool, int aiStage) Global" in code,
      "the native penalty handoff hook is missing")
hook = re.search(r"Function ApplyStagePenalty\(string asPool, int aiStage\) Global(.*?)\nEndFunction",
                 code, re.S)
if hook:
    body = hook.group(1)
    for mutation in ("AddSpell", "RemoveSpell", "SetValue", "ModValue"):
        check(mutation not in body,
              f"ApplyStagePenalty must stay inert until the native bridge exists ({mutation})")

functions_json = json.loads(text(SCRIPT_FUNCTIONS))
global_functions = functions_json.get("globalFunctions", [])
check(any(entry.get("scriptName") == NAMESPACE and entry.get("functionName") == "OnGameDataReady"
          for entry in global_functions),
      "ScriptFunctions.json must bind OnGameDataReady to RadSurvival.OnGameDataReady")
for entry in global_functions:
    check(entry.get("eventName"), "every bound global function needs an eventName")
    check(f"Function {entry.get('functionName')}(" in code,
          f"bound function {entry.get('functionName')} is missing from the script")

save_maps = json.loads(text(SAVE_MAPS))
namespaces = {entry.get("namespace") for entry in save_maps.get("saveMaps", [])}
check(NAMESPACE in namespaces, "the RadSurvival save-map namespace must be registered")
check(re.search(r'Function Namespace\(\) Global\s+return "RadSurvival"', code, re.S) is not None,
      "the script's save-map namespace must match ScriptSaveMaps.json")

tuning = json.loads(text(TUNING))
native = json.loads(text(NATIVE_NEEDS))
native_rates = native["hourlyRates"]
scripted_rates = tuning["hourlyRates"]
for key in ("hunger", "thirst", "sleepDebt"):
    check(key in scripted_rates, f"survival.json is missing hourlyRates.{key}")
    if key in scripted_rates:
        check(abs(float(scripted_rates[key]) - float(native_rates[key])) < 1e-9,
              f"hourlyRates.{key} diverged from the native needs runtime "
              f"({scripted_rates[key]} vs {native_rates[key]})")

config_path = re.search(r'Function ConfigPath\(\) Global\s+return "([^"]+)"', code, re.S)
check(config_path is not None, "the script must declare its config path")
if config_path:
    expected = config_path.group(1)
    check(expected.replace("\\", "/") == "Data/F4SE/Plugins/FalloutAnomaly/survival.json",
          f"config path must resolve to the shipped deploy file, found {expected}")
    relative = expected.replace("Data/", "", 1)
    check((ROOT / "deploy" / relative).is_file(), "config path does not match a shipped deploy file")

for key in ("enabled", "skipWhenVanillaSurvival", "tickSeconds", "recovery"):
    check(key in tuning, f"survival.json is missing {key}")

if failures:
    print("survival contract failures:")
    for failure in failures:
        print("  FAIL", failure)
    sys.exit(1)

print("Rad survival (Hydra) contracts passed")
