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
    (FALLOUT4, 0x000CEAC4),
    (FALLOUT4, 0x0018B214),
    (FALLOUT4, 0x00042565),
    (FALLOUT4, 0x0004322C),
    (FALLOUT4, 0x000D4166),
    (FALLOUT4, 0x001184C1),
    (FALLOUT4, 0x000787D8),
    (FALLOUT4, 0x0007239E),
}
SURGICAL_MASK = (FALLOUT4, 0x000787DA)
GAS_MASK_KEYWORD = (SELF, 0x00000809)
RAD_WEATHER_LIST = (SELF, 0x00000802)
MANAGED_STORM_WEATHERS = {(SELF, form_id) for form_id in range(0x840, 0x845)}
MOUTH_SLOT = 1 << 19
HAIR_OCCLUSION_SLOTS = (1 << 0) | (1 << 1) | (1 << 22)
REQUIRED_NPC_HAIR_SAFE_MASKS = {
    (FALLOUT4, 0x001184C1),
    (FALLOUT4, 0x000787D8),
}


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


def biped_slots(payload, flags):
    for name, value in subrecords(payload, flags):
        if name == b"BOD2":
            if len(value) < 4:
                raise ValueError("invalid BOD2 payload")
            return u32(value, 0)
    return 0


def form_list_items(payload, flags, masters):
    items = []
    for name, value in subrecords(payload, flags):
        if name != b"LNAM":
            continue
        if len(value) != 4:
            raise ValueError("invalid FLST LNAM payload")
        items.append(form_key(u32(value, 0), masters))
    return items


def section(text, start, end, label):
    start_pos = text.find(start)
    if start_pos < 0:
        raise SystemExit(f"missing {label} start marker: {start!r}")
    end_pos = text.find(end, start_pos + len(start))
    if end_pos < 0:
        raise SystemExit(f"missing {label} end marker: {end!r}")
    return text[start_pos:end_pos]


def require_tokens(text, tokens, label):
    missing = [token for token in tokens if token not in text]
    if missing:
        raise SystemExit(f"{label} contract missing tokens: {missing!r}")


def verify_source_contract():
    root = Path(__file__).resolve().parents[1]
    mask_source = (root / "src" / "RadEngine_Mask.cpp").read_text(encoding="utf-8")
    storm_source = (root / "src" / "RadEngine_Storm_Update.cpp").read_text(encoding="utf-8")

    require_tokens(
        mask_source,
        (
            "RE::BIPED_OBJECT::kHairTop",
            "RE::BIPED_OBJECT::kHairLong",
            "RE::BIPED_OBJECT::kScalp",
            "armor->HasKeyword(powerKeyword)",
            "armor->FillsBipedSlot(kHelmetSlot)",
            "kActorTypeHumanKeywordID = 0x0002CB72",
            "kTemporaryNpcGasMaskID = 0x001184C1",
            "actor->GetParentCell() != cell",
            "race->HasKeyword(humanKeyword)",
        ),
        "NPC hair/PA/cell/human helper",
    )

    mask_policy = section(
        mask_source,
        "void RadiationEngine::EquipNearbyNpcMasks(bool equip)",
        "// ----------------------------------------------------------------------------- gas-mask filter",
        "NPC mask policy",
    )
    require_tokens(
        mask_policy,
        (
            "IsHumanInCell(actor, playerCell, humanKeyword)",
            "WearsPowerArmorHelmet(actor, _armorTypePowerKeyword)",
            "RadMaskOwnership::ClaimTemporary",
            "actor->AddObjectToContainer(tempMask",
            "RE::TESObjectREFR::RemoveItemData data(tempMask, 1)",
            "entry.displacedFormIDs",
            "cleanupEntry(actor, *entry)",
        ),
        "NPC temporary mask policy",
    )
    if "if (IsActorInPowerArmor(actor)) continue;" in mask_policy:
        raise SystemExit("NPC mask policy regressed to full-PA exemption instead of PA-helmet exemption")
    if "if (_stormPhase == StormPhase::Active)" in mask_policy:
        raise SystemExit("managed storms must not suppress NPC gas masks")
    if "for (auto* weather : _stormWeathers)" in mask_policy:
        raise SystemExit("managed storm weather must not force NPC gas-mask cleanup")

    active_policy = section(
        storm_source,
        "case StormPhase::Active:",
        "case StormPhase::Aftermath:",
        "managed storm Active policy",
    )
    require_tokens(
        active_policy,
        (
            "if (--_fleeTick <= 0)",
            "EquipNearbyNpcMasks(true);",
            "MakeNearbyNPCsFlee();",
        ),
        "managed storm Active policy",
    )
    if "st.lethal && (--_fleeTick <= 0)" in active_policy:
        raise SystemExit("managed event fleeing regressed to lethal storms only")
    if "EquipNearbyNpcMasks(false);" in active_policy:
        raise SystemExit("managed Active storms must keep eligible human NPCs masked while they flee")
    mask_pos = active_policy.find("EquipNearbyNpcMasks(true);")
    flee_call_pos = active_policy.find("MakeNearbyNPCsFlee();")
    if not (0 <= mask_pos < flee_call_pos):
        raise SystemExit("managed storm mask/flee ordering changed")


def main(plugin_path):
    verify_source_contract()

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

    rad_weather_records = [
        (flags, payload)
        for kind, flags, form_id, payload in all_records
        if kind == b"FLST" and form_key(form_id, masters) == RAD_WEATHER_LIST
    ]
    if len(rad_weather_records) != 1:
        raise SystemExit(f"expected one RadWeatherList FLST, found {len(rad_weather_records)}")
    rad_weather_items = set(form_list_items(rad_weather_records[0][1], rad_weather_records[0][0], masters))
    missing_storms = sorted(MANAGED_STORM_WEATHERS - rad_weather_items)
    if missing_storms:
        raise SystemExit(f"managed storm weathers missing from RadWeatherList: {missing_storms!r}")

    tagged = set()
    missing_mouth_slot = []
    slot_masks = {}
    for kind, flags, form_id, payload in all_records:
        keyword_form_keys = {form_key(keyword_id, masters) for keyword_id in keyword_ids(payload, flags)}
        if kind == b"ARMO" and GAS_MASK_KEYWORD in keyword_form_keys:
            key = form_key(form_id, masters)
            slots = biped_slots(payload, flags)
            tagged.add(key)
            slot_masks[key] = slots
            if not slots & MOUTH_SLOT:
                missing_mouth_slot.append(key)

    if tagged != EXPECTED_MASKS:
        missing = sorted(EXPECTED_MASKS - tagged)
        unexpected = sorted(tagged - EXPECTED_MASKS)
        raise SystemExit(f"FARO_GasMask catalog mismatch: missing={missing!r}, unexpected={unexpected!r}")
    if SURGICAL_MASK in tagged:
        raise SystemExit("FARO_GasMask must not be applied to Clothes_Raider_SurgicalMask")
    if missing_mouth_slot:
        raise SystemExit(f"FARO_GasMask catalog has non-mouth armor: {missing_mouth_slot!r}")

    hair_safe = {key for key, slots in slot_masks.items() if not slots & HAIR_OCCLUSION_SLOTS}
    missing_hair_safe = sorted(REQUIRED_NPC_HAIR_SAFE_MASKS - hair_safe)
    if missing_hair_safe:
        details = {key: f"0x{slot_masks[key]:08X}" for key in missing_hair_safe}
        raise SystemExit(f"plain gas masks no longer preserve NPC hair: {details!r}")

    print(
        "FARO_GasMask catalog verified:",
        ", ".join(f"{local_id:08X}:{master}" for master, local_id in sorted(tagged)),
    )
    print(
        "NPC hair-safe gas masks:",
        ", ".join(f"{local_id:08X}:{master}" for master, local_id in sorted(hair_safe)),
    )
    print("Managed storm weathers verified in RadWeatherList.")
    print("NPC weather response policy verified: same-cell humans receive temporary hair-safe masks; managed events mask and flee.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} RADIATION_OVERHAUL.ESP")
    main(sys.argv[1])
