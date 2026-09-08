from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

actors = (SRC / "RadEngine_Storm_Actors.cpp").read_text(encoding="utf-8")
fx = (SRC / "RadEngine_Storm_FX.cpp").read_text(encoding="utf-8")
update = (SRC / "RadEngine_Storm_Update.cpp").read_text(encoding="utf-8")
weather = (SRC / "RadEngine_Weather.cpp").read_text(encoding="utf-8")
debug = (SRC / "RadEngine_Debug.cpp").read_text(encoding="utf-8")
engine = (SRC / "RadiationEngine.cpp").read_text(encoding="utf-8")
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
    "lethal storm spawn call": (update, "SpawnStormGhouls();"),
    "vanilla radstorm classification": (weather, "void RadiationEngine::CheckRadstormWarning()"),
    "ghoul debug controls": (debug, 'a.rfind("ghouls:chance:", 0) == 0'),
    "ghoul controls tab": (hud, 'id="tabbtn-ghouls"'),
    "ghoul chance slider": (hud, 'id="ghoulChance"'),
    "ghoul tab routing": (hud_js, '"ghouls"'),
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

for token in (
    "if (!_stormGhoulsEnabled) return;",
    "chance(rng) > _stormGhoulSpawnChance",
):
    assert token in actors, f"storm ghoul setting is not applied: {token}"

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

print("ghoul contract verified: storm/NPC handling present, same-cell peak scope enforced, player ghoulification absent")
