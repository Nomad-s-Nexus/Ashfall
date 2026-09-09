import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
DEPLOY = ROOT / "deploy" / "F4SE" / "Plugins" / "FalloutAnomaly"

config = json.loads((DEPLOY / "climate_weather.json").read_text(encoding="utf-8"))
assert config.get("version") == 1
assert isinstance(config.get("weatherOverrides"), list)

loader = (SRC / "ClimateWeatherConfig.cpp").read_text(encoding="utf-8")
header = (SRC / "ClimateWeatherConfig.h").read_text(encoding="utf-8")
runtime = (SRC / "ClimateRuntime.cpp").read_text(encoding="utf-8")
runtime_header = (SRC / "ClimateRuntime.h").read_text(encoding="utf-8")

for token in (
    "RE::TESDataHandler::GetSingleton()",
    "dataHandler->LookupForm(*localFormID, plugin)",
    "form->GetFormType() != RE::ENUM_FORM_ID::kWTHR",
    "temperatureOffsetC",
    "raw < -50.0 || raw > 50.0",
    "precipitation",
    "raw < 0.0 || raw > 100.0",
    "static_cast<float>(raw / 100.0)",
    "weather flags remain authoritative",
):
    assert token in loader, f"weather config contract missing: {token}"

for token in (
    "std::optional<Profile> Override",
    "std::unordered_map<std::uint32_t, Profile> _overrides",
    "std::optional<float> precipitation01",
):
    assert token in header, f"weather config header contract missing: {token}"

for token in (
    "ClimateWeatherConfig _weatherConfig;",
    "bool _weatherConfigLoaded{ false };",
):
    assert token in runtime_header, f"weather runtime cache contract missing: {token}"

for token in (
    "_weatherConfig.Load(\"Data/F4SE/Plugins/FalloutAnomaly/climate_weather.json\")",
    "_weatherConfig.Override(weather->GetFormID())",
    "environment.precipitation01 = *weatherOverride->precipitation01;",
    "environment.ambientC += weatherOverride->temperatureOffsetC;",
):
    assert token in runtime, f"weather runtime application missing: {token}"

for token in ("json root", "json::", "file >> root", "nlohmann::json"):
    assert token not in runtime, f"weather JSON parsing must remain outside the 4 Hz runtime: {token}"

for token in (
    "AddFormToFormList",
    "RemoveFormFromFormList",
    "CreateForm",
    "SetFormID",
    "ForceWeather",
):
    assert token not in loader

print("climate weather config verified: automatic flags first, load-once exact WTHR overrides, no plugin mutation")
