import sys
from pathlib import Path


html = Path(sys.argv[1]).read_text(encoding="utf-8")
for marker in ('HEALTH', 'STAMINA (AP)', 'TISSUE', "fallback=!!d.fallback", "var show=fallback||t>0"):
    if marker not in html:
        raise SystemExit(f"missing HUD fallback contract: {marker}")

source = Path(sys.argv[2]).read_text(encoding="utf-8")
for marker in ('_hudApi ? "false" : "true"', 'values->health', 'values->actionPoints'):
    if marker not in source:
        raise SystemExit(f"missing HUD fallback data: {marker}")
print("HUD fallback contract verified")
