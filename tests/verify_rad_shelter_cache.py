from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src" / "RadEngine_Shelter.cpp").read_text(encoding="utf-8")

for token in (
    "RE::TESObjectLoadedEvent",
    "g_shelterProbeCache.dirty.store(true, std::memory_order_release);",
    "g_shelterProbeCache.dirty.exchange(false, std::memory_order_acq_rel)",
    "RadClimateShelterPolicy::ShouldProbe",
    "if (cell->IsInterior()) return true;",
    "if (!cell->IsAttached()) return true;",
    "return g_shelterProbeCache.covered;",
    "const bool debugJustEnabled = _debugShelter && !g_shelterProbeCache.debugWasEnabled;",
):
    assert token in source, f"rad shelter cache contract missing: {token}"

assert source.count("RayHitsStructure(") == 5

cache_return = source.index("return g_shelterProbeCache.covered;")
first_raycast = source.index("RayHitsStructure(")
assert cache_return < first_raycast, "cached shelter result must short-circuit before physics rays"

sink = source.split("class ShelterLoadSink", 1)[1].split("void EnsureShelterLoadSink", 1)[0]
assert "RayHitsStructure" not in sink
assert "ComputeEnclosedByStructure" not in sink

for token in (
    "SetDelete",
    "Enable(",
    "Disable(",
    "ModActorValue",
    "SetPosition",
):
    assert token not in source, f"shelter cache path must remain read-only: {token}"

print("rad shelter cache verified: load/movement invalidation, stationary cached return, no periodic physics work")
