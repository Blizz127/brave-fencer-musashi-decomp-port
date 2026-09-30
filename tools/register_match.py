#!/usr/bin/env python3
"""Register one or more functions in provenance/matches.json, gated by the oracle.

Each function is rebuilt from its source and compared against retail exactly
as tools/verify_registry.py would compare it. Only a byte-exact MATCH is
written to the registry; anything less is reported and leaves no trace. A
function that is already registered, or whose size cannot be read from the
splat disassembly, is refused.

    python3 tools/register_match.py main 0x8002F658 src/main/8002f658.c
    python3 tools/register_match.py main 0x8002F658 src/main/8002f658.c --optimization=-O0
"""

from __future__ import annotations

import argparse
import contextlib
import fcntl
import shlex
import sys
import tempfile
from pathlib import Path

import batch_match
import build_candidate
import match_function
import verify_registry
from retail_common import RetailError, load_json, write_json_atomic

REPO = Path(__file__).resolve().parents[1]
ASM_BY_REGION = {
    "main": REPO / "asm/main.s",
    "main_0007": REPO / "asm/overlays/main_0007/main_0007.s",
    "main_0010": REPO / "asm/overlays/main_0010/main_0010.s",
    "main_0012": REPO / "asm/overlays/main_0012/main_0012.s",
}


def returns_at(text: str, function, size: int) -> bool:
    """True when the word two before ``size`` is `jr $ra`, i.e. a function ends there."""

    words = {addr: word for addr, word, _op in batch_match.instruction_words(text, function)}
    return words.get(function.vram + size - 8) == 0x03E00008


def gate(entry: dict, targets: dict) -> tuple[bool, str]:
    with tempfile.TemporaryDirectory(prefix="register_match.") as scratch:
        candidate = Path(scratch) / "candidate.bin"
        build_argv, match_argv = verify_registry.plan(entry, targets, candidate)
        code, out = verify_registry._quiet(build_candidate.main, build_argv)
        if code != 0 or not candidate.is_file():
            return False, "BUILD FAILED\n" + out
        code, out = verify_registry._quiet(match_function.main, match_argv)
        return code == 0, out


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("region", nargs="?")
    parser.add_argument("vram", nargs="?", type=lambda value: int(value, 0))
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--size", type=lambda value: int(value, 0),
                        help="override the splat size when splat split one real function into several")
    parser.add_argument("--recovery", default="c", choices=("c", "assembly", "mixed", "unclassified"))
    parser.add_argument("--rodata-base", type=lambda value: int(value, 0),
                        help="retail address of the function's .rodata (switch tables)")
    parser.add_argument("--assembler", choices=build_candidate.ASSEMBLERS,
                        help="the Sony object was GNU-as assembled (default: ASPSX via maspsx)")
    parser.add_argument("--origin", help='provenance tag, e.g. "vendor-derived (Druthulu/BFM-decomp)"')
    parser.add_argument("--replace", action="store_true",
                        help="upgrade an existing non-C entry at this address (e.g. assembly) to the new source")
    parser.add_argument("--registry", type=Path, default=REPO / "provenance/matches.json")
    parser.add_argument("--batch", type=Path,
                        help="file of lines 'REGION VRAM SOURCE [options]'; gated in parallel, registered in one write")
    parser.add_argument("--jobs", type=int, default=2, help="parallel gates in --batch mode")
    return parser


@contextlib.contextmanager
def registry_lock(registry: Path):
    """Serialise read-modify-write of the registry across processes."""

    with open(registry.with_name(registry.name + ".lock"), "w") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


_SPLAT_CACHE: dict[str, tuple[str, dict]] = {}


def splat_lookup(region: str, vram: int):
    if region not in _SPLAT_CACHE:
        asm = ASM_BY_REGION.get(region, REPO / "asm/overlays" / region / f"{region}.s")
        if not asm.is_file():
            raise RetailError(f"no splat disassembly known for region {region!r}")
        text = asm.read_text()
        _SPLAT_CACHE[region] = (text, {f.vram: f for f in batch_match.enumerate_functions(text)})
    text, functions = _SPLAT_CACHE[region]
    if vram not in functions:
        raise RetailError(f"no function at 0x{vram:08X} in region {region}")
    return text, functions[vram]


def prepare(args, entries: list[dict]) -> tuple[dict, int | None]:
    """Validate one request against the registry; returns (entry, index to replace)."""

    existing = [i for i, e in enumerate(entries)
                if e["region"] == args.region and int(e["vram"]) == args.vram]
    if existing and not args.replace:
        raise RetailError(f"0x{args.vram:08X} is already registered in {args.region}")
    if existing and entries[existing[0]].get("recovery") == "c":
        raise RetailError(f"0x{args.vram:08X} is already registered as C; --replace only upgrades non-C entries")
    if args.replace and not existing:
        raise RetailError(f"--replace given but 0x{args.vram:08X} is not registered in {args.region}")
    text, function = splat_lookup(args.region, args.vram)
    name, size = function.name, function.size
    if args.size is not None:
        if args.size <= 0 or args.size % 4:
            raise RetailError(f"--size 0x{args.size:X} must be positive and word-aligned")
        # splat sometimes folds a following function into this range; a
        # shorter size is accepted only where the retail code returns.
        if args.size < size and not returns_at(text, function, args.size):
            raise RetailError(f"--size 0x{args.size:X} is shorter than the splat range 0x{size:X} "
                              "and does not end at a `jr $ra` + delay slot")
        size = args.size
    # A piece of a function that is already registered is not a
    # function of its own; covering earlier fragments is allowed.
    for e in entries:
        if e["region"] != args.region or int(e["vram"]) == args.vram:
            continue
        start, end = int(e["vram"]), int(e["vram"]) + int(e["size"])
        if start <= args.vram and args.vram + size <= end:
            raise RetailError(f"0x{args.vram:08X}+0x{size:X} lies inside registered {e['name']} "
                              f"[0x{start:08X},0x{end:08X})")
    entry = {
        "name": name,
        "vram": args.vram,
        "size": size,
        "region": args.region,
        "source": str(args.source.resolve().relative_to(REPO)),
        "recovery": args.recovery,
        "extent": "function",
        "optimization": args.optimization,
    }
    if args.rodata_base is not None:
        entry["rodata_base"] = args.rodata_base
    if args.assembler:
        entry["assembler"] = args.assembler
    if args.origin:
        entry["origin"] = args.origin
    if existing:
        previous = entries[existing[0]]
        if entry["size"] < int(previous["size"]):
            raise RetailError(f"replacement covers 0x{entry['size']:X} bytes, less than the existing 0x{int(previous['size']):X}")
        return entry, existing[0]
    return entry, None


def commit(entries: list[dict], entry: dict, index: int | None) -> None:
    if index is None:
        entries.append(entry)
    else:
        entries[index] = entry


def _gate_worker(job: tuple[dict, dict]) -> tuple[bool, str]:
    return gate(*job)


def run_batch(args) -> int:
    parser = build_parser()
    requests = []
    for number, line in enumerate(args.batch.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        request = parser.parse_args(shlex.split(line))
        if request.region is None or request.vram is None or request.source is None:
            raise RetailError(f"{args.batch}:{number}: needs REGION VRAM SOURCE")
        requests.append(request)
    data = load_json(args.registry)
    prepared, refused = [], 0
    for request in requests:
        try:
            prepared.append(prepare(request, data["matches"])[0])
        except RetailError as exc:
            refused += 1
            print(f"REFUSED 0x{request.vram:08X} ({request.region}): {exc}")
    jobs = [(entry, data.get("targets", {})) for entry in prepared]
    if args.jobs > 1 and len(jobs) > 1:
        import multiprocessing
        with multiprocessing.Pool(args.jobs) as pool:
            results = pool.map(_gate_worker, jobs, chunksize=4)
    else:
        results = [_gate_worker(job) for job in jobs]
    passed = [entry for entry, (ok, _out) in zip(prepared, results) if ok]
    for entry, (ok, _out) in zip(prepared, results):
        if not ok:
            refused += 1
            print(f"REFUSED {entry['name']} ({entry['region']}): not a byte-exact match")
    # Re-validate against the registry as it is now; other writers may have
    # registered overlapping entries while the gates ran.
    registered = 0
    with registry_lock(args.registry):
        data = load_json(args.registry)
        entries = data["matches"]
        by_key = {(r.region, r.vram): r for r in requests}
        for entry in passed:
            try:
                fresh, index = prepare(by_key[(entry["region"], entry["vram"])], entries)
            except RetailError as exc:
                refused += 1
                print(f"REFUSED {entry['name']} ({entry['region']}): {exc}")
                continue
            if fresh != entry:
                refused += 1
                print(f"REFUSED {entry['name']} ({entry['region']}): request changed while gating")
                continue
            commit(entries, entry, index)
            registered += 1
            print(f"REGISTERED {entry['name']} ({entry['region']}) size=0x{entry['size']:X} {entry['source']}")
        write_json_atomic(args.registry, data)
    print(f"batch: {registered} registered, {refused} refused")
    return 0 if refused == 0 else 1


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.batch:
            return run_batch(args)
        if args.region is None or args.vram is None or args.source is None:
            raise RetailError("give REGION VRAM SOURCE, or --batch FILE")
        data = load_json(args.registry)
        entry, _index = prepare(args, data["matches"])
        ok, out = gate(entry, data.get("targets", {}))
        if not ok:
            print(f"REFUSED {entry['name']}: not a byte-exact match\n{out}")
            return 1
        with registry_lock(args.registry):
            data = load_json(args.registry)
            entry, index = prepare(args, data["matches"])
            commit(data["matches"], entry, index)
            write_json_atomic(args.registry, data)
        print(f"REGISTERED {entry['name']} size=0x{entry['size']:X} {entry['source']} {args.optimization}")
        return 0
    except RetailError as exc:
        print(f"register_match: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
