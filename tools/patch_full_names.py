#!/usr/bin/env python3
"""Set record display names the FO4IDE writer cannot.

The native writer refuses Authoring\\FULL on every record type, so records it
creates (the cure ingestible, disease perks) have no Pip-Boy name. This rewrites
the plugin in place: for each EDID in the manifest it inserts or replaces that
record's FULL subrecord, then rebuilds every record dataSize and group size so
the file stays loadable. All other bytes are copied verbatim.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

RECORD_HEADER = 24
GROUP_HEADER = 24


def read_subrecords(data: bytes) -> list[tuple[bytes, bytes]]:
    subs: list[tuple[bytes, bytes]] = []
    offset = 0
    while offset + 6 <= len(data):
        sig = data[offset : offset + 4]
        size = struct.unpack_from("<H", data, offset + 4)[0]
        offset += 6
        if sig == b"XXXX":
            size = struct.unpack_from("<I", data, offset)[0]
            offset += 4
            continue
        subs.append((sig, data[offset : offset + size]))
        offset += size
    if offset != len(data):
        raise ValueError(f"subrecord walk ended at {offset} of {len(data)} bytes")
    return subs


def encode_subrecords(subs: list[tuple[bytes, bytes]]) -> bytes:
    out = bytearray()
    for sig, payload in subs:
        if len(payload) > 0xFFFF:
            out += b"XXXX" + struct.pack("<HI", 4, len(payload))
        out += sig + struct.pack("<H", len(payload)) + payload
    return bytes(out)


class Node:
    def __init__(self, kind: str, header: bytes, payload: bytes, children: list["Node"] | None = None):
        self.kind = kind
        self.header = header
        self.payload = payload
        self.children = children or []


def parse(data: bytes, start: int, end: int) -> list[Node]:
    nodes: list[Node] = []
    offset = start
    while offset < end:
        sig = data[offset : offset + 4]
        if sig == b"GRUP":
            size = struct.unpack_from("<I", data, offset + 4)[0]
            header = data[offset : offset + GROUP_HEADER]
            nodes.append(Node("group", header, b"", parse(data, offset + GROUP_HEADER, offset + size)))
            offset += size
        else:
            size = struct.unpack_from("<I", data, offset + 4)[0]
            header = data[offset : offset + RECORD_HEADER]
            nodes.append(Node("record", header, data[offset + RECORD_HEADER : offset + RECORD_HEADER + size]))
            offset += RECORD_HEADER + size
    if offset != end:
        raise ValueError(f"record walk ended at {offset} of {end} bytes")
    return nodes


def serialize(nodes: list[Node]) -> bytes:
    out = bytearray()
    for node in nodes:
        if node.kind == "group":
            body = serialize(node.children)
            header = bytearray(node.header)
            struct.pack_into("<I", header, 4, GROUP_HEADER + len(body))
            out += header + body
        else:
            payload = node.payload
            header = bytearray(node.header)
            struct.pack_into("<I", header, 4, len(payload))
            out += header + payload
    return bytes(out)


def rename(plugin: Path, names: dict[str, str]) -> list[str]:
    data = plugin.read_bytes()
    if data[:4] == b"TES4":
        first = RECORD_HEADER + struct.unpack_from("<I", data, 4)[0]
    else:  # pragma: no cover - every Fallout 4 plugin starts with TES4
        raise ValueError("plugin does not start with a TES4 header")
    nodes = parse(data, first, len(data))

    renamed: list[str] = []

    def visit(items: list[Node]) -> None:
        for node in items:
            if node.kind == "group":
                visit(node.children)
                continue
            subs = read_subrecords(node.payload)
            edid = next((payload for sig, payload in subs if sig == b"EDID"), None)
            if edid is None:
                continue
            editor_id = edid.rstrip(b"\x00").decode("ascii", "replace")
            wanted = names.get(editor_id)
            if wanted is None:
                continue
            encoded = wanted.encode("utf-8") + b"\x00"
            result: list[tuple[bytes, bytes]] = []
            placed = False
            for sig, payload in subs:
                if sig == b"FULL":
                    result.append((b"FULL", encoded))
                    placed = True
                else:
                    result.append((sig, payload))
                if sig == b"EDID" and not placed:
                    result.append((b"FULL", encoded))
                    placed = True
            node.payload = encode_subrecords(result)
            renamed.append(editor_id)

    visit(nodes)
    if renamed:
        plugin.write_bytes(data[:first] + serialize(nodes))
    return renamed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    names = manifest.get("names", {})
    renamed = rename(args.plugin, names)
    missing = sorted(set(names) - set(renamed))
    for editor_id in renamed:
        print(f"named {editor_id}: {names[editor_id]!r}")
    for editor_id in missing:
        print(f"missing record {editor_id} (not in {args.plugin.name})", file=sys.stderr)
    return 0 if not missing else 1


if __name__ == "__main__":
    raise SystemExit(main())
