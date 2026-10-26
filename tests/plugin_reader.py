"""Read-only plugin reader for the survival contract tests.

CI has no FO4IDE, so the tests must be able to read RadSurvival.esp themselves. This module holds
only the parsing side: TES4/GRUP/record layout, subrecord lookup and EditorId/form-id helpers. It
never writes, so it cannot drift from what the shipped plugin actually contains.

Records are authored in FO4IDE now, including a spell's magic effect list (EFID/EFIT); nothing in
this module is an authoring path.
"""

import struct

TES4_HEADER = b"TES4"
GROUP = b"GRUP"


class PluginError(Exception):
    """Raised when the plugin does not look the way the reader expects."""


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
        """Return the first matching subrecord's bytes, or None."""
        for sig, start, size in self.subrecords(buf):
            if sig == signature:
                return bytes(buf[start:start + size])
        return None

    def form_id(self, buf):
        """Read this record's form id out of its header."""
        return struct.unpack_from("<I", buf, self.offset + 12)[0]


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
        if sig == GROUP:
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
    """Yield every node, groups first, then their children."""
    for node in nodes:
        yield node
        yield from walk(node.kids)


def find_records(nodes, signature):
    return [node for node in walk(nodes) if node.kind == "record" and node.sig == signature]


def records_by_form_id(buf, nodes, signature):
    """Map form id -> node for every record of one signature."""
    return {node.form_id(buf): node for node in find_records(nodes, signature)}


def editor_id(buf, node):
    raw = node.field(buf, b"EDID")
    if raw is None:
        return None
    return raw.rstrip(b"\x00").decode("ascii", "replace")


def vmad_properties(buf, node):
    """Decode a record's VMAD into {script name: {property name: value}}.

    FO4 VMAD v6: version/objectFormat/scriptCount, then per script name, flags and properties;
    an object property is unused/alias/form-id, and the alias is 0xFFFF when unbound. Scalar types
    decode to Python values. A type this reader does not model raises PluginError instead of
    letting the next property be read from the wrong offset.
    """
    raw = node.field(buf, b"VMAD") if node is not None else None
    if raw is None:
        return {}
    version, _object_format, script_count = struct.unpack_from("<HHH", raw, 0)
    if version != 6:
        raise PluginError(f"VMAD version {version} is not the FO4 layout this reader models")
    offset = 6
    scripts = {}
    for _ in range(script_count):
        (name_len,) = struct.unpack_from("<H", raw, offset)
        offset += 2
        script = raw[offset:offset + name_len].decode("ascii", "replace")
        offset += name_len + 1
        (property_count,) = struct.unpack_from("<H", raw, offset)
        offset += 2
        properties = {}
        for _ in range(property_count):
            (prop_len,) = struct.unpack_from("<H", raw, offset)
            offset += 2
            name = raw[offset:offset + prop_len].decode("ascii", "replace")
            offset += prop_len
            prop_type = raw[offset]
            offset += 2
            if prop_type == 1:
                _unused, alias, form_id = struct.unpack_from("<HHI", raw, offset)
                offset += 8
                properties[name] = form_id if alias == 0xFFFF else (form_id, alias)
            elif prop_type == 2:
                (value_len,) = struct.unpack_from("<H", raw, offset)
                offset += 2
                properties[name] = raw[offset:offset + value_len].decode("ascii", "replace")
                offset += value_len
            elif prop_type in (3, 4):
                properties[name] = struct.unpack_from("<i" if prop_type == 3 else "<f", raw, offset)[0]
                offset += 4
            elif prop_type == 5:
                properties[name] = bool(raw[offset])
                offset += 1
            else:
                raise PluginError(f"VMAD property {name} has unmodelled type {prop_type}")
        scripts[script] = properties
    return scripts


def form_key(value):
    """Accept a bare form id ("0x01000804") or an int; reject a master-qualified key."""
    if isinstance(value, int):
        return value
    if isinstance(value, str) and value.lower().startswith("0x") and ":" not in value:
        return int(value, 16)
    raise PluginError(f"expected a bare form id such as 0x01000804, got {value!r}")
