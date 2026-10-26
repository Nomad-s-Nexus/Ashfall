"""Writes the Papyrus build stamp after a local compile.

Run this only after `compile_papyrus` has produced fresh `.pex` files, then commit both the bytecode
and `deploy/Scripts/papyrus-build.json` in the same change. CI cannot compile Papyrus (the engine
ships inside FO4IDE and is Windows-only; the build runner is Linux), so this stamp is how CI can
still fail a source edit that never reached the bytecode.
"""

import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "deploy/Scripts"
SOURCE_ROOT = SCRIPTS / "Source/User"
STAMP = SCRIPTS / "papyrus-build.json"
SOURCE_TOKEN = re.compile(rb"[A-Za-z0-9_:\\./$-]{2,}\.psc")


def source_hash(path):
    # Constraint: normalise line endings - the checkout is CRLF on Windows and LF in CI.
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def recorded_sources(path):
    return [match.group().decode("ascii", "replace") for match in SOURCE_TOKEN.finditer(path.read_bytes())]


def main():
    entries = []
    for source in sorted(SOURCE_ROOT.glob("*.psc")):
        pex = SCRIPTS / (source.stem + ".pex")
        if not pex.is_file():
            raise SystemExit(f"missing bytecode for {source.name}; compile before stamping")
        recorded = recorded_sources(pex)
        if [Path(name).name for name in recorded] != [source.name]:
            raise SystemExit(f"{pex.name} records {recorded}; expected only {source.name}")
        entries.append({
            "script": source.stem,
            "source": source.relative_to(ROOT).as_posix(),
            "sourceSha256": source_hash(source),
            "pex": pex.relative_to(ROOT).as_posix(),
            "pexSha256": hashlib.sha256(pex.read_bytes()).hexdigest(),
            "recordedSource": recorded[0],
        })

    stamp = {
        "compiler": "FO4IDE built-in Papyrus engine (compile_papyrus, engine=builtin)",
        "command": (
            "compile_papyrus source=<repo>/Rad-Overhaul-Fallout-4/deploy/Scripts/Source/User/<Script>.psc "
            "output=<repo>/Rad-Overhaul-Fallout-4/deploy/Scripts imports=<vanilla script sources> "
            "flags=Institute_Papyrus_Flags.flg engine=builtin"
        ),
        "sourceRoot": SOURCE_ROOT.relative_to(ROOT).as_posix(),
        "scripts": entries,
    }
    STAMP.write_text(json.dumps(stamp, indent=2) + "\n", encoding="utf-8")
    print(f"Stamped {len(entries)} scripts into {STAMP.relative_to(ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
