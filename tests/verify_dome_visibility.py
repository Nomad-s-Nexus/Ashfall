#!/usr/bin/env python3
"""Contract: the dome visibility table lists exactly the BSTriShape names of the shipped dome NIF and is a fresh bake of it."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import bake_dome_visibility as bake  # noqa: E402

nif = bake.Nif(bake.NIF.read_bytes())
nif_shapes = nif.shape_names()
assert len(nif_shapes) == 36, "expected 36 BSTriShape blocks, found %d" % len(nif_shapes)

header = bake.HEADER.read_text(encoding="utf-8").replace("\r\n", "\n")
names_block = header.split("kShapeNames[kShapeCount] = {", 1)[1].split("};", 1)[0]
table_names = re.findall(r'"([^"]+)"', names_block)
assert table_names == nif_shapes, "table names differ from NIF shape names: %r" % (set(table_names) ^ set(nif_shapes))

shapes, wins = bake.bake()
assert shapes == nif_shapes
fresh = bake.render(shapes, wins)
assert header == fresh, "src/RadDomeVisibilityTable.h is stale; run tools/bake_dome_visibility.py --write"

begin = [int(x) for x in re.findall(r"\d+", header.split("kWindowBegin[kShapeCount + 1] = {", 1)[1].split("};", 1)[0])]
assert len(begin) == 37 and begin[0] == 0 and begin == sorted(begin)
assert begin[-1] == sum(len(w) for w in wins.values())
for name in shapes:
    for a, b in wins[name]:
        assert 0.0 <= a < b <= bake.CYCLE_SEC, (name, a, b)

policy = (ROOT / "src" / "RadCorrectnessPolicy.h").read_text(encoding="utf-8")
assert "VisibleShapeMask(" in policy
dome = (ROOT / "src" / "RadEngine_Storm_Dome.cpp").read_text(encoding="utf-8")
assert "VisibleShapeMask(" in dome and "SetAppCulled(" in dome
assert dome.count("SetAppCulled(") == 1
print("verify_dome_visibility: OK (%d shapes, %d windows)" % (len(shapes), begin[-1]))