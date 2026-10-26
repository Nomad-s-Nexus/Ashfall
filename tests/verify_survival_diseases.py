"""Record contract for the authored taint shortlists and the carried cure.

`tests/verify_survival.py` already owns the disease rules and the script wiring, and it sits at the
repository's 750-line cap, so the record-level half lives here: the manifest in
`tools/rad_diseases.json` is read, `deploy/RadSurvival.esp` is parsed with `tests/plugin_reader.py`,
and every keyword, form list, ingestible, magic effect and VMAD binding the script depends on is
checked against it. Read-only; re-authoring happens through FO4IDE.
"""

import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "deploy/RadSurvival.esp"
MANIFEST = ROOT / "tools/rad_diseases.json"
DISEASE_SOURCE = ROOT / "deploy/Scripts/Source/User/RadSurvivalDiseases.psc"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import plugin_reader  # noqa: E402

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def text(path):
    return path.read_text(encoding="utf-8")


check(PLUGIN.is_file(), f"missing {PLUGIN.relative_to(ROOT)}")
check(MANIFEST.is_file(), f"missing {MANIFEST.relative_to(ROOT)}")
check(DISEASE_SOURCE.is_file(), f"missing {DISEASE_SOURCE.relative_to(ROOT)}")
diseases = json.loads(text(MANIFEST)) if MANIFEST.is_file() else {}
disease_code = DISEASE_SOURCE.read_text(encoding="utf-8") if DISEASE_SOURCE.is_file() else ""

if PLUGIN.is_file():
    esp = PLUGIN.read_bytes()
    check(esp[:4] == b"TES4", "RadSurvival.esp is not a plugin")
    try:
        tree = plugin_reader.parse(esp)
    except plugin_reader.PluginError as exc:
        tree = []
        check(False, f"RadSurvival.esp does not parse as a plugin: {exc}")

    def plugin_records(signature):
        return plugin_reader.records_by_form_id(esp, tree, signature)

    # Constraint: the disease script reads authored taint records, not a hardcoded item set.
    taint = diseases.get("taint", {})
    taint_keywords = taint.get("keywords", [])
    taint_lists = taint.get("lists", [])
    cure = taint.get("cure", {})
    check(len(taint_keywords) == 4,
          "the taint table must declare three exposure keywords and the cure marker")
    check(len(taint_lists) == 3 and sorted(entry.get("category", 0) for entry in taint_lists) == [1, 2, 3],
          "the taint table must name one list per exposure category, 1-3 exactly once")

    keyword_ids = {}
    for entry in taint_keywords:
        form_id = int(entry.get("formKey", "0x0"), 16)
        keyword_ids[entry.get("editorId")] = form_id
        node = plugin_records(b"KYWD").get(form_id)
        check(node is not None and plugin_reader.editor_id(esp, node) == entry.get("editorId"),
              f"missing taint keyword {entry.get('editorId')}")

    for entry in taint_lists:
        node = plugin_records(b"FLST").get(int(entry.get("formKey", "0x0"), 16))
        check(node is not None and plugin_reader.editor_id(esp, node) == entry.get("editorId"),
              f"missing taint form list {entry.get('editorId')}")
        if node is None:
            continue
        expected = [int(key.split(":")[0], 16) for key in entry.get("entries", [])]
        actual = [int.from_bytes(esp[start:start + 4], "little")
                  for sig, start, size in node.subrecords(esp) if sig == b"LNAM"]
        check(expected and actual == expected,
              f"{entry.get('editorId')} entries diverged from the manifest")
        check(entry.get("keyword") in keyword_ids,
              f"{entry.get('editorId')} names no authored keyword")
        check(f"FormList Property {entry.get('property')} Auto" in disease_code,
              f"the disease script must read {entry.get('property')}")

    item = plugin_records(b"ALCH").get(int(cure.get("itemFormId", "0x0"), 16))
    effect = plugin_records(b"MGEF").get(int(cure.get("effectFormId", "0x0"), 16))
    check(item is not None and effect is not None, "missing the cure ingestible or its effect")
    if item is not None and effect is not None:
        check(plugin_reader.editor_id(esp, item) == cure.get("itemEditorId")
              and plugin_reader.editor_id(esp, effect) == cure.get("effectEditorId"),
              "the cure ingestible and its effect must be the records the manifest names")
        efid = item.field(esp, b"EFID") or b"\x00" * 4
        enit = item.field(esp, b"ENIT") or b"\x00" * 20
        weight = item.field(esp, b"DATA") or b"\x00" * 4
        data = effect.field(esp, b"DATA") or b""
        check(int.from_bytes(efid, "little") == int(cure.get("effectFormId", "0x0"), 16),
              "the cure item must reference the authored effect, or consuming it does nothing")
        check(int.from_bytes(enit[:4], "little") == int(cure.get("itemValue", 0))
              and abs(struct.unpack("<f", weight[:4])[0] - float(cure.get("itemWeight", 0))) < 1e-6,
              "the cure item value or weight does not match the manifest")
        check(len(data) >= 68
              and int.from_bytes(data[0:4], "little") == int(cure.get("effectFlags", 0))
              and int.from_bytes(data[64:68], "little") == int(cure.get("effectArchetype", 0)),
              "the cure effect must keep the manifest's flags and Script archetype")

    # Constraint: every authored taint record must be bound on the disease script, never left None.
    quest = plugin_records(b"QUST").get(0x01000800)
    scripts = plugin_reader.vmad_properties(esp, quest) if quest is not None else {}
    check("RadSurvivalDiseases" in scripts, "the quest VMAD must attach the disease script")
    disease_script = scripts.get("RadSurvivalDiseases", {})
    for entry in taint_lists + taint_keywords:
        name = entry.get("property")
        form_id = int(entry.get("formKey", "0x0"), 16)
        check(disease_script.get(name) == form_id,
              f"the disease script must bind {name} to 0x{form_id:08X}, found {disease_script.get(name)}")

if failures:
    print("survival record contract failures:")
    for failure in failures:
        print("  FAIL", failure)
    sys.exit(1)

print("Survival taint records verified: four keywords, three filled lists, the cure item and its bindings")
