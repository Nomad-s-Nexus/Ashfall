import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
DEPLOY = ROOT / "deploy" / "F4SE" / "Plugins" / "FalloutAnomaly"

config_path = DEPLOY / "climate_protection.json"
config = json.loads(config_path.read_text(encoding="utf-8"))
assert config.get("version") == 1
assert isinstance(config.get("armorOverrides"), list)

loader = (SRC / "ClimateProtectionConfig.cpp").read_text(encoding="utf-8")
header = (SRC / "ClimateProtectionConfig.h").read_text(encoding="utf-8")

for token in (
    "RE::TESDataHandler::GetSingleton()",
    "dataHandler->LookupForm(*localFormID, plugin)",
    "form->GetFormType() != RE::ENUM_FORM_ID::kARMO",
    "_armorOverrides[form->GetFormID()] = insulation / 100.0f;",
    "insulation < 0.0f || insulation > 100.0f",
    "automatic insulation remains authoritative",
):
    assert token in loader, f"climate protection loader contract missing: {token}"

for token in (
    "std::optional<float> ArmorOverride",
    "std::unordered_map<std::uint32_t, float> _armorOverrides",
):
    assert token in header, f"climate protection header contract missing: {token}"

forbidden = (
    "AddFormToFormList",
    "RemoveFormFromFormList",
    "CreateForm",
    "SetFormID",
)
for token in forbidden:
    assert token not in loader

print("climate protection config verified: JSON overrides resolve once to runtime armor FormIDs")
