#!/usr/bin/env python3
"""Search for the source edit that turns a near-miss draft into a match.

A draft that compiles but differs from retail by a handful of instruction
words is not really a failed decompilation. It is usually one of a small
set of recurring mistakes m2c makes when it has no type information, and
each one has a mechanical source-level fix. The catalogue lives in
docs/MATCHING.md; this tool applies the candidates and lets the oracle say
which, if any, was right.

The search is deliberately dumb. It generates variants of the source,
builds each one through the pinned Psy-Q chain, and compares the bytes
against retail. It never reasons about whether a variant is "probably"
correct, and it never edits a file unless tools/match_function.py reported
MATCH for the exact range. That is the same rule the rest of the harness
runs on: a build is not a match.

Safety, in the same spirit as batch_match.py:

* A function already in the registry is skipped before any work starts.
* A source without the UNVERIFIED draft marker is never touched, so
  verified bodies, hand-written bodies and assembly overlays are safe.
* Every variant is written into a scratch directory. The target file is
  written exactly once, after the MATCH, and only its `#else` body changes.
* The MUSASHI_NATIVE_MIPS_WORD_EXPORT words are compared before and after;
  a variant that would change them is refused.

Most functions will not be repaired by any of these. That is expected: the
transformations cover the drift this codebase actually exhibits, not the
general problem of recovering C from machine code.
"""

from __future__ import annotations

import argparse
import itertools
import json
import re
import sys
import tempfile
from pathlib import Path

import batch_match
import build_candidate
import match_function
from recarve_drafts import EXPORT_WORD_RE, REGIONS, split_dual_shape
from retail_common import RetailError

DRAFT_MARKER = "UNVERIFIED draft"

EXTERN_PTR_RE = re.compile(r"^extern\s+(\w+)\s*\*\s*(D_[0-9A-Fa-f]{8})\s*;", re.MULTILINE)
EXTERN_SCALAR_RE = re.compile(r"^extern\s+(u8|s8|u16|s16|u32|s32)\s+(D_[0-9A-Fa-f]{8})\s*;", re.MULTILINE)
SCALED_DEREF_RE = re.compile(
    r"\*\(&(D_[0-9A-Fa-f]{8})\s*\+\s*\(([^()]+?)\s*\*\s*(\d+)\)\)"
)
ADDEQ_RE = re.compile(r"^(\s*)(\w+)\s*\+=\s*([\w>\-.\[\]]+);\s*$", re.MULTILINE)
OREQ_RE = re.compile(r"^(\s*)(\w+)\s*\|=\s*([\w>\-.\[\]]+);\s*$", re.MULTILINE)
SIGNATURE_RE = re.compile(
    r"^(?P<ret>[\w \t*]+?)\s*(?P<name>func_[0-9A-Fa-f]{8})\s*\((?P<params>[^)]*)\)\s*\{",
    re.MULTILINE,
)


def _variants_pointer_scale(body: str):
    """`*(&D_X + (i * K))` indexes bytes, not elements of a guessed type."""

    for match in set(SCALED_DEREF_RE.findall(body)):
        symbol, index, scale = match
        old = f"*(&{symbol} + ({index} * {scale}))"
        if old not in body:
            continue
        yield f"index-{symbol}", body.replace(old, f"(&{symbol})[{index.strip()}]")


def _variants_byte_array(body: str):
    """Retype a global pointer as a byte array so offsets stay byte offsets."""

    for kind, symbol in set(EXTERN_PTR_RE.findall(body)):
        rewritten = body.replace(
            f"extern {kind} *{symbol};", f"extern u8 {symbol}[];"
        )
        rewritten = re.sub(
            rf"&{symbol}\s*\+\s*(0x[0-9A-Fa-f]+|\d+)",
            rf"&{symbol}[\1]",
            rewritten,
        )
        if rewritten != body:
            yield f"bytes-{symbol}", rewritten


_SCALED_BASE_RE = re.compile(r"\(([^()]+?)\)\s*\+\s*&(D_[0-9A-Fa-f]{8})\b")


def _variants_indexed_base(body: str):
    """`(i * K) + &D` scales the whole byte offset again.

    m2c types the global as a pointer, so `&D` is a pointer-to-pointer and
    adding an already-computed byte offset multiplies it by four. Retail's
    `sll $v0,$v0,0x2` against the draft's `sll $v0,$v0,0x4` is this bug.
    The array form makes the name the label's own address and the offset
    stay bytes. Distinct from _variants_byte_array, which only handles a
    literal displacement.
    """

    for offset, symbol in set(_SCALED_BASE_RE.findall(body)):
        rewritten = body.replace(f"({offset}) + &{symbol}", f"&{symbol}[{offset}]")
        for kind in ("s32", "u32", "s16", "u16", "s8", "u8", "void"):
            declaration = f"extern {kind} *{symbol};"
            if declaration in rewritten:
                rewritten = rewritten.replace(declaration, f"extern u8 {symbol}[];")
                break
        if rewritten != body:
            yield f"base-{symbol}", rewritten


def _variants_volatile(body: str):
    """Volatile stores are not scheduled into a branch delay slot."""

    for kind, symbol in set(EXTERN_PTR_RE.findall(body)):
        yield (
            f"volatile-{symbol}",
            body.replace(f"extern {kind} *{symbol};", f"extern volatile {kind} *{symbol};"),
        )
    for kind, symbol in set(EXTERN_SCALAR_RE.findall(body)):
        yield (
            f"volatile-{symbol}",
            body.replace(f"extern {kind} {symbol};", f"extern volatile {kind} {symbol};"),
        )
    all_ptrs = set(EXTERN_PTR_RE.findall(body))
    if len(all_ptrs) > 1:
        rewritten = body
        for kind, symbol in all_ptrs:
            rewritten = rewritten.replace(
                f"extern {kind} *{symbol};", f"extern volatile {kind} *{symbol};"
            )
        yield "volatile-all", rewritten


def _variants_address_of(body: str):
    """Retail passes `&D_X`; the draft passes its contents."""

    for kind, symbol in set(EXTERN_SCALAR_RE.findall(body)):
        rewritten = re.sub(rf"(?<![&\w]){symbol}\b(?!\s*[;=\[])", f"&{symbol}", body)
        rewritten = rewritten.replace(f"extern {kind} &{symbol};", f"extern {kind} {symbol};")
        if rewritten != body:
            yield f"addr-{symbol}", rewritten


def _variants_commute(body: str):
    """`a += b` and `a = b + a` are the same value, different instructions."""

    def commute(pattern, operator, label):
        for match in pattern.finditer(body):
            indent, left, right = match.groups()
            old = match.group(0)
            new = f"{indent}{left} = {right} {operator} {left};"
            yield f"{label}-{left}", body.replace(old, new, 1)

    yield from commute(ADDEQ_RE, "+", "commute-add")
    yield from commute(OREQ_RE, "|", "commute-or")


def _variants_leading_params(body: str):
    """m2c guessed too few parameters; retail passes the missing ones on."""

    match = SIGNATURE_RE.search(body)
    if match is None:
        return
    params = match.group("params").strip()
    existing = "" if params in ("", "void") else params
    for count in (1, 2, 3):
        names = [f"m2c_arg{index}" for index in range(count)]
        added = ", ".join(f"s32 {name}" for name in names)
        merged = added if not existing else f"{added}, {existing}"
        head = match.group(0).replace(f"({params})", f"({merged})", 1)
        rewritten = body[: match.start()] + head + body[match.end():]
        # The added parameters have to be used, or GCC will not keep them in
        # the argument registers the call sequence expects.
        rewritten = rewritten.replace(
            head, head + "\n    (void) " + "; (void) ".join(names) + ";", 1
        )
        yield f"params+{count}", rewritten


def _variants_narrow(body: str):
    """A `sb`/`sh` where the draft emits `sw` means a narrower global."""

    for kind, symbol in set(EXTERN_SCALAR_RE.findall(body)):
        for narrower in ("u8", "u16", "s8", "s16", "u32"):
            if narrower == kind:
                continue
            yield (
                f"{narrower}-{symbol}",
                body.replace(f"extern {kind} {symbol};", f"extern {narrower} {symbol};"),
            )


M2C_FIELD_RE = re.compile(
    r"M2C_FIELD\((?P<expr>[^,]+),\s*(?P<type>[us](?:8|16|32))\s*\*\s*,\s*(?P<offset>[^)]+)\)"
)
ACCESS_TYPES = ("s8", "u8", "s16", "u16", "s32", "u32")


def _variants_field_type(body: str):
    """m2c guesses the access width and signedness from one instruction.

    A `lh` where the draft emits `lhu`, or a `sb` where it emits `sw`, is
    this guess being wrong. Vary one access at a time: changing them all
    together would mostly produce nonsense, and the offsets are what tie a
    site to a field, so each site is independent.
    """

    sites = list(M2C_FIELD_RE.finditer(body))
    if not sites:
        return
    for site in sites:
        for access in ACCESS_TYPES:
            if access == site.group("type"):
                continue
            replacement = (
                f"M2C_FIELD({site.group('expr')}, {access} *, {site.group('offset')})"
            )
            rewritten = body[: site.start()] + replacement + body[site.end():]
            yield f"field-{site.group('offset').strip()}-{access}", rewritten
    # A whole draft is often consistently wrong about one width, e.g. every
    # halfword access read as unsigned. Try the sweeping version too.
    kinds = {site.group("type") for site in sites}
    for kind in kinds:
        for access in ACCESS_TYPES:
            if access == kind:
                continue
            rewritten = M2C_FIELD_RE.sub(
                lambda m: "M2C_FIELD({}, {} *, {})".format(
                    m.group("expr"),
                    access if m.group("type") == kind else m.group("type"),
                    m.group("offset"),
                ),
                body,
            )
            if rewritten != body:
                yield f"field-all-{kind}-{access}", rewritten


GENERATORS = (
    _variants_field_type,
    _variants_indexed_base,
    _variants_pointer_scale,
    _variants_byte_array,
    _variants_address_of,
    _variants_volatile,
    _variants_commute,
    _variants_leading_params,
    _variants_narrow,
)


def variants(body: str, depth: int):
    """Yield (label, body). Depth 2 composes pairs of single edits."""

    seen = {body}
    first: list[tuple[str, str]] = []
    for generator in GENERATORS:
        for label, rewritten in generator(body):
            if rewritten in seen:
                continue
            seen.add(rewritten)
            first.append((label, rewritten))
            yield label, rewritten
    if depth < 2:
        return
    for (left_label, left), generator in itertools.product(first, GENERATORS):
        for right_label, rewritten in generator(left):
            if rewritten in seen:
                continue
            seen.add(rewritten)
            yield f"{left_label}+{right_label}", rewritten


def oracle_argv(region: str, spec: dict, vram: int, size: int, candidate: Path) -> list[str]:
    argv = [
        "--vram", f"0x{vram:X}",
        "--size", f"0x{size:X}",
        "--candidate", str(candidate),
    ]
    if spec.get("kind") == "blob":
        argv += [
            "--retail-file", str(spec["file"]),
            "--base", f"0x{int(spec['base']):X}",
            "--sha256", str(spec["sha256"]),
        ]
    return argv


def try_source(source: Path, name: str, vram: int, size: int, region: str,
               spec: dict, scratch: Path) -> str | None:
    """Return the optimization that matched, or None."""

    candidate = scratch / "candidate.bin"
    argv = oracle_argv(region, spec, vram, size, candidate)
    for optimization in batch_match.OPTIMIZATION_CANDIDATES:
        candidate.unlink(missing_ok=True)
        code = batch_match._quiet(
            build_candidate.main,
            [
                str(source),
                "--symbol", name,
                "--link-base", f"0x{vram:X}",
                f"--optimization={optimization}",
                "--output", str(candidate),
            ],
        )
        if code != 0 or not candidate.is_file():
            continue
        if batch_match._quiet(match_function.main, argv) == 0:
            return optimization
    return None


def repair_region(region: str, repo: Path, registered: set[int], spec: dict,
                  limit: int | None, depth: int, dry_run: bool,
                  only: set[str] | None = None) -> list[tuple[str, str, str]]:
    asm = repo / REGIONS[region]
    text = asm.read_text()
    repaired: list[tuple[str, str, str]] = []
    examined = 0
    with tempfile.TemporaryDirectory() as directory:
        scratch = Path(directory)
        for function in batch_match.enumerate_functions(text):
            if function.vram in registered:
                continue
            if only is not None and function.name not in only:
                continue
            target = batch_match.source_path(repo, region, function.vram)
            if not target.is_file():
                continue
            original = target.read_text()
            if DRAFT_MARKER not in original:
                continue
            parts = split_dual_shape(original)
            if parts is None:
                continue
            export, body = parts
            if limit is not None and examined >= limit:
                break
            examined += 1
            for label, rewritten in variants(body, depth):
                probe = scratch / f"{function.name}.c"
                probe.write_text(rewritten)
                optimization = try_source(
                    probe, function.name, function.vram, function.size, region, spec, scratch
                )
                if optimization is None:
                    continue
                merged = export + "#else" + rewritten.rstrip() + "\n#endif\n"
                if EXPORT_WORD_RE.findall(merged) != EXPORT_WORD_RE.findall(original):
                    raise RetailError(
                        f"refusing to write {target}: export words would change"
                    )
                print(f"  MATCH {function.name} via {label} {optimization}", flush=True)
                repaired.append((function.name, label, optimization))
                if not dry_run:
                    target.write_text(merged)
                break
    return repaired


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--region", action="append", choices=sorted(REGIONS))
    parser.add_argument("--registry", type=Path, default=Path("provenance/matches.json"))
    parser.add_argument("--limit", type=int, help="examine at most this many drafts per region")
    parser.add_argument("--depth", type=int, default=1, choices=(1, 2),
                        help="2 also tries pairs of edits; much slower")
    parser.add_argument("--names", type=Path,
                        help="file of function names, one per line; restrict the search to these")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    repo = Path(__file__).resolve().parent.parent
    registry = json.loads((repo / args.registry).read_text())
    only = None
    if args.names is not None:
        only = {line.strip() for line in args.names.read_text().splitlines() if line.strip()}
    total = 0
    for region in args.region or sorted(REGIONS):
        registered = {
            match["vram"] for match in registry["matches"] if match["region"] == region
        }
        print(f"{region}:")
        repaired = repair_region(
            region, repo, registered, registry["targets"][region],
            args.limit, args.depth, args.dry_run, only,
        )
        print(f"  {len(repaired)} repaired")
        total += len(repaired)
    print(f"\n{total} drafts now match. Register them with")
    print("tools/batch_match.py --register-existing, then tools/verify_registry.py.")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    raise SystemExit(main())
