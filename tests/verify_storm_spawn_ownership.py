from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

actors = (SRC / "RadEngine_Storm_Actors.cpp").read_text(encoding="utf-8")
weather = (SRC / "RadEngine_Storm_Weather.cpp").read_text(encoding="utf-8")
dome = (SRC / "RadEngine_Storm_Dome.cpp").read_text(encoding="utf-8")
main = (SRC / "main.cpp").read_text(encoding="utf-8")

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
    "std::size_t liveHunters = 0;",
    "ref->GetFormType() == RE::ENUM_FORM_ID::kACHR",
    "static_cast<RE::Actor*>(ref)",
    "actor && !actor->IsDead(false)",
    "Existing hunter pack still active",
    "Previous hunter pack defeated",
    "ClearStormGhouls();",
):
    assert token in ghoul_spawn, f"storm ghoul pack bound is missing: {token}"

assert "ref->As<RE::Actor>()" not in ghoul_spawn
assert "_stormGhoulRefs.erase" not in ghoul_spawn

ghoul_cleanup = actors.split("void RadiationEngine::CheckGhoulPackCleanup()", 1)[1].split(
    "void RadiationEngine::ClearStormGhouls()", 1
)[0]
assert "Player went indoors. Despawning the hunter pack." in ghoul_cleanup
assert "Player ownership unresolved during streaming; deferring pack cleanup." in ghoul_cleanup
unresolved = ghoul_cleanup.split(
    "if (!cell || !cell->IsExterior() || !cell->IsAttached() || !world || cell->worldSpace != world)", 1
)[1].split("const std::uint32_t wid", 1)[0]
assert "ClearStormGhouls();" not in unresolved
assert "return;" in unresolved

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

dome_follow = dome.split("void RadiationEngine::UpdateStormDomeFollow()", 1)[1].split(
    "void RadiationEngine::UpdateDustMotes()", 1
)[0]
dome_preflight, dome_move = dome_follow.split("if (!needsMove) return;", 1)
assert "if (!IsValidDomeReference(ref, _stormDomeWorld) || !ref->Get3D()) return;" in dome_preflight
assert "ref->Update3DPosition(true);" in dome_move
assert "IsValidDomeReference" not in dome_move
assert "ref->Get3D()" not in dome_move

hud_wait = main.split("void WaitThenCreateRadHud", 1)[1].split(
    "void StartRadHudWaitIfIdle", 1
)[0]
for token in (
    "auto probeQueued = std::make_shared<std::atomic<bool>>(false);",
    "probeQueued->compare_exchange_strong",
    "[free, probeQueued, generation]",
    "probeQueued->store(false, std::memory_order_release);",
):
    assert token in hud_wait, f"HUD readiness probe is not coalesced: {token}"

print("storm ownership, handoff, dome-follow, and HUD queue guards verified")
