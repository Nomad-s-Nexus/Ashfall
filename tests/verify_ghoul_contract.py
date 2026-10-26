from pathlib import Path

from verify_mask_catalog import form_key, master_names, records, subrecords

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

actors = (SRC / "RadEngine_Storm_Actors.cpp").read_text(encoding="utf-8")
fx = (SRC / "RadEngine_Storm_FX.cpp").read_text(encoding="utf-8")
update = (SRC / "RadEngine_Storm_Update.cpp").read_text(encoding="utf-8")
weather = (SRC / "RadEngine_Weather.cpp").read_text(encoding="utf-8")
debug = (SRC / "RadEngine_Debug.cpp").read_text(encoding="utf-8")
# Constraint: the record lookups live in the ESL module; read it as part of the engine text.
engine = (SRC / "RadiationEngine.cpp").read_text(encoding="utf-8")
engine += (SRC / "RadEngine_ESL.cpp").read_text(encoding="utf-8")
header = (SRC / "RadiationEngine.h").read_text(encoding="utf-8")
internal = (SRC / "RadEngine_Internal.h").read_text(encoding="utf-8")
readme = (ROOT / "README.md").read_text(encoding="utf-8")
hud = (ROOT / "deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.html").read_text(encoding="utf-8")
hud_js = (ROOT / "deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/radhud.js").read_text(encoding="utf-8")
all_native = "\n".join(
    path.read_text(encoding="utf-8")
    for path in SRC.rglob("*")
    if path.suffix in {".cpp", ".h", ".hpp"}
)

required = {
    "storm ghoul spawn": (actors, "void RadiationEngine::SpawnStormGhouls()"),
    "storm ghoul cleanup": (actors, "void RadiationEngine::CheckGhoulPackCleanup()"),
    "storm ghoul clear": (actors, "void RadiationEngine::ClearStormGhouls()"),
    "ghoul actor classification": (fx, "bool RadiationEngine::IsGhoulActor"),
    "ghoul peak healing": (fx, "void RadiationEngine::ApplyStormPeakToNPCs()"),
    "ghoul flee exemption": (fx, "if (IsGhoulActor(actor)) continue;"),
    "rad-weather spawn call": (engine, "if (enteredRadWeather &&"),
    "vanilla radstorm classification": (weather, "void RadiationEngine::CheckRadstormWarning()"),
    "ghoul debug controls": (debug, 'a.rfind("ghouls:chance:", 0) == 0'),
    "ghoul controls tab": (hud, 'id="tabbtn-ghouls"'),
    "ghoul chance slider": (hud, 'id="ghoulChance"'),
    "ghoul state display": (hud, 'id="ghoulState"'),
    "ghoul tab routing": (hud_js, '"ghouls"'),
    "ghoul state sync": (hud_js, "function setGhoulSettings(state)"),
    "feral leveled-list lookup": (engine, "LookupForm(0x0758AE"),
    "glowing-one lookup": (engine, "LookupForm(0x0D39EE"),
    "ghoul keyword lookup": (engine, "LookupForm(kActorTypeGhoulKeyword"),
    "storm ghoul state": (header, "_stormGhoulForm"),
    "ghoul keyword constant": (internal, "kActorTypeGhoulKeyword"),
}

for name, (text, token) in required.items():
    assert token in text, f"missing required {name}: {token}"

radstorm_warning = weather.split("void RadiationEngine::CheckRadstormWarning()", 1)[1].split(
    "void RadiationEngine::BeginSmoothWeather", 1
)[0]
accepted_spawn = radstorm_warning.split("_radstormWarned  = true;", 1)[1]
assert "SpawnStormGhouls();" in accepted_spawn

ghoul_state = hud_js.split("function setGhoulSettings(state)", 1)[1].split(
    "window.SetDebugMenu", 1
)[0]
for token in (
    'var d = (typeof state === "string") ? JSON.parse(state) : state;',
    "var enabled = !!(d && d.enabled);",
    "Number(d && d.chance)",
    'enabled ? "ON" : "OFF"',
):
    assert token in ghoul_state, f"ghoul state payload handling is incomplete: {token}"
assert "enabled = !!(d && d.chance)" not in ghoul_state

for token in (
    "if (!_stormGhoulsEnabled) return;",
    "chance(rng) >= _stormGhoulSpawnChance",
):
    assert token in actors, f"storm ghoul setting is not applied: {token}"

for token in (
    "_lastRadWeatherFormID",
    "_radWeatherList->ContainsItem(sky->currentWeather)",
    "enteredRadWeather",
    "!vanillaRadstorm || _debugWeatherHold",
):
    assert token in engine or token in header, f"rad-weather ghoul ownership is incomplete: {token}"

assert "SpawnStormGhouls();" not in update

for token in ("ghouls:on", "ghouls:off", "ghouls:clear", "ghouls:reset"):
    assert token in debug, f"missing ghoul debug command: {token}"

peak = fx.split("void RadiationEngine::ApplyStormPeakToNPCs()", 1)[1].split(
    "void RadiationEngine::MakeNearbyNPCsFlee()", 1
)[0]
for token in (
    "auto* player = RE::PlayerCharacter::GetSingleton();",
    "auto* playerCell = player ? player->GetParentCell() : nullptr;",
    "if (!pl || !av || !playerCell) return;",
    "if (actor->GetParentCell() != playerCell) continue;",
):
    assert token in peak, f"ghoul peak effect must be player-cell scoped: {token}"

forbidden_player = (
    "RadOverhaul_GhoulRace",
    "GhoulifyPlayer",
    "PlayerGhoulRace",
    "_playerGhoul",
    "_ghoulified",
    "LookupForm(0x854",
    "LookupForm(0x870",
)

for token in forbidden_player:
    assert token not in all_native, f"player ghoulification code returned: {token}"

assert "Player-ghoulification (globals 0x854 / quest 0x870) was removed" in internal
assert "RadOverhaul_GhoulRace.esp" not in readme

plugin = (ROOT / "deploy/RadiationOverhaul.esp").read_bytes()
masters = master_names(plugin)
plugin_records = {}
for kind, flags, form_id, payload in records(plugin, 0, len(plugin)):
    if kind == b"TES4":
        continue
    key = form_key(form_id, masters)
    assert key not in plugin_records, f"duplicate plugin FormKey: {key}"
    plugin_records[key] = kind
    for tag, value in subrecords(payload, flags):
        if tag == b"EDID":
            assert value.rstrip(b"\0") != b"RadOverhaul_GhoulQuest", "obsolete player ghoul quest returned"

for local_id in (0x854, 0x870):
    assert ("RadiationOverhaul.esp", local_id) not in plugin_records, f"obsolete player ghoul record returned: {local_id:06X}"
for local_id, kind in ((0x819, b"SPEL"), (0x829, b"PERK"), (0x871, b"QUST")):
    assert plugin_records.get(("RadiationOverhaul.esp", local_id)) == kind, f"required severity/storm record missing: {local_id:06X}"

print("ghoul contract verified: storm/NPC handling present; obsolete player records absent from source and compiled ESP; severity/storm records retained")
