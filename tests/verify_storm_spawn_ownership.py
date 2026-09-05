from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

actors = (SRC / "RadEngine_Storm_Actors.cpp").read_text(encoding="utf-8")
weather = (SRC / "RadEngine_Storm_Weather.cpp").read_text(encoding="utf-8")

ghoul_spawn = actors.split("void RadiationEngine::SpawnStormGhouls()", 1)[1].split(
    "void RadiationEngine::CheckGhoulPackCleanup()", 1
)[0]

for token in (
    "auto* cell = player->GetParentCell();",
    "!cell->IsExterior()",
    "!cell->IsAttached()",
    "cell->worldSpace != world",
):
    assert token in ghoul_spawn, f"storm ghoul spawn is missing ownership gate: {token}"

for token in (
    "ref->GetFormType() == RE::ENUM_FORM_ID::kACHR",
    "static_cast<RE::Actor*>(ref)",
    "actor->IsDead(false)",
    "Existing hunter pack still active",
):
    assert token in ghoul_spawn, f"storm ghoul pack bound is missing: {token}"

assert "ref->As<RE::Actor>()" not in ghoul_spawn

fire_ring = weather.split("void RadiationEngine::SpawnFireRing()", 1)[1].split(
    "bool RadiationEngine::SpawnDistantNuke()", 1
)[0]

for token in (
    "auto* cell = player->GetParentCell();",
    "!cell->IsExterior()",
    "!cell->IsAttached()",
    "cell->worldSpace != world",
):
    assert token in fire_ring, f"fire-ring spawn is missing ownership gate: {token}"

release = weather.split("void RadiationEngine::ReleaseStormWeather()", 1)[1].split(
    "void RadiationEngine::ForceClearAndSilence()", 1
)[0]
already_current = release.split("if (sky->currentWeather == target)", 1)[1].split("return;", 1)[0]
assert "_weatherHandoffSettleTicks = 0;" in already_current
assert "_weatherHandoffWaitTicks = 0;" in already_current

print("storm spawn ownership and handoff counter guards verified")
