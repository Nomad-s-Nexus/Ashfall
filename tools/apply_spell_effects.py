"""Append the effect entry (EFID/EFIT) to the FO4IDE-authored Rad survival spells.

FO4IDE (build f594af075) authors the MGEF fields and the SPEL skeleton, but it does not expose a
spell's effect list, and a Peak Value Modifier's magnitude lives in that list rather than in the
magic effect. This tool adds the missing entry, and nothing else:

    EFID  4 bytes           the paired magic effect's form id
    EFIT  12 bytes          float magnitude, uint32 area, uint32 duration

Only the target spell records grow (28 bytes each); the enclosing group size is adjusted to suit.
The tool is idempotent: an existing entry is verified, and a magnitude that no longer matches
tools/rad_penalty_spells.json is rewritten in place. Re-saving the plugin in FO4IDE drops the
effect list, so run this afterwards; tests/verify_survival.py fails while it is missing.

Usage:
    python tools/apply_spell_effects.py [--plugin PATH] [--manifest PATH]
"""

import argparse
import json
import os
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_PLUGIN = os.path.join(ROOT, "deploy", "RadSurvival.esp")
DEFAULT_MANIFEST = os.path.join(ROOT, "tools", "rad_penalty_spells.json")

EFID_SIZE = 4
EFIT_SIZE = 12
EFFECT_BYTES = 6 + EFID_SIZE + 6 + EFIT_SIZE


class PluginError(Exception):
    """Raised when the plugin or the manifest does not look the way the tool expects."""


class Node:
    """One record or group in a plugin file, with the offsets of its own bytes."""

    __slots__ = ("kind", "sig", "offset", "end", "data_start", "data_end", "kids", "parent")

    def __init__(self, kind, sig, offset, end, data_start, data_end, parent):
        self.kind = kind
        self.sig = sig
        self.offset = offset
        self.end = end
        self.data_start = data_start
        self.data_end = data_end
        self.kids = []
        self.parent = parent

    def subrecords(self, buf):
        """Yield (signature, data_start, length) for a record's subrecords."""
        off = self.data_start
        while off < self.data_end:
            sig = bytes(buf[off:off + 4])
            (size,) = struct.unpack_from("<H", buf, off + 4)
            yield sig, off + 6, size
            off += 6 + size

    def field(self, buf, signature):
        for sig, start, size in self.subrecords(buf):
            if sig == signature:
                return bytes(buf[start:start + size])
        return None


def parse(buf, start=0, end=None, parent=None):
    """Parse a plugin file (or one group's contents) into a list of nodes."""
    if end is None:
        end = len(buf)
    nodes = []
    off = start
    while off < end:
        sig = bytes(buf[off:off + 4])
        if len(sig) < 4:
            raise PluginError(f"truncated record header at {off}")
        (size,) = struct.unpack_from("<I", buf, off + 4)
        if sig == b"GRUP":
            if size < 24 or off + size > end:
                raise PluginError(f"GRUP at {off} claims {size} bytes, past the {end}-byte block")
            node = Node("group", bytes(buf[off + 8:off + 12]), off, off + size, off + 24, off + size, parent)
            node.kids = parse(buf, off + 24, off + size, node)
            nodes.append(node)
            off += size
        else:
            if off + 24 + size > end:
                raise PluginError(f"{sig!r} at {off} claims {size} bytes, past the {end}-byte block")
            node = Node("record", sig, off, off + 24 + size, off + 24, off + 24 + size, parent)
            nodes.append(node)
            off = node.end
    if off != end:
        raise PluginError(f"trailing bytes: consumed {off} of {end}")
    return nodes


def walk(nodes):
    for node in nodes:
        yield node
        yield from walk(node.kids)


def find_records(nodes, signature):
    return [node for node in walk(nodes) if node.kind == "record" and node.sig == signature]


def editor_id(buf, node):
    raw = node.field(buf, b"EDID")
    if raw is None:
        return None
    return raw.rstrip(b"\x00").decode("ascii", "replace")


def form_key(value):
    if isinstance(value, int):
        return value
    if isinstance(value, str) and value.lower().startswith("0x") and ":" not in value:
        return int(value, 16)
    raise PluginError(f"expected a bare form id such as 0x01000804, got {value!r}")


def load_manifest(path):
    with open(path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    spells = manifest.get("spells") or []
    if not spells:
        raise PluginError(f"{path} lists no spells")
    seen = set()
    for entry in spells:
        key = (entry["pool"], entry["stage"])
        if key in seen:
            raise PluginError(f"duplicate pool/stage {key} in {path}")
        seen.add(key)
        if form_key(entry["mgefFormId"]) >> 24 != 1 or form_key(entry["spellFormId"]) >> 24 != 1:
            raise PluginError(f"{entry['spellEditorId']}: self-owned form ids must use master index 1")
    return manifest


def check_magic_effects(buf, manifest):
    """Every paired MGEF must be the archetype/actor-value the manifest claims."""
    effects = {editor_id(buf, node): node for node in find_records(parse(buf), b"MGEF")}
    for entry in manifest["spells"]:
        node = effects.get(entry["mgefEditorId"])
        if node is None:
            raise PluginError(f"missing magic effect {entry['mgefEditorId']}")
        data = node.field(buf, b"DATA")
        if data is None or len(data) < 84:
            raise PluginError(f"{entry['mgefEditorId']}: magic effect DATA is missing or short")
        (archetype,) = struct.unpack_from("<I", data, 64)
        (actor_value,) = struct.unpack_from("<I", data, 68)
        (casting_type,) = struct.unpack_from("<I", data, 80)
        if archetype != manifest["mgefDefaults"]["archetype"]:
            raise PluginError(f"{entry['mgefEditorId']}: archetype {archetype}, expected 34")
        if casting_type != manifest["mgefDefaults"]["castingType"]:
            raise PluginError(f"{entry['mgefEditorId']}: casting type {casting_type}, expected 0")
        expected = form_key(entry["actorValueFormId"].split(":")[0])
        if actor_value != expected:
            raise PluginError(
                f"{entry['mgefEditorId']}: actor value 0x{actor_value:08X}, expected 0x{expected:08X}"
            )


def plan_edits(buf, manifest):
    """Return (inserts, rewrites): inserts add an effect entry, rewrites fix a magnitude."""
    spells = {editor_id(buf, node): node for node in find_records(parse(buf), b"SPEL")}
    inserts, rewrites = [], []
    for entry in manifest["spells"]:
        node = spells.get(entry["spellEditorId"])
        if node is None:
            raise PluginError(f"missing ability spell {entry['spellEditorId']}")
        mgef_id = form_key(entry["mgefFormId"])
        magnitude = float(entry["magnitude"])
        efid = node.field(buf, b"EFID")
        if efid is None:
            inserts.append((node, mgef_id, magnitude))
            continue
        (found,) = struct.unpack("<I", efid)
        if found != mgef_id:
            raise PluginError(
                f"{entry['spellEditorId']}: effect already points at 0x{found:08X}, expected 0x{mgef_id:08X}"
            )
        for sig, start, size in node.subrecords(buf):
            if sig == b"EFIT":
                (current,) = struct.unpack_from("<f", buf, start)
                if abs(current - magnitude) > 1e-6:
                    rewrites.append((start, magnitude))
                break
        else:
            raise PluginError(f"{entry['spellEditorId']}: EFID has no matching EFIT")
    return inserts, rewrites


def apply(plugin_path, manifest_path, dry_run=False):
    with open(plugin_path, "rb") as handle:
        original = bytearray(handle.read())
    if original[:4] != b"TES4":
        raise PluginError(f"{plugin_path} is not a plugin")
    manifest = load_manifest(manifest_path)
    check_magic_effects(original, manifest)
    inserts, rewrites = plan_edits(original, manifest)

    buf = bytearray(original)
    for start, magnitude in rewrites:
        struct.pack_into("<f", buf, start, magnitude)

    deltas = {}
    for node, mgef_id, magnitude in sorted(inserts, key=lambda item: item[0].data_end, reverse=True):
        entry = struct.pack("<4sHI4sHfII", b"EFID", EFID_SIZE, mgef_id, b"EFIT", EFIT_SIZE, magnitude, 0, 0)
        if len(entry) != EFFECT_BYTES:
            raise PluginError("internal: effect entry size drifted")
        buf[node.data_end:node.data_end] = entry
        (size,) = struct.unpack_from("<I", buf, node.offset + 4)
        struct.pack_into("<I", buf, node.offset + 4, size + EFFECT_BYTES)
        parent = node.parent
        while parent is not None:
            deltas[parent.offset] = deltas.get(parent.offset, 0) + EFFECT_BYTES
            parent = parent.parent

    for offset, delta in deltas.items():
        (size,) = struct.unpack_from("<I", buf, offset + 4)
        struct.pack_into("<I", buf, offset + 4, size + delta)

    check_magic_effects(bytes(buf), manifest)
    remaining, _ = plan_edits(bytes(buf), manifest)
    if remaining:
        raise PluginError(f"{len(remaining)} spell(s) still have no effect entry after patching")

    if not dry_run and (inserts or rewrites):
        directory = os.path.dirname(os.path.abspath(plugin_path))
        handle = tempfile.NamedTemporaryFile(dir=directory, delete=False, suffix=".tmp")
        try:
            handle.write(buf)
            handle.close()
            os.replace(handle.name, plugin_path)
        except BaseException:
            handle.close()
            os.unlink(handle.name)
            raise

    return {"added": len(inserts), "retuned": len(rewrites), "bytes": len(buf) - len(original)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--plugin", default=DEFAULT_PLUGIN)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--dry-run", action="store_true", help="report the change without writing")
    args = parser.parse_args(argv)
    try:
        result = apply(args.plugin, args.manifest, dry_run=args.dry_run)
    except (PluginError, OSError, KeyError) as exc:
        print(f"apply_spell_effects: {exc}", file=sys.stderr)
        return 1
    print(
        f"{os.path.basename(args.plugin)}: {result['added']} effect entr"
        f"{'y' if result['added'] == 1 else 'ies'} added, {result['retuned']} magnitude(s) retuned, "
        f"{result['bytes']} bytes written"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
