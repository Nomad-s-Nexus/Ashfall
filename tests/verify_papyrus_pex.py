"""Fails when a committed .pex does not belong to the committed .psc.

The Papyrus compiler ships inside FO4IDE and is Windows-only, while the build runner is Linux, so CI
cannot compile the scripts. What it can do is refuse to trust bytecode that predates its source:
`tools/stamp_papyrus.py` records the source and bytecode hashes right after a local compile, and this
check recomputes them. It also refuses bytecode that names a source other than its own, which is how
a stale or crossed build would show up.
"""

import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "deploy/Scripts"
STAMP = SCRIPTS / "papyrus-build.json"
SOURCE_ROOT = SCRIPTS / "Source/User"
SOURCE_TOKEN = re.compile(rb"[A-Za-z0-9_:\\./$-]{2,}\.psc")


def check(condition, message):
    if not condition:
        raise SystemExit(f"papyrus build: {message}")


def source_hash(path):
    # Constraint: normalise line endings - the checkout is CRLF on Windows and LF in CI.
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def recorded_sources(path):
    return [match.group().decode("ascii", "replace") for match in SOURCE_TOKEN.finditer(path.read_bytes())]


check(STAMP.is_file(), "deploy/Scripts/papyrus-build.json is missing; run tools/stamp_papyrus.py")
stamp = json.loads(STAMP.read_text(encoding="utf-8"))
entries = {entry["script"]: entry for entry in stamp["scripts"]}

sources = {path.stem: path for path in SOURCE_ROOT.glob("*.psc")}
check(set(entries) == set(sources),
      f"stamp lists {sorted(entries)} but the source tree has {sorted(sources)}")

for name, entry in sorted(entries.items()):
    source = ROOT / entry["source"]
    pex = ROOT / entry["pex"]
    check(source.is_file(), f"{entry['source']} is missing")
    check(pex.is_file(), f"{entry['pex']} is missing")
    check(source_hash(source) == entry["sourceSha256"],
          f"{source.name} changed after the last compile; recompile and re-stamp")
    check(hashlib.sha256(pex.read_bytes()).hexdigest() == entry["pexSha256"],
          f"{pex.name} changed without a re-stamp")
    recorded = recorded_sources(pex)
    check([Path(item).name for item in recorded] == [source.name],
          f"{pex.name} records {recorded}; it must name only {source.name}")
    check(recorded[0] == entry["recordedSource"],
          f"{pex.name} records {recorded[0]!r} but the stamp says {entry['recordedSource']!r}")

for pex in SCRIPTS.glob("*.pex"):
    check(pex.stem in entries, f"{pex.name} has no stamp entry")

print(f"Papyrus bytecode verified: {len(entries)} scripts stamped and matched")
