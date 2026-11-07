"""Roofless-interior rain contract: one shipped cell, resolved by the climate loader, no hard-coded form id."""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "deploy/F4SE/Plugins/FalloutAnomaly/climate_weather.json"
CONFIG_SOURCE = ROOT / "src/ClimateWeatherConfig.cpp"
POLICY_SOURCE = ROOT / "src/ClimatePolicy.h"
RUNTIME_SOURCE = ROOT / "src/ClimateRuntime.cpp"

EXPECTED_CELLS = ["Fallout4.esm:0x017DF7"]

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def text(path):
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def check_config():
    check(CONFIG.is_file(), f"missing {CONFIG.relative_to(ROOT)}")
    if not CONFIG.is_file():
        return
    try:
        root = json.loads(text(CONFIG))
    except json.JSONDecodeError as error:
        check(False, f"climate_weather.json is not valid JSON: {error}")
        return
    check(root.get("version") == 1, "climate_weather.json must keep object version 1")
    check(isinstance(root.get("weatherOverrides"), list), "weatherOverrides must remain an array")
    cells = root.get("rooflessInteriorCells")
    check(isinstance(cells, list), "rooflessInteriorCells must be an array")
    if isinstance(cells, list):
        check(cells == EXPECTED_CELLS,
              f"exactly one approved roofless cell must ship (expected {EXPECTED_CELLS}, got {cells})")
        for entry in cells:
            check(isinstance(entry, str) and re.fullmatch(r"[\w .-]+\.esm:0x[0-9A-Fa-f]+", entry or "") is not None,
                  f"rooflessInteriorCells entries must be 'Plugin.esp:0xLOCALID' strings, got {entry!r}")


def check_loader():
    config_source = text(CONFIG_SOURCE)
    check('"rooflessInteriorCells"' in config_source,
          "ClimateWeatherConfig::Load must read the rooflessInteriorCells key")
    check("IsRooflessInterior" in text(ROOT / "src/ClimateWeatherConfig.h"),
          "ClimateWeatherConfig must expose IsRooflessInterior")
    check("LookupForm(" in config_source,
          "roofless cells must resolve plugin + local id through TESDataHandler")
    check("IsInterior()" in config_source,
          "the loader must reject non-interior cells")
    check("ParseCellEntry" in config_source,
          "the loader must parse Plugin.esp:0xLOCALID entries")


def check_policy_and_runtime():
    policy = text(POLICY_SOURCE)
    runtime = text(RUNTIME_SOURCE)
    check("PrecipitationFor(" in policy,
          "ClimatePolicy.h must own the pure PrecipitationFor decision")
    check("rooflessInterior" in policy,
          "the environment sample must carry the roofless-interior flag for the wetness rule")
    check("PrecipitationFor(" in runtime,
          "ClimateRuntime::Update must call PrecipitationFor")
    check("IsRooflessInterior(" in runtime,
          "ClimateRuntime::Update must ask the weather config whether the current cell is listed")
    check("SampleRooflessInterior(" in runtime,
          "ClimateRuntime::Update must sample the existing roof raycast for a listed interior")


def check_no_hardcoded_cell():
    pattern = re.compile(r"017DF7|0x17DF7", re.IGNORECASE)
    for path in sorted((ROOT / "src").glob("*")):
        if path.suffix.lower() not in (".cpp", ".h"):
            continue
        found = pattern.search(text(path))
        check(found is None, f"the approved cell id must live in the JSON, not in {path.name}")


check_config()
check_loader()
check_policy_and_runtime()
check_no_hardcoded_cell()

if failures:
    for failure in failures:
        print(f"[FAIL] {failure}")
    raise SystemExit(f"{len(failures)} roofless-interior contract(s) failed")
print("Roofless-interior rain contracts passed")
