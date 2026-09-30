#!/usr/bin/env python3
"""Compare a rebuilt byte range against the pinned retail executable.

This is the matching oracle. Every claim that a function matches has to come
from here: a build that merely succeeds proves nothing, and neither does a
disassembly that looks plausible.

The tool reports a verdict, a digest, and the location of the first differing
instruction word. It deliberately does not print retail bytes; inspecting them
while working is the job of a local differ, not of the provenance record.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path
from typing import Any

from retail_common import PSX_EXE_HEADER_SIZE, RetailError, load_json, sha256_file

WORD_SIZE = 4


def _text_bounds(header: dict[str, Any]) -> tuple[int, int]:
    try:
        text_address = int(header["text_address"])
        text_size = int(header["text_size_bytes"])
    except (KeyError, TypeError, ValueError) as exc:
        raise RetailError(f"executable header lacks a usable text segment: {exc}") from exc
    if text_size <= 0:
        raise RetailError(f"executable header reports a non-positive text size: {text_size}")
    return text_address, text_size


def vram_to_offset(vram: int, header: dict[str, Any]) -> int:
    """Translate a runtime address to a file offset in the PS-X EXE.

    The 2048-byte header precedes the text segment on disc, so text_address maps
    to offset 0x800.
    """

    text_address, text_size = _text_bounds(header)
    if vram < text_address:
        raise RetailError(f"vram 0x{vram:08X} precedes the text segment at 0x{text_address:08X}")
    if vram >= text_address + text_size:
        raise RetailError(
            f"vram 0x{vram:08X} is past the text segment end 0x{text_address + text_size:08X}"
        )
    return (vram - text_address) + PSX_EXE_HEADER_SIZE


def extract_retail_bytes(exe_path: Path, header: dict[str, Any], vram: int, size: int) -> bytes:
    """Read `size` bytes of retail text starting at runtime address `vram`."""

    if size <= 0:
        raise RetailError(f"size must be positive, got {size}")
    if size % WORD_SIZE:
        raise RetailError(f"size {size} is not a multiple of {WORD_SIZE}; MIPS instructions are words")
    if vram % WORD_SIZE:
        raise RetailError(f"vram 0x{vram:08X} is not word-aligned")

    text_address, text_size = _text_bounds(header)
    end = vram + size
    if end > text_address + text_size:
        raise RetailError(
            f"range 0x{vram:08X}+0x{size:X} overruns the text segment end "
            f"0x{text_address + text_size:08X}"
        )

    offset = vram_to_offset(vram, header)
    with exe_path.open("rb") as stream:
        stream.seek(offset)
        data = stream.read(size)
    if len(data) != size:
        raise RetailError(f"short read at offset 0x{offset:X}: wanted {size} bytes, got {len(data)}")
    return data


def extract_blob_bytes(blob: Path, base: int, vram: int, size: int) -> bytes:
    """Read `size` bytes from a flat blob loaded at `base`.

    Overlay code never appears in SLUS_007.26, so its addresses fall outside the
    executable's text range. A blob is addressed by its load base instead, with
    the same alignment and bounds refusals.
    """

    if size <= 0:
        raise RetailError(f"size must be positive, got {size}")
    if size % WORD_SIZE:
        raise RetailError(f"size {size} is not a multiple of {WORD_SIZE}; MIPS instructions are words")
    if vram % WORD_SIZE:
        raise RetailError(f"vram 0x{vram:08X} is not word-aligned")
    if vram < base:
        raise RetailError(f"vram 0x{vram:08X} precedes the load base 0x{base:08X}")

    blob_size = blob.stat().st_size
    offset = vram - base
    if offset + size > blob_size:
        raise RetailError(
            f"range 0x{vram:08X}+0x{size:X} overruns the blob, which ends at "
            f"0x{base + blob_size:08X}"
        )
    with blob.open("rb") as stream:
        stream.seek(offset)
        data = stream.read(size)
    if len(data) != size:
        raise RetailError(f"short read at offset 0x{offset:X}: wanted {size} bytes, got {len(data)}")
    return data


def compare_words(candidate: bytes, retail: bytes) -> dict[str, Any]:
    """Compare two equal-length ranges word by word.

    Lengths must agree exactly. Padding a short candidate to fit would
    manufacture a match, so it is refused instead.
    """

    if not candidate or not retail:
        raise RetailError("cannot compare an empty range")
    if len(candidate) != len(retail):
        raise RetailError(
            f"length mismatch: candidate {len(candidate)} bytes, retail {len(retail)} bytes"
        )
    if len(retail) % WORD_SIZE:
        raise RetailError(f"range length {len(retail)} is not a multiple of {WORD_SIZE}")

    total = len(retail) // WORD_SIZE
    matched = 0
    first_difference: int | None = None
    for index in range(total):
        start = index * WORD_SIZE
        if candidate[start : start + WORD_SIZE] == retail[start : start + WORD_SIZE]:
            matched += 1
        elif first_difference is None:
            first_difference = index
    return {
        "matching": matched == total,
        "total_words": total,
        "matched_words": matched,
        "first_difference_word": first_difference,
        "match_ratio": matched / total,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identity", type=Path, help="observed identity JSON (default: provenance/exe_identity.json)")
    parser.add_argument(
        "--retail-file",
        type=Path,
        help="compare against a flat blob instead of the pinned executable, for overlay code",
    )
    parser.add_argument("--base", help="load address of --retail-file, e.g. 0x800CEDF8")
    parser.add_argument("--sha256", help="pinned digest of --retail-file")
    parser.add_argument("--vram", required=True, help="function start address, e.g. 0x80010000")
    parser.add_argument("--size", required=True, help="function size in bytes, e.g. 0xA0")
    parser.add_argument("--candidate", type=Path, help="rebuilt bytes to compare; omit to summarise the range")
    return parser


def _resolve_exe(root: Path, identity: dict[str, Any]) -> Path:
    if identity.get("status") != "observed":
        raise RetailError(
            f"executable identity is {identity.get('status')!r}, not 'observed'; "
            "run identify_retail.py first"
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
    actual = sha256_file(exe_path)
    if actual.lower() != str(recorded_sha256).lower():
        raise RetailError(
            f"executable SHA-256 mismatch for {exe_path}: expected {recorded_sha256}, got {actual}"
        )
    return exe_path


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    identity_path = (args.identity or root / "provenance/exe_identity.json").resolve()
    try:
        try:
            vram = int(str(args.vram), 0)
            size = int(str(args.size), 0)
        except ValueError as exc:
            raise RetailError(f"could not parse --vram/--size: {exc}") from exc

        if args.retail_file is not None:
            # Blob mode, for overlay code. The blob is pinned by hash for the
            # same reason the executable is: an unpinned blob could be anything.
            if args.base is None or args.sha256 is None:
                raise RetailError("--retail-file requires both --base and --sha256")
            try:
                base = int(str(args.base), 0)
            except ValueError as exc:
                raise RetailError(f"could not parse --base: {exc}") from exc
            if not args.retail_file.is_file():
                raise RetailError(f"retail blob not found: {args.retail_file}")
            actual = sha256_file(args.retail_file)
            if actual.lower() != str(args.sha256).lower():
                raise RetailError(
                    f"blob SHA-256 mismatch for {args.retail_file}: expected {args.sha256}, got {actual}"
                )
            retail = extract_blob_bytes(args.retail_file, base, vram, size)
            source = f"{args.retail_file.name} @ 0x{base:08X}"
        else:
            identity = load_json(identity_path)
            exe_path = _resolve_exe(root, identity)
            header = identity.get("header")
            if not isinstance(header, dict):
                raise RetailError("executable identity lacks a header object")
            retail = extract_retail_bytes(exe_path, header, vram, size)
            source = f"{exe_path.name} @ file offset 0x{vram_to_offset(vram, header):X}"

        digest = hashlib.sha256(retail).hexdigest()

        if args.candidate is None:
            print(f"RANGE vram=0x{vram:08X} size=0x{size:X} words={size // WORD_SIZE}")
            print(f"  source={source}")
            print(f"  retail sha256={digest}")
            print("  no candidate supplied; nothing compared")
            return 0

        if not args.candidate.is_file():
            raise RetailError(f"candidate is not a file: {args.candidate}")
        report = compare_words(args.candidate.read_bytes(), retail)
        verdict = "MATCH" if report["matching"] else "MISMATCH"
        print(f"{verdict} vram=0x{vram:08X} size=0x{size:X}")
        print(
            f"  words {report['matched_words']}/{report['total_words']} "
            f"({report['match_ratio'] * 100:.2f}%)"
        )
        if not report["matching"]:
            index = report["first_difference_word"]
            print(f"  first difference at word {index} (vram 0x{vram + index * WORD_SIZE:08X})")
        print(f"  retail sha256={digest}")
        return 0 if report["matching"] else 1
    except RetailError as exc:
        print(f"match_function: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
