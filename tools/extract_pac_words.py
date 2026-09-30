#!/usr/bin/env python3
"""Emit one SC01 member's decoded PAC code stream as a C word initializer.

`pc_port/mips_formatter.c` includes `sc01_pac<N>_words.inc` to build the
resident SC01 overlay images the formatter compares live guest RAM against:
member 0 is the New Game opening, member 1 the Allucaneet castle interior the
opening fades into. The include has no checked-in source: retail words are
never transcribed into decomp C files (tests/test_no_retail_words.py), so the
only honest input is the pinned disc archive.

SC01.CD is pinned by SHA-256 (`decode_title_pac.SC01_SHA256`); each member
must be a single code PAC (kind 4) with a zero-padded header sector, and its
decoded stream must equal `--expected-sha256`. Member 0 additionally goes
through `decode_title_pac.title_code`, which pins its extent and header.

A decoded stream can end a few bytes past its last whole instruction word
(member 0 runs 513935 bytes). Those bytes are dropped rather than zero-padded
into another word: padding would hand the formatter an instruction retail
never had, and the formatter only ever indexes below the array's size.
`--expected-words` still fails the configure step on any other drift.

This is extraction evidence, not a substitute for guest execution.
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from decode_title_pac import SC01_SHA256, decode, title_code  # noqa: E402

PAC_MAGIC = 0x00434150
PAC_CODE = 4


def member_code(archive: Path, member: int) -> bytes:
    if member == 0:
        return title_code(archive)
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != SC01_SHA256:
        raise SystemExit(f"{archive}: SC01 retail hash mismatch")
    sector, size = struct.unpack_from("<II", data, 8 + member * 8)
    base = sector * 2048
    if not sector or base + 0x800 > len(data) or base + size > len(data):
        raise SystemExit(f"{archive}: member {member} extent out of range")
    magic, kind, _field, length = struct.unpack_from("<4I", data, base)
    if magic != PAC_MAGIC or kind != PAC_CODE or not 0x800 < length <= size:
        raise SystemExit(f"{archive}: member {member} is not a single code PAC")
    if any(data[base + 0x10:base + 0x800]):
        raise SystemExit(f"{archive}: member {member} header padding is not zero")
    code, _consumed = decode(data[base + 0x800:base + length])
    return code


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path, help="pinned retail SC01.CD")
    parser.add_argument("output", type=Path)
    parser.add_argument("--member", type=int, default=0)
    parser.add_argument("--expected-words", type=int, required=True)
    parser.add_argument("--expected-sha256", required=True,
                        help="SHA-256 of the whole decoded stream")
    args = parser.parse_args()

    code = member_code(args.archive, args.member)
    digest = hashlib.sha256(code).hexdigest()
    if digest != args.expected_sha256:
        raise SystemExit(f"{args.archive}: member {args.member} decoded sha256 "
                         f"{digest}, expected {args.expected_sha256}")
    whole = len(code) // 4
    words = struct.unpack(f"<{whole}I", code[:whole * 4])
    if len(words) != args.expected_words:
        raise SystemExit(
            f"{args.archive}: member {args.member} decoded {len(words)} words, "
            f"expected {args.expected_words}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("".join(f"0x{word:08x}u,\n" for word in words))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
