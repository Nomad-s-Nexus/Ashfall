"""Fails when the native side stops reading the RadSurvival.esp globals.

The Papyrus quest owns hunger, thirst, sleep debt, disease risk and disease state; the DLL is only
allowed to read them. This guard pins that contract so a future edit cannot quietly reintroduce a
second model (the defect this replaced: a native clock with its own rates and its own co-save record).
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def text(path):
    return path.read_text(encoding="utf-8", errors="replace")


def check(condition, message):
    if not condition:
        raise SystemExit(f"needs globals contract: {message}")


needs_runtime = text(SRC / "NeedsRuntime.cpp")
needs_header = text(SRC / "NeedsRuntime.h")
hud_needs = text(SRC / "RadHudNeeds.cpp")
serialization = text(SRC / "RadEngine_Serialization.cpp")
main_source = text(SRC / "main.cpp")

# Requires one lookup per pool, plus the two disease mirrors the HUD renders.
for form in ("0x000801", "0x000802", "0x000803", "0x000822", "0x000823"):
    check(form in needs_runtime, f"NeedsRuntime.cpp no longer looks up {form}")
check(needs_runtime.count("ResolveGlobal(data, ") >= 5,
      "NeedsRuntime.cpp must resolve every pool and mirror through the global reader")
check("RE::ENUM_FORM_ID::kGLOB" in needs_runtime,
      "every resolved pool must be type-checked as a GLOB")
check("LookupForm(a_formID, kPlugin)" in needs_runtime,
      "the resolution must go through TESDataHandler::LookupForm")
check("RadSurvival.esp" in needs_runtime, "the lookup must name RadSurvival.esp")
check("RE::TESDataHandler" in needs_runtime, "the lookup must go through the data handler")
check("Reset" in needs_header, "a load must clear the cached global pointers")
check("_clock" not in needs_header and "CalendarState" not in needs_header,
      "the native needs clock must stay deleted")

# The HUD payload must carry what the reader produces.
for key in ('"hunger"', '"thirst"', '"sleepDebt"', '"risk"', '"disease"'):
    check(key in hud_needs, f"RadHudNeeds.cpp must publish {key}")

# Nothing may write the old co-save record, but an old save must still load.
check("kRecord" in serialization, "LoadState must still recognise the legacy NEED record")
check("OpenRecord(RadNeedsSerialization" not in main_source,
      "the needs co-save writer must not come back")
check(not (ROOT / "deploy/F4SE/Plugins/FalloutAnomaly/needs.json").exists(),
      "needs.json was deleted with the native clock; the quest script owns the rates")

print("Needs globals verified: five TESGlobal lookups, no native clock, legacy record read and skipped")
