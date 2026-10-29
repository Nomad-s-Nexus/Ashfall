"""Consumption contract: relief is confirmed by the engine's item-removal event, not by a count drop."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "deploy/Scripts/Source/User/RadSurvivalQuest.psc"

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


def event_body(source_code, signature):
    match = re.search(re.escape(signature) + r"(.*?)\nEndEvent", source_code, re.S)
    return match.group(1) if match else ""


check(SOURCE.is_file(), f"missing {SOURCE.relative_to(ROOT)}")
source = text(SOURCE) if SOURCE.is_file() else ""
code = code_only(source)

EQUIP = "Event Actor.OnItemEquipped(Actor akSender, Form akBaseObject, ObjectReference akReference)"
REMOVE = ("Event ObjectReference.OnItemRemoved(ObjectReference akSender, Form akBaseItem, int aiItemCount, "
          "ObjectReference akItemReference, ObjectReference akDestContainer)")

check(EQUIP in code, "the equip handler must stay registered so a use can be paired")
check(REMOVE in code, "the confirmed-removal handler is missing; relief would fall back to a timing-dependent check")

armed = function_body(code, "Function ArmPlayerEvents(Actor player)")
check("RemoveAllInventoryEventFilters()" in armed and "AddInventoryEventFilter(None)" in armed,
      "arming must install the inventory filter; objects without a filter receive no inventory events")
check('RegisterForRemoteEvent(player, "OnItemRemoved")' in armed,
      "the removal event must be registered with the other remote events")
check('UnregisterForRemoteEvent(player, "OnItemRemoved")' in armed,
      "the removal event must be unregisterable so re-arming cannot double-deliver")

remove_body = event_body(code, REMOVE)
for guard in ("akSender != player", "akDestContainer != None", "aiItemCount <= 0", "IsConsumable("):
    check(guard in remove_body, f"the removal handler must reject non-consumption with {guard}")
check("PairRemove(" in remove_body, "the removal handler must feed the pairing rule")

equip_body = event_body(code, EQUIP)
for guard in ("akSender != player", "akReference != None", "akReference.IsQuestItem()", "IsConsumable("):
    check(guard in equip_body, f"the equip handler must reject non-uses with {guard}")
check("PairEquip(" in equip_body, "the equip handler must feed the pairing rule")

for helper in (
    "Function PairEquip(Form akItem)",
    "Function PairRemove(Form akItem, int aiCount)",
    "int Function FindCandidate(Form[] akForms, Form akItem)",
    "Function PruneConsumptionCandidates()",
    "Function ClearConsumptionCandidates()",
):
    check(helper in code, f"missing consumption pairing helper: {helper}")
check("float Property PairWindowSeconds = 2.0 Auto" in code,
      "the equip/removal pair window must stay a tunable property")
check("PairWindowSeconds" in function_body(code, "Function PruneConsumptionCandidates()"),
      "stale candidates must expire on the pair window")
check("ClearConsumptionCandidates()" in event_body(code, "Event Actor.OnPlayerLoadGame(Actor akSender)"),
      "a load must clear the pairing candidates so a stale pair cannot cross a save")

for stale in ("PendingForm", "PendingCount", "PendingRealTime", "ArmPending(", "ExpirePending(",
              "ConfirmConsumption(", "GetItemCount("):
    check(stale not in code, f"the timing-dependent count-drop path must stay removed: {stale}")

for marker in ("consume-arm", "consume-confirm", "consume-apply"):
    check(marker in code, f"missing consumption trace marker: {marker}")

consumable = function_body(code, "bool Function IsConsumable(Form akItem)")
check("IsFoodOrDrink(" in consumable and "IsCureItem(" in consumable,
      "food, drink and cure items must share one consumable rule")

food_body = function_body(code, "bool Function IsFoodOrDrink(Form akItem)")
check("GetFormFromFile" not in food_body,
      "classification must use the cached keywords, not a form-table lookup per event")
resolve = function_body(code, "Function ResolveKeywords()")
for form_id in ("0x00055ECC", "0x00249F30", "0x000F4AEC", "0x000F4AED", "0x0010C416"):
    check(re.search(rf"GetFormFromFile\({form_id},\s*\"Fallout4\.esm\"\)", resolve, re.IGNORECASE) is not None,
          f"keyword {form_id} must resolve from Fallout4.esm")

if failures:
    for failure in failures:
        print(f"[FAIL] {failure}")
    raise SystemExit(f"{len(failures)} consumption contract(s) failed")
print("Survival consumption contracts passed")
