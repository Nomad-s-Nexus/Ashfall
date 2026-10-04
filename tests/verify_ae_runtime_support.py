import os
import re
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main_cpp = (root / "src" / "main.cpp").read_text(encoding="utf-8")
storm_cpp = (root / "src" / "RadEngine_Storm_Weather.cpp").read_text(encoding="utf-8")

assert "if (ver == F4SE::RUNTIME_1_10_163)" in main_cpp
assert "ver >= F4SE::RUNTIME_1_10_162" not in main_cpp
assert "ver < F4SE::RUNTIME_1_10_980" not in main_cpp
assert "if (ver == F4SE::RUNTIME_1_11_240)" in main_cpp
assert "if (ver > F4SE::RUNTIME_1_10_984)" not in main_cpp
assert "if (ver >= F4SE::RUNTIME_1_11_137)" not in main_cpp
assert "ver <= F4SE::RUNTIME_1_11_240" not in main_cpp
assert "return GameVersion::AE;" in main_cpp
assert "g_gameVersion != GameVersion::OG && g_gameVersion != GameVersion::AE" in main_cpp
assert "world->cellMap" not in storm_cpp
assert "BGSWorldLocation" not in storm_cpp
assert "tes->gridCells" in storm_cpp
assert "grid->GetCell" in storm_cpp
assert "cell->IsAttached()" in storm_cpp
assert "cell->worldSpace != world" in storm_cpp
assert "cell->GetbhkWorld()" in storm_cpp
assert "cell->Pick(pick)" in storm_cpp
assert "pos, rot, nullptr, player->cachedWorldspace" in storm_cpp

commonlib = Path(os.environ["COMMONLIBF4_PATH"])
ids = (commonlib / "include" / "RE" / "IDs.h").read_text(encoding="utf-8")
handler = (commonlib / "include" / "RE" / "T" / "TESDataHandler.h").read_text(encoding="utf-8")
tes_header = (commonlib / "include" / "RE" / "T" / "TES.h").read_text(encoding="utf-8")
cell_header = (commonlib / "include" / "RE" / "T" / "TESObjectCELL.h").read_text(encoding="utf-8")
grid_header = (commonlib / "include" / "RE" / "G" / "GridCellArray.h").read_text(encoding="utf-8")
grid_array_header = (commonlib / "include" / "RE" / "G" / "GridArray.h").read_text(encoding="utf-8")
version_header = (commonlib / "include" / "F4SE" / "Version.h").read_text(encoding="utf-8")
shared = commonlib / "lib" / "commonlib-shared"
fmodule = (shared / "src" / "REX" / "FModule.cpp").read_text(encoding="utf-8")
variant = (shared / "include" / "REL" / "VariantID.h").read_text(encoding="utf-8")
iddb_header = (shared / "include" / "REL" / "IDDB.h").read_text(encoding="utf-8")

assert re.search(r"CreateExplosionAtLocation\s*\{\s*1105815\s*,\s*0\s*,\s*2192306\s*\}", ids)
assert re.search(r"Get\s*\{\s*1330136\s*,\s*2194566\s*\}", ids)
assert re.search(r"GetbhkWorld\s*\{\s*1326073\s*,\s*2200260\s*\}", ids)
assert re.search(r"Pick\s*\{\s*434717\s*,\s*2200263\s*\}", ids)
assert re.search(r"ctor\s*\{\s*526783\s*,\s*2230668\s*\}", ids)
assert re.search(r"SetStartEnd\s*\{\s*747470\s*,\s*2236622\s*\}", ids)
assert re.search(r"HasHit\s*\{\s*1181584\s*,\s*2277770\s*\}", ids)
assert re.search(r"GetHitFraction\s*\{\s*476687\s*,\s*2277771\s*\}", ids)
assert "!REX::FModule::IsRuntimeOG() && !REX::FModule::IsRuntimeAE()" in handler
assert re.search(r"GridCellArray\s*\*\s*gridCells\s*;", tes_header)
assert re.search(r"TESWorldSpace\s*\*\s*worldSpace\s*;", tes_header)
assert re.search(r"TESObjectCELL\s*\*\s*GetCell\s*\(", grid_header)
assert re.search(r"std::uint32_t\s+dimension\s*;", grid_array_header)
assert re.search(r"NiAVObject\s*\*\s*Pick\s*\(\s*bhkPickData\s*&", cell_header)
assert re.search(r"bool\s+IsAttached\s*\(\)\s+const\s+noexcept", cell_header)
assert re.search(r"TESWorldSpace\s*\*\s*worldSpace\s*;", cell_header)
assert re.search(r"bhkWorldM\s*\*\s*GetbhkWorld\s*\(\)\s+const", cell_header)
assert re.search(r"OG_LATEST_VERSION\s*=\s*\{\s*1\s*,\s*10\s*,\s*163\s*,\s*0\s*\}", fmodule)
assert re.search(r"NG_LATEST_VERSION\s*=\s*\{\s*1\s*,\s*10\s*,\s*984\s*,\s*0\s*\}", fmodule)
assert "if (safe_verm == OG_LATEST_VERSION)" in fmodule
assert "if (safe_verm > NG_LATEST_VERSION)" in fmodule
assert "return FModule::Runtime::kAE;" in fmodule
assert "GetRuntimeIndex()" in variant
assert "iddb->offset(m_offs[index].value)" in variant
assert "std::uint64_t id;" in iddb_header
assert "std::uint64_t offset;" in iddb_header


def lookup_v0(bin_path, address_id):
    data = bin_path.read_bytes()
    assert len(data) >= 8
    count = struct.unpack_from("<Q", data, 0)[0]
    assert len(data) == 8 + count * 16
    for entry_id, offset in struct.iter_unpack("<QQ", data[8:]):
        if entry_id == address_id:
            return offset
    return None


bins = commonlib / "AddressBins" / "F4SE" / "Plugins"
og_offset = lookup_v0(bins / "version-1-10-163-0.bin", 1105815)
assert og_offset == 0x114FE0

header_ae_builds = {
    int(build) for build in re.findall(r"RUNTIME_1_11_(\d+)\s*\{", version_header)
}
bin_ae_builds = {
    int(match.group(1))
    for path in bins.glob("version-1-11-*-0.bin")
    if (match := re.fullmatch(r"version-1-11-(\d+)-0\.bin", path.name))
}
assert header_ae_builds
assert header_ae_builds == bin_ae_builds

ae_builds = tuple(sorted(header_ae_builds))
required_ae_ids = {
    "CreateExplosionAtLocation": 2192306,
    "GridCellArray::Get": 2194566,
    "TESObjectCELL::GetbhkWorld": 2200260,
    "TESObjectCELL::Pick": 2200263,
    "bhkPickData::ctor": 2230668,
    "bhkPickData::SetStartEnd": 2236622,
    "bhkPickData::HasHit": 2277770,
    "bhkPickData::GetHitFraction": 2277771,
}
ae_offsets = {}
for build in ae_builds:
    bin_path = bins / f"version-1-11-{build}-0.bin"
    build_offsets = {}
    for name, address_id in required_ae_ids.items():
        offset = lookup_v0(bin_path, address_id)
        assert offset is not None and offset != 0, f"{name} missing from AE 1.11.{build}"
        build_offsets[name] = offset
    ae_offsets[build] = build_offsets

assert 240 in ae_offsets
assert ae_offsets[240]["CreateExplosionAtLocation"] == 0x2E6570
print("AE CreateExplosionAtLocation coverage: " + ", ".join(
    f"1.11.{build}=0x{offsets['CreateExplosionAtLocation']:X}"
    for build, offsets in ae_offsets.items()))
print("All ground-probe relocations exist in every CommonLib AE Address Library bin")
print("Runtime and RE contracts match commonlib_NonVR")
