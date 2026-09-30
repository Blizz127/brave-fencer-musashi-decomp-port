#!/usr/bin/env python3
"""Fail if a registered source carries retail data instead of referring to it.

A decompiled source is supposed to contain only our code. Game data (string
literals, const tables, initialised variables) stays in the retail image and
is reached through an `extern` declaration at its retail address, so the
compiled object of a matching source should hold nothing but `.text`.

This compiles each source exactly the way the registry gate does (through
build_candidate.py, with the entry's optimisation and rodata base) and then
inspects the *object*, not the text of the source: a regex over C cannot tell
a string literal from an asm template or a comment, but the object's sections
cannot lie.

    python3 tools/check_no_retail_data.py                   # whole registry
    python3 tools/check_no_retail_data.py --region main_0012 --region sc03_0053 --jobs 2

Allowed: `.text`, empty data sections, `.bss` (no content), and a read-only
section that is entirely a switch jump table, i.e. every word of it is an
R_MIPS_32 relocation against `.text` (a table of text addresses, which gcc
generates from the `switch` itself). Anything else in `.data`, `.sdata`,
`.rodata` or `.rdata` is reported with the file, the symbols defined in the
section, and the section size, and the tool exits 1. A source that fails to
build is reported and makes the tool exit 2.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import verify_registry  # noqa: E402
from retail_common import RetailError, load_json  # noqa: E402

DATA_SECTION = re.compile(r"^\.(?:s?data|rodata|rdata)(?:\..*)?$")
READ_ONLY = re.compile(r"^\.(?:rodata|rdata)(?:\..*)?$")


@dataclass
class Finding:
    section: str
    size: int
    symbols: list[str] = field(default_factory=list)

    def describe(self) -> str:
        names = ", ".join(self.symbols) if self.symbols else "<unnamed>"
        return f"{self.section} 0x{self.size:X} bytes ({names})"


def _objdump() -> str:
    found = shutil.which("mips-linux-gnu-objdump")
    if found is None:
        raise RetailError("required binutil 'mips-linux-gnu-objdump' was not found on PATH")
    return found


def _run(argv: list[str]) -> str:
    completed = subprocess.run(argv, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RetailError(f"{argv[0]} failed: {completed.stderr.strip()}")
    return completed.stdout


def section_sizes(objdump_h: str) -> dict[str, int]:
    """Section name -> size from `objdump -h` output."""
    sizes: dict[str, int] = {}
    for m in re.finditer(r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s", objdump_h, re.M):
        sizes[m.group(1)] = int(m.group(2), 16)
    return sizes


def relocations(objdump_r: str) -> list[tuple[int, str, str]]:
    """(offset, type, value) triples from `objdump -r -j SECTION` output."""
    out = []
    for line in objdump_r.splitlines():
        m = re.match(r"^([0-9a-fA-F]{8})\s+(\S+)\s+(\S+)", line)
        if m:
            out.append((int(m.group(1), 16), m.group(2), m.group(3)))
    return out


def is_jump_table(size: int, relocs: list[tuple[int, str, str]], content: bytes | None = None) -> bool:
    """True if a section holds only switch jump tables.

    Every word must be an R_MIPS_32 reference into .text, except a zero word that
    is alignment padding between two tables: gcc emits each table after
    `.align 3`, so a table of odd length is followed by one zero word before the
    next table starts on an 8-byte boundary. Padding needs the section content
    to prove it is zero; without it, only relocated words are accepted.
    """
    if size == 0 or size % 4:
        return False
    text_words = {off for off, kind, value in relocs
                  if kind == "R_MIPS_32" and (value == ".text" or value.startswith(".text+")
                                               or value.startswith(".text-"))}
    for off in range(0, size, 4):
        if off in text_words:
            continue
        padding = (content is not None and off % 8 == 4 and off + 4 in text_words
                   and content[off:off + 4] == b"\0\0\0\0")
        if not padding:
            return False
    return bool(text_words)


def section_content(objdump_s: str) -> bytes:
    """Raw bytes of a section from `objdump -s -j SECTION` output."""
    out = bytearray()
    for line in objdump_s.splitlines():
        m = re.match(r"^ ([0-9a-f]{4,8}) ((?:[0-9a-f]{2,8} ?){1,4})", line)
        if m:
            out += bytes.fromhex(m.group(2).replace(" ", ""))
    return bytes(out)


def section_symbols(objdump_t: str, section: str) -> list[str]:
    names = []
    for line in objdump_t.splitlines():
        parts = line.split()
        if len(parts) >= 5 and section in parts and parts[-1] != section:
            names.append(parts[-1])
    return names


def inspect_object(obj: Path) -> list[Finding]:
    """Data a compiled object carries beyond switch jump tables."""
    objdump = _objdump()
    sizes = section_sizes(_run([objdump, "-h", str(obj)]))
    symtab = _run([objdump, "-t", str(obj)])
    findings = []
    for name, size in sizes.items():
        if not DATA_SECTION.match(name) or size == 0:
            continue
        if READ_ONLY.match(name):
            relocs = relocations(_run([objdump, "-r", "-j", name, str(obj)]))
            content = section_content(_run([objdump, "-s", "-j", name, str(obj)]))
            if is_jump_table(size, relocs, content):
                continue
        findings.append(Finding(name, size, section_symbols(symtab, name)))
    return findings


def check_build(build_argv: list[str]) -> list[Finding]:
    """Compile with build_candidate.py (keeping intermediates) and inspect unit.o."""
    with tempfile.TemporaryDirectory(prefix="check_no_retail_data.") as scratch:
        argv = list(build_argv)
        out_index = argv.index("--output") + 1
        argv[out_index] = str(Path(scratch) / "candidate.bin")
        completed = subprocess.run(
            [sys.executable, str(REPO / "tools/build_candidate.py"), *argv, "--keep-intermediates"],
            capture_output=True, text=True, cwd=REPO,
            env={**os.environ, "TMPDIR": scratch},
        )
        m = re.search(r"intermediates=(\S+)", completed.stdout + completed.stderr)
        if completed.returncode != 0 or not m:
            tail = (completed.stdout + completed.stderr).strip().splitlines()[-3:]
            raise RetailError("build failed: " + " | ".join(tail))
        return inspect_object(Path(m.group(1)) / "unit.o")


def check_source(source: Path, symbol: str, link_base: int, optimization: str = "-O2",
                 rodata_base: int | None = None, assembler: str | None = None) -> list[Finding]:
    argv = [str(source), "--symbol", symbol, "--link-base", f"0x{link_base:X}",
            "--output", "unused", f"--optimization={optimization}"]
    if rodata_base is not None:
        argv += ["--rodata-base", f"0x{rodata_base:X}"]
    if assembler is not None:
        argv += ["--assembler", assembler]
    return check_build(argv)


def check_entry(entry: dict, targets: dict) -> list[Finding]:
    build_argv, _ = verify_registry.plan(entry, targets, Path("unused"))
    return check_build(build_argv)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--registry", type=Path, default=REPO / "provenance/matches.json")
    parser.add_argument("--region", action="append", help="check only this region (repeatable)")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--json", action="store_true", help="print one JSON object per finding")
    args = parser.parse_args(argv)

    data = load_json(args.registry)
    targets = data.get("targets", {})
    entries = [e for e in data["matches"] if not args.region or e["region"] in args.region]

    def job(entry: dict):
        try:
            return entry, check_entry(entry, targets), None
        except RetailError as exc:
            return entry, None, str(exc)

    bad = errors = 0
    with ThreadPoolExecutor(max(1, args.jobs)) as pool:
        for entry, findings, error in pool.map(job, entries):
            where = f"{entry['region']} 0x{int(entry['vram']):08X} {entry['source']}"
            if error is not None:
                errors += 1
                print(f"ERROR {where}: {error}", flush=True)
            elif findings:
                bad += 1
                if args.json:
                    for f in findings:
                        print(json.dumps({"region": entry["region"], "vram": int(entry["vram"]),
                                          "source": entry["source"], "section": f.section,
                                          "size": f.size, "symbols": f.symbols}), flush=True)
                else:
                    print(f"DATA  {where}: " + "; ".join(f.describe() for f in findings), flush=True)
    print(f"checked {len(entries)} sources: {bad} carry retail data, {errors} failed to build")
    if errors:
        return 2
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
