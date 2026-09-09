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
    "_config.ArmorOverride(armor)",
    "RadClimateInsulationPolicy::CombinedInsulation",
    "Data/F4SE/Plugins/FalloutAnomaly/climate_protection.json",
    "piece.armor->GetFile(0)",
    "piece.armor->GetLocalFormID()",
    "file->GetFilename()",
    "if (wornForms != _lastWornForms)",
    "_lastWornForms = wornForms;",
    "dataHandler->LookupForm(0x0004D8A1, \"Fallout4.esm\")",
    "armor->HasKeyword(_powerArmorKeyword)",
    "RadClimatePowerArmorPolicy::IsCompleteSuit",
    "result.powerArmor",
    "class ClimateEquipSink final : public RE::BSTEventSink<RE::ActorEquipManagerEvent::Event>",
    "RE::ActorEquipManager::GetSingleton()",
    "equipManager->RegisterSink(&sink);",
    "event.actorAffected == player",
    "event.itemAffected->object->GetFormType() == RE::ENUM_FORM_ID::kARMO",
    "_runtime->Invalidate();",
    "_dirty.store(true, std::memory_order_release);",
    "_dirty.exchange(false, std::memory_order_acq_rel)",
):
    assert token in runtime, f"climate protection runtime contract missing: {token}"

for token in (
    "void Invalidate() noexcept;",
    "ClimateProtectionConfig _config;",
    "RadClimatePolicy::ProtectionSample _cached{};",
    "std::vector<std::uint32_t> _lastWornForms;",
    "RE::BGSKeyword* _powerArmorKeyword{ nullptr };",
    "std::atomic<bool> _dirty{ true };",
    "bool _eventsRegistered{ false };",
    "float LastInsulation01() const noexcept",
    "bool LastPowerArmor() const noexcept",
):
    assert token in header, f"climate protection cache contract missing: {token}"

assert "_refreshTick" not in runtime
assert "_refreshTick" not in header
assert "BSTEvent.h" not in header
assert "ActorEquipManagerEvent.h" not in header
assert "const auto protection = _protection.Sample(player);" in climate
assert "_protection.Reset();" in climate

for token in (
    "EquipObject(",
    "UnequipObject(",
    "AddItem",
    "RemoveItem",
    "ModActorValue",
    "SetDelete",
):
    assert token not in runtime, f"climate protection scan must remain read-only: {token}"

print("climate worn protection verified: event-driven armor invalidation, exact/keyword overrides, authored coverage, complete Power Armor detection")
