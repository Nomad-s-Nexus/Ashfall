import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
trace = (ROOT / "src" / "NeedsConsumptionTrace.cpp").read_text(encoding="utf-8")
lifecycle = (ROOT / "src" / "RadLifecycle.cpp").read_text(encoding="utf-8")
ini = (ROOT / "deploy" / "F4SE" / "Plugins" / "FalloutAnomalyRadiation.ini").read_text(encoding="utf-8")

section = ini.split("[NeedsTrace]", 1)
assert len(section) == 2, "missing [NeedsTrace] INI section"
assert re.search(r"^Enabled=0\s*$", section[1].split("\n[", 1)[0], re.M), "trace must ship disabled"

assert '::GetPrivateProfileIntA("NeedsTrace", "Enabled", 0, kIni)' in trace
assert "RadNeedsTracePolicy::IsTraceEnabledSetting(setting)" in trace
assert "if (!Hooks::IsGameMainThread())" in trace
activation = trace.split("bool NeedsConsumptionTrace::ActivateOnGameThread()", 1)[1]
assert activation.index("IsGameMainThread") < activation.index("IsTraceEnabledSetting") < activation.index("Register<")
assert trace.count("g_active.store(true") == 1

for sink in ("ActorEquipManagerEvent::Event", "InventoryInterface::CountChangedEvent",
             "TESContainerChangedEvent", "TESMagicEffectApplyEvent"):
    assert f"RE::BSTEventSink<RE::{sink}>" in trace, sink

assert 'Register<RE::InventoryInterface::CountChangedEvent>(countSource, &countSink, "CountChanged")' in trace

assert trace.count("write_vfunc") == 1
assert "write_vfunc(RE::VTABLE_INDEX::Actor::DrinkPotion, &PlayerDrinkPotionTrace)" in trace
assert "RE::VTABLE::PlayerCharacter[0]" in trace
drink = trace.split("bool PlayerDrinkPotionTrace(", 1)[1].split("\n    }\n", 1)[0]
assert "const bool result = g_originalDrinkPotion(a_this, a_potion, a_stackID);" in drink
assert drink.rstrip().endswith("return result;")
assert activation.index("IsTraceEnabledSetting") < activation.index("InstallDrinkPotionTrace()")

for forbidden in ("write_call", "write_branch", "REL::ID", "REL::VariantID", "0x119",
                  "RemoveItem", "AddObjectToContainer", "SetActorValue", "ModActorValue", "RestoreActorValue",
                  "->DrinkPotion(", "QueueMainThreadTask", "GetInventoryObjectCount", "ForEachInventoryItem",
                  "SetInterval", "std::thread", "localStorage"):
    assert forbidden not in trace, f"diagnostic trace must stay observation-only: {forbidden}"

for line in trace.splitlines():
    assert not line.lstrip().startswith("//"), "no // comments"

activate = lifecycle.split("bool RadLifecycle::EnsureEngineActiveOnGameThread()", 1)[1]
assert activate.index("Hooks::IsGameMainThread()") < activate.index("NeedsConsumptionTrace::ActivateOnGameThread();")

print("needs consumption trace verified: opt-in, game-main registration, four existing event sources plus a pass-through player DrinkPotion trace, no state mutation")
