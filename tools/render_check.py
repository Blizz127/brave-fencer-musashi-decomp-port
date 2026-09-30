#!/usr/bin/env python3
"""Run a render target headless and judge the pixels it reports.

The render binary only samples and prints `PIXEL <name> 0x<value>` lines; the
verdict is made here, from tools/vram_pixel.py, so the comparison stays
unit-tested and the binary needs no assertions baked in.

Headless means xvfb-run with Mesa's software renderer. Without xvfb-run the
check cannot run and says so loudly, exiting with the code ctest treats as
SKIPPED rather than passing vacuously or failing spuriously.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import vram_pixel  # noqa: E402
from retail_common import RetailError  # noqa: E402

SKIP_EXIT_CODE = 77
CHILD_TIMEOUT_SECONDS = 180

# Defaults describe pc_port/render_quad.c: a red quad on a blue field. Other
# targets pass their own colours.
DEFAULT_INSIDE = "248,0,0"
DEFAULT_OUTSIDE = "0,0,255"


def run_headless(binary: Path, arguments: list[str] | None = None) -> str:
    xvfb = shutil.which("xvfb-run")
    if xvfb is None:
        raise RetailError("xvfb-run is not installed; the headless render check cannot run")
    env = dict(os.environ, LIBGL_ALWAYS_SOFTWARE="1")
    try:
        completed = subprocess.run(
            [xvfb, "-a", str(binary), *(arguments or [])],
            check=False,
            capture_output=True,
            text=True,
            errors="replace",
            stdin=subprocess.DEVNULL,
            timeout=CHILD_TIMEOUT_SECONDS,
            env=env,
        )
    except subprocess.TimeoutExpired as exc:
        raise RetailError(f"render target timed out after {CHILD_TIMEOUT_SECONDS}s") from exc
    if completed.returncode == SKIP_EXIT_CODE:
        raise RetailError(f"SKIP: render target exited {SKIP_EXIT_CODE} (no display or init failed)")
    if completed.returncode != 0:
        raise RetailError(
            f"render target exited {completed.returncode}: {completed.stderr.strip()[-400:]}"
        )
    return completed.stdout


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="the render target executable")
    parser.add_argument("--expect-inside", default=DEFAULT_INSIDE, help="R,G,B expected inside the shape")
    parser.add_argument("--expect-outside", default=DEFAULT_OUTSIDE, help="R,G,B expected for the clear colour")
    parser.add_argument("--argument", action="append", default=[], dest="arguments",
                        help="extra argument passed through to the render target, repeatable")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if shutil.which("xvfb-run") is None:
        print("RENDER CHECK SKIPPED: xvfb-run not installed, no headless display available", file=sys.stderr)
        return SKIP_EXIT_CODE
    try:
        if not args.binary.is_file():
            raise RetailError(f"render target not found: {args.binary}")
        expect_inside = vram_pixel.from_rgb8(*vram_pixel.parse_rgb8(args.expect_inside))
        expect_outside = vram_pixel.from_rgb8(*vram_pixel.parse_rgb8(args.expect_outside))
        output = run_headless(args.binary, args.arguments)
        samples = vram_pixel.parse_report(output)
        problems = vram_pixel.judge(samples, expect_inside, expect_outside)
        inside, outside = vram_pixel.decode(samples["inside"]), vram_pixel.decode(samples["outside"])
        if problems:
            print("RENDER CHECK FAILED", file=sys.stderr)
            for problem in problems:
                print(f"  {problem}", file=sys.stderr)
            return 1
        print(f"RENDER CHECK OK: inside rgb5={inside} outside rgb5={outside}")
        return 0
    except RetailError as exc:
        if str(exc).startswith("SKIP:"):
            print(f"{exc}", file=sys.stderr)
            return SKIP_EXIT_CODE
        print(f"render_check: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
