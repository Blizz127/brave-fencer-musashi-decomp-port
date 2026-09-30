#!/usr/bin/env python3
"""How much of the guest code a port run executes is byte-matched C.

Reads a headless run's executed-path trace and classifies every executed
guest function by what the default native-lane build runs for it:

  matched_c      the native lane runs byte-matched decomp C (a registry
                 entry with recovery "c", admitted by tools/native_lane_gen.py)
  port_shim      hand-written port code runs instead of the retail
                 instructions: an active PsyQ compat entry (HLE or PsyCross
                 wrapper), a BIOS call (host HLE), or a guest function the
                 host skips (called, but its first instruction never steps)
  interpreted    the interpreter runs the retail instructions from the
                 user's disc, split by whether matched C exists for it

Inputs (all produced locally; nothing here is retail data):
  --trace-counts  <run>/trace.counts (MUSASHI_TRACE_FUNCS)
  --trace-insns   <run>/trace.insns  (optional, MUSASHI_TRACE_INSNS: steps per
                  PC; tools/patches/trace-insns.patch). Without it the
                  instruction share is an estimate: calls x registry size/4.
  --lane-report   <build>/generated/native_lane/report.json
  provenance/matches.json and pc_port/platform/psyq/bfm_psyq_compat.def

Run the trace with MUSASHI_NATIVE_LANE=0 in a lane-ON build, which gives
complete counts: every call and every instruction goes through the
interpreter, which records them. The classes then say what the lane-ON
default would run for each one. A lane-ON trace misses native-to-native
calls (direct C calls are not traced), so --lane-run on only marks the
result partial.

Output: JSON (--json PATH, or stdout) and a one-line summary on stderr.
"""
import argparse
import bisect
import collections
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEVICE = {"VSync", "DrawSync", "DrawOTag", "LoadImage", "StoreImage", "MoveImage",
          "PutDrawEnv", "PutDispEnv", "SetDispMask", "CdSearchFile", "putchar"}
BIOS = {0xA0, 0xB0, 0xC0}


def is_bios(pc):
    """BIOS call vectors (A0/B0/C0, any segment) and kernel RAM below the EXE."""
    return (pc & 0x1FFFFFFF) in BIOS or (pc & 0x1FFFFFFF) < 0x10000


def load_counts(path):
    rows = []
    for line in Path(path).read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        f = line.split()
        rows.append({"pc": int(f[0], 16), "calls": int(f[1]), "kind": f[2],
                     "caller": int(f[3], 16), "lane": f[4] if len(f) > 4 else None})
    return rows


def load_insns(path):
    steps = {}
    for line in Path(path).read_text().splitlines():
        if line and not line.startswith("#"):
            pc, n = line.split()
            steps[int(pc, 16)] = int(n)
    return steps


def load_psyq(path):
    out = {}
    pat = re.compile(r"^BFM_PSYQ\((0x[0-9A-Fa-f]+),\s*(\w+),\s*BFM_PSYQ_(\w+),", re.M)
    for m in pat.finditer(Path(path).read_text()):
        out[int(m.group(1), 16)] = {"name": m.group(2), "status": m.group(3)}
    return out


def load_formatter_ranges(path):
    """Code ranges the interpreter admits (address pairs in the formatter's
    range table), used as function extents where the registry has none."""
    pat = re.compile(r"^\s*\{ 0x(8[0-9a-fA-F]{7})u, 0x(8[0-9a-fA-F]{7})u, k\w+Words \}", re.M)
    return sorted((int(m.group(1), 16), int(m.group(2), 16))
                  for m in pat.finditer(Path(path).read_text(errors="replace")))


SUBSEG_RE = re.compile(r"^\s*-\s*\[\s*(0x[0-9A-Fa-f]+)\s*(?:,\s*(\w+)\s*(?:,\s*([\w.]+))?)?\s*\]", re.M)
SYM_FUNC_RE = re.compile(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;.*type:func", re.M)


def load_members(config_dir):
    """Code map of each overlay member from its splat config (local analysis
    input): {region: (code ranges [(lo, hi)], sorted function starts)}.
    Code = asm/c/hasm subsegments; starts come from symbol_addrs.<member>
    (.auto).txt. Members without a config are absent (registry fallback)."""
    members = {}
    d = Path(config_dir)
    for y in sorted(d.glob("overlay_*.yaml")):
        text = y.read_text(errors="replace")
        seg = re.search(r"-\s*name:\s*(\w+)\s*\n\s*type:\s*code\s*\n\s*start:\s*(0x[0-9A-Fa-f]+)"
                        r"\s*\n\s*vram:\s*(0x[0-9A-Fa-f]+)", text)
        if not seg:
            continue
        name, start, vram = seg.group(1), int(seg.group(2), 16), int(seg.group(3), 16)
        subs = [(int(m.group(1), 16), m.group(2)) for m in SUBSEG_RE.finditer(text)]
        ranges = []
        for (off, kind), nxt in zip(subs, subs[1:]):
            if kind in ("asm", "c", "hasm"):
                ranges.append((vram + off - start, vram + nxt[0] - start))
        starts = set()
        for f in (d / f"symbol_addrs.{name}.auto.txt", d / f"symbol_addrs.{name}.txt"):
            if f.exists():
                starts |= {int(m.group(2), 16) for m in SYM_FUNC_RE.finditer(f.read_text(errors="replace"))}
        if ranges:
            members[name] = (ranges, sorted(starts))
            tp = re.search(r"target_path:\s*(\S+)", text)
            if tp:
                MEMBER_FILES[name] = (tp.group(1), vram, start)
    return members


# Sony SDK code linked into overlay members (not game code). The PsyQ
# manifest covers the main EXE only. Port rule: platform code, proven
# faithful by differential; never decompiled PsyQ.
SDK_OVERLAY_RANGES = [
    # libpress DecDCTvlcSize / DecDCTvlc: handwritten SDK assembly (trapping
    # add/addi, bgez $zero), the intro movie player's VLC decoder.
    ("main_0003", 0x800D3204, 0x800D3574, "libpress"),
]


def sdk_overlay(region, pc):
    for r, lo, hi, lib in SDK_OVERLAY_RANGES:
        if r == region and lo <= pc < hi:
            return lib
    return None


MEMBER_FILES = {}  # region -> (target_path relative to the repo root, vram, start)


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def resolve_resident(path, members, root):
    """MUSASHI_TRACE_RESIDENT rows (target, digest of 32 bytes, count) ->
    {target: Counter(member -> entries)}, by hashing each candidate member's
    own bytes at the target (local extracted files). Exact, not inferred;
    several members matching means their bytes there are identical."""
    cache, out, unmatched = {}, collections.defaultdict(collections.Counter), 0
    for line in Path(path).read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        t, d, n = line.split()
        t, d, n = int(t, 16), int(d, 16), int(n)
        hit = []
        for region, (ranges, _) in members.items():
            if not any(lo <= t < hi for lo, hi in ranges) or region not in MEMBER_FILES:
                continue
            rel, vram, start = MEMBER_FILES[region]
            f = Path(root) / rel
            if f not in cache:
                cache[f] = f.read_bytes() if f.exists() else None
            data = cache[f]
            off = t - vram + start
            if data is not None and 0 <= off and off + 32 <= len(data) and fnv1a64(data[off:off + 32]) == d:
                hit.append(region)
        if hit:
            for region in hit:
                out[t][region] += n
        else:
            unmatched += n
    return out, unmatched


class Registry:
    """Function lookup by guest PC. Main is unambiguous. Overlay members
    (MAIN.CD members main_NNNN, scene overlays scNN_NNNN) share load
    addresses, and a trace records PCs, not which member was resident. So
    each overlay PC resolves to the member function containing it, choosing:
    first a member whose containing function's entry was traced, then the
    member with the most traced entries overall. This is an inference;
    "overlay_attribution" in the output says so and lists the members used.
    Registry gaps inside the main EXE are filled from the formatter's code
    ranges (reported as unregistered); overlay addresses never are."""

    def __init__(self, matches, traced_targets, ranges=(), members=None, resident=None):
        self.resident = resident or {}
        main = [m for m in matches if m["region"] == "main"]
        lo_main = min(m["vram"] for m in main) if main else 0
        hi_main = max(m["vram"] + max(m.get("size") or 4, 4) for m in main) if main else 0
        spans = sorted((m["vram"], m["vram"] + max(m.get("size") or 4, 4)) for m in main)
        for lo, hi in ranges:  # fill main-EXE registry gaps only
            if lo < lo_main or hi > hi_main:
                continue
            i = bisect.bisect_left(spans, (lo, 0))
            if (i < len(spans) and spans[i][0] < hi) or (i and spans[i - 1][1] > lo):
                continue
            spans.insert(i, (lo, hi))
            main.append({"name": None, "vram": lo, "size": hi - lo, "region": "main",
                         "recovery": None, "unregistered": True})
        self.main = sorted(main, key=lambda m: m["vram"])
        self.main_starts = [m["vram"] for m in self.main]
        self.main_span = (lo_main, hi_main)
        self.traced = traced_targets
        by_region = collections.defaultdict(list)
        for m in matches:
            if m["region"] != "main":
                by_region[m["region"]].append(m)
        # Each member's functions: from its code map when a config exists
        # (every start, registered or not, extent to the next start or the
        # end of its code range), else its registry entries.
        funcs_by = {}
        for region in set(by_region) | set(members or {}):
            reg_at = {m["vram"]: m for m in by_region.get(region, [])}
            if members and region in members:
                ranges_m, starts = members[region]
                starts = sorted(set(starts) | {v for v in reg_at
                                               if any(lo <= v < hi for lo, hi in ranges_m)})
                fl = []
                for lo, hi in ranges_m:
                    ss = [x for x in starts if lo <= x < hi]
                    if not ss or ss[0] != lo:
                        ss = [lo] + ss
                    for a, b in zip(ss, ss[1:] + [hi]):
                        m = reg_at.get(a)
                        fl.append(dict(m, size=b - a) if m else
                                  {"name": "func_%08X" % a, "vram": a, "size": b - a, "region": region,
                                   "recovery": None, "unregistered": True})
                funcs_by[region] = fl
            else:
                funcs_by[region] = list(by_region[region])
        self.hits = {}
        for region, ms in funcs_by.items():
            n = sum(1 for m in ms if m["vram"] in traced_targets)
            if n:
                self.hits[region] = n
        self.regions = {}
        self.ambiguous = collections.Counter()
        for region in sorted(self.hits):
            ms = sorted(funcs_by[region], key=lambda m: m["vram"])
            self.regions[region] = (ms, [m["vram"] for m in ms])
        self.used = collections.Counter()
        self.last_by_hits = False
        self.last_exact = False

    def find(self, pc):
        i = bisect.bisect_right(self.main_starts, pc) - 1
        if i >= 0 and pc < self.main[i]["vram"] + max(self.main[i].get("size") or 4, 4):
            return self.main[i]
        if self.main_span[0] <= pc < self.main_span[1]:
            return None
        best, score, entered = None, None, 0
        for region, (ms, starts) in self.regions.items():
            k = bisect.bisect_right(starts, pc) - 1
            if k < 0 or pc >= ms[k]["vram"] + max(ms[k].get("size") or 4, 4):
                continue
            ev = self.resident.get(ms[k]["vram"])
            sc = ((ev or {}).get(region, 0) if ev else -1,  # exact residency evidence first
                  ms[k]["vram"] in self.traced, self.hits[region])
            entered += sc[1]
            if score is None or sc > score:
                best, score, tie = ms[k], sc, False
            elif sc == score:
                tie = True  # first member by name keeps it; counted below
        if best is not None and tie:
            self.ambiguous[best["region"]] += 1
        ev = self.resident.get(best["vram"]) if best is not None else None
        self.last_exact = bool(ev and ev.get(best["region"]))
        self.last_by_hits = not self.last_exact and entered > 1
        return best


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--trace-counts", required=True)
    ap.add_argument("--trace-insns")
    ap.add_argument("--lane-report", required=True)
    ap.add_argument("--lane-run", choices=("off", "on"), default="off",
                    help="whether MUSASHI_NATIVE_LANE was on during the traced run")
    ap.add_argument("--registry", default=str(ROOT / "provenance/matches.json"))
    ap.add_argument("--psyq-def", default=str(ROOT / "pc_port/platform/psyq/bfm_psyq_compat.def"))
    ap.add_argument("--psyq-lane", choices=("pure", "all", "off"), default="pure",
                    help="MUSASHI_PSYQ_LANE of the configuration being classified")
    ap.add_argument("--formatter", default=str(ROOT / "pc_port/mips_formatter.c"),
                    help="source of the interpreter's code ranges (extents for registry gaps)")
    ap.add_argument("--config-dir", default=str(ROOT / "config"),
                    help="splat overlay configs + symbol_addrs (.auto) files for member code maps "
                         "(the .auto files are local, ignored analysis inputs)")
    ap.add_argument("--trace-resident", help="<run>/trace.resident (MUSASHI_TRACE_RESIDENT, "
                    "tools/patches/trace-resident.patch): exact resident member per overlay entry")
    ap.add_argument("--extracted-root", default=str(ROOT),
                    help="checkout holding extracted/overlays (the members' own bytes, local)")
    ap.add_argument("--idle", default="800424e4",
                    help="comma-separated function PCs that are idle waits (VSync polling); "
                         "shares are also given without them")
    ap.add_argument("--label", default="headless boot to the title")
    ap.add_argument("--json")
    a = ap.parse_args(argv)

    counts = load_counts(a.trace_counts)
    steps = load_insns(a.trace_insns) if a.trace_insns else None
    report = json.loads(Path(a.lane_report).read_text())
    matches = json.loads(Path(a.registry).read_text())["matches"]
    psyq = load_psyq(a.psyq_def)
    hooked_psyq = {int(x, 16) for x in report.get("psyq_hooked", [])}
    lane_fn = {f["vram"]: f for f in report["functions"]}
    admitted = {pc for pc, f in lane_fn.items() if f.get("admitted")}

    def psyq_active(pc):
        p = psyq.get(pc)
        if not p or p["status"] == "STUB" or pc in hooked_psyq or a.psyq_lane == "off":
            return False
        return a.psyq_lane == "all" or p["name"] not in DEVICE

    try:
        sys.path.insert(0, str(ROOT / "tools"))
        import psyq_manifest
        psyq_ranges = psyq_manifest.ranges()
    except Exception:  # the split is informative only
        psyq_manifest, psyq_ranges = None, None
    traced = {r["pc"]: r for r in counts}
    fr = Path(a.formatter)
    members = load_members(a.config_dir) if Path(a.config_dir).is_dir() else {}
    resident, resident_unmatched = ({}, 0)
    if a.trace_resident:
        resident, resident_unmatched = resolve_resident(a.trace_resident, members, a.extracted_root)
    reg = Registry(matches, set(traced), load_formatter_ranges(fr) if fr.exists() else (), members,
                   resident)

    # Functions: every traced entry, plus any registry function that stepped
    # without a traced entry (reached by a jump or fall-through).
    funcs = {}
    for r in counts:
        m = reg.find(r["pc"]) if r["pc"] >> 28 == 8 else None
        # keyed by (member, pc): overlay members share addresses
        funcs[(m["region"] if m else None, r["pc"])] = {"pc": r["pc"], "calls": r["calls"], "steps": 0, "reg": m,
                          "lane_column": r["lane"]}
    unattributed_steps = 0
    by_hits = collections.Counter()
    exact_resident = collections.Counter()
    if steps is not None:
        for pc, n in steps.items():
            m = reg.find(pc)
            key = (m["region"], m["vram"]) if m else None
            if m is not None and m["region"] != "main":
                reg.used[m["region"]] += n
                if reg.last_by_hits:
                    by_hits[m["region"]] += n
                if reg.last_exact:
                    exact_resident[m["region"]] += n
            if key is None:
                unattributed_steps += n
                continue
            f = funcs.get(key)
            if f is None:
                f = funcs[key] = {"pc": key[1], "calls": 0, "steps": 0, "reg": m, "lane_column": None}
            f["steps"] += n

    for f in funcs.values():
        pc, m = f["pc"], f["reg"]
        exact = m is not None and m["vram"] == pc
        rec = m["recovery"] if exact else None
        if is_bios(pc):
            cls, why = "port_shim", "bios_hle"
        elif pc in admitted:
            cls, why = "matched_c", "native lane"
        elif psyq_active(pc):
            cls, why = "port_shim", "psyq_" + psyq[pc]["status"].lower()
        elif steps is not None and f["calls"] and not steps.get(pc) and f["steps"] == 0 \
                and (pc >> 28) == 8:
            cls, why = "port_shim", "host_skipped"
        else:
            cls = "interpreted"
            if rec == "c":
                if m["region"] != "main":
                    why = "matched_c_unused: overlay (no lane for overlays yet)"
                elif pc in lane_fn:
                    why = "matched_c_unused: lane rule " + lane_fn[pc]["rule"]
                elif pc in psyq:
                    why = "matched_c_unused: psyq entry " + psyq[pc]["status"].lower()
                elif psyq_ranges is not None and psyq_manifest.psyq_segment(pc, psyq_ranges):
                    why = "matched_c_unused: PsyQ library range (lane policy excludes)"
                else:
                    why = "matched_c_unused: not a lane candidate"
            elif rec in ("assembly", "mixed"):
                why = "retail_asm: registry " + rec
            elif m is not None and sdk_overlay(m["region"], pc):
                why = "unregistered: interpreted retail, unmatched (SDK %s)" % sdk_overlay(m["region"], pc)
            elif m is not None and m.get("unregistered"):
                why = "unregistered: no registry entry"
            elif m is not None:
                why = "retail_asm: inside " + str(m["name"])
            else:
                why = "unregistered"
        f["class"], f["why"] = cls, why
        if is_bios(pc) or (pc >> 28) != 8:
            f["domain"] = "bios"
        elif pc in psyq or (psyq_ranges is not None and psyq_manifest.psyq_segment(pc, psyq_ranges)) \
                or (m is not None and sdk_overlay(m["region"], pc)):
            f["domain"] = "psyq"
        else:
            f["domain"] = "game"
        size = (m.get("size") or 4) if exact else 4
        f["est_steps"] = f["calls"] * size // 4
        if a.lane_run == "on" and steps is not None and f["lane_column"] == "native":
            # ran natively: no interpreted steps exist for its body
            f["steps"] = f["est_steps"]

    key = "steps" if steps is not None else "est_steps"
    tot_f = len(funcs)
    tot_c = sum(f["calls"] for f in funcs.values())
    tot_s = sum(f[key] for f in funcs.values()) + (unattributed_steps if steps is not None else 0)
    classes = {}
    for cls in ("matched_c", "port_shim", "interpreted"):
        fs = [f for f in funcs.values() if f["class"] == cls]
        sub = collections.Counter()
        subc = collections.Counter()
        subs = collections.Counter()
        for f in fs:
            w = f["why"].split(":")[0]
            sub[w] += 1
            subc[w] += f["calls"]
            subs[w] += f[key]
        s = sum(f[key] for f in fs)
        classes[cls] = {
            "functions": len(fs), "functions_share": round(len(fs) / tot_f, 4) if tot_f else 0,
            "calls": sum(f["calls"] for f in fs),
            "instructions": s, "instructions_share": round(s / tot_s, 4) if tot_s else 0,
            "by_reason": {w: {"functions": sub[w], "calls": subc[w], "instructions": subs[w]}
                          for w in sorted(sub)},
        }

    idle = {int(x, 16) for x in a.idle.split(",") if x}
    idle_s = sum(f[key] for f in funcs.values() if f["pc"] in idle)
    busy = tot_s - idle_s
    for cls, c in classes.items():
        s = c["instructions"] - sum(f[key] for f in funcs.values()
                                    if f["class"] == cls and f["pc"] in idle)
        c["instructions_share_excluding_idle"] = round(s / busy, 4) if busy else 0

    domains = {}
    for dom in ("game", "psyq", "bios"):
        fs = [f for f in funcs.values() if f.get("domain") == dom]
        busy_d = sum(f[key] for f in fs if f["pc"] not in idle)
        reg_c = sum(f[key] for f in fs if f["pc"] not in idle and f["reg"] is not None
                    and f["reg"]["vram"] == f["pc"] and f["reg"].get("recovery") == "c")
        d = {"functions": len(fs), "instructions": sum(f[key] for f in fs),
             "instructions_excluding_idle": busy_d,
             # byte-matched C EXISTS in the registry (whether or not the port runs it)
             "matched_c_exists_excluding_idle": reg_c,
             "matched_c_exists_share_excluding_idle": round(reg_c / busy_d, 4) if busy_d else 0}
        for cls in ("matched_c", "port_shim", "interpreted"):
            cs = [f for f in fs if f["class"] == cls]
            b = sum(f[key] for f in cs if f["pc"] not in idle)
            d[cls] = {"functions": len(cs), "instructions_excluding_idle": b,
                      "share_excluding_idle": round(b / busy_d, 4) if busy_d else 0}
        domains[dom] = d

    def row(f):
        m = f["reg"]
        return {"pc": "%08x" % f["pc"], "name": m["name"] if m and m["vram"] == f["pc"] else None,
                "calls": f["calls"], "instructions": f[key], "why": f["why"],
                "source": m.get("source") if m and m["vram"] == f["pc"] else None}

    shadows = [dict(row(f), shim=psyq.get(f["pc"], {}).get("name"))
               for f in funcs.values() if f["class"] == "port_shim" and f["reg"] is not None
               and f["reg"]["vram"] == f["pc"] and f["reg"]["recovery"] == "c"]
    shadows.sort(key=lambda r: -r["instructions"])
    # Registered matched C shadowed by an active shim, executed or not.
    static_shadows = sorted("%08x %s" % (pc, p["name"]) for pc, p in psyq.items()
                            if psyq_active(pc) and any(m["region"] == "main" and m["vram"] == pc
                                                       and m["recovery"] == "c" for m in reg.main))
    unused = sorted((row(f) for f in funcs.values() if f["why"].startswith("matched_c_unused")),
                    key=lambda r: -r["instructions"])
    shims = sorted((row(f) for f in funcs.values() if f["class"] == "port_shim"),
                   key=lambda r: -r["calls"])
    lane_disagree = [("%08x" % f["pc"]) for f in funcs.values()
                     if f["lane_column"] == "native" and f["class"] != "matched_c"
                     and not f["why"].startswith("psyq")]

    result = {
        "label": a.label,
        "instructions_basis": ("ESTIMATE: calls x registry size / 4 (no loops, no early returns)"
                               if steps is None else
                               "exact interpreted steps per PC (MUSASHI_TRACE_INSNS)" if a.lane_run == "off" else
                               "interpreted steps exact; natively run functions ESTIMATED as calls x size/4, "
                               "and native-to-native calls are not traced at all"),
        "trace_complete": a.lane_run == "off",
        "classified_as": "lane-ON default build, MUSASHI_PSYQ_LANE=%s" % a.psyq_lane,
        "idle_functions": sorted("%08x" % x for x in idle),
        "totals": {"functions": tot_f, "calls": tot_c, "instructions": tot_s,
                   "idle_instructions": idle_s,
                   "unattributed_instructions": unattributed_steps},
        "classes": classes,
        "by_domain": domains,
        "overlay_attribution": {
            "method": ("EXACT where trace.resident covers the containing function's entry (member bytes "
                       "hashed against the resident digest), else " if a.trace_resident else "") +
                      "INFERRED per PC: the member whose containing function's entry was traced, "
                      "then the member with most traced entries",
            "instructions_exact_by_member": dict(exact_resident.most_common()),
            "resident_entries_unmatched": resident_unmatched,
            "entry_hits": dict(sorted(reg.hits.items(), key=lambda kv: -kv[1])),
            "instructions_by_member": dict(reg.used.most_common()),
            "ambiguous_lookups_by_member": dict(reg.ambiguous.most_common()),
            # instructions where more than one member's containing function
            # was entered, so the global entry-hit count chose the member
            "instructions_decided_by_global_hits": dict(by_hits.most_common()),
        },
        "shadowed_executed": shadows,
        "shadowed_static": static_shadows,
        "port_shims_executed": shims,
        "matched_c_unused_by_reason": {w: {"functions": sum(1 for u in unused if u["why"] == w),
                                           "instructions": sum(u["instructions"] for u in unused
                                                               if u["why"] == w)}
                                       for w in sorted({u["why"] for u in unused})},
        "matched_c_unused_top": unused[:60],
        # Game code whose matched C could replace what runs now (interpreted
        # retail or host code); the batches of game-code switches draw on this.
        "game_switch_candidates": sorted(
            (dict(row(f), lane_detail=lane_fn.get(f["pc"], {}).get("detail"))
             for f in funcs.values() if f.get("domain") == "game" and f["reg"] is not None
             and f["reg"]["vram"] == f["pc"] and f["reg"]["recovery"] == "c"
             and (f["why"].startswith("matched_c_unused") or f["why"] == "host_skipped")),
            key=lambda r: -r["instructions"]),
        "lane_column_disagreements": lane_disagree,
    }
    text = json.dumps(result, indent=1)
    if a.json:
        Path(a.json).write_text(text + "\n")
    else:
        print(text)
    c = classes
    def pct(cls):
        return "%.1f%% (%.1f%% excl. idle)" % (100 * c[cls]["instructions_share"],
                                               100 * c[cls]["instructions_share_excluding_idle"])
    print("port_matched_ratio: %s: of instructions, matched C %s [%d/%d functions], "
          "port shims %s [%d], interpreted retail %s [%d, %d with unused matched C]; "
          "%d shims shadow matched C (%d executed); game code: matched C %.1f%% [%d/%d functions], "
          "PsyQ: shims %.1f%%%s" % (
              a.label, pct("matched_c"), c["matched_c"]["functions"], tot_f,
              pct("port_shim"), c["port_shim"]["functions"],
              pct("interpreted"), c["interpreted"]["functions"],
              c["interpreted"]["by_reason"].get("matched_c_unused", {}).get("functions", 0),
              len(static_shadows), len(shadows),
              100 * domains["game"]["matched_c"]["share_excluding_idle"],
              domains["game"]["matched_c"]["functions"], domains["game"]["functions"],
              100 * domains["psyq"]["port_shim"]["share_excluding_idle"],
              (" [instruction share ESTIMATED]" if steps is None else "") +
              (" [lane-ON trace: native-to-native calls missing, native bodies estimated]"
               if a.lane_run == "on" else "")), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
