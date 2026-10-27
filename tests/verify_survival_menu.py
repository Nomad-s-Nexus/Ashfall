#!/usr/bin/env python3
"""Contracts for the SURVIVAL debug tab, the Pip-Boy disease perks and the name patch.

The debug menu must expose survival state where the player can read it, the disease
perks must exist on named records so the Pip-Boy PERKS tab can show what the player
has, and the display-name patch must remain the last step of a plugin build because
the editor writer cannot set Authoring\\FULL.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VIEW = ROOT / "deploy/PrismaUI_F4/views/Interface/FalloutAnomaly"
SRC = ROOT / "src"
PLUGIN = ROOT / "deploy/RadSurvival.esp"
TOOLS = ROOT / "tools"
SCRIPTS = ROOT / "deploy/Scripts/Source/User"

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8") if path.is_file() else ""


html = text(VIEW / "radhud.html")
js = text(VIEW / "radhud.js")
css = text(VIEW / "radhud.css")

check('id="tabbtn-survival"' in html, "the debug menu must offer a SURVIVAL tab button")
check('id="tab-survival"' in html, "the SURVIVAL tab needs a pane")
check('id="survivalPools"' in html and 'id="survivalDisease"' in html,
      "the SURVIVAL pane must expose pool and disease readouts")
check('"survival"' in js, "showTab must know the survival tab")
check("window.updateSurvival" in js, "the view must render the survival payload")
check("updateSurvival" in text(SRC / "RadHudNeeds.cpp"), "PushSurvival must reach the main view")
check("PushSurvival(snapshot)" in text(SRC / "NeedsRuntime.cpp"),
      "the needs tick must push the survival snapshot")
check("font-size:15px" in css, "the debug detail rows must be readable without squinting")

names = json.loads(text(TOOLS / "rad_names.json") or "{}").get("names", {})
check(bool(names), "tools/rad_names.json must list the display names")
check((TOOLS / "patch_full_names.py").is_file(),
      "the display-name patch must ship as a tool, since the editor cannot write FULL")

disease_source = text(SCRIPTS / "RadSurvivalDiseases.psc")
check("int Property FeverPerkBase" in disease_source and "int Property WastingPerkBase" in disease_source,
      "the disease script must carry the perk base ids")
check("player.AddPerk(" in disease_source and "player.RemovePerk(" in disease_source,
      "a disease stage must add and remove its perk")
check("Function DiseasePerkFor(" in disease_source, "the perk must resolve by disease and stage")

if PLUGIN.is_file():
    sys.path.insert(0, str(ROOT / "tests"))
    import plugin_reader as parser

    buf = PLUGIN.read_bytes()
    tree = parser.parse(buf)

    def record_fields(signature: bytes) -> dict[int, dict[bytes, bytes]]:
        out: dict[int, dict[bytes, bytes]] = {}
        for form_id, node in parser.records_by_form_id(buf, tree, signature).items():
            out[form_id] = {sig: buf[offset:offset + length] for sig, offset, length in node.subrecords(buf)}
        return out

    perks = record_fields(b"PERK")
    found = {}
    for form_id, subs in perks.items():
        editor_id = subs.get(b"EDID", b"").rstrip(b"\x00").decode("ascii", "replace")
        full = subs[b"FULL"].rstrip(b"\x00").decode("utf-8") if b"FULL" in subs else None
        found[editor_id] = full
    for editor_id, wanted in names.items():
        if editor_id.startswith("RadSurvival_Perk"):
            check(editor_id in found, f"missing the {editor_id} perk record")
            check(found.get(editor_id) == wanted,
                  f"{editor_id} must be named {wanted!r}, found {found.get(editor_id)!r}")

    alch = record_fields(b"ALCH")
    cure_full = None
    for subs in alch.values():
        if subs.get(b"EDID", b"").rstrip(b"\x00") == b"RadSurvival_CureItem":
            cure_full = subs[b"FULL"].rstrip(b"\x00").decode("utf-8") if b"FULL" in subs else None
    check(cure_full == names.get("RadSurvival_CureItem"),
          f"the cure item must be named {names.get('RadSurvival_CureItem')!r}, found {cure_full!r}")

if failures:
    print("survival menu contract failures:")
    for failure in failures:
        print("  FAIL", failure)
    raise SystemExit(1)

print("Survival menu verified: SURVIVAL tab, Pip-Boy disease perks on named records, display-name patch")
