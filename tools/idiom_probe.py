#!/usr/bin/env python3
"""Discriminate candidate toolchains by the code they emit for a known idiom.

The first matched function proved the build pipeline works but said nothing
about which compiler built the game: a three-instruction arithmetic leaf has
one sensible encoding, so every candidate reproduced it. Evidence requires an
idiom where candidates actually disagree.

Integer division is that idiom. Psy-Q's assembler expands a division into a
divide-by-zero guard, and the shape of that expansion is exactly what maspsx's
--aspsx-version and --expand-div exist to reproduce. This tool reads the
expansion the retail image uses, emits the same construct with each candidate
toolchain, and reports which candidates reproduce it and which do not.

Mnemonics are compared without operands. Register allocation differs freely
between a probe and retail code; the expansion's instruction shape is the
fingerprint.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import sys
import tempfile
from collections import Counter
from pathlib import Path

import build_candidate
import identify_compiler
from retail_common import RetailError

DIVISION_ANCHORS = frozenset(("div", "divu"))
DEFAULT_WINDOW = 4
DEFAULT_PREFIX_LENGTH = 3

# objdump renders "  <offset>:\t<word> \t<mnemonic>\t<operands>".
OBJDUMP_RE = re.compile(r"^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(?P<mnemonic>[a-z][a-z0-9._]*)")

PROBE_SOURCE = """
int probe_div(int a, int b) {
    return a / b;
}
"""


def idiom_windows(
    mnemonics: list[str], anchors: frozenset[str], window: int
) -> list[tuple[str, ...]]:
    """Return the mnemonic window beginning at each anchor instruction."""

    if window <= 0:
        raise RetailError(f"window must be positive, got {window}")
    return [
        tuple(mnemonics[index : index + window])
        for index, mnemonic in enumerate(mnemonics)
        if mnemonic in anchors
    ]


def retail_idioms(
    functions: dict[str, list[str]], anchors: frozenset[str], window: int
) -> Counter[tuple[str, ...]]:
    """Count idiom windows across functions, without crossing their boundaries.

    A window that ran past the end of one function into the next would splice
    two unrelated instruction streams into a fake pattern.
    """

    counted: Counter[tuple[str, ...]] = Counter()
    for mnemonics in functions.values():
        counted.update(idiom_windows(mnemonics, anchors, window))
    return counted


def parse_objdump_mnemonics(text: str) -> list[str]:
    """Extract the mnemonic stream from `objdump -d` output."""

    mnemonics = []
    for line in text.splitlines():
        match = OBJDUMP_RE.match(line)
        if match:
            mnemonics.append(match.group("mnemonic"))
    return mnemonics


def dominant_prefix(
    windows: list[tuple[str, ...]], length: int
) -> tuple[tuple[str, ...], float]:
    """Return the most common post-anchor prefix and the share of windows using it.

    The anchor itself is dropped: div and divu are the same idiom, and keeping
    them apart would split one pattern into two.
    """

    if not windows:
        raise RetailError("cannot determine a dominant prefix from no windows")
    if length <= 0:
        raise RetailError(f"length must be positive, got {length}")
    prefixes = Counter(window[1 : 1 + length] for window in windows)
    prefix, count = prefixes.most_common(1)[0]
    return prefix, count / sum(prefixes.values())


def compare_idiom(
    candidate: list[str], retail_prefix: tuple[str, ...], anchors: frozenset[str]
) -> dict[str, object]:
    """Judge whether a candidate's expansion matches retail's."""

    windows = idiom_windows(candidate, anchors, len(retail_prefix) + 1)
    if not windows:
        return {"reproduces": False, "candidate_prefix": None, "retail_prefix": retail_prefix}
    candidate_prefix = windows[0][1 : 1 + len(retail_prefix)]
    return {
        "reproduces": candidate_prefix == retail_prefix,
        "candidate_prefix": candidate_prefix,
        "retail_prefix": retail_prefix,
    }


def _load_retail_functions(disassembly: Path) -> dict[str, list[str]]:
    if not disassembly.is_file():
        raise RetailError(
            f"disassembly not found: {disassembly}; run "
            "'python3 -m splat split config/slus_007.26.yaml' first"
        )
    parsed = identify_compiler.parse_disassembly(
        disassembly.read_text(encoding="utf-8", errors="replace")
    )
    return {name: [i.mnemonic for i in instructions] for name, instructions in parsed.items()}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disassembly", type=Path, help="splat output (default: asm/main.s)")
    parser.add_argument("--toolchain-root", type=Path, help="directory of toolchain subdirectories")
    parser.add_argument("--maspsx", type=Path, help="path to maspsx.py")
    parser.add_argument("--optimization", default="-O2", help="use the = form, e.g. --optimization=-O2")
    parser.add_argument("--gp", default="-G0", help="use the = form, e.g. --gp=-G0")
    parser.add_argument(
        "--no-expand-div",
        dest="expand_div",
        action="store_false",
        help="probe without maspsx division expansion, to contrast with retail's guard",
    )
    return parser


def _probe_toolchain(
    name: str,
    root: Path,
    maspsx: Path,
    workspace: Path,
    *,
    optimization: str,
    gp: str,
    expand_div: bool,
) -> list[str]:
    """Compile the division probe with one toolchain and return its mnemonics."""

    toolchain = build_candidate.resolve_toolchain(name)
    directory = root / name
    cpp = directory / "cpp"
    cc1 = directory / "cc1"
    if not cpp.is_file() or not cc1.is_file():
        raise RetailError(f"toolchain {name} is not installed under {root}")

    gnu_as = build_candidate._resolve_binutil("mips-linux-gnu-as")
    objdump = build_candidate._resolve_binutil("mips-linux-gnu-objdump")

    source = workspace / f"{name}.c"
    source.write_text(PROBE_SOURCE, encoding="utf-8")
    preprocessed = workspace / f"{name}.i"
    assembly = workspace / f"{name}.s"
    obj = workspace / f"{name}.o"

    build_candidate._run(build_candidate.cpp_command(cpp, source), f"cpp[{name}]", stdout=preprocessed)
    build_candidate._run(
        build_candidate.cc1_command(cc1, preprocessed, assembly, optimization=optimization, gp=gp),
        f"cc1[{name}]",
    )
    build_candidate._run(
        build_candidate.maspsx_command(
            maspsx,
            assembly,
            obj,
            aspsx_version=toolchain.aspsx_version,
            gnu_as=gnu_as,
            gp=gp,
            expand_div=expand_div,
        ),
        f"maspsx[{name}]",
    )
    return parse_objdump_mnemonics(
        build_candidate._capture([str(objdump), "-d", str(obj)], f"objdump[{name}]")
    )


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    repo = Path(__file__).resolve().parents[1]
    try:
        functions = _load_retail_functions(args.disassembly or repo / "asm/main.s")
        counted = retail_idioms(functions, DIVISION_ANCHORS, DEFAULT_WINDOW)
        if not counted:
            raise RetailError("no division sites found in the disassembly")
        windows = list(counted.elements())
        retail_prefix, share = dominant_prefix(windows, DEFAULT_PREFIX_LENGTH)

        print(f"RETAIL division sites={len(windows)} distinct={len(counted)}")
        print(f"  dominant expansion: div + {' '.join(retail_prefix)}  ({share * 100:.1f}% of sites)")
        print()

        env_root = os.environ.get(build_candidate.DEFAULT_TOOLCHAIN_ROOT_ENV)
        root = args.toolchain_root or (
            Path(env_root) if env_root else build_candidate.default_toolchain_root(repo)
        )
        maspsx = args.maspsx or build_candidate.default_maspsx_path(repo)
        if not Path(maspsx).is_file():
            raise RetailError(f"maspsx.py not found: {maspsx}; run tools/fetch_toolchains.sh")

        workspace = Path(tempfile.mkdtemp(prefix="idiom."))
        reproduced: list[str] = []
        try:
            print(f"{'toolchain':<20}{'aspsx':<8}{'expansion':<34}verdict")
            for name in sorted(build_candidate.TOOLCHAINS):
                toolchain = build_candidate.TOOLCHAINS[name]
                try:
                    mnemonics = _probe_toolchain(
                        name,
                        Path(root),
                        Path(maspsx),
                        workspace,
                        optimization=args.optimization,
                        gp=args.gp,
                        expand_div=args.expand_div,
                    )
                except RetailError as exc:
                    print(f"{name:<20}{toolchain.aspsx_version:<8}{'-':<34}skipped ({exc})")
                    continue
                verdict = compare_idiom(mnemonics, retail_prefix, DIVISION_ANCHORS)
                prefix = verdict["candidate_prefix"]
                rendered = "div + " + " ".join(prefix) if prefix else "(no division emitted)"
                label = "REPRODUCES" if verdict["reproduces"] else "differs"
                if verdict["reproduces"]:
                    reproduced.append(name)
                print(f"{name:<20}{toolchain.aspsx_version:<8}{rendered:<34}{label}")
        finally:
            shutil.rmtree(workspace, ignore_errors=True)

        print()
        if not reproduced:
            print("NO candidate reproduced the retail expansion; the probe does not identify it")
        elif len(reproduced) == 1:
            print(f"SINGLE candidate reproduces the retail expansion: {reproduced[0]}")
        else:
            print(
                f"{len(reproduced)} candidates reproduce it ({', '.join(reproduced)}); "
                "this probe does not discriminate between them"
            )
        return 0
    except RetailError as exc:
        print(f"idiom_probe: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
