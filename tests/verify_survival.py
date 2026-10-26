"""Survival contract checks for the vanilla Papyrus quest and Rad-owned records."""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "deploy/Scripts/Source/User/RadSurvivalQuest.psc"
COMPILED = ROOT / "deploy/Scripts/RadSurvivalQuest.pex"
PLUGIN = ROOT / "deploy/RadSurvival.esp"
NATIVE_NEEDS = ROOT / "deploy/F4SE/Plugins/FalloutAnomaly/needs.json"

PEX_MAGIC = bytes((0xDE, 0xC0, 0x57, 0xFA))
SCRIPT_NAME = "RadSurvivalQuest"
GLOB_EDITOR_IDS = ("RadSurvival_Hunger", "RadSurvival_Thirst", "RadSurvival_Sleep")
PROPERTY_NAMES = GLOB_EDITOR_IDS
REMOVED_RUNTIME = re.compile(r"(?<![A-Za-z])" + "Hy" + "dra" + r"(?![A-Za-z])", re.IGNORECASE)

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


def function_body(source_code, signature):
    match = re.search(re.escape(signature) + r"(.*?)\nEndFunction", source_code, re.S)
    return match.group(1) if match else ""


check(SOURCE.is_file(), f"missing {SOURCE.relative_to(ROOT)}")
source = text(SOURCE) if SOURCE.is_file() else ""
code = code_only(source)

check(f"ScriptName {SCRIPT_NAME} extends Quest" in code,
      f"script must declare 'ScriptName {SCRIPT_NAME} extends Quest'")
check(SCRIPT_NAME in SOURCE.name, "script file name must match the script name")
check(re.search(r"Function\s+\w+\([^)]*\)\s+Global\b", code) is None,
      "quest scripts must use instance functions, not global functions")
check("Import " not in code, "quest script must not import a script-extender namespace")

for required in (
    'RegisterForRemoteEvent(player, "OnItemEquipped")',
    'UnregisterForRemoteEvent(player, "OnItemEquipped")',
    "RegisterForPlayerSleep()",
    "RegisterForPlayerWait()",
    "UnregisterForPlayerSleep()",
    "UnregisterForPlayerWait()",
):
    check(required in code, f"missing event wiring: {required}")

check("Event Actor.OnPlayerLoadGame(Actor akSender)" in code,
      "the script must re-anchor on load")
check("Event Actor.OnItemEquipped(Actor akSender, Form akBaseObject, ObjectReference akReference)" in code,
      "the remote item event handler signature is wrong")
check("Event OnPlayerSleepStart(" in code, "sleep start must be handled")
check("Event OnPlayerSleepStop(bool abInterrupted, ObjectReference akBed)" in code,
      "sleep stop must handle the interrupted flag")
check("Event OnPlayerWaitStart(" in code, "wait start must be handled")
check("Event OnPlayerWaitStop(bool abInterrupted)" in code, "wait stop must be handled")
check("Event OnTimerGameTime(int aiTimerID)" in code, "the game-time timer must be handled")

timer_body = function_body(code, "Event OnTimerGameTime(int aiTimerID)")
check("StartTimerGameTime(" in timer_body,
      "StartTimerGameTime is one-shot, so OnTimerGameTime must re-arm it")
check("CancelTimerGameTime(" in code, "the timer must be cancelled before a jump restart")

sleep_body = function_body(code, "Event OnPlayerSleepStop(bool abInterrupted, ObjectReference akBed)")
check("abInterrupted" in sleep_body and "InterruptedScale" in sleep_body,
      "interrupted sleep must scale recovery")
check("AccrueElapsed(" in sleep_body, "sleep must accrue its own elapsed interval")
wait_body = function_body(code, "Event OnPlayerWaitStop(bool abInterrupted)")
check("AccrueElapsed(" in wait_body, "waiting must accrue its own elapsed interval")

check("MaxElapsedHours" in code and "240.0" in code, "progression must clamp a large jump")
check("Utility.GetCurrentGameTime" in code, "progression must use game time")
check("GetDifficulty()" in code and "kVanillaSurvivalDifficulty" in code,
      "the vanilla-survival double-apply gate is missing")

check("PendingForm" in code and "PendingCount" in code,
      "consumption must be armed with a live form and an inventory count")
check("GetItemCount(" in code, "consumption confirmation must read the player inventory count")
check("ExpirePending(" in code, "an armed consumption candidate must expire")
check("Game.GetForm(" not in code,
      "the pending form must stay a live reference, not be re-resolved by id")

for name in PROPERTY_NAMES:
    check(f"GlobalVariable Property {name} Auto" in code,
          f"missing GLOB property declaration: {name}")
mirror_body = function_body(code, "Function MirrorToGlobal(GlobalVariable akTarget, float afValue)")
check(".SetValue(" in mirror_body, "pool changes must be mirrored into the GLOB records")
check("Function ApplyStagePenalty(string asPool, int aiStage)" in code,
      "the single penalty hook is missing")
hook_body = function_body(code, "Function ApplyStagePenalty(string asPool, int aiStage)")
for forbidden in ("AddSpell", "RemoveSpell", "SetValue", "ModValue"):
    check(forbidden not in hook_body,
          f"ApplyStagePenalty must stay inert until SPEL records exist ({forbidden})")

for forbidden in ("HC_Manager", "HC_", "GetHardcore", "SurvivalModeEnabled"):
    check(forbidden not in code, f"must not touch vanilla survival state ({forbidden})")
for mutation in (".ModValue(", ".DamageValue(", ".RestoreValue(", ".ForceActorValue("):
    check(mutation not in code, f"Papyrus must not mutate actor values ({mutation})")

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

check(COMPILED.is_file(), f"missing {COMPILED.relative_to(ROOT)}")
if COMPILED.is_file():
    data = COMPILED.read_bytes()
    check(data[:4] == PEX_MAGIC, "compiled PEX does not have the FO4 PEX header")
    check(SCRIPT_NAME.encode() in data, "compiled PEX is missing the script name")

check(PLUGIN.is_file(), f"missing {PLUGIN.relative_to(ROOT)}")
if PLUGIN.is_file():
    esp = PLUGIN.read_bytes()
    check(esp[:4] == b"TES4", "RadSurvival.esp is not a plugin")
    check(b"QUST" in esp, "RadSurvival.esp is missing the quest record")
    check(b"GLOB" in esp, "RadSurvival.esp is missing the global records")
    check(esp.count(b"GLOB") >= 3, "RadSurvival.esp must hold three GLOB records")
    check(SCRIPT_NAME.encode() in esp, "the quest record has no script binding")
    for name in GLOB_EDITOR_IDS:
        check(name.encode() in esp, f"RadSurvival.esp is missing {name}")
    for name in PROPERTY_NAMES:
        check(name.encode() in esp, f"the quest VMAD is missing property {name}")
    check(b"FNAM\x01\x00f" in esp, "the GLOB records must be float globals")
    check(b"MAST" in esp and b"Fallout4.esm" in esp,
          "RadSurvival.esp must declare Fallout4.esm as a master")
    for form_id in (0x01000800, 0x01000801, 0x01000802, 0x01000803):
        check(form_id.to_bytes(4, "little") in esp,
              f"missing record form id 0x{form_id:08X}")
    vmad_at = esp.find(b"VMAD")
    check(vmad_at > 0, "the quest record has no VMAD")
    if vmad_at > 0:
        vmad_size = int.from_bytes(esp[vmad_at + 4:vmad_at + 6], "little")
        vmad = esp[vmad_at + 6:vmad_at + 6 + vmad_size]
        for form_id in (0x01000801, 0x01000802, 0x01000803):
            check(form_id.to_bytes(4, "little") in vmad,
                  f"VMAD property value is not bound to 0x{form_id:08X}")


def constant(source_code, name):
    match = re.search(rf"float Property {name} = ([0-9.]+) Auto Const", source_code)
    return float(match.group(1)) if match else None


native = json.loads(text(NATIVE_NEEDS))["hourlyRates"]
for prop, key in (("HourlyHunger", "hunger"), ("HourlyThirst", "thirst"), ("HourlySleepDebt", "sleepDebt")):
    value = constant(source, prop)
    check(value is not None, f"missing tuning constant {prop}")
    if value is not None:
        check(abs(value - float(native[key])) < 1e-9,
              f"{prop} diverged from the native needs runtime ({value} vs {native[key]})")

# The design rationale keeps the removed runtime's name; shipped files must not.
for path in ROOT.rglob("*"):
    if not path.is_file():
        continue
    rel = path.relative_to(ROOT)
    if rel.parts[0] == "docs":
        continue
    if path.resolve() == Path(__file__).resolve():
        continue
    if path.suffix.lower() in {".png", ".dds", ".nif", ".ba2", ".pex", ".esp", ".zip"}:
        continue
    try:
        body = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        continue
    check(REMOVED_RUNTIME.search(body) is None,
          f"{rel} still mentions the removed script-extender runtime")

if failures:
    print("survival contract failures:")
    for failure in failures:
        print("  FAIL", failure)
    sys.exit(1)

print("Rad survival (vanilla Papyrus quest) contracts passed")
