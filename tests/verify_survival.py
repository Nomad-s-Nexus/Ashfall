"""Survival contract checks for the vanilla Papyrus quest and Rad-owned records."""

import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "deploy/Scripts/Source/User/RadSurvivalQuest.psc"
COMPILED = ROOT / "deploy/Scripts/RadSurvivalQuest.pex"
DISEASE_SOURCE = ROOT / "deploy/Scripts/Source/User/RadSurvivalDiseases.psc"
DISEASE_COMPILED = ROOT / "deploy/Scripts/RadSurvivalDiseases.pex"
PLUGIN = ROOT / "deploy/RadSurvival.esp"

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

check(DISEASE_SOURCE.is_file(), f"missing {DISEASE_SOURCE.relative_to(ROOT)}")
disease_source = text(DISEASE_SOURCE) if DISEASE_SOURCE.is_file() else ""
disease_code = code_only(disease_source)

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
for required in ('RegisterForRemoteEvent(player, "OnPlayerLoadGame")',
                 'UnregisterForRemoteEvent(player, "OnPlayerLoadGame")'):
    check(required in code, f"missing load-event wiring: {required}")
armed = function_body(code, "Function ArmPlayerEvents(Actor player)")
check('RegisterForRemoteEvent(player, "OnPlayerLoadGame")' in armed,
      "OnPlayerLoadGame is an Actor remote event, so arming must register it or the handler never fires")
check('RegisterForRemoteEvent(player, "OnPlayerSwimming")' in armed,
      "arming must register the swimming event")
check('RegisterForHitEvent(player)' in armed, "arming must register the hit event")
for signature in ("Event OnInit()", "Event Actor.OnPlayerLoadGame(Actor akSender)"):
    check("ArmPlayerEvents(player)" in function_body(code, signature),
          f"{signature} must arm the player events")

hit_signature = ("Event OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, "
                 "Projectile akProjectile, bool abPowerAttack, bool abSneakAttack, bool abBashAttack, "
                 "bool abHitBlocked, string apMaterial)")
check(hit_signature in code, "the hit handler signature must match the engine event")
check('UnRegisterForHitEvent(player)' in code, "the hit event must be unregisterable")
hit_body = function_body(code, hit_signature)
check("RegisterForHitEvent(player)" in hit_body,
      "a hit event is consumed by its delivery, so the handler must re-arm it")
check("QualifiesAsWound(" in hit_body, "the hit handler must defer its filters to the wound rule")
check("Diseases.ExposureHit(" in hit_body, "a landed hit must feed the disease exposure")
wound_body = function_body(disease_code, "bool Function QualifiesAsWound(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Actor akPlayer)")
for guard in ("hurt != akPlayer", "attacker == akPlayer", "akSource as Spell"):
    check(guard in wound_body, f"the wound rule must reject hits where {guard}")
check("return false" in wound_body, "a rejected hit must not accrue risk")
check('UnregisterForRemoteEvent(player, "OnPlayerSwimming")' in code,
      "the swimming event must be unregisterable")
check("Event Actor.OnPlayerSwimming(Actor akSender)" in code, "the swimming handler is missing")
check("Diseases.ExposureSwim(" in function_body(code, "Event Actor.OnPlayerSwimming(Actor akSender)"),
      "swimming must feed the disease exposure")
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

check("PairEquip(" in code and "PairRemove(" in code,
      "consumption must pair the equip event with the engine's item-removal event; "
      "the full contract is tests/verify_survival_consumption.py")
check("Game.GetForm(" not in code,
      "consumption must stay on live form references, not be re-resolved by id")

for name in PROPERTY_NAMES:
    check(f"GlobalVariable Property {name} Auto" in code,
          f"missing GLOB property declaration: {name}")
mirror_body = function_body(code, "Function MirrorToGlobal(GlobalVariable akTarget, float afValue)")
check(".SetValue(" in mirror_body, "pool changes must be mirrored into the GLOB records")
check("Function ApplyStagePenalty(string asPool, int aiStage)" in code,
      "the single penalty hook is missing")
hook_body = function_body(code, "Function ApplyStagePenalty(string asPool, int aiStage)")
check("AddSpell(" in hook_body, "ApplyStagePenalty must add the staged ability spell")
check("RemoveSpell(" in hook_body, "ApplyStagePenalty must remove the previous stage spell")
check("PenaltiesEnabled" in hook_body, "the penalty hook must honour the enable switch")
check("AppliedPenalty(" in hook_body and "SetAppliedPenalty(" in hook_body,
      "the hook must reconcile the spell on the player instead of stacking another one")
for forbidden in ("SetValue", "ModValue", "DamageValue", "RestoreValue", "ForceActorValue"):
    check(forbidden not in hook_body,
          f"penalties must be spell abilities, not actor-value writes ({forbidden})")

for name, base in (("HungerPenaltySpellBase", 2061), ("ThirstPenaltySpellBase", 2064),
                   ("SleepPenaltySpellBase", 2067)):
    check(f"int Property {name} = {base} Auto Const" in code,
          f"missing or retuned penalty spell base {name}")
check('string Property PenaltyPlugin = "RadSurvival.esp" Auto Const' in code,
      "penalty spells must resolve from RadSurvival.esp")
check("bool Property PenaltiesEnabled = true Auto Const" in code,
      "penalties need an enable switch that does not require a rebuild")

resync_body = function_body(code, "Function ResyncPenalties()")
for pool in ("hunger", "thirst", "sleep"):
    check(f'ApplyStagePenalty("{pool}"' in resync_body,
          f"ResyncPenalties must reconcile the {pool} spell")
check("StageFor(" in resync_body, "ResyncPenalties must recount stages from the pools")
for event in ("Event OnInit()", "Event Actor.OnPlayerLoadGame(Actor akSender)"):
    check("ResyncPenalties()" in function_body(code, event),
          f"{event} must reconcile penalties")
clear_calls = function_body(code, "Function ClearAllPenalties()")
for pool in ("hunger", "thirst", "sleep"):
    check(f'ApplyStagePenalty("{pool}", 0)' in clear_calls,
          f"ClearAllPenalties must clear the {pool} spell")
update_body = function_body(code, "Function UpdatePenaltyStages()")
check("ClearAllPenalties()" in update_body,
      "a suspended layer must clear its penalties")

# Invariant: every pool is reconciled on every call, never only on a stage change.
for pool in ("hunger", "thirst", "sleep"):
    check(f'ApplyStagePenalty("{pool}", {pool})' in update_body,
          f"UpdatePenaltyStages must reconcile the {pool} spell on every call")
# Constraint: the calls must sit at function indent; nested deeper they are back inside a guard.
update_calls = [line for line in update_body.splitlines() if "ApplyStagePenalty(" in line]
check(update_calls == [f'    ApplyStagePenalty("{pool}", {pool})'
                       for pool in ("hunger", "thirst", "sleep")],
      "each pool must be reconciled exactly once, unconditionally, at function indent")
check(re.search(r"!=\s*\w*Stage\b", update_body) is None,
      "UpdatePenaltyStages must not gate reconciliation on a stage change")
check("NotifyStage(" in update_body,
      "the stage trackers still have to drive the stage notifications")
check("if applied == wanted" in hook_body,
      "ApplyStagePenalty must be idempotent so reconciling every call is free")

# Constraint: a steady-state reconcile must not resolve the same stage through the form table.
resolver_body = function_body(code, "Spell Function PenaltySpellFor(string asPool, int aiStage)")
check(resolver_body.count("GetFormFromFile(") == 1,
      "PenaltySpellFor must resolve from the plugin in exactly one place")
check("return cachedSpell" in resolver_body and
      resolver_body.index("return cachedSpell") < resolver_body.index("GetFormFromFile("),
      "PenaltySpellFor must return the memoised spell before it resolves again")
check("if !resolved" in resolver_body,
      "a failed resolve must not be memoised, or a missing plugin never retries")
for name in ("CachedHungerStage", "CachedThirstStage", "CachedSleepStage"):
    check(f"int {name} = -1" in code, f"missing penalty spell cache {name}")
check("Function ClearPenaltyCache()" in code, "the penalty spell cache needs a reset")
check("ClearPenaltyCache()" in function_body(code, "Function ResyncPenalties()"),
      "a load must drop the penalty cache so a retuned table is picked up")

# Constraint: the tick must reconcile after the active/suspended branch, not inside one of them.
timer_body = function_body(code, "Event OnTimerGameTime(int aiTimerID)")
check("UpdatePenaltyStages()" in timer_body,
      "the game-time tick must reconcile penalties")
check("UpdatePenaltyStages()" in timer_body[timer_body.rfind("endif"):],
      "the tick must reconcile after the active/suspended branch, not inside one of them")

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

check(DISEASE_COMPILED.is_file(), f"missing {DISEASE_COMPILED.relative_to(ROOT)}")
if DISEASE_COMPILED.is_file():
    disease_pex = DISEASE_COMPILED.read_bytes()
    check(disease_pex[:4] == PEX_MAGIC, "the disease PEX is missing the FO4 header")
    check(b"RadSurvivalDiseases" in disease_pex,
          "the disease PEX is missing the script name")

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
    check(b"DNAM\x0c\x00\x09\x00\x00\x00" in esp,
          "the quest must be Start Game Enabled (QUST flags 9)")
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


sys.path.insert(0, str(Path(__file__).resolve().parent))
import plugin_reader

manifest_path = ROOT / "tools/rad_penalty_spells.json"
check(manifest_path.is_file(), "missing tools/rad_penalty_spells.json")
manifest = json.loads(text(manifest_path)) if manifest_path.is_file() else {"spells": []}
entries = manifest.get("spells", [])
check(manifest.get("plugin") == "RadSurvival.esp", "the spell manifest must target RadSurvival.esp")
check(abs(float(manifest.get("mgefDefaults", {}).get("dualCastingScale", 0)) - 1.0) < 1e-6,
      "the manifest must record vanilla's DualCastingScale of 1.0")
check(len(entries) == 9, "the penalty table must hold 9 spells (3 pools x 3 stages)")
pools = {(entry.get("pool"), entry.get("stage")) for entry in entries}
check(pools == {(pool, stage) for pool in ("hunger", "thirst", "sleep") for stage in (1, 2, 3)},
      "stage -> spell mapping is not 1:1 across the three pools")
check(len({entry.get("spellFormId") for entry in entries}) == len(entries),
      "penalty spell form ids must be unique")
check(len({entry.get("mgefFormId") for entry in entries}) == len(entries),
      "penalty magic effect form ids must be unique")

SPELL_BASE = {"hunger": 0x80D, "thirst": 0x810, "sleep": 0x813}
for entry in entries:
    base = SPELL_BASE.get(entry.get("pool"), 0)
    expected = f"0x{0x01000000 | (base + int(entry.get('stage', 0)) - 1):08x}"
    check(entry.get("spellFormId", "").lower() == expected,
          f"{entry.get('spellEditorId')} does not match its script base form id {expected}")
    check(isinstance(entry.get("magnitude"), (int, float)) and entry["magnitude"] < 0,
          f"{entry.get('spellEditorId')} needs a negative magnitude")

for pool in ("hunger", "thirst", "sleep"):
    staged = sorted((entry for entry in entries if entry.get("pool") == pool),
                    key=lambda entry: entry.get("stage", 0))
    magnitudes = [abs(float(entry["magnitude"])) for entry in staged if isinstance(entry.get("magnitude"), (int, float))]
    check(len(magnitudes) == 3 and magnitudes[0] < magnitudes[1] < magnitudes[2],
          f"{pool} penalties must grow with the stage")

disease_path = ROOT / "tools/rad_diseases.json"
check(disease_path.is_file(), "missing tools/rad_diseases.json")
diseases = json.loads(text(disease_path)) if disease_path.is_file() else {"diseases": [], "timing": {}}
layer = diseases.get("diseases", [])
check(diseases.get("plugin") == "RadSurvival.esp", "the disease manifest must target RadSurvival.esp")
check(len(layer) == 2, "the disease table must hold two illnesses")
check(len({disease.get("id") for disease in layer}) == len(layer), "disease ids must be unique")
disease_stages = [stage for disease in layer for stage in disease.get("stages", [])]
check(len(disease_stages) == 6, "the disease table must hold three stages per illness")
check(len({stage.get("spellFormId") for stage in disease_stages}) == len(disease_stages),
      "disease ability form ids must be unique")
check(len({stage.get("mgefFormId") for stage in disease_stages}) == len(disease_stages),
      "disease magic effect form ids must be unique")
for disease in layer:
    staged = sorted(disease.get("stages", []), key=lambda entry: entry.get("stage", 0))
    check([entry.get("stage") for entry in staged] == [1, 2, 3],
          f"disease {disease.get('name')} must have stages 1-3")
    severities = [abs(float(entry["magnitude"])) for entry in staged]
    check(severities[0] < severities[1] < severities[2],
          f"disease {disease.get('name')} must worsen with the stage")
    base = int(disease.get("spellBase", 0))
    for entry in staged:
        expected = f"0x{0x01000000 | (base + int(entry['stage']) - 1):08x}"
        check(entry.get("spellFormId", "").lower() == expected,
              f"{entry.get('spellEditorId')} does not match its script spell base {expected}")

quest_script = {}
disease_script = {}
if PLUGIN.is_file():
    parser = plugin_reader
    try:
        tree = parser.parse(esp)
    except parser.PluginError as exc:
        tree = []
        check(False, f"RadSurvival.esp does not parse as a plugin: {exc}")

    def plugin_records(signature):
        return parser.records_by_form_id(esp, tree, signature)

    magic_effects = plugin_records(b"MGEF")
    ability_spells = plugin_records(b"SPEL")
    check(len(magic_effects) == 16,
          f"RadSurvival.esp must hold 16 magic effects (9 penalty + 6 disease + 1 cure), found {len(magic_effects)}")
    check(len(ability_spells) == 15,
          f"RadSurvival.esp must hold 15 abilities (9 penalty + 6 disease), found {len(ability_spells)}")

    for entry in entries:
        mgef_node = magic_effects.get(int(entry["mgefFormId"], 16))
        spell_node = ability_spells.get(int(entry["spellFormId"], 16))
        check(mgef_node is not None, f"missing magic effect {entry['mgefEditorId']}")
        check(spell_node is not None, f"missing ability spell {entry['spellEditorId']}")
        if mgef_node is None or spell_node is None:
            continue

        check(parser.editor_id(esp, mgef_node) == entry["mgefEditorId"],
              f"{entry['mgefFormId']} is not {entry['mgefEditorId']}")
        data = mgef_node.field(esp, b"DATA")
        check(data is not None and len(data) >= 84, f"{entry['mgefEditorId']} has no usable DATA")
        if data:
            (archetype,) = struct.unpack_from("<I", data, 64)
            (actor_value,) = struct.unpack_from("<I", data, 68)
            (casting_type,) = struct.unpack_from("<I", data, 80)
            (flags,) = struct.unpack_from("<I", data, 0)
            (dual_cast,) = struct.unpack_from("<f", data, 112)
            check(archetype == 34, f"{entry['mgefEditorId']} must be a Peak Value Modifier")
            check(casting_type == 0, f"{entry['mgefEditorId']} must be a Constant Effect")
            check(abs(dual_cast - 1.0) < 1e-6,
                  f"{entry['mgefEditorId']} must keep vanilla's DualCastingScale of 1.0 (DATA+112)")
            check(actor_value == int(entry["actorValueFormId"].split(":")[0], 16),
                  f"{entry['mgefEditorId']} targets the wrong actor value")
            check(flags & 0x800, f"{entry['mgefEditorId']} must be area-less")

        check(parser.editor_id(esp, spell_node) == entry["spellEditorId"],
              f"{entry['spellFormId']} is not {entry['spellEditorId']}")
        spit = spell_node.field(esp, b"SPIT")
        check(spit is not None and len(spit) == 36, f"{entry['spellEditorId']} has no SPIT")
        if spit:
            (spell_type,) = struct.unpack_from("<I", spit, 8)
            (cast_type,) = struct.unpack_from("<I", spit, 16)
            check(spell_type == 4, f"{entry['spellEditorId']} must be an Ability spell")
            check(cast_type == 0, f"{entry['spellEditorId']} must be a Constant Effect")

        efid = spell_node.field(esp, b"EFID")
        check(efid is not None,
              f"{entry['spellEditorId']} has no effect entry; author it with FO4IDE add_magic_effect")
        if efid:
            (linked,) = struct.unpack("<I", efid)
            check(linked == int(entry["mgefFormId"], 16),
                  f"{entry['spellEditorId']} applies the wrong magic effect")
        efit = spell_node.field(esp, b"EFIT")
        check(efit is not None and len(efit) == 12, f"{entry['spellEditorId']} has no EFIT")
        if efit:
            (magnitude, area, duration) = struct.unpack("<fII", efit)
            check(abs(magnitude - float(entry["magnitude"])) < 1e-6,
                  f"{entry['spellEditorId']} magnitude {magnitude} != {entry['magnitude']}")
            check(area == 0 and duration == 0,
                  f"{entry['spellEditorId']} must be a permanent, area-less ability")

    for disease in layer:
        expected_av = int(disease["actorValueFormId"].split(":")[0], 16)
        for entry in disease.get("stages", []):
            mgef_node = magic_effects.get(int(entry["mgefFormId"], 16))
            spell_node = ability_spells.get(int(entry["spellFormId"], 16))
            check(mgef_node is not None, f"missing disease magic effect {entry['mgefEditorId']}")
            check(spell_node is not None, f"missing disease ability {entry['spellEditorId']}")
            if mgef_node is None or spell_node is None:
                continue
            check(parser.editor_id(esp, mgef_node) == entry["mgefEditorId"],
                  f"{entry['mgefFormId']} is not {entry['mgefEditorId']}")
            data = mgef_node.field(esp, b"DATA")
            check(data is not None and len(data) >= 116,
                  f"{entry['mgefEditorId']} has no usable DATA")
            if data:
                check(struct.unpack_from("<I", data, 64)[0] == 34,
                      f"{entry['mgefEditorId']} must be a Peak Value Modifier")
                check(struct.unpack_from("<I", data, 68)[0] == expected_av,
                      f"{entry['mgefEditorId']} targets the wrong actor value for {disease['name']}")
                check(struct.unpack_from("<I", data, 0)[0] & 0x800,
                      f"{entry['mgefEditorId']} must be area-less")
                check(abs(struct.unpack_from("<f", data, 112)[0] - 1.0) < 1e-6,
                      f"{entry['mgefEditorId']} must keep vanilla's DualCastingScale of 1.0")
            check(parser.editor_id(esp, spell_node) == entry["spellEditorId"],
                  f"{entry['spellFormId']} is not {entry['spellEditorId']}")
            spit = spell_node.field(esp, b"SPIT")
            check(spit is not None and struct.unpack_from("<I", spit, 8)[0] == 4,
                  f"{entry['spellEditorId']} must be an Ability spell")
            efid = spell_node.field(esp, b"EFID")
            check(efid is not None,
                  f"{entry['spellEditorId']} has no effect entry; author it with FO4IDE add_magic_effect")
            efit = spell_node.field(esp, b"EFIT")
            check(efit is not None and len(efit) == 12, f"{entry['spellEditorId']} has no EFIT")
            if efit:
                check(abs(struct.unpack("<f", efit[:4])[0] - float(entry["magnitude"])) < 1e-6,
                      f"{entry['spellEditorId']} magnitude does not match the manifest")

    globals_by_id = plugin_records(b"GLOB")
    quest_node = plugin_records(b"QUST").get(0x01000800)
    scripts = parser.vmad_properties(esp, quest_node) if quest_node is not None else {}
    quest_script = scripts.get("RadSurvivalQuest", {})
    disease_script = scripts.get("RadSurvivalDiseases", {})
    check(bool(quest_script) and bool(disease_script),
          "the quest VMAD must attach both scripts exactly once")
    for key in ("risk", "state"):
        glob = diseases.get(key, {})
        form_id = int(glob.get("globFormId", "0x0"), 16)
        node = globals_by_id.get(form_id)
        check(node is not None, f"missing disease GLOB {glob.get('globEditorId')}")
        if node is None:
            continue
        check(parser.editor_id(esp, node) == glob.get("globEditorId"),
              f"{glob.get('globFormId')} is not {glob.get('globEditorId')}")
        check(node.field(esp, b"FNAM") == b"f",
              f"{glob.get('globEditorId')} must be a float global")
        property_name = glob.get("globEditorId")
        check(disease_script.get(property_name) == form_id,
              f"the disease script must bind {property_name} to 0x{form_id:08X}")

    # Constraint: the pools stay on the quest script, the mirrors on the disease script.
    for name in GLOB_EDITOR_IDS:
        check(quest_script.get(name) is not None,
              f"the quest script must bind {name}, or the pools never reach the HUD")
    check(quest_script.get("Diseases") == 0x01000800,
          "the quest script's Diseases property must bind the quest form, or the cure cannot reach the disease script")


def tune(props, name):
    return float(props[name]) if isinstance(props.get(name), (int, float)) else None


# Constraint: tuning lives in VMAD floats, so changing it needs no Papyrus compile.
for prop, expected in (("HourlyHunger", 0.05), ("HourlyThirst", 0.10), ("HourlySleepDebt", 0.025)):
    value = tune(quest_script, prop)
    check(value is not None, f"missing tuning float {prop} on the quest script VMAD")
    if value is not None:
        check(abs(value - expected) < 1e-6,
              f"{prop} is {value}, expected {expected}: the quest script owns the pool rates")
    check(re.search(rf"float Property {prop} = [0-9.]+ Auto$", source, re.M) is not None,
          f"{prop} must stay a writable Auto float (no Const), or retuning needs a recompile")
readme = ROOT / "README.md"
check("needs.json" not in text(readme) if readme.is_file() else True,
      "the README must not still describe the removed needs.json rates")

# Invariant: disease risk is time-driven, staged on a ladder, and curable by rest.
for name in ("RadSurvival_Risk", "RadSurvival_Disease"):
    check(f"GlobalVariable Property {name} Auto" in disease_code,
          f"missing disease GLOB property declaration: {name}")
check("bool Property DiseasesEnabled = true Auto Const" in disease_code,
      "diseases need an enable switch that does not require a rebuild")
check("int Property CurrentStateVersion = 2 Auto Const" in disease_code,
      "the disease state needs a version marker for save migration")
for name, base in (("FeverSpellBase", 2076), ("WastingSpellBase", 2079)):
    check(f"int Property {name} = {base} Auto Const" in disease_code,
          f"missing or retuned disease spell base {name}")

# Constraint: the quest script owns the events and delegates; the disease script owns the rules.
check("RadSurvivalDiseases Property Diseases Auto" in code,
      "the quest script must bind the disease script")
accrue_body = function_body(code, "Function Accrue(float afHours)")
check("TickDiseases(" in accrue_body, "every elapsed interval must feed the disease layer")
weather_tick = function_body(code, "Function TickDiseases(float afHours)")
check("Diseases.ExposureWeather(" in weather_tick,
      "the sky exposure must ride the same interval as the disease tick")
check("afHours" in weather_tick.split("Diseases.ExposureWeather(", 1)[1][:40],
      "the sky exposure must be given the elapsed interval, or it charges the wrong amount")
for call, signature in (("Diseases.Tick(", "Function TickDiseases(float afHours)"),
                        ("Diseases.TryRestCure(", "Function RestCureDiseases(float afHours, bool abInterrupted)"),
                        ("Diseases.Reconcile(", "Function ReconcileDiseases()"),
                        ("Diseases.Resync(", "Function ResyncDiseases()")):
    wrapper = function_body(code, signature)
    check(call in wrapper, f"{signature} must call into the disease script")
    check("Diseases == None" in wrapper,
          f"{signature} must survive a quest whose disease script is not bound")
    check("IsActive()" in wrapper, f"{signature} must pass the layer gate down")
check("RestCureDiseases(" in sleep_body, "the sleep handler must offer the rest cure")
check("ReconcileDiseases(" in timer_body, "the tick must reconcile the disease ability")

risk_body = function_body(
    disease_code, "Function AccrueRisk(float afHours, bool abActive, int aiHungerStage, int aiThirstStage, int aiSleepStage)")
check("HourlyRisk" in risk_body and "RiskPerWeakStage" in risk_body,
      "risk must accrue from the tuned rate and the pool stages")
check("IsImmune(" in risk_body, "risk must honour the immunity window")
check("StartDisease(" in risk_body, "a full risk pool must start an illness")
check("DiseaseId > 0" in risk_body, "risk must not build while an illness is live")

hit_exposure = function_body(disease_code, "Function ExposureHit(bool abActive)")
check("HitCooldownSeconds" in hit_exposure,
      "a hit needs a cooldown, or an automatic burst floods the pool")
check("RiskPerHit" in hit_exposure, "a hit must add the tuned amount")
check("AddRisk(" in hit_exposure, "a hit must go through the shared exposure rule")
swim_exposure = function_body(disease_code, "Function ExposureSwim(bool abActive)")
check("SwimIntervalGameDays" in swim_exposure and "GetCurrentGameTime(" in swim_exposure,
      "swimming is throttled on game time because the event repeats while in water")
check("RiskPerSwim" in swim_exposure, "swimming must add the tuned amount")
check("AddRisk(" in swim_exposure, "swimming must go through the shared exposure rule")
weather_exposure = function_body(disease_code, "Function ExposureWeather(float afHours, bool abActive)")
check("IsInInterior()" in weather_exposure,
      "sky exposure must stop at an interior cell, or a shelter offers no protection")
check("Weather.GetCurrentWeather()" in weather_exposure, "sky exposure must read the active weather")
check("IsRadWeather(" in weather_exposure, "a rad storm must be told apart from ordinary rain")
check("GetClassification()" in weather_exposure and "RainClassification" in weather_exposure,
      "contaminated rain must key off the weather classification")
check("RiskPerRadHour" in weather_exposure, "a rad storm must add the tuned hourly amount")
check("RiskPerRainHour" in weather_exposure, "rain must add the tuned hourly amount")
check(weather_exposure.count("AddRisk(") == 2,
      "both sky sources must go through the shared exposure rule, not their own pond")
check("afHours" in weather_exposure, "sky exposure must scale with the elapsed interval")
for guard in ("abActive", "DiseasesEnabled", "DiseaseId > 0"):
    check(guard in weather_exposure, f"sky exposure must be gated by {guard}")
rad_weather = function_body(disease_code, "bool Function IsRadWeather(Form akWeather)")
check("RadWeatherForms" in rad_weather, "the rad weather set must be cached, not resolved per tick")
resolved_weather = function_body(disease_code, "Form[] Function ResolveRadWeather()")
check("GetFormFromFile(" in resolved_weather,
      "weather forms live in another plugin, so they resolve by file name, not by VMAD binding")
check("StormWeatherPlugin" in resolved_weather and "StormWeatherCount" in resolved_weather,
      "the storm weather set must come from the tuned plugin and count")
check("RadWeatherForms = resolved" in resolved_weather,
      "a successful resolve must be cached")
check("resolved.Length == 0" in resolved_weather,
      "an empty resolve must not be cached, or a missing plugin is never retried")
add_risk = function_body(disease_code, "Function AddRisk(float afAmount, bool abActive)")
for guard in ("abActive", "DiseasesEnabled", "IsImmune(", "DiseaseId > 0"):
    check(guard in add_risk, f"exposure must be gated by {guard}")
check("StartDisease(" in add_risk, "a pool filled by exposure must start an illness")
check("MirrorRisk()" in add_risk, "exposure must refresh the risk mirror")

immune_body = function_body(disease_code, "bool Function IsImmune()")
check("ImmunityUntil" in immune_body and "GetCurrentGameTime(" in immune_body,
      "immunity must expire on game time")

advance_body = function_body(disease_code, "Function AdvanceDisease(float afHours, bool abActive)")
for phase in ("IncubationHours", "Stage2Hours", "Stage3Hours"):
    check(phase in advance_body, f"disease escalation must use {phase}")
check("Reconcile(" in advance_body, "a stage change must re-apply the ability")
check("abActive" in advance_body, "disease progression must freeze while the layer is suspended")

rest_body = function_body(
    disease_code, "Function TryRestCure(float afHours, bool abInterrupted, bool abActive)")
check("abInterrupted" in rest_body, "an interrupted night must not cure")
check("RestCureBaseHours" in rest_body, "the rest cure needs a duration threshold")
check("CureDisease()" in rest_body, "a long enough rest must cure")

cure_body = function_body(disease_code, "Function CureDisease()")
check("ImmunityUntil =" in cure_body, "a cure must open an immunity window")
check("DiseaseId = 0" in cure_body, "a cure must clear the disease id")
check("ApplySpell(None)" in cure_body, "a cure must remove the ability")
check("MirrorDisease()" in cure_body and "MirrorRisk()" in cure_body,
      "a cure must refresh both HUD mirrors")

reconcile_body = function_body(disease_code, "Function Reconcile(bool abActive)")
check("abActive" in reconcile_body and "DiseasesEnabled" in reconcile_body,
      "the disease ability must follow the layer gate and the enable switch")
apply_body = function_body(disease_code, "Function ApplySpell(Spell akWanted)")
check("AddSpell(" in apply_body and "RemoveSpell(" in apply_body,
      "the disease ability must be added and removed")
check("AppliedDiseaseSpell" in apply_body,
      "the disease ability must be reconciled, never stacked")
for forbidden in ("SetValue", "ModValue", "DamageValue", "RestoreValue", "ForceActorValue"):
    check(forbidden not in apply_body + reconcile_body,
          f"diseases must be spell abilities, not actor-value writes ({forbidden})")

version_body = function_body(disease_code, "Function EnsureStateVersion()")
check("CurrentStateVersion" in version_body and "SavedStateVersion" in version_body,
      "the disease state must be versioned")
resync_disease = function_body(disease_code, "Function Resync(bool abActive)")
check("EnsureStateVersion()" in resync_disease, "a load must migrate the state before use")
check("Reconcile(" in resync_disease, "a load must restore the disease ability")
check("MirrorRisk()" in resync_disease and "MirrorDisease()" in resync_disease,
      "a load must refresh the HUD mirrors")
for event in ("Event OnInit()", "Event Actor.OnPlayerLoadGame(Actor akSender)"):
    check("ResyncDiseases()" in function_body(code, event),
          f"{event} must resync the disease state")
for forbidden in ("HC_", "RegisterFor"):
    check(forbidden not in disease_code,
          f"the disease script must not touch {forbidden} (no HC_ state, no event ownership)")

for prop, key in (("HourlyRisk", "hourlyRisk"), ("RiskPerWeakStage", "riskPerWeakStage"),
                  ("IncubationHours", "incubationHours"), ("Stage2Hours", "stage2Hours"),
                  ("Stage3Hours", "stage3Hours"), ("RestCureBaseHours", "restCureBaseHours"),
                  ("ImmunityHours", "immunityHours"), ("RiskPerHit", "riskPerHit"),
                  ("RiskPerSwim", "riskPerSwim"), ("SwimIntervalGameDays", "swimIntervalGameDays"),
                  ("HitCooldownSeconds", "hitCooldownSeconds"), ("RiskPerRadHour", "riskPerRadHour"),
                  ("RiskPerRainHour", "riskPerRainHour")):
    value = tune(disease_script, prop)
    check(value is not None, f"missing disease tuning float {prop} on the disease script VMAD")
    if value is not None:
        check(abs(value - float(diseases["timing"][key])) < 1e-6,
              f"{prop} diverged from tools/rad_diseases.json ({value} vs {diseases['timing'][key]})")
    check(re.search(rf"float Property {prop} = [0-9.]+ Auto$", disease_source, re.M) is not None,
          f"{prop} must stay a writable Auto float (no Const), or retuning needs a recompile")

# Invariant: one confirmed consumption reaches the disease script, which routes taint and cure.
relief_body = function_body(code, "Function ApplyRelief(Form akItem, int aiCount)")
check("Diseases.ConsumeItem(" in relief_body,
      "a confirmed consumption must reach the disease script")
check("Diseases != None" in relief_body or "Diseases == None" in relief_body,
      "relief must survive a quest whose disease script is not bound")
check("IsConsumable(" in code and "IsCureItem(" in code,
      "cure items must be classified through the shared consumable rule")
check("Diseases.ConsumeItem(" in relief_body,
      "a confirmed consumption must reach the disease script")
consume_body = function_body(disease_code, "Function ConsumeItem(Form akItem, int aiCount, bool abActive)")
check("IsCureItem(" in consume_body, "a consumed item must be tested for the cure first")
check("CureDisease()" in consume_body,
      "the cure item must clear the state through the existing cure path")
check("NotifyDisease(" in consume_body,
      "a cure taken while healthy must say so instead of vanishing silently")
check("ExposureConsume(" in consume_body, "a consumed item must charge the taint exposure")
taint_body = function_body(disease_code, "Function ExposureConsume(Form akItem, int aiCount, bool abActive)")
for guard in ("abActive", "DiseasesEnabled", "DiseaseId > 0"):
    check(guard in taint_body, f"taint consumption must be gated by {guard}")
check("AddRisk(" in taint_body, "taint must charge the pool through the shared exposure rule")
check("CategoryRisk(" in taint_body, "the per-category amount must come from the tuning")
category_body = function_body(disease_code, "int Function ExposureCategory(Form akItem)")
for name in ("TaintedFoodList", "TaintedWaterList", "ContaminatedList"):
    check(name in category_body, f"the category rule must consult {name}")
for name in ("KeywordTaintedFood", "KeywordTaintedWater", "KeywordContaminated"):
    check(name in category_body, f"the keyword opt-in must be consulted: {name}")
list_body = function_body(disease_code, "bool Function ListHas(FormList akList, Form akItem)")
check("HasForm(" in list_body, "list membership must read the authored form list")
check("!akList" in list_body, "a missing list must be fail-open, not a charge")
fallback_body = function_body(disease_code, "Function TraceTaintLists()")
check("Debug.Trace(" in fallback_body, "a missing taint list must be traced, not silent")
check("TraceTaintLists()" in function_body(disease_code, "Function Resync(bool abActive)"),
      "a load must report a missing taint list")
cure_test = function_body(disease_code, "bool Function IsCureItem(Form akItem)")
check("CureItemFormId" in cure_test and "KeywordCureItem" in cure_test,
      "the cure test must resolve the shipped item and accept the keyword")
check("as Potion" in cure_test,
      "the cure test must reject a non-ingestible before it pays a form lookup per equip")
taint_manifest = diseases.get("taint", {})
for entry in taint_manifest.get("keywords", []):
    prop = entry.get("riskProperty")
    if not prop:
        continue
    value = tune(disease_script, prop)
    check(value is not None, f"missing taint tuning float {prop} on the disease script VMAD")
    if value is not None:
        check(abs(value - float(entry["risk"])) < 1e-6,
              f"{prop} diverged from tools/rad_diseases.json ({value} vs {entry['risk']})")
cure_match = re.search(r"int Property CureItemFormId = ([0-9]+) Auto Const", disease_source)
check(cure_match is not None, "the disease script must carry the cure item's local form id")
if cure_match:
    check(int(cure_match.group(1)) == int(taint_manifest.get("cure", {}).get("itemFormId", "0x0"), 16) & 0xFFFFFF,
          "CureItemFormId does not resolve the cure item the manifest ships")
rad_away_ids = taint_manifest.get("cure", {}).get("extraCureFormIds", [])
check(len(rad_away_ids) == 1, "the manifest must name the obtainable cure item")
rad_away_match = re.search(r"int Property RadAwayFormId = ([0-9]+) Auto Const", disease_source)
check(rad_away_match is not None
      and "radAway" in function_body(disease_code, "bool Function IsCureItem(Form akItem)"),
      "the disease script must accept the obtainable cure item, not only the nameless shipped one")
if rad_away_match and rad_away_ids:
    check(int(rad_away_match.group(1)) == int(rad_away_ids[0].split(":")[0], 16),
          "RadAwayFormId does not resolve the obtainable cure the manifest names")


# Constraint: the weather set and its ids are shared with the manifest, not restated by hand.
weather = diseases.get("weather", {})
for prop, key in (("StormWeatherBase", "stormWeatherBaseDecimal"),
                  ("StormWeatherCount", "stormWeatherCount"),
                  ("VanillaRadStormFormId", "vanillaRadStormDecimal"),
                  ("RainClassification", "rainClassification")):
    match = re.search(rf"int Property {prop} = ([0-9]+) Auto Const", disease_source)
    check(match is not None, f"missing weather constant {prop}")
    if match and key in weather:
        check(int(match.group(1)) == int(weather[key]),
              f"{prop} diverged from tools/rad_diseases.json ({match.group(1)} vs {weather[key]})")
plugin_match = re.search(r'string Property StormWeatherPlugin = "([^"]+)" Auto Const', disease_source)
check(plugin_match is not None, "missing StormWeatherPlugin constant")
if plugin_match and "stormWeatherPlugin" in weather:
    check(plugin_match.group(1) == weather["stormWeatherPlugin"],
          "StormWeatherPlugin diverged from tools/rad_diseases.json")

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
