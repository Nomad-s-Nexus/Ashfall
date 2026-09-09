import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
DEPLOY = ROOT / "deploy" / "F4SE" / "Plugins" / "FalloutAnomaly"

config_path = DEPLOY / "climate_heat.json"
config = json.loads(config_path.read_text(encoding="utf-8"))
assert config.get("version") == 1
assert isinstance(config.get("heatSources"), list)

loader = (SRC / "ClimateHeatConfig.cpp").read_text(encoding="utf-8")
header = (SRC / "ClimateHeatConfig.h").read_text(encoding="utf-8")
runtime = (SRC / "ClimateHeatRuntime.cpp").read_text(encoding="utf-8")
runtime_header = (SRC / "ClimateHeatRuntime.h").read_text(encoding="utf-8")

for token in (
    "RE::TESDataHandler::GetSingleton()",
    "dataHandler->LookupForm(*localFormID, plugin)",
    "entry[\"plugin\"].is_string()",
    "entry[\"radius\"].is_number()",
    "entry[\"strength\"].is_number()",
    "rawRadius <= 0.0 || rawRadius > 10000.0",
    "rawStrength < 0.0 || rawStrength > 100.0",
    "static_cast<float>(rawStrength / 100.0)",
    "built-in heat sources remain active",
):
    assert token in loader, f"climate heat loader contract missing: {token}"

for token in (
    "std::optional<Profile> Override",
    "std::unordered_map<std::uint32_t, Profile> _overrides",
    "float MaxRadius() const noexcept",
):
    assert token in header, f"climate heat header contract missing: {token}"

for token in (
    "_config.Load(\"Data/F4SE/Plugins/FalloutAnomaly/climate_heat.json\")",
    "_searchRadius = std::max(kBuiltInMaximumSearchRadius, _config.MaxRadius())",
    "world->ForEachReferenceInRange(player, _searchRadius",
    "const auto profile = ProfileFor(base->GetFormID())",
    "if (const auto overrideProfile = _config.Override(runtimeFormID))",
    "!ref->Get3D()",
    "RadClimateHeatPolicy::ShouldRefresh",
    "class ClimateHeatLoadSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>",
    "RE::TESObjectLoadedEvent::GetEventSource()",
    "source->RegisterSink(&sink);",
    "_runtime->Invalidate();",
    "_dirty.exchange(false, std::memory_order_acq_rel)",
):
    assert token in runtime, f"climate heat runtime contract missing: {token}"

for token in (
    "ClimateHeatConfig _config;",
    "void Invalidate() noexcept;",
    "std::atomic<bool> _dirty{ true };",
    "bool _eventsRegistered{ false };",
):
    assert token in runtime_header, f"climate heat cache contract missing: {token}"

assert "_refreshTick" not in runtime
assert "_refreshTick" not in runtime_header
assert "BSTEvent.h" not in runtime_header
assert "TESObjectLoadedEvent.h" not in runtime_header
for token in ("nlohmann::json", "json root", "json::", "file >> root"):
    assert token not in runtime, f"JSON parsing must remain outside the heat runtime hot path: {token}"

forbidden = (
    "AddFormToFormList",
    "RemoveFormFromFormList",
    "CreateForm",
    "SetFormID",
    "SetDelete",
    "Enable(",
    "Disable(",
)
for token in forbidden:
    assert token not in loader
    assert token not in runtime

print("climate heat config verified: load-once overrides, movement/load-event cache invalidation, no plugin mutation or timer polling")
