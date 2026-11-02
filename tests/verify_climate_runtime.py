import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

runtime = (SRC / "ClimateRuntime.cpp").read_text(encoding="utf-8")
header = (SRC / "ClimateRuntime.h").read_text(encoding="utf-8")
hooks = (SRC / "Hooks.cpp").read_text(encoding="utf-8")
main = (SRC / "main.cpp").read_text(encoding="utf-8")
serialization = (SRC / "RadEngine_Serialization.cpp").read_text(encoding="utf-8")
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
    "auto shelter = rooflessInterior",
    "environment.rooflessInterior = rooflessInterior;",
    "? _shelter.SampleRooflessInterior(player, cell)",
    ": _shelter.Sample(player);",
    "ClimateRuntime::Reset()",
    "_shelter.Reset();",
    "_heat.Reset();",
    "RadClimateSerialization::Snapshot(_state)",
    "RadClimateSerialization::Restore(state)",
    "QueryPauseInfo()",
    "RadClimatePolicy::StepTimeJump",
    "RadClimatePolicy::ElapsedGameSeconds",
    "RadClimatePolicy::kTimeJumpThresholdDays",
    "RadClimatePolicy::kMaximumTimeJumpHours",
    "RadClimatePolicy::kMaximumStepSeconds",
    "return 8;",
):
    assert token in runtime, f"climate runtime input contract missing: {token}"

# Constraint: one sampler per tick, chosen by the roofless flag; never sample both.
sampler_ternary = re.compile(
    r"auto shelter = rooflessInterior\s+\? _shelter\.SampleRooflessInterior\(player, cell\)\s+"
    r": _shelter\.Sample\(player\);")
assert sampler_ternary.search(runtime) is not None, (
    "Update must choose one shelter sampler with the roofless ternary")
assert runtime.count("auto shelter") == 1, "Update must declare the shelter sample exactly once"
assert runtime.count("_shelter.Sample(player)") == 1, "Update must call the normal shelter sampler exactly once"
assert runtime.count("SampleRooflessInterior(player, cell)") == 1, (
    "Update must call the roofless sampler exactly once")


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
    "float _prevGameDays{ -1.0f };",
    "PrevGameDays() const noexcept",
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
assert "timeScale" not in runtime
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
save_callback = main.split("void SaveCallback", 1)[1].split(
    "void LoadCallback", 1
)[0]
assert "ClimateRuntime::GetSingleton().Reset();" not in post_load
assert "ClimateRuntime::GetSingleton().Reset();" in new_game

assert "OpenRecord(RadClimateSerialization::kRecord" in save_callback
assert "SnapshotForSave" in save_callback
assert "type == RadClimateSerialization::kRecord" in serialization
assert "RadClimateSerialization::ClassifyCandidates" in serialization
assert "ClimateRuntime::GetSingleton().RestoreFromSave" in serialization

print("climate runtime contract verified: event-driven protection/heat, movement-cached shelter, weather+season inputs, sprint activity, save boundary, game-main tick, no gameplay mutation")
