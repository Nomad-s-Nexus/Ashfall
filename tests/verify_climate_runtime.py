from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

runtime = (SRC / "ClimateRuntime.cpp").read_text(encoding="utf-8")
header = (SRC / "ClimateRuntime.h").read_text(encoding="utf-8")

for token in (
    "RE::PlayerCharacter::GetSingleton()",
    "player->GetParentCell()",
    "cell->IsInterior()",
    "player->IsSwimming()",
    "RE::Sky::GetSingleton()",
    "sky->currentWeather",
    "RE::TESWeather::WeatherDataFlags::kRainy",
    "RE::Calendar::GetSingleton()",
    "cal->gameDaysPassed->GetValue()",
    "RadClimateEnvironmentPolicy::AmbientTemperatureC",
    "RadClimatePolicy::Step",
    "environment.precipitation01",
    "environment.submerged",
    "environment.indoors",
    "environment.shelter01",
    "ClimateRuntime::Reset()",
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
):
    assert token in header, f"climate runtime state contract missing: {token}"

print("climate runtime contract verified: trusted inputs only, tested ambient policy, no gameplay mutation")
