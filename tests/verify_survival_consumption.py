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
for guard in ("akDestContainer != None", "aiItemCount <= 0", "IsConsumable(", "akBaseItem as Ammo"):
    check(guard in remove_body, f"the removal handler must reject non-consumption with {guard}")
check("akSender != Game.GetPlayer()" in remove_body, "the removal handler must check the sender")

check("PairRemove(" in remove_body, "the removal handler must feed the pairing rule")

equip_body = event_body(code, EQUIP)
for guard in ("akReference != None", "akReference.IsQuestItem()", "IsConsumable("):
    check(guard in equip_body, f"the equip handler must reject non-uses with {guard}")
check("akSender != Game.GetPlayer()" in equip_body, "the equip handler must check the sender")

check("PairEquip(" in equip_body, "the equip handler must feed the pairing rule")

for helper in (
    "Function PairEquip(Form akItem)",
    "Function PairRemove(Form akItem)",
    "Function PruneConsumptionCandidates()",
    "Function ClearConsumptionCandidates()",
):
    check(helper in code, f"missing consumption pairing helper: {helper}")
check("float Property PairWindowSeconds = 2.0 Auto" in code,
      "the equip/removal pair window must stay a tunable property")
prune_body = function_body(code, "Function PruneConsumptionCandidates()")
check("PairWindowSeconds" in prune_body,
      "stale candidates must expire on the pair window")
check(prune_body.count("> PairWindowSeconds") == 2,
      "both candidate arrays must expire on a strict > PairWindowSeconds comparison")
check(re.search(
    r"^\s*if\s+!ConsumptionArraysReady\s*\|\|\s*EquipCandidateForms == None\s*\|\|\s*RemoveCandidateForms == None\s*$",
    prune_body, re.MULTILINE) is not None,
      "the prune guard must test the ready flag and both candidate arrays for None on one if line")
for helper in ("Function PairEquip(Form akItem)", "Function PairRemove(Form akItem)"):
    check("ApplyRelief(akItem, 1)" in function_body(code, helper),
          f"{helper} must credit exactly one use, never the dropped stack count")
check("RemoveCandidateCounts" not in code,
      "the removal-count bookkeeping is gone because a paired use always credits one")
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


# ---- OnInit ordering and array guards ------------------------------------------------------
oninit = event_body(code, "Event OnInit()")
check("ClearConsumptionCandidates()" in oninit,
      "OnInit must initialise the consumption candidate arrays or the first meal writes to None arrays")
check(oninit.find("ClearConsumptionCandidates()") < oninit.find("ArmPlayerEvents(player)"),
      "OnInit must initialise the candidate arrays before it arms the item events")
prune = function_body(code, "Function PruneConsumptionCandidates()")
check(re.search(
    r"^\s*if\s+!ConsumptionArraysReady[^\n]*\n\s*ClearConsumptionCandidates\(\)", prune, re.MULTILINE) is not None,
      "the prune guard must re-initialise the arrays in the same if branch when an event arrives before OnInit")
clear = function_body(code, "Function ClearConsumptionCandidates()")
check("ConsumptionArraysReady = true" in clear,
      "clearing the candidates must mark the arrays usable")
check("FindCandidate(" not in code, "the pairing search must use the built-in array Find")
check("RemoveCandidateForms.Find(" in code and "EquipCandidateForms.Find(" in code,
      "both pairing directions must use array Find")

# Constraint: the relief INI bridge assertions follow.
check("int Function GetReliefFlags(Form akItem) native" in code,
      "the quest must declare the single native relief bridge function")
check(code.count(" native") == 2 and "float Function TuningOverride(int aiKey) native" in code,
      "only the relief and tuning natives may exist, keeping a not-loaded DLL to a bounded error count")
relief_body = function_body(code, "Function ApplyRelief(Form akItem, int aiCount)")
check("GetReliefFlags(" in relief_body, "relief must consult the INI bridge flags")
check("(akItem as Potion)" in relief_body and "(akItem as Potion)" in function_body(code, "bool Function IsExtraReliefItem(Form akItem)"),
      "the native must only be called for ingestibles, so weapon/armour equips never hit an unbound native")
check("IsExtraReliefItem(" in consumable,
      "the consumable rule must accept INI bridge entries")

root = SOURCE.parents[4]
relief_source = text(root / "src/RadReliefConfig.cpp") if (root / "src/RadReliefConfig.cpp").is_file() else ""
check("RegisterPapyrusFunctions" in relief_source and "InstallPapyrusBridge" in relief_source and
      "GetReliefFlags" in relief_source,
      "the native relief bridge must register the Papyrus functions")
for key in ("ExtraFoodKeywords", "ExtraThirstKeywords", "ExtraFoodItems", "ExtraThirstItems"):
    check(key in relief_source, f"the native bridge must read [Relief] {key}")
check("LookupForm(" in relief_source, "the bridge must resolve plugin + local id through TESDataHandler")
check("plugin or form not found" in relief_source and "malformed, expected" in relief_source,
      "malformed/missing entries must be logged with their raw text")
check("only the first part is used" in relief_source,
      "a truncated [Relief] list must warn instead of silently dropping entries")
main_source = text(root / "src/main.cpp") if (root / "src/main.cpp").is_file() else ""
check("RadReliefConfig::InstallPapyrusBridge()" in main_source,
      "F4SEPlugin_Load must install the relief bridge before scripts load")
lifecycle_source = text(root / "src/RadLifecycle.cpp") if (root / "src/RadLifecycle.cpp").is_file() else ""
check("RadReliefConfig::ActivateOnGameThread()" in lifecycle_source,
      "the relief lists must resolve on the game thread after records load")
ini_source = text(root / "deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini") if (root / "deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini").is_file() else ""
check("[Relief]" in ini_source, "the INI must document the [Relief] section")
thirst_line = [line for line in ini_source.splitlines() if line.startswith("ExtraThirstItems=")]
check(bool(thirst_line) and "Fallout4.esm:0x058AB8" in thirst_line[0] and "Fallout4.esm:0x0E9284" in thirst_line[0],
      "the shipped ExtraThirstItems must carry the two HC-keyword-only vanilla drinks")

# Constraint: Python model of the Papyrus pairing rule, so both event orders are covered.
PAIR_WINDOW = 2.0


class PairModel:
    def __init__(self):
        self.ready = False
        self.equips = []
        self.removes = []
        self.reliefs = []

    def clear(self):
        self.equips = []
        self.removes = []
        self.ready = True

    def prune(self, now):
        if not self.ready:
            self.clear()
        self.equips = [(f, t) for f, t in self.equips if now - t <= PAIR_WINDOW]
        self.removes = [(f, t) for f, t in self.removes if now - t <= PAIR_WINDOW]

    @staticmethod
    def find(entries, form):
        for index, entry in enumerate(entries):
            if entry[0] == form:
                return index
        return -1

    def pair_equip(self, form, now):
        self.prune(now)
        match = self.find(self.removes, form)
        if match >= 0:
            self.removes.pop(match)
            self.reliefs.append((form, 1))
            return
        self.equips.append((form, now))

    def pair_remove(self, form, now):
        self.prune(now)
        match = self.find(self.equips, form)
        if match >= 0:
            self.equips.pop(match)
            self.reliefs.append((form, 1))
            return
        self.removes.append((form, now))


model = PairModel()
model.pair_equip("A", 0.0)
check(model.ready, "a pairing call on a fresh model must initialise the arrays (None-array guard)")
model.pair_remove("A", 0.1)
check(model.reliefs == [("A", 1)], "equip then remove must credit exactly one relief")
model = PairModel()
model.pair_remove("A", 0.0)
model.pair_equip("A", 0.1)
check(model.reliefs == [("A", 1)], "remove then equip must credit exactly one relief (order independence)")
model = PairModel()
model.pair_equip("A", 0.0)
model.pair_remove("A", 0.1)
model.pair_equip("A", 0.2)
model.pair_remove("A", 0.3)
check(model.reliefs == [("A", 1), ("A", 1)], "two uses in a row must credit twice")
model = PairModel()
model.pair_equip("A", 0.0)
model.pair_remove("A", PAIR_WINDOW + 0.5)
check(model.reliefs == [], "an equip older than the pair window must not pair with a later removal")
model = PairModel()
model.pair_equip("A", 0.0)
model.pair_equip("A", 0.1)
model.pair_remove("A", 0.2)
check(model.reliefs == [("A", 1)], "a duplicate equip must not double-credit one removal")
model = PairModel()
model.pair_remove("A", 0.0)
model.pair_equip("B", 0.1)
check(model.reliefs == [], "a stale remove candidate must not pair with a different form")
model = PairModel()
model.pair_remove("A", 0.0)
model.pair_equip("A", 0.1)
check(model.reliefs == [("A", 1)],
      "a drop followed by eating the same item must credit one use, not the dropped stack size")

if failures:
    for failure in failures:
        print(f"[FAIL] {failure}")
    raise SystemExit(f"{len(failures)} consumption contract(s) failed")
print("Survival consumption contracts passed")
