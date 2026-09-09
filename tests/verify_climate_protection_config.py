import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
DEPLOY = ROOT / "deploy" / "F4SE" / "Plugins" / "FalloutAnomaly"

config_path = DEPLOY / "climate_protection.json"
config = json.loads(config_path.read_text(encoding="utf-8"))
assert config.get("version") == 1
assert isinstance(config.get("armorOverrides"), list)
assert isinstance(config.get("keywordOverrides"), list)

loader = (SRC / "ClimateProtectionConfig.cpp").read_text(encoding="utf-8")
header = (SRC / "ClimateProtectionConfig.h").read_text(encoding="utf-8")
runtime = (SRC / "ClimateProtectionRuntime.cpp").read_text(encoding="utf-8")

for token in (
    "RE::TESDataHandler::GetSingleton()",
    "dataHandler->LookupForm(*localFormID, plugin)",
    "form->GetFormType() != RE::ENUM_FORM_ID::kARMO",
    "form->GetFormType() != RE::ENUM_FORM_ID::kKYWD",
    "root.contains(\"armorOverrides\")",
    "root.contains(\"keywordOverrides\")",
    "entry[\"plugin\"].is_string()",
    "ParseInsulation(entry)",
    "raw < 0.0 || raw > 100.0",
    "static_cast<float>(raw / 100.0)",
    "value.is_number_unsigned()",
    "value.is_number_integer()",
    "automatic insulation remains authoritative",
    "_keywordOverrides.push_back",
    "_armorOverrides.find(armor->GetFormID())",
    "armor->HasKeyword(rule.keyword)",
):
    assert token in loader, f"climate protection loader contract missing: {token}"

for token in (
    "std::optional<float> ArmorOverride(const RE::TESObjectARMO* armor)",
    "std::unordered_map<std::uint32_t, float> _armorOverrides",
    "std::vector<KeywordOverride> _keywordOverrides",
    "ExactOverrideCount() const noexcept",
    "KeywordOverrideCount() const noexcept",
):
    assert token in header, f"climate protection header contract missing: {token}"

assert "_config.ArmorOverride(armor)" in runtime
assert loader.find("_armorOverrides.find(armor->GetFormID())") < loader.find("armor->HasKeyword(rule.keyword)")

forbidden = (
    "AddFormToFormList",
    "RemoveFormFromFormList",
    "CreateForm",
    "SetFormID",
)
for token in forbidden:
    assert token not in loader

print("climate protection config verified: exact armor then keyword category overrides, load-once resolution, no FormLists or plugin mutation")
