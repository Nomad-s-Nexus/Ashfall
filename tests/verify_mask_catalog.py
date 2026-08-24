#!/usr/bin/env python3
"""Verify the FARO_GasMask ARMO catalog directly from RadiationOverhaul.esp."""

import struct
import sys
import zlib
from pathlib import Path


RECORD_HEADER_SIZE = 24
COMPRESSED = 0x00040000
EXPECTED_MASTERS = ("DLCCoast.esm", "DLCNukaWorld.esm", "Fallout4.esm")
FALLOUT4 = "Fallout4.esm"
SELF = "RadiationOverhaul.esp"
EXPECTED_MASKS = {
    (FALLOUT4, 0x000CEAC4),  # Armor_HazmatSuit
    (FALLOUT4, 0x0018B214),  # Armor_HazmatSuitDamaged
    (FALLOUT4, 0x00042565),  # Armor_FlightHelmet
    (FALLOUT4, 0x0004322C),  # Armor_FlightHelmetRed
    (FALLOUT4, 0x000D4166),  # Armor_FlightHelmetBrown
    (FALLOUT4, 0x001184C1),  # Armor_Gasmask
    (FALLOUT4, 0x000787D8),  # Armor_Raider_Gasmask
    (FALLOUT4, 0x0007239E),  # Armor_Raider_GreenHoodGasmask
}
SURGICAL_MASK = (FALLOUT4, 0x000787DA)  # Clothes_Raider_SurgicalMask
GAS_MASK_KEYWORD = (SELF, 0x00000809)
MOUTH_SLOT = 1 << 19


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def records(data, start, end):
    offset = start
    while offset < end:
        kind = data[offset : offset + 4]
        size = u32(data, offset + 4)
        if kind == b"GRUP":
            group_end = offset + size
            if group_end > end or size < RECORD_HEADER_SIZE:
                raise ValueError("invalid GRUP bounds")
            yield from records(data, offset + RECORD_HEADER_SIZE, group_end)
            offset = group_end
            continue

        record_end = offset + RECORD_HEADER_SIZE + size
        if record_end > end:
            raise ValueError("invalid record bounds")
        yield kind, u32(data, offset + 8), u32(data, offset + 12), data[offset + RECORD_HEADER_SIZE : record_end]
        offset = record_end


def master_names(data):
    if data[:4] != b"TES4":
        raise ValueError("plugin does not start with a TES4 header")
    header_size = u32(data, 4)
    header_end = RECORD_HEADER_SIZE + header_size
    if header_end > len(data):
        raise ValueError("invalid TES4 header bounds")
    return tuple(
        value.rstrip(b"\x00").decode("ascii")
        for name, value in subrecords(data[RECORD_HEADER_SIZE:header_end], 0)
        if name == b"MAST"
    )


def form_key(form_id, masters):
    master_index = form_id >> 24
    local_id = form_id & 0x00FFFFFF
    if master_index < len(masters):
        return masters[master_index], local_id
    if master_index == len(masters):
        return SELF, local_id
    raise ValueError(f"FormID {form_id:08X} has invalid master index {master_index}")


def subrecords(payload, flags):
    if flags & COMPRESSED:
        expected_size = u32(payload, 0)
        payload = zlib.decompress(payload[4:])
        if len(payload) != expected_size:
            raise ValueError("compressed record size mismatch")

    offset = 0
    extended_size = None
    while offset < len(payload):
        name = payload[offset : offset + 4]
        size = u16(payload, offset + 4)
        offset += 6
        if name == b"XXXX":
            if size != 4:
                raise ValueError("invalid XXXX subrecord")
            extended_size = u32(payload, offset)
            offset += size
            continue
        if extended_size is not None:
            size = extended_size
            extended_size = None
        value = payload[offset : offset + size]
        if len(value) != size:
            raise ValueError("invalid subrecord bounds")
        yield name, value
        offset += size


def keyword_ids(payload, flags):
    for name, value in subrecords(payload, flags):
        if name == b"KWDA":
            if len(value) % 4:
                raise ValueError("invalid KWDA payload")
            return set(struct.unpack("<" + "I" * (len(value) // 4), value))
    return set()


def editor_id(payload, flags):
    for name, value in subrecords(payload, flags):
        if name == b"EDID":
            return value.rstrip(b"\x00").decode("ascii")
    return ""


def fills_mouth(payload, flags):
    for name, value in subrecords(payload, flags):
        if name == b"BOD2":
            if len(value) < 4:
                raise ValueError("invalid BOD2 payload")
            return bool(u32(value, 0) & MOUTH_SLOT)
    return False


def main(plugin_path):
    data = Path(plugin_path).read_bytes()
    masters = master_names(data)
    if masters != EXPECTED_MASTERS:
        raise SystemExit(
            f"master order changed: expected={EXPECTED_MASTERS!r}, actual={masters!r}"
        )
    all_records = list(records(data, 0, len(data)))
    keyword_form_keys = [
        form_key(form_id, masters)
        for kind, flags, form_id, payload in all_records
        if kind == b"KYWD"
        and editor_id(payload, flags) == "FARO_GasMask"
        and form_key(form_id, masters) == GAS_MASK_KEYWORD
    ]
    if keyword_form_keys != [GAS_MASK_KEYWORD]:
        raise SystemExit(f"expected exactly one FARO_GasMask keyword, found {keyword_form_keys!r}")

    tagged = set()
    missing_mouth_slot = []
    for kind, flags, form_id, payload in all_records:
        keyword_form_keys = {form_key(keyword_id, masters) for keyword_id in keyword_ids(payload, flags)}
        if kind == b"ARMO" and GAS_MASK_KEYWORD in keyword_form_keys:
            tagged.add(form_key(form_id, masters))
            if not fills_mouth(payload, flags):
                missing_mouth_slot.append(form_key(form_id, masters))

    if tagged != EXPECTED_MASKS:
        missing = sorted(EXPECTED_MASKS - tagged)
        unexpected = sorted(tagged - EXPECTED_MASKS)
        raise SystemExit(f"FARO_GasMask catalog mismatch: missing={missing!r}, unexpected={unexpected!r}")
    if SURGICAL_MASK in tagged:
        raise SystemExit("FARO_GasMask must not be applied to Clothes_Raider_SurgicalMask")
    if missing_mouth_slot:
        raise SystemExit(f"FARO_GasMask catalog has non-mouth armor: {missing_mouth_slot!r}")

    print(
        "FARO_GasMask catalog verified:",
        ", ".join(f"{local_id:08X}:{master}" for master, local_id in sorted(tagged)),
    )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} RADIATION_OVERHAUL.ESP")
    main(sys.argv[1])
