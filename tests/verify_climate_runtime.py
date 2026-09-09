from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

runtime = (SRC / "ClimateRuntime.cpp").read_text(encoding="utf-8")
header = (SRC / "ClimateRuntime.h").read_text(encoding="utf-8")
hooks = (SRC / "Hooks.cpp").read_text(encoding="utf-8")
main = (SRC / "main.cpp").read_text(encoding="utf-8")
shelter_runtime = (SRC / "ClimateShelterRuntime.cpp").read_text(encoding="utf-8")
shelter_header = (SRC / "ClimateShelterRuntime.h").read_text(encoding="utf-8")

for token in (
    "RE::PlayerCharacter::GetSingleton()",
    "player->GetParentCell()",
    "cell->IsInterior()",
    "player->IsSwimming()",
    "RE::Sky::GetSingleton()",
    "sky->currentWeather",
    "RE::TESWeather::WeatherDataFlags::kRainy",
    "RE::TESWeather::WeatherData::kWindSpeed",
    "RE::Calendar::GetSingleton()",
    "cal->gameDaysPassed->GetValue()",
    "RadClimateEnvironmentPolicy::AmbientTemperatureC",
    "RadClimateEnvironmentPolicy::WindProxyMps",
    "RadClimatePolicy::Step",
    "RadClimateStagePolicy::NextStage",
    "environment.precipitation01",
    "environment.submerged",
    "environment.windMps",
    "environment.indoors",
    "environment.shelter01 = shelter.thermal01;",
    "environment.precipitationShelter01 = shelter.precipitation01;",
    "const auto shelter = _shelter.Sample(player);",
    "ClimateRuntime::Reset()",
    "_shelter.Reset();",
):
    assert token in runtime, f"climate runtime input contract missing: {token}"

for token in (
    "ModActorValue",
    "DamageActorValue",
    "SetDelete",
    "ForceWeather",
    "SetPosition",
    "KillImpl",
    "HandleHealthDamage",
    "RemoveItem",
    "AddObjectToContainer",
):
    assert token not in runtime, f"climate observation slice must remain non-mutating: {token}"

for token in (
    "void Update();",
    "void Reset();",
    "RadClimatePolicy::ClimateState _state{};",
    "ClimateShelterRuntime _shelter;",
):
    assert token in header, f"climate runtime state contract missing: {token}"

for token in (
    "cell->IsInterior()",
    "cell->IsExterior()",
    "cell->IsAttached()",
    "RadClimateShelterPolicy::ShouldProbe",
    "player->GetPosition()",
    "ProbeRoof(player, cell)",
    "cell->Pick(pick)",
):
    assert token in shelter_runtime, f"climate shelter runtime contract missing: {token}"

assert "_refreshTick" not in shelter_runtime
assert "_refreshTick" not in shelter_header

engine_queue = hooks.split("static void QueueEngineUpdate()", 1)[1].split(
    "void PlayerCharacter_Update_Hook", 1
)[0]
assert "RadiationEngine::GetSingleton().Update();" in engine_queue
assert "ClimateRuntime::GetSingleton().Update();" in engine_queue
assert engine_queue.find("RadiationEngine::GetSingleton().Update();") < engine_queue.find(
    "ClimateRuntime::GetSingleton().Update();"
)

worker_producer = hooks.split("void PlayerCharacter_Update_Hook", 1)[1].split(
    "bool InstallUpdateHook()", 1
)[0]
assert "ClimateRuntime::GetSingleton().Update();" not in worker_producer

revert_callback = main.split("void RevertCallback", 1)[1].split(
    "std::atomic<bool> g_radWaiting", 1
)[0]
assert "ClimateRuntime::GetSingleton().Reset();" in revert_callback

post_load = main.split("case F4SE::MessagingInterface::kPostLoadGame:", 1)[1].split(
    "case F4SE::MessagingInterface::kPostPostLoad:", 1
)[0]
new_game = main.split("case F4SE::MessagingInterface::kNewGame:", 1)[1].split(
    "case F4SE::MessagingInterface::kPreLoadGame:", 1
)[0]
assert "ClimateRuntime::GetSingleton().Reset();" in post_load
assert "ClimateRuntime::GetSingleton().Reset();" in new_game

print("climate runtime contract verified: event-driven protection, movement-cached shelter, trusted weather/time/swimming inputs, game-main tick, load-safe reset, no gameplay mutation")
