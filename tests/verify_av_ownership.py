"""Fails when two Rad systems, or two Rad owners, modify the same actor value.

A constant-effect ability is a peak-value modifier, and modifiers on the same actor value add up.
Two owners on one actor value therefore stack silently, and nothing in the game distinguishes them.
The contract is: exactly one Rad system owns each actor value, and while the layer is suspended
(vanilla Survival, or the enable switch) Rad owns none of them.
"""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
SCRIPTS = ROOT / "deploy/Scripts/Source/User"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import plugin_reader as pr  # noqa: E402

# Constraint: enum values as stored in MGEF DATA+68, keyed by the name the manifests use.
ACTOR_VALUES = {
    0x2DC: "CarryWeight",
    0x2D5: "ActionPoints",
    0x358: "HealRateMult",
    0x2DA: "SpeedMult",
    0x2E3: "DamageResist",
}


def check(condition, message):
    if not condition:
        raise SystemExit(f"actor-value ownership: {message}")


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def load_manifest(name):
    import json

    return json.loads(read(ROOT / "tools" / name))


def spell_owners(plugin_name):
    """Map actor value -> list of (spell EditorId, magnitude) for every Rad-ability spell."""
    buf = (ROOT / "deploy" / plugin_name).read_bytes()
    nodes = pr.parse(buf)
    effects = {node.form_id(buf) & 0xFFFFFF: node for node in pr.find_records(nodes, b"MGEF")}
    owners = {}
    for spell in pr.find_records(nodes, b"SPEL"):
        efid = spell.field(buf, b"EFID")
        efit = spell.field(buf, b"EFIT")
        if not efid or not efit or len(efid) < 4:
            continue
        effect = effects.get(int.from_bytes(efid[:4], "little") & 0xFFFFFF)
        if effect is None:
            continue
        data = effect.field(buf, b"DATA")
        if not data or len(data) < 72:
            continue
        actor_value = struct.unpack_from("<I", data, 68)[0]
        magnitude = struct.unpack_from("<f", efit, 0)[0]
        owners.setdefault(actor_value, []).append((pr.editor_id(buf, spell), round(magnitude, 3)))
    return owners


penalties = load_manifest("rad_penalty_spells.json")
diseases = load_manifest("rad_diseases.json")

penalty_values = {entry["actorValue"] for entry in penalties["spells"]}
disease_values = {disease["actorValue"] for disease in diseases["diseases"]}

# Constraint: one owner per actor value - the two systems must not overlap.
shared = penalty_values & disease_values
check(not shared, f"both Rad systems modify {sorted(shared)}")

owners = spell_owners("RadSurvival.esp")
check(len(owners) == 5, f"expected five modified actor values in the plugin, found {len(owners)}")
for actor_value, entries in owners.items():
    name = ACTOR_VALUES.get(actor_value, f"0x{actor_value:03X}")
    check(actor_value in ACTOR_VALUES, f"spell set modifies an unlisted actor value 0x{actor_value:03X}")
    check(len(entries) == 3, f"{name} has {len(entries)} abilities; a pool owns three stages")
    check(len({spell for spell, _ in entries}) == 3, f"{name} has a duplicated ability")

# Constraint: the shipped plugin owns exactly the actor values the two manifests claim.
check({ACTOR_VALUES[value] for value in owners} == penalty_values | disease_values,
      "the plugin and the manifests disagree about which actor values Rad modifies")

# The base plugin's radiation-stage perks must stay inert; a populated one is a second owner.
buf = (ROOT / "deploy/RadiationOverhaul.esp").read_bytes()
for spell in pr.find_records(pr.parse(buf), b"SPEL"):
    editor_id = pr.editor_id(buf, spell) or ""
    if not editor_id.startswith("RadOverhaul_Stage"):
        continue
    check(spell.field(buf, b"EFID") is None,
          f"{editor_id} now carries an effect; add it to the ownership table and this test")

# Constraint: no layer writes an actor value directly; every change is an ability spell.
for path in sorted(list(SRC.glob("*.cpp")) + list(SRC.glob("*.h")) + list(SCRIPTS.glob("*.psc"))):
    body = read(path)
    for token in ("ModValue", "SetActorValue", "ForceActorValue", "DamageActorValue",
                  "RestoreActorValue"):
        check(token not in body, f"{path.name} writes an actor value directly with {token}")

quest = read(SCRIPTS / "RadSurvivalQuest.psc")
suspend = quest.split("Function UpdatePenaltyStages(string asReliefPool)", 1)[1].split("int Function StageFor", 1)[0]
check("if !IsActive()" in suspend and "ClearAllPenalties()" in suspend,
      "a suspended layer must clear its penalty abilities")
tick = quest.split("Event OnTimerGameTime(", 1)[1].split("Function RestartTimer", 1)[0]
check("UpdatePenaltyStages(\"\")" in tick and "ReconcileDiseases()" in tick,
      "the tick must reconcile both ability sets, penalties and diseases")

disease_script = read(SCRIPTS / "RadSurvivalDiseases.psc")
reconcile = disease_script.split("Function Reconcile(", 1)[1].split("Function TryRestCure", 1)[0]
check("abActive && DiseasesOn()" in reconcile, "the disease ability must be gated on an active layer")
check("ApplySpell(wanted)" in reconcile, "the disease reconcile must derive the ability from state")

notes = read(ROOT / "docs/code-notes/Rad-Overhaul-Fallout-4.md")
table = notes.split("Actor-value ownership", 1)
check(len(table) == 2, "code-notes must carry the actor-value ownership table")
for name in sorted(penalty_values | disease_values):
    check(name in table[1], f"code-notes ownership table does not name {name}")

print("Actor-value ownership verified: five values, one owner each, none while suspended")
