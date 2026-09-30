#!/usr/bin/env python3
"""Enumerate the Psy-Q library entry points that the game code calls.

Inputs (all local; nothing retail is written out):
  - vendor/bfm-decomp/config/splat.us.exe.yaml   Psy-Q segment ranges (snd*/lib*/apicard*)
  - vendor/bfm-decomp/config/symbols.us*.txt     names
  - vendor/bfm-decomp/src/<lib>.c                Psy-Q object labels (XXX_NN_OBJ_off)
  - the local splat disassembly (gitignored, generated from the user's disc):
    asm/main.s (game code 0x80010000..0x8003A444) and asm/overlays/*/*.s.
    Call sites are every jal/j word into a Psy-Q segment. The tool fails
    clearly when the disassembly is absent.

Outputs (facts only: addresses, names, counts):
  pc_port/platform/psyq/bfm_psyq_calls.json
  pc_port/platform/psyq/bfm_psyq_compat.def    X-macro table (with tools/psyq_compat_status.py)
  docs/PSYQ-COMPAT.md                           generated coverage report

  tools/psyq_callsites.py [--vendor DIR] [--asm DIR] [--check]
--check regenerates in memory and fails if the committed outputs differ.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import psyq_compat_status as status_table  # noqa: E402

GAME_MAIN_LO, GAME_MAIN_HI = 0x80010000, 0x8003A444
OUT_JSON = ROOT / "pc_port/platform/psyq/bfm_psyq_calls.json"
OUT_DEF = ROOT / "pc_port/platform/psyq/bfm_psyq_compat.def"
OUT_MD = ROOT / "docs/PSYQ-COMPAT.md"


def psyq_segments(vendor: Path):
    segs = []
    pat = re.compile(r"\s+- \[0x[0-9A-Fa-f]+, *\w+, *((?:snd|lib|apicard)\w*)\]\s*#.*?vram (0x[0-9A-Fa-f]+)-(0x[0-9A-Fa-f]+)")
    for line in (vendor / "config/splat.us.exe.yaml").read_text().splitlines():
        m = pat.match(line)
        if m:
            segs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
    return segs


def symbol_names(vendor: Path):
    names = {}
    for f in ("symbols.us.txt", "symbols.us.ram.txt"):
        p = vendor / "config" / f
        if not p.is_file():
            continue
        for line in p.read_text().splitlines():
            m = re.match(r"\s*([A-Za-z_]\w*)\s*=\s*(0x[0-9A-Fa-f]+);\s*//\s*func", line)
            if m:
                names.setdefault(int(m.group(2), 16), m.group(1))
    return names


def object_labels(vendor: Path, segs):
    """addr -> (object, offset) from the lib*.c INCLUDE_ASM / definition order."""
    out = {}
    for name, lo, hi in segs:
        p = vendor / "src" / f"{name}.c"
        if not p.is_file():
            continue
        entries = re.findall(r'(?:INCLUDE_ASM\("[^"]+", *(\w+)\)|^\w[\w\s\*]*?\b(\w+)\s*\([^;]*\)\s*\{)',
                             p.read_text(), re.M)
        seq = [a or b for a, b in entries]
        # anchor object labels: NAME_OBJ_off with a known object base once one func address is known
        for i, lab in enumerate(seq):
            m = re.match(r"(func_)([0-9A-Fa-f]{8})$", lab)
            if not m:
                continue
            addr = int(m.group(2), 16)
            for later in seq[i + 1:i + 6]:
                mm = re.match(r"(\w+?)_OBJ_([0-9A-Fa-f]+)$", later)
                if mm:
                    out[addr] = (mm.group(1) + ".o", None)
                    break
    return out


def gte_shape_names(words_by_addr):
    """Name libgte setters from their exact coprocessor-2 instruction shapes."""
    out = {}
    for addr, words in words_by_addr.items():
        if 0x03E00008 in words:                       # up to the first jr $ra + delay slot
            words = words[:words.index(0x03E00008) + 2]
        ctc = [((w >> 16) & 31, (w >> 11) & 31) for w in words if (w >> 21) == 0x246]   # ctc2 rt, rd
        body = [w for w in words if w != 0]
        if not ctc:
            cfc_only = [(w >> 11) & 31 for w in words if (w >> 21) == 0x242]
            if sorted(cfc_only) == [24, 25]:
                out[addr] = "ReadGeomOffset"
            elif cfc_only == [26] and len([w for w in words if w]) <= 2:
                out[addr] = "ReadGeomScreen"
            continue
        rds = [rd for _, rd in ctc]
        cfc = [(w >> 11) & 31 for w in words if (w >> 21) == 0x242]   # cfc2
        # a pure register setter: only lw, ctc2 and the return (plus nops)
        pure = all(w == 0 or (w >> 26) == 0x23 or (w >> 21) == 0x246 or w == 0x03E00008 for w in words)
        if rds == [26] and len(body) <= 3:
            out[addr] = "SetGeomScreen"
        elif rds == [24, 25] and len(body) <= 6:
            out[addr] = "SetGeomOffset"
        elif sorted(rds) == [0, 1, 2, 3, 4] and pure:
            out[addr] = "SetRotMatrix"
        elif sorted(rds) == [5, 6, 7] and pure:
            out[addr] = "SetTransMatrix"
        elif sorted(rds) == [8, 9, 10, 11, 12] and pure:
            out[addr] = "SetLightMatrix"
        elif rds == [13, 14, 15] and sum(1 for w in words if (w >> 26) == 0 and (w & 0x3F) == 0 and w) == 3 \
                and all(w == 0 or (w >> 21) == 0x246 or w == 0x03E00008 or ((w >> 26) == 0 and (w & 0x3F) == 0)
                        for w in words):
            out[addr] = "SetBackColor"                        # sll 4 x3 + ctc2 13..15
        elif sorted(rds) == [16, 17, 18, 19, 20] and pure:
            out[addr] = "SetColorMatrix"
    return out


ASM_LINE = re.compile(r"/\*\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+\*/")


def asm_files(asm: Path):
    main = asm / "main.s"
    if not main.is_file():
        raise FileNotFoundError(
            "%s not found. The Psy-Q call-site scan reads the local splat disassembly "
            "(asm/main.s and asm/overlays/*/*.s), which is generated from your own disc "
            "and never committed. Run the repository's splat extraction first, or pass "
            "--asm PATH." % main)
    yield main, "main"
    for f in sorted((asm / "overlays").glob("*/*.s")):
        yield f, f.parent.name


def read_asm(path: Path, jumps_only: bool = False):
    """Yields (vram, word) for instruction lines of a splat .s file."""
    with path.open(errors="replace") as fh:
        for line in fh:
            if jumps_only and "*/  j" not in line:
                continue
            m = ASM_LINE.search(line)
            if m:
                yield int(m.group(1), 16), int.from_bytes(bytes.fromhex(m.group(2)), "little")


def scan(vendor: Path, asm: Path):
    segs = psyq_segments(vendor)
    names = symbol_names(vendor)

    def seg_of(a):
        for n, lo, hi in segs:
            if lo <= a < hi:
                return n
        return None

    calls = collections.Counter()
    binaries_per = collections.defaultdict(set)
    nfiles = 0
    for f, binary in asm_files(asm):
        nfiles += 1
        for vram, w in read_asm(f, jumps_only=True):
            if binary == "main" and not (GAME_MAIN_LO <= vram < GAME_MAIN_HI):
                continue
            if (w >> 26) in (2, 3):
                t = (vram & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                if seg_of(t):
                    calls[t] += 1
                    binaries_per[t].add(binary)

    # instruction words of each Psy-Q function (for shape naming), by glabel
    psyq_words = {}
    cur = None
    with (asm / "main.s").open(errors="replace") as fh:
        for line in fh:
            g = re.match(r"\s*glabel\s+(?:func_)?([0-9A-Fa-f]{8})\b", line)
            if g:
                cur = int(g.group(1), 16) if seg_of(int(g.group(1), 16)) else None
                if cur is not None:
                    psyq_words[cur] = []
                continue
            m = ASM_LINE.search(line)
            if m and cur is not None:
                psyq_words[cur].append(int.from_bytes(bytes.fromhex(m.group(2)), "little"))
    shapes = gte_shape_names(psyq_words)
    objects = object_labels(vendor, segs)

    entries = []
    for addr in sorted(calls):
        if addr in names and not names[addr].startswith("func_"):
            name, src = names[addr], "symbol"
        elif addr in shapes:
            name, src = shapes[addr], "shape"
        elif addr in status_table.INFERRED:
            name, src = status_table.INFERRED[addr][0], "inferred"
        else:
            name, src = "func_%08X" % addr, "unnamed"
        entries.append({
            "addr": "0x%08X" % addr,
            "name": name,
            "name_source": src,
            "evidence": status_table.INFERRED[addr][1] if src == "inferred" else None,
            "segment": seg_of(addr),
            "object": objects.get(addr, (None,))[0],
            "callsites": calls[addr],
            "binaries": len(binaries_per[addr]),
        })
    meta = {"asm_files": nfiles, "callsites": sum(calls.values()),
            "psyq_range": ["0x%08X" % min(s[1] for s in segs), "0x%08X" % max(s[2] for s in segs)]}
    return {"meta": meta, "entries": entries}


def render_def(data):
    lines = ["/* GENERATED by tools/psyq_callsites.py from vendor/bfm-decomp config + this repo's game code,",
             " * with statuses from tools/psyq_compat_status.py. Do not edit by hand.",
             " *",
             " * BFM_PSYQ(pc, name, status, argc, returns, wrapper)",
             " *   status  BFM_PSYQ_PSYCROSS | BFM_PSYQ_HLE | BFM_PSYQ_STUB (stub-TBD: the lane",
             " *           refuses and the interpreter runs the retail code from the disc)",
             " *   wrapper int wrapper(uint32_t *r) over the o32 register file; bfm_psyq_stub if none */", ""]
    for e in data["entries"]:
        st = status_table.lookup(e["name"], e["segment"])
        lines.append("BFM_PSYQ(%s, %s, BFM_PSYQ_%s, %d, %d, %s)" % (
            e["addr"], e["name"], st.status.upper().replace("-TBD", ""), st.argc, st.returns,
            st.wrapper or "bfm_psyq_stub"))
    return "\n".join(lines) + "\n"


def render_md(data):
    rows = []
    tally = collections.Counter()
    sites = collections.Counter()
    for e in data["entries"]:
        st = status_table.lookup(e["name"], e["segment"])
        tally[st.status] += 1
        sites[st.status] += e["callsites"]
        rows.append("| `%s` | %s | %s | %s | %d | %d | %s | %s |" % (
            e["addr"], e["name"] if e["name_source"] != "unnamed" else "*unnamed*",
            e["segment"], e["object"] or "", e["callsites"], e["binaries"], st.status, st.note))
    total, total_sites = len(data["entries"]), data["meta"]["callsites"]
    def pct(n, d):
        return "%.1f%%" % (100.0 * n / d) if d else "-"
    named = sum(1 for e in data["entries"] if e["name_source"] != "unnamed")
    inferred = sum(1 for e in data["entries"] if e["name_source"] == "inferred")
    out = ["# Psy-Q → PsyCross/HLE compatibility table (generated)", "",
           "Generated by `tools/psyq_callsites.py` (see `docs/ARCHITECTURE-PORT.md`,",
           "\"Psy-Q compatibility layer\"). Do not edit; edit `tools/psyq_compat_status.py`",
           "and regenerate.", "",
           "Game code scanned: the local splat disassembly, %d files (`asm/main.s` 0x80010000–0x8003A444"
           % data["meta"]["asm_files"],
           "and every `asm/overlays/*/*.s`); %d `jal`/`j` call sites into the Psy-Q range %s–%s." % (
               total_sites, data["meta"]["psyq_range"][0], data["meta"]["psyq_range"][1]), "",
           "| Status | Entry points | Call sites |", "|---|---|---|"]
    for s in ("psycross", "hle", "stub-TBD"):
        out.append("| %s | %d (%s) | %d (%s) |" % (s, tally[s], pct(tally[s], total), sites[s], pct(sites[s], total_sites)))
    covered = tally["psycross"] + tally["hle"]
    covered_sites = sites["psycross"] + sites["hle"]
    out += ["| **covered (psycross + hle)** | **%d / %d (%s)** | **%d / %d (%s)** |" % (
        covered, total, pct(covered, total), covered_sites, total_sites, pct(covered_sites, total_sites)),
            "", "Named: %d / %d entry points (vendor symbols, libgte instruction shape, or %d inferred"
            % (named, total, inferred),
            "from call context: see `INFERRED` in tools/psyq_compat_status.py). The rest are listed",
            "by address and Psy-Q object for later naming.", "",
            "| Address | Name | Segment | Object | Call sites | Binaries | Status | Notes |",
            "|---|---|---|---|---|---|---|---|"] + rows
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--vendor", type=Path,
                    default=ROOT.parent / "brave-fencer-musashi-decomp/vendor/bfm-decomp")
    ap.add_argument("--asm", type=Path, default=None,
                    help="splat disassembly dir (default: ./asm, else the main checkout's asm/)")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    if not (args.vendor / "config/splat.us.exe.yaml").is_file():
        print("psyq_callsites: vendor tree not found at %s" % args.vendor, file=sys.stderr)
        return 2
    asm = args.asm
    if asm is None:
        asm = ROOT / "asm"
        if not (asm / "main.s").is_file():
            asm = ROOT.parent / "brave-fencer-musashi-decomp/asm"
    try:
        data = scan(args.vendor, asm)
    except FileNotFoundError as e:
        print("psyq_callsites: %s" % e, file=sys.stderr)
        return 2
    outputs = {OUT_JSON: json.dumps(data, indent=1) + "\n", OUT_DEF: render_def(data), OUT_MD: render_md(data)}
    if args.check:
        bad = [str(p.relative_to(ROOT)) for p, text in outputs.items()
               if not p.is_file() or p.read_text() != text]
        if bad:
            print("psyq_callsites: stale: " + ", ".join(bad), file=sys.stderr)
            return 1
        return 0
    for p, text in outputs.items():
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    print("psyq_callsites: %d entry points, %d call sites" % (len(data["entries"]), data["meta"]["callsites"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
