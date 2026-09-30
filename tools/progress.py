#!/usr/bin/env python3
"""Report registered byte ranges and explicit source-recovery classifications.

The registry can contain C, assembly, and partial ranges. A .c filename and an
exact byte match do not establish complete C recovery. Missing classifications
remain unclassified. This report reads claims; verify_registry.py re-earns the
byte comparisons. See docs/PROGRESS.md for classification requirements.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any

from retail_common import RetailError, load_json

REQUIRED_FIELDS = ("name", "vram", "size", "source", "region")
RECOVERY_KINDS = ("c", "assembly", "mixed", "unclassified")
EXTENTS = ("function", "partial", "unclassified")
QUALIFYING_RECOVERIES = frozenset({"c", "assembly"})

# Identified split code: SLUS_007.26 t_size + MAIN.CD members 0001 [0x800CEDFC,
# 0x800CF3E8), 0008 [0x800CEDFC, 0x800CF0FC), 0009 [0x800CD384, 0x800CDD08)
# + member 0003's code
# [0x800CEED0, 0x800D3574) (including two handwritten MDEC routines) + member
# 0007 + member 0010's code [0x800CEDFC, 0x800D3408) + member 0011's code
# [0x800CEE74, 0x800D31F8) + member 0012's code chunk [0x28000, 0x86000)
# + the md members 0013-0047 and the SC md scene modules listed below. Overlay members not yet shown to be code are outside
# this denominator, and so are their registered ranges: a region only counts
# toward coverage once its code size is part of the denominator.
IDENTIFIED_REGION_BYTES = {
    "main": 411648,
    "main_0001": 1516,
    "main_0003": 18084,
    "main_0007": 9600,
    "main_0008": 768,
    "main_0009": 2436,
    "main_0010": 17932,
    "main_0011": 17284,
    "main_0012": 385024,
    # MAIN.CD md members 0013-0047: each member's code range, from after
    # the payload's leading data word to its last function (see each
    # config/overlay_main_NNNN.yaml).
    "main_0013": 2596,
    "main_0014": 1968,
    "main_0015": 3252,
    "main_0016": 1808,
    "main_0017": 1292,
    "main_0018": 2012,
    "main_0019": 4512,
    "main_0020": 1000,
    "main_0021": 1776,
    "main_0022": 3716,
    "main_0023": 308,
    "main_0024": 252,
    "main_0025": 3696,
    "main_0026": 3332,
    "main_0027": 4904,
    "main_0028": 4232,
    "main_0029": 1680,
    "main_0030": 1612,
    "main_0031": 4400,
    "main_0032": 1120,
    "main_0033": 2636,
    "main_0034": 7008,
    "main_0035": 1132,
    "main_0036": 4488,
    "main_0037": 1992,
    "main_0038": 3384,
    "main_0039": 4128,
    "main_0040": 2748,
    "main_0041": 2504,
    "main_0042": 628,
    "main_0043": 3004,
    "main_0044": 5828,
    "main_0045": 2888,
    "main_0046": 5360,
    "main_0047": 2984,
    # SC03/SC04/SC05/SC07 md scene modules (uncompressed): code ranges from each
    # config/overlay_<region>.yaml.
    "sc03_0053": 1440,
    "sc03_0054": 3056,
    "sc03_0056": 200,
    "sc03_0073": 2580,
    "sc03_0074": 2748,
    "sc03_0075": 3972,
    "sc03_0076": 13104,
    "sc03_0077": 3068,
    "sc03_0078": 2404,
    "sc03_0079": 2680,
    "sc03_0132": 2556,
    "sc03_0133": 2724,
    "sc03_0134": 3948,
    "sc03_0135": 13080,
    "sc03_0136": 3012,
    "sc03_0137": 2380,
    "sc03_0138": 3316,
    "sc04_0024": 2596,
    "sc04_0025": 2764,
    "sc04_0026": 3940,
    "sc04_0027": 13120,
    "sc04_0028": 3052,
    "sc04_0029": 2420,
    "sc04_0030": 2712,
    "sc05_0023": 2636,
    "sc05_0024": 2804,
    "sc05_0025": 3980,
    "sc05_0026": 13160,
    "sc05_0027": 3092,
    "sc05_0028": 2460,
    "sc05_0029": 2752,
    "sc07_0003": 25064,
    "sc07_0004": 63552,
}
IDENTIFIED_CODE_BYTES = sum(IDENTIFIED_REGION_BYTES.values())

# The SC scene overlays (LZSS code images decoded by
# tools/decode_sc_overlays.py, loaded at 0x80128158) are reported on their
# own line, not in the headline: each is ~350 KB, most of it engine code
# shared across scenes, so folding them in would swamp the headline. A
# region's bytes are its code span, [first function, end of the last
# function) in asm/overlays/<region>/<region>.s, which is also the "Code:"
# range of its config/overlay_<region>.yaml. Every registered SC region
# belongs here or, for the small members that load elsewhere, in
# IDENTIFIED_REGION_BYTES; the report names any that is in neither.
SCENE_OVERLAY_REGION_BYTES = {
    "sc01_0000": 354916,
    "sc01_0001": 374452,
    "sc01_0004": 356472,
    "sc01_0005": 369164,
    "sc01_0008": 357868,
    "sc01_0009": 370644,
    "sc01_0074": 353364,
    "sc01_0084": 386728,
    "sc02_0000": 404056,
    "sc02_0004": 351032,
    "sc02_0005": 427836,
    "sc02_0011": 428940,
    "sc02_0015": 352416,
    "sc02_0016": 377560,
    "sc02_0017": 402224,
    "sc02_0021": 358692,
    "sc02_0026": 384888,
    "sc02_0027": 405660,
    "sc02_0028": 406176,
    "sc02_0031": 379596,
    "sc02_0035": 377644,
    "sc02_0037": 375400,
    "sc02_0039": 356184,
    "sc02_0041": 379944,
    "sc03_0001": 413896,
    "sc03_0003": 356640,
    "sc03_0012": 352164,
    "sc03_0013": 363916,
    "sc03_0014": 406472,
    "sc03_0023": 355652,
    "sc03_0024": 388456,
    "sc03_0029": 390764,
    "sc03_0031": 369092,
    "sc03_0089": 398528,
    "sc03_0090": 407780,
    "sc03_0091": 411532,
    "sc03_0092": 379740,
    "sc03_0093": 384308,
    "sc03_0094": 390960,
    "sc03_0095": 363920,
    "sc03_0096": 362768,
    "sc03_0097": 384240,
    "sc03_0098": 381308,
    "sc03_0099": 373372,
    "sc03_0100": 378696,
    "sc03_0101": 374564,
    "sc03_0102": 384988,
    "sc03_0103": 376716,
    "sc03_0104": 404252,
    "sc03_0105": 401828,
    "sc03_0107": 369412,
    "sc03_0108": 363260,
    "sc03_0109": 352680,
    "sc03_0110": 367560,
    "sc03_0111": 378748,
    "sc03_0112": 382340,
    "sc03_0113": 371764,
    "sc03_0114": 354460,
    "sc03_0115": 361224,
    "sc03_0116": 370444,
    "sc03_0117": 378648,
    "sc03_0118": 399300,
    "sc03_0121": 368608,
    "sc03_0124": 403308,
    "sc03_0125": 375772,
    "sc03_0126": 356564,
    "sc04_0006": 358960,
    "sc04_0007": 383464,
    "sc04_0008": 354592,
    "sc04_0009": 354636,
    "sc04_0010": 352248,
    "sc04_0012": 353372,
    "sc04_0015": 377740,
    "sc04_0016": 360024,
    "sc04_0020": 373160,
    "sc04_0021": 356564,
    "sc05_0000": 352488,
    "sc05_0003": 369596,
    "sc05_0009": 354592,
    "sc05_0010": 395260,
    "sc05_0011": 351092,
    "sc05_0017": 413076,
    "sc05_0019": 356564,
    "sc06_0000": 393444,
    "sc06_0006": 367624,
    "sc06_0008": 382576,
    "sc06_0010": 387148,
    "sc06_0011": 361132,
    "sc06_0013": 354856,
    "sc06_0014": 359820,
    "sc06_0015": 354532,
    "sc06_0018": 439344,
    "sc06_0020": 380600,
    "sc06_0024": 423500,
    "sc06_0025": 379632,
    "sc06_0027": 351792,
    "sc06_0029": 410560,
    "sc06_0030": 367796,
    "sc06_0033": 431760,
    "sc07_0001": 365448,
    "sc07_0007": 372108,
    "sc07_0008": 348508,
    "sc07_0009": 351232,
    "sc07_0010": 368156,
    "sc07_0011": 353500,
}

# ceil(30% of 806272) = 241882 unique qualifying bytes.
COVERAGE_THRESHOLD_BYTES = (IDENTIFIED_CODE_BYTES * 3 + 9) // 10

# Two instructions or fewer: an empty stub, a single store, a plain return.
# These are genuine matches but are not meaningful progress, and a total that
# hides them behind a headline count invites exactly the inflation the
# rr-decomp retraction was about.
TRIVIAL_MAX_BYTES = 8


_COMMENT_RE = re.compile(r"/\*.*?\*/|//.*?$", re.S | re.M)
_NATIVE_IFELSE_RE = re.compile(
    r"#\s*if(?:n?def)?\s+MUSASHI_NATIVE_MIPS_WORD_EXPORT\b.*?"
    r"#\s*else\b(.*?)"
    r"#\s*endif",
    re.S,
)
_NATIVE_IF_RE = re.compile(
    r"#\s*if(?:n?def)?\s+MUSASHI_NATIVE_MIPS_WORD_EXPORT\b.*?#\s*endif",
    re.S,
)
_NATIVE_MACRO_RE = re.compile(r"^\s*MUSASHI_NATIVE_MIPS_WORD\s*\(.*$", re.M)
_ASM_BLOCK_RE = re.compile(
    r"(?:__asm__|asm)\s*(?:volatile)?\s*\(\s*(?P<payload>.*?)\)\s*;",
    re.S,
)
_STRING_LIT_RE = re.compile(r'"(?:\\.|[^"\\])*"')


def _asm_template(payload: str) -> str:
    """GNU asm template strings; ignore operand colons inside those strings."""

    in_str = False
    escape = False
    for index, char in enumerate(payload):
        if in_str:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_str = False
        elif char == '"':
            in_str = True
        elif char == ":":
            return payload[:index]
    return payload


def classify_recovery(source: str) -> str:
    """Classify the PS1 implementation in source text.

    Native word-export macros are ignored: they are not the compiled
    implementation. Empty register constraints emit no instructions and stay
    C. Instruction `__asm__` overlays without a C body are assembly; a C body
    plus instruction asm is mixed. Verbatim `.word 0x...` dumps are
    unclassified transcription, not decompilation.
    """

    text = _COMMENT_RE.sub("", source)
    # Keep the `#else` C body; drop native-export-only `#ifdef` blocks.
    text = _NATIVE_IFELSE_RE.sub(r"\1", text)
    text = _NATIVE_IF_RE.sub("", text)
    text = _NATIVE_MACRO_RE.sub("", text)
    instruction_asm = False
    word_dump = False
    for block in _ASM_BLOCK_RE.finditer(text):
        # Only the template strings before the first operand colon count.
        # Constraint strings (`"=r"`) are not instructions. Label colons
        # inside the template must not cut the template short.
        header = _asm_template(block.group("payload"))
        literals = "".join(
            bytes(piece, "utf-8").decode("unicode_escape")[1:-1]
            for piece in _STRING_LIT_RE.findall(header)
        )
        if not literals.strip():
            continue
        directives = 0
        mnemonics = 0
        for stmt in literals.replace("\\n", "\n").splitlines():
            stmt = stmt.strip()
            if not stmt or stmt.startswith("#"):
                continue
            if stmt.startswith(".word"):
                directives += 1
            elif stmt.startswith(".") or stmt.endswith(":"):
                continue
            else:
                mnemonics += 1
        if directives and directives >= mnemonics:
            word_dump = True
        elif mnemonics or directives:
            instruction_asm = True
    without_asm = _ASM_BLOCK_RE.sub("", text)
    has_c = "{" in without_asm
    if word_dump and not instruction_asm:
        # Verbatim retail-word transcription is not decompilation.
        return "unclassified"
    if instruction_asm and has_c:
        return "mixed"
    if instruction_asm:
        return "assembly"
    if has_c:
        return "c"
    return "unclassified"


PORTABLE_RECOVERIES = frozenset({"c"})


def _qualifies(match: dict[str, Any], recoveries: frozenset = QUALIFYING_RECOVERIES) -> bool:
    """Complete reviewed functions only; mixed/partial/unclassified do not count."""

    recovery = match.get("recovery", "unclassified")
    extent = match.get("extent", "unclassified")
    return (recovery in recoveries and extent == "function"
            and not match.get("fakematch", False))


def unique_qualifying_bytes(
    matches: list[dict[str, Any]],
    recoveries: frozenset = QUALIFYING_RECOVERIES,
    regions: dict[str, int] | None = None,
) -> int:
    """Union overlapping vram spans per region; overlapping ranges count once."""

    spans: dict[str, list[tuple[int, int]]] = {}
    for match in matches:
        if not _qualifies(match, recoveries):
            continue
        if match["region"] not in (IDENTIFIED_REGION_BYTES if regions is None else regions):
            continue
        start = match["vram"]
        end = start + match["size"]
        spans.setdefault(match["region"], []).append((start, end))

    total = 0
    for intervals in spans.values():
        intervals.sort()
        merged_start, merged_end = intervals[0]
        for start, end in intervals[1:]:
            if start <= merged_end:
                merged_end = max(merged_end, end)
            else:
                total += merged_end - merged_start
                merged_start, merged_end = start, end
        total += merged_end - merged_start
    return total


def qualifying_coverage(
    matches: list[dict[str, Any]],
    identified_bytes: int = IDENTIFIED_CODE_BYTES,
) -> dict[str, Any]:
    """Unique qualifying coverage against the identified-code denominator."""

    unique = unique_qualifying_bytes(matches)
    portable = unique_qualifying_bytes(matches, PORTABLE_RECOVERIES)
    return {
        "unique_qualifying_bytes": unique,
        "unique_c_bytes": portable,
        "unique_assembly_bytes": unique_qualifying_bytes(matches, frozenset({"assembly"})),
        "identified_bytes": identified_bytes,
        "meets_threshold": unique >= COVERAGE_THRESHOLD_BYTES,
    }


def fill_missing_classifications(
    matches: list[dict[str, Any]],
    *,
    read_source,
    complete_functions: set[tuple[str, int, int]],
) -> int:
    """Fill missing recovery/extent from source text and splat completeness.

    Already-reviewed labels are never overwritten. Native-export-only files
    stay unclassified. A range is a complete function only when splat
    enumerated the same region/vram/size.
    """

    filled = 0
    for match in matches:
        if "recovery" not in match:
            match["recovery"] = classify_recovery(read_source(match["source"]))
            filled += 1
        if "extent" not in match:
            key = (match["region"], match["vram"], match["size"])
            match["extent"] = "function" if key in complete_functions else "unclassified"
            filled += 1
    return filled


def load_registry(path: Path) -> list[dict[str, Any]]:
    """Read the match registry, refusing anything malformed."""

    if not path.is_file():
        raise RetailError(f"match registry not found: {path}")
    document = load_json(path)
    matches = document.get("matches")
    if not isinstance(matches, list):
        raise RetailError(f"registry {path} has no 'matches' list")
    return matches


def summarise(matches: list[dict[str, Any]]) -> dict[str, Any]:
    """Total registered ranges and declared recovery, without inferring either."""

    # Overlays share load addresses, so a vram repeats only within a region.
    seen: set[tuple[str, int]] = set()
    total_bytes = 0
    regions: Counter[str] = Counter()
    recovery_counts: Counter[str] = Counter()
    recovery_bytes: Counter[str] = Counter()
    extents: Counter[str] = Counter()

    for match in matches:
        missing = [field for field in REQUIRED_FIELDS if field not in match]
        if missing:
            raise RetailError(
                f"match {match.get('name', '<unnamed>')!r} is missing {', '.join(missing)}"
            )
        size = match["size"]
        if not isinstance(size, int) or size <= 0:
            raise RetailError(f"match {match['name']!r} has a non-positive size: {size}")
        vram = match["vram"]
        key = (match["region"], vram)
        if key in seen:
            raise RetailError(f"match {match['name']!r} repeats vram 0x{vram:08X} in {match['region']}")
        seen.add(key)
        total_bytes += size
        regions[match["region"]] += 1
        recovery = match.get("recovery", "unclassified")
        extent = match.get("extent", "unclassified")
        if "fakematch" in match and not isinstance(match["fakematch"], bool):
            raise RetailError(f"match {match['name']!r} has non-boolean fakematch tag")
        if recovery not in RECOVERY_KINDS or extent not in EXTENTS:
            raise RetailError(f"match {match['name']!r} has invalid recovery/extent classification")
        recovery_counts[recovery] += 1
        recovery_bytes[recovery] += size
        extents[extent] += 1

    trivial = sum(1 for m in matches if m["size"] <= TRIVIAL_MAX_BYTES)
    coverage = qualifying_coverage(matches)
    portable = unique_qualifying_bytes(matches, PORTABLE_RECOVERIES)
    return {
        # Historical keys retained for callers; these count ranges, not proven
        # functions, and the size threshold does not imply substantive recovery.
        "function_count": len(matches),
        "matched_bytes": total_bytes,
        "by_region": dict(regions),
        "trivial_count": trivial,
        "substantive_count": len(matches) - trivial,
        "by_recovery": dict(recovery_counts),
        "bytes_by_recovery": dict(recovery_bytes),
        "by_extent": dict(extents),
        "unique_qualifying_bytes": coverage["unique_qualifying_bytes"],
        "unique_c_bytes": portable,
        "unique_assembly_bytes": coverage["unique_assembly_bytes"],
        "identified_bytes": coverage["identified_bytes"],
        "meets_coverage_threshold": coverage["meets_threshold"],
        "fakematch_count": sum(bool(m.get("fakematch", False)) for m in matches),
        "fakematch_bytes": sum(m["size"] for m in matches if m.get("fakematch", False)),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, help="default: provenance/matches.json")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    repo = Path(__file__).resolve().parents[1]
    registry = args.registry or repo / "provenance/matches.json"
    try:
        matches = load_registry(registry)
        summary = summarise(matches)
        print(f"REGISTERED {summary['function_count']} ranges, {summary['matched_bytes']:,} summed bytes")
        for region, count in sorted(summary["by_region"].items()):
            region_bytes = sum(m["size"] for m in matches if m["region"] == region)
            print(f"  {region:<12} {count:>4} ranges  {region_bytes:>8,} bytes")
        print()
        print(
            f"  size <= {TRIVIAL_MAX_BYTES} bytes: {summary['trivial_count']}"
            f"    larger ranges: {summary['substantive_count']}"
        )
        print()
        print("Declared source recovery (not inferred from filenames):")
        for kind, count in sorted(summary["by_recovery"].items()):
            print(f"  {kind:<12} {count:>4} ranges  {summary['bytes_by_recovery'][kind]:>8,} bytes")
        print("Declared extent:")
        for extent, count in sorted(summary["by_extent"].items()):
            print(f"  {extent:<12} {count:>4} ranges")
        print()
        unique = summary["unique_qualifying_bytes"]
        portable = summary["unique_c_bytes"]
        identified = summary["identified_bytes"]
        percent = (100.0 * unique / identified) if identified else 0.0
        c_percent = (100.0 * portable / identified) if identified else 0.0
        print(
            "Qualifying unique coverage (complete C/assembly functions, "
            "overlap-unioned per region):"
        )
        print(
            f"  {unique:,} / {identified:,} bytes "
            f"({percent:.2f}%)"
        )
        print(
            "Portable C-only subset (complete C functions; what the "
            "native port can execute):"
        )
        print(
            f"  {portable:,} / {identified:,} bytes "
            f"({c_percent:.2f}%)"
        )
        assembly = summary["unique_assembly_bytes"]
        print("Mnemonic assembly subset (complete assembly functions; not portable C):")
        print(f"  {assembly:,} / {identified:,} bytes "
              f"({(100.0 * assembly / identified) if identified else 0.0:.2f}%)")
        print(f"FAKEMATCH (excluded from qualifying C and assembly): "
              f"{summary['fakematch_count']:,} ranges / {summary['fakematch_bytes']:,} summed bytes")
        scene_total = sum(SCENE_OVERLAY_REGION_BYTES.values())
        scene_c = unique_qualifying_bytes(matches, PORTABLE_RECOVERIES, SCENE_OVERLAY_REGION_BYTES)
        print(
            f"Scene overlays, reported separately ({len(SCENE_OVERLAY_REGION_BYTES)} SC ov regions started):"
        )
        print(
            f"  C: {scene_c:,} / {scene_total:,} bytes "
            f"({(100.0 * scene_c / scene_total) if scene_total else 0.0:.2f}%)"
        )
        scene_assembly = unique_qualifying_bytes(matches, frozenset({"assembly"}), SCENE_OVERLAY_REGION_BYTES)
        print(f"  Mnemonic assembly: {scene_assembly:,} / {scene_total:,} bytes "
              f"({(100.0 * scene_assembly / scene_total) if scene_total else 0.0:.2f}%)")
        untabled = sorted({str(m.get("region")) for m in matches
                           if str(m.get("region", "")).startswith("sc")}
                          - set(SCENE_OVERLAY_REGION_BYTES) - set(IDENTIFIED_REGION_BYTES))
        if untabled:
            print(f"  NOT COUNTED: {len(untabled)} registered SC regions missing from the tables: "
                  + " ".join(untabled))
        print(
            f"  30% threshold: {COVERAGE_THRESHOLD_BYTES:,} bytes"
            f"    {'MET' if summary['meets_coverage_threshold'] else 'NOT MET'}"
        )
        print()
        print("Registry claims only; run tools/verify_registry.py for fresh byte verification.")
        print("Summed ranges are not unique coverage or a full-decompilation percentage.")
        print("Classification is a reviewed claim, not proof of native execution or menu boot.")
        return 0
    except RetailError as exc:
        print(f"progress: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
