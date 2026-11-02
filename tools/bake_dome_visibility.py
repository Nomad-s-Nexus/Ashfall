#!/usr/bin/env python3
"""Bake the per-shape visibility windows of the storm dome mesh into src/RadDomeVisibilityTable.h.

The mesh CloudArena01_4.nif carries one NiControllerSequence ("Storm", 0..360 s) whose 36
BSEffectShaderPropertyFloatController alpha blocks (controller id "5") are the only source of
shape opacity. A shape whose alpha is <= THRESHOLD for a stretch of the cycle can be app-culled
for that stretch. The windows are derived from the real linear float keys, padded and wrapped.

Usage:
    python tools/bake_dome_visibility.py            print the generated header to stdout
    python tools/bake_dome_visibility.py --write    rewrite src/RadDomeVisibilityTable.h
    python tools/bake_dome_visibility.py --check    exit 1 when the committed header differs from a fresh bake
"""
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NIF = ROOT / "deploy" / "Meshes" / "KORsMeshes" / "Anomaly" / "CloudArena01_4.nif"
HEADER = ROOT / "src" / "RadDomeVisibilityTable.h"

THRESHOLD = 0.01
PAD_SEC = 2.0
CYCLE_SEC = 360.0
ALPHA_CONTROLLER_TYPE = "BSEffectShaderPropertyFloatController"
ALPHA_CONTROLLER_ID = "5"


class Nif:
    def __init__(self, data):
        self.b = data
        b = data
        nl = b.index(b"\n")
        o = nl + 1
        o += 4  # file version
        o += 1  # endian
        o += 4  # user version
        (self.num_blocks,) = struct.unpack_from("<I", b, o)
        o += 4
        o += 4  # BS stream version
        for _ in range(4):
            o += 1 + b[o]
        (nt,) = struct.unpack_from("<H", b, o)
        o += 2
        types = []
        for _ in range(nt):
            (n,) = struct.unpack_from("<I", b, o)
            o += 4
            types.append(b[o:o + n].decode())
            o += n
        nb = self.num_blocks
        idx = struct.unpack_from("<%dH" % nb, b, o)
        o += 2 * nb
        sizes = struct.unpack_from("<%dI" % nb, b, o)
        o += 4 * nb
        ns, _maxlen = struct.unpack_from("<II", b, o)
        o += 8
        strs = []
        for _ in range(ns):
            (n,) = struct.unpack_from("<I", b, o)
            o += 4
            strs.append(b[o:o + n].decode())
            o += n
        (ng,) = struct.unpack_from("<I", b, o)
        o += 4 + 4 * ng
        self.strings = strs
        self.blocks = []
        for i in range(nb):
            self.blocks.append((types[idx[i]], o, sizes[i]))
            o += sizes[i]
        (num_roots,) = struct.unpack_from("<I", b, o)
        if o + 4 + 4 * num_roots != len(b):
            raise SystemExit("NIF block sizes plus footer do not cover the file (%d != %d)" % (o + 4 + 4 * num_roots, len(b)))

    def u32(self, o):
        return struct.unpack_from("<I", self.b, o)[0]

    def i32(self, o):
        return struct.unpack_from("<i", self.b, o)[0]

    def string(self, i):
        return "" if i == 0xFFFFFFFF else self.strings[i]

    def shape_names(self):
        return sorted(self.string(self.u32(off)) for t, off, _ in self.blocks if t == "BSTriShape")

    def sequence_blocks(self):
        for t, off, _ in self.blocks:
            if t == "NiControllerSequence":
                o = off
                count = self.u32(o + 4)
                o += 12
                entries = []
                for _ in range(count):
                    ip, ct = struct.unpack_from("<2i", self.b, o)
                    ids = struct.unpack_from("<5I", self.b, o + 9)
                    entries.append((ip, ct, [self.string(x) for x in ids]))
                    o += 29
                yield entries

    def float_keys(self, interp_block):
        t, off, _ = self.blocks[interp_block]
        if t != "NiFloatInterpolator":
            raise SystemExit("alpha interpolator %d is %s, expected NiFloatInterpolator" % (interp_block, t))
        data = self.i32(off + 4)
        t2, off2, _ = self.blocks[data]
        if t2 != "NiFloatData":
            raise SystemExit("alpha data %d is %s, expected NiFloatData" % (data, t2))
        n = self.u32(off2)
        interp_type = self.u32(off2 + 4) if n else 0
        if interp_type != 1:
            raise SystemExit("alpha keys of block %d are not linear" % data)
        keys = []
        o = off2 + 8
        for _ in range(n):
            keys.append(struct.unpack_from("<ff", self.b, o))
            o += 8
        return keys


def alpha_keys(nif):
    result = {}
    for entries in nif.sequence_blocks():
        for ip, _ct, ids in entries:
            if ids[2] == ALPHA_CONTROLLER_TYPE and ids[3] == ALPHA_CONTROLLER_ID:
                result[ids[0]] = nif.float_keys(ip)
    return result


def above_intervals(keys, th):
    out = []
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if v0 <= th and v1 <= th:
            continue
        a, b = t0, t1
        if v0 <= th:
            a = t0 + (th - v0) / (v1 - v0) * (t1 - t0)
        if v1 <= th:
            b = t0 + (th - v0) / (v1 - v0) * (t1 - t0)
        out.append((a, b))
    return out


def merge(intervals):
    intervals = sorted(intervals)
    merged = []
    for a, b in intervals:
        if merged and a <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], b))
        else:
            merged.append((a, b))
    return merged


def windows_for(keys):
    padded = []
    for a, b in merge(above_intervals(keys, THRESHOLD)):
        lo, hi = a - PAD_SEC, b + PAD_SEC
        if lo < 0.0:
            padded.append((CYCLE_SEC + lo, CYCLE_SEC))
            lo = 0.0
        if hi > CYCLE_SEC:
            padded.append((0.0, hi - CYCLE_SEC))
            hi = CYCLE_SEC
        padded.append((lo, hi))
    out = []
    for a, b in merge(padded):
        out.append((math.floor(a * 100.0) / 100.0, math.ceil(b * 100.0) / 100.0))
    return out


def bake(nif_path=NIF):
    nif = Nif(Path(nif_path).read_bytes())
    shapes = nif.shape_names()
    keys = alpha_keys(nif)
    if sorted(keys) != shapes:
        raise SystemExit("alpha controller names differ from shape names")
    return shapes, {n: windows_for(keys[n]) for n in shapes}


def fmt(x):
    s = ("%.2f" % x).rstrip("0").rstrip(".")
    return (s if "." in s else s + ".0") + "f"


def render(shapes, wins):
    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("// Invariant: generated by tools/bake_dome_visibility.py from CloudArena01_4.nif; do not edit by hand.")
    lines.append("namespace RadDomeVisibility")
    lines.append("{")
    lines.append("    struct Window")
    lines.append("    {")
    lines.append("        float start;")
    lines.append("        float end;")
    lines.append("    };")
    lines.append("")
    lines.append("    constexpr int kShapeCount = %d;" % len(shapes))
    lines.append("    constexpr float kCycleSec = %s;" % fmt(CYCLE_SEC))
    lines.append("")
    lines.append("    constexpr const char* kShapeNames[kShapeCount] = {")
    for i in range(0, len(shapes), 6):
        lines.append("        " + " ".join('"%s",' % n for n in shapes[i:i + 6]))
    lines.append("    };")
    lines.append("")
    offsets = [0]
    flat = []
    for n in shapes:
        flat.extend(wins[n])
        offsets.append(len(flat))
    lines.append("    constexpr int kWindowBegin[kShapeCount + 1] = {")
    for i in range(0, len(offsets), 12):
        lines.append("        " + " ".join("%d," % x for x in offsets[i:i + 12]))
    lines.append("    };")
    lines.append("")
    lines.append("    constexpr Window kWindows[] = {")
    for n in shapes:
        if not wins[n]:
            continue
        for i in range(0, len(wins[n]), 4):
            lines.append("        " + " ".join("{ %s, %s }," % (fmt(a), fmt(b)) for a, b in wins[n][i:i + 4]))
    lines.append("    };")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main():
    shapes, wins = bake()
    text = render(shapes, wins)
    if "--check" in sys.argv:
        current = HEADER.read_text(encoding="utf-8").replace("\r\n", "\n")
        if current != text:
            print("RadDomeVisibilityTable.h is stale; run tools/bake_dome_visibility.py --write")
            sys.exit(1)
        print("RadDomeVisibilityTable.h matches the NIF")
    elif "--write" in sys.argv:
        HEADER.write_text(text, encoding="utf-8", newline="\n")
        print("wrote", HEADER)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()