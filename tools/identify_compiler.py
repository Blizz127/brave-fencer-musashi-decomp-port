#!/usr/bin/env python3
"""Measure code-generation fingerprints in the pinned executable's disassembly.

The retail image carries no build metadata: a full printable-string scan of
SLUS_007.26 yields no compiler, SDK, or source-path marker at all. Toolchain
identification therefore has to rest on structural evidence, which is what this
tool collects.

It deliberately stops at measurement. docs/COMPILER-ID.md forbids naming a
toolchain from anything short of disassembly or linker fingerprint evidence, so
the emitted record always reports an unresolved compiler and leaves the
conclusion to a reviewed comparison against candidate toolchain output.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from retail_common import RetailError, load_json, sha256_file, write_json_atomic


# splat emits "    /* <fileoff> <vram> <bytes> */  <mnemonic> <operands>".
INSTRUCTION_RE = re.compile(
    r"^\s*/\*\s*[0-9A-Fa-f]+\s+(?P<vram>[0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s*\*/\s*(?P<mnemonic>[a-z][a-z0-9._]*)(?P<operands>.*)$"
)
GLABEL_RE = re.compile(r"^\s*glabel\s+(?P<name>\S+)")

BRANCH_MNEMONICS = frozenset(
    "b beq beql bne bnel bnez bnezl beqz beqzl bgez bgezl bgezal bgtz bgtzl "
    "blez blezl bltz bltzl bltzal bc1t bc1f bc1tl bc1fl".split()
)
JUMP_MNEMONICS = frozenset("j jal jr jalr".split())
CONTROL_TRANSFER = BRANCH_MNEMONICS | JUMP_MNEMONICS

# COP2 register moves plus the GTE operation mnemonics spimdisasm emits.
GTE_MNEMONICS = frozenset(
    "mtc2 mfc2 ctc2 cfc2 lwc2 swc2 rtps rtpt nclip avsz3 avsz4 mvmva ncds ncdt "
    "nccs ncct ncs nct cc cdp dcpl dpcs dpct intpl sqr op gpf gpl".split()
)

TOOLCHAIN_MARKER_RE = re.compile(
    r"(?i)(psy-?q|sn\s*systems|ccpsx|aspsx|cygnus|gcc|\bgnu\b|libgpu|libgte|libspu|libcd)"
)
PRINTABLE_RUN_RE = re.compile(rb"[\x20-\x7e]{4,}")

DIV_MNEMONICS = frozenset(("div", "divu"))
DIV_GUARD_WINDOW = 3

RETURN_REGISTER_RE = re.compile(r"\$ra\b")

AGGREGATE_CAVEAT = (
    "delay_slots is the aggregate over every control transfer and is NOT "
    "diagnostic of compiler version: every GCC 2.x at -O2 fills a large share "
    "of branch delay slots and no reference distribution exists. Use "
    "return_delay_slots instead."
)
MIXING_CAVEAT = (
    "Square PS1 executables routinely mix two to four compilers in one binary, "
    "so a single whole-image rate can blend distinct toolchain regions. Treat "
    "these figures as whole-image aggregates, not per-object evidence."
)


@dataclass(frozen=True)
class Instruction:
    vram: int
    mnemonic: str
    operands: str


def parse_disassembly(text: str) -> dict[str, list[Instruction]]:
    """Group instructions under the glabel that introduces them.

    Local branch targets (.L…) and assembler directives are not function
    boundaries and are skipped.
    """

    functions: dict[str, list[Instruction]] = {}
    current: list[Instruction] | None = None
    for line in text.splitlines():
        label = GLABEL_RE.match(line)
        if label:
            current = []
            functions[label.group("name")] = current
            continue
        match = INSTRUCTION_RE.match(line)
        if match is None or current is None:
            continue
        current.append(
            Instruction(
                vram=int(match.group("vram"), 16),
                mnemonic=match.group("mnemonic"),
                operands=match.group("operands").strip(),
            )
        )
    return functions


def measure_delay_slots(functions: dict[str, list[Instruction]]) -> dict[str, float | int]:
    """Count how often the compiler filled a delay slot instead of wasting it.

    Delay slots are derived from the preceding control transfer rather than from
    splat's cosmetic extra indent, so reformatting cannot change the result.
    """

    filled = 0
    empty = 0
    for instructions in functions.values():
        for index, instruction in enumerate(instructions[:-1]):
            if instruction.mnemonic not in CONTROL_TRANSFER:
                continue
            if instructions[index + 1].mnemonic == "nop":
                empty += 1
            else:
                filled += 1
    total = filled + empty
    return {
        "total": total,
        "filled": filled,
        "nop": empty,
        "fill_rate": (filled / total) if total else 0.0,
    }


def measure_return_delay_slots(functions: dict[str, list[Instruction]]) -> dict[str, float | int]:
    """Count fills in the delay slot of `jr $ra` specifically.

    This is the discriminating measurement. GCC 2.8.1 fills the slot after a
    return; 2.7.2 leaves a nop. The aggregate branch fill rate does not separate
    compiler versions, so returns are counted on their own and indirect jumps
    through other registers are excluded.
    """

    filled = 0
    empty = 0
    for instructions in functions.values():
        for index, instruction in enumerate(instructions[:-1]):
            if instruction.mnemonic != "jr" or not RETURN_REGISTER_RE.search(instruction.operands):
                continue
            if instructions[index + 1].mnemonic == "nop":
                empty += 1
            else:
                filled += 1
    total = filled + empty
    return {
        "total": total,
        "filled": filled,
        "nop": empty,
        "fill_rate": (filled / total) if total else 0.0,
    }


def count_div_zero_guards(functions: dict[str, list[Instruction]]) -> int:
    """Count divisions followed by a break, the divide-by-zero trap idiom."""

    guards = 0
    for instructions in functions.values():
        for index, instruction in enumerate(instructions):
            if instruction.mnemonic not in DIV_MNEMONICS:
                continue
            window = instructions[index + 1 : index + 1 + DIV_GUARD_WINDOW]
            if any(following.mnemonic == "break" for following in window):
                guards += 1
    return guards


def count_gte_instructions(functions: dict[str, list[Instruction]]) -> int:
    """Count inline COP2/GTE instructions."""

    return sum(
        1
        for instructions in functions.values()
        for instruction in instructions
        if instruction.mnemonic in GTE_MNEMONICS
    )


def measure_trailing_padding(functions: dict[str, list[Instruction]]) -> dict[int, int]:
    """Histogram of trailing alignment nops per function.

    A nop occupying the delay slot of the final control transfer is required by
    the architecture, not alignment padding, so it is excluded.
    """

    histogram: Counter[int] = Counter()
    for instructions in functions.values():
        if not instructions:
            continue
        run = 0
        for instruction in reversed(instructions):
            if instruction.mnemonic != "nop":
                break
            run += 1
        preceding_index = len(instructions) - run - 1
        if run and preceding_index >= 0 and instructions[preceding_index].mnemonic in CONTROL_TRANSFER:
            run -= 1
        histogram[run] += 1
    return dict(histogram)


def scan_toolchain_strings(data: bytes) -> list[str]:
    """Return printable runs that look like toolchain or SDK markers."""

    found = {
        run.decode("ascii")
        for run in PRINTABLE_RUN_RE.findall(data)
        if TOOLCHAIN_MARKER_RE.search(run.decode("ascii"))
    }
    return sorted(found)


def build_evidence(
    functions: dict[str, list[Instruction]], toolchain_strings: list[str]
) -> dict[str, object]:
    """Assemble the evidence record. The compiler is always left unresolved."""

    delay_slots = measure_delay_slots(functions)
    mnemonics = Counter(
        instruction.mnemonic for instructions in functions.values() for instruction in instructions
    )
    return {
        "schema": 1,
        "status": "unresolved",
        "compiler": None,
        "reason": (
            "measurement only; no toolchain conclusion may be drawn until these "
            "fingerprints are compared against candidate toolchain output per "
            "docs/COMPILER-ID.md"
        ),
        "toolchain_strings": toolchain_strings,
        "caveats": [AGGREGATE_CAVEAT, MIXING_CAVEAT],
        "metrics": {
            "function_count": len(functions),
            "instruction_count": sum(mnemonics.values()),
            "delay_slots": delay_slots,
            "return_delay_slots": measure_return_delay_slots(functions),
            "div_zero_guards": count_div_zero_guards(functions),
            "gte_instructions": count_gte_instructions(functions),
            "trailing_padding_histogram": {
                str(key): value for key, value in sorted(measure_trailing_padding(functions).items())
            },
            "mnemonic_histogram": dict(mnemonics.most_common()),
        },
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identity", type=Path, help="observed identity JSON (default: provenance/exe_identity.json)")
    parser.add_argument("--disassembly", type=Path, help="splat output (default: asm/main.s)")
    parser.add_argument("--output", type=Path, help="evidence JSON (default: provenance/compiler_evidence.json)")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    identity_path = (args.identity or root / "provenance/exe_identity.json").resolve()
    disassembly_path = (args.disassembly or root / "asm/main.s").resolve()
    output_path = (args.output or root / "provenance/compiler_evidence.json").resolve()
    try:
        identity = load_json(identity_path)
        if identity.get("status") != "observed":
            raise RetailError(
                f"executable identity is {identity.get('status')!r}, not 'observed'; "
                "run identify_retail.py before measuring fingerprints"
            )
        recorded_sha256 = identity.get("sha256")
        recorded_path = identity.get("path")
        if not recorded_sha256 or not recorded_path:
            raise RetailError("executable identity is missing a path or sha256")

        exe_path = Path(str(recorded_path))
        if not exe_path.is_absolute():
            exe_path = root / exe_path
        if not exe_path.is_file():
            raise RetailError(f"pinned executable is not present: {exe_path}")
        actual_sha256 = sha256_file(exe_path)
        if actual_sha256.lower() != str(recorded_sha256).lower():
            raise RetailError(
                f"executable SHA-256 mismatch for {exe_path}: expected {recorded_sha256}, got {actual_sha256}"
            )

        if not disassembly_path.is_file():
            raise RetailError(
                f"disassembly not found: {disassembly_path}; run "
                "'python3 -m splat split config/slus_007.26.yaml' first"
            )

        functions = parse_disassembly(disassembly_path.read_text(encoding="utf-8", errors="replace"))
        if not functions:
            raise RetailError(f"no functions parsed from {disassembly_path}")

        evidence = build_evidence(functions, scan_toolchain_strings(exe_path.read_bytes()))
        write_json_atomic(output_path, evidence)

        metrics = evidence["metrics"]
        delay = metrics["delay_slots"]
        print(f"EVIDENCE status={evidence['status']} compiler=unresolved output={output_path}")
        print(f"  functions={metrics['function_count']} instructions={metrics['instruction_count']}")
        returns = metrics["return_delay_slots"]
        print(
            f"  return delay slots: {returns['filled']}/{returns['total']} filled "
            f"({returns['fill_rate'] * 100:.2f}%)  <- discriminating measurement"
        )
        print(
            f"  all delay slots:    {delay['filled']}/{delay['total']} filled "
            f"({delay['fill_rate'] * 100:.2f}%)  (aggregate, not diagnostic)"
        )
        print(f"  gte_instructions={metrics['gte_instructions']} div_zero_guards={metrics['div_zero_guards']}")
        print(f"  toolchain_strings={len(evidence['toolchain_strings'])}")
        return 0
    except RetailError as exc:
        print(f"identify_compiler: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
