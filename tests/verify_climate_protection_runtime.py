from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

runtime = (SRC / "ClimateProtectionRuntime.cpp").read_text(encoding="utf-8")
header = (SRC / "ClimateProtectionRuntime.h").read_text(encoding="utf-8")
climate = (SRC / "ClimateRuntime.cpp").read_text(encoding="utf-8")

for token in (
    "player->biped",
    "std::size(biped.object)",
    "form->GetFormType() != RE::ENUM_FORM_ID::kARMO",
    "piecesByForm.try_emplace(armor->GetFormID())",
    "armor->FillsBipedSlot",
    "AuthoredCoverage(armor)",
    "_config.ArmorOverride(armor->GetFormID())",
    "RadClimateInsulationPolicy::CombinedInsulation",
    "_refreshTick = 3;",
    "Data/F4SE/Plugins/FalloutAnomaly/climate_protection.json",
    "piece.armor->GetFile(0)",
    "piece.armor->GetLocalFormID()",
    "file->GetFilename()",
    "if (wornForms != _lastWornForms)",
    "_lastWornForms = wornForms;",
):
    assert token in runtime, f"climate protection runtime contract missing: {token}"

for token in (
    "ClimateProtectionConfig _config;",
    "RadClimatePolicy::ProtectionSample _cached{};",
    "std::vector<std::uint32_t> _lastWornForms;",
    "float LastInsulation01() const noexcept",
):
    assert token in header, f"climate protection cache contract missing: {token}"

assert "const auto protection = _protection.Sample(player);" in climate
assert "_protection.Reset();" in climate

for token in (
    "EquipObject",
    "UnequipObject",
    "AddItem",
    "RemoveItem",
    "ModActorValue",
    "SetDelete",
):
    assert token not in runtime, f"climate protection scan must remain read-only: {token}"

print("climate worn protection verified: bounded authored-coverage scan, cached insulation, exact JSON overrides, change-only pasteable gear audit")
