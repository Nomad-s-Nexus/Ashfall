from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

runtime = (SRC / "ClimateRuntime.cpp").read_text(encoding="utf-8")
header = (SRC / "ClimateRuntime.h").read_text(encoding="utf-8")
hooks = (SRC / "Hooks.cpp").read_text(encoding="utf-8")
main = (SRC / "main.cpp").read_text(encoding="utf-8")
shelter_runtime = (SRC / "ClimateShelterRuntime.cpp").read_text(encoding="utf-8")
shelter_header = (SRC / "ClimateShelterRuntime.h").read_text(encoding="utf-8")
heat_runtime = (SRC / "ClimateHeatRuntime.cpp").read_text(encoding="utf-8")
heat_header = (SRC / "ClimateHeatRuntime.h").read_text(encoding="utf-8")

for token in (
    "RE::PlayerCharacter::GetSingleton()",
    "player->GetParentCell()",
    "cell->IsInterior()",
    "player->IsSwimming()",
    "player->DoGetSprinting()",
    "RE::Sky::GetSingleton()",
    "sky->currentWeather",
    "RE::TESWeather::WeatherDataFlags::kPleasant",
    "RE::TESWeather::WeatherDataFlags::kCloudy",
    "RE::TESWeather::WeatherDataFlags::kRainy",
    "RE::TESWeather::WeatherDataFlags::kSnow",
    "RE::TESWeather::WeatherData::kWindSpeed",
    "RE::Calendar::GetSingleton()",
    "cal->gameDaysPassed->GetValue()",
    "cal->GetMonth()",
    "ClassifyWeather(weather)",
    "RadClimateEnvironmentPolicy::AmbientTemperatureC(indoors, weatherClass, GameDays(), gameMonth)",
    "RadClimateEnvironmentPolicy::PrecipitationIntensity01",
    "RadClimateEnvironmentPolicy::WindProxyMps",
    "RadClimatePolicy::Step",
    "RadClimateStagePolicy::NextStage",
    "environment.precipitation01",
    "environment.submerged",
    "environment.windMps",
    "environment.indoors",
    "environment.shelter01 = shelter.thermal01;",
    "environment.precipitationShelter01 = shelter.precipitation01;",
    "environment.heat01 = _heat.Sample(player);",
    "environment.activity01 = player->DoGetSprinting() ? 1.0f : 0.0f;",
    "const auto shelter = _shelter.Sample(player);",
    "ClimateRuntime::Reset()",
    "_shelter.Reset();",
    "_heat.Reset();",
    "RadClimateSerialization::Snapshot(_state)",
    "RadClimateSerialization::Restore(state)",
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
    "SnapshotForSave() const noexcept",
    "RestoreFromSave(const RadClimateSerialization::PersistedState& state) noexcept",
    "RadClimatePolicy::ClimateState _state{};",
    "ClimateShelterRuntime _shelter;",
    "ClimateHeatRuntime _heat;",
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

for token in (
    "RE::TES::GetSingleton()",
    "world->ForEachReferenceInRange(player, _searchRadius",
    "RadClimateHeatPolicy::ShouldRefresh",
    "ref->GetObjectReference()",
    "ref->Get3D()",
    "ProfileFor(base->GetFormID())",
    "RadClimateHeatPolicy::Contribution(sample)",
    "_config.Load(\"Data/F4SE/Plugins/FalloutAnomaly/climate_heat.json\")",
    "RE::TESObjectLoadedEvent::GetEventSource()",
    "_runtime->Invalidate();",
    "add(0x002476B7",
    "add(0x00048280",
    "add(0x00048281",
    "add(0x00048282",
):
    assert token in heat_runtime, f"climate heat runtime contract missing: {token}"

assert "0x0010C3B6" not in heat_runtime
assert "_refreshTick" not in shelter_runtime
assert "_refreshTick" not in shelter_header
assert "_refreshTick" not in heat_runtime
assert "_refreshTick" not in heat_header

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

print("climate runtime contract verified: event-driven protection/heat, movement-cached shelter, weather+season inputs, sprint activity, save boundary, game-main tick, no gameplay mutation")
