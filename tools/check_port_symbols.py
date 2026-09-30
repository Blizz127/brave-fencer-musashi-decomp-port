#!/usr/bin/env python3
"""Fail the build if decomp code calls a Psy-Q entry point that is unsafe here.

PsyCross's LIBETC.C returns a callback pointer through an int:

    int old = (int)vsync_callback;

That was fine on the 32-bit PlayStation and truncates on x86-64. The port build
downgrades the resulting error so PsyCross compiles at all, which means nothing
otherwise stops decomp-owned C from storing that value and installing it again
later as a corrupted pointer. The failure would be a wild jump at runtime, far
from its cause.

Calling ResetCallback or VSyncCallback is fine; it is their *return value* that
cannot be trusted. Distinguishing those at link time is not possible, so the
reference itself is refused and the exception is made deliberately, in the open.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from build_candidate import parse_undefined_symbols
from retail_common import RetailError

# Returns a pointer through an int; see the module docstring.
TRUNCATING_CALLBACKS = frozenset({"ResetCallback", "VSyncCallback"})

CHILD_TIMEOUT_SECONDS = 60


def forbidden_callbacks(allow_vsync_discard: bool) -> frozenset[str]:
    """Keep the VBlank-registration exception explicit at each call site."""

    if allow_vsync_discard:
        return TRUNCATING_CALLBACKS - {"VSyncCallback"}
    return TRUNCATING_CALLBACKS


def find_forbidden_references(nm_output: str, forbidden: frozenset[str] | set[str]) -> list[str]:
    """Return forbidden symbols this object *references* but does not define.

    A definition is not a problem: PsyCross is expected to define these. Only an
    undefined reference means our own code is calling one.
    """

    undefined = set(parse_undefined_symbols(nm_output))
    return sorted(undefined & set(forbidden))


def check_objects(paths: list[Path], forbidden: frozenset[str] | set[str]) -> list[tuple[Path, str]]:
    """Report every (object, forbidden symbol) reference across `paths`."""

    # These are host objects, so the host nm is correct; the cross nm is a fallback.
    nm = shutil.which("nm") or shutil.which("mips-linux-gnu-nm")
    if nm is None:
        raise RetailError("no nm found on PATH; cannot check port symbols")

    hits: list[tuple[Path, str]] = []
    for path in paths:
        if not path.is_file():
            raise RetailError(f"object not found: {path}")
        try:
            completed = subprocess.run(
                [nm, "--undefined-only", str(path)],
                check=False,
                capture_output=True,
                text=True,
                errors="replace",
                stdin=subprocess.DEVNULL,
                timeout=CHILD_TIMEOUT_SECONDS,
            )
        except subprocess.TimeoutExpired as exc:
            raise RetailError(f"nm timed out on {path}") from exc
        except OSError as exc:
            raise RetailError(f"failed to run nm on {path}: {exc}") from exc
        if completed.returncode != 0:
            raise RetailError(f"nm failed on {path}: {completed.stderr.strip()}")
        for symbol in find_forbidden_references(completed.stdout, forbidden):
            hits.append((path, symbol))
    return hits


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("objects", nargs="+", type=Path, help="decomp-owned objects or archives")
    parser.add_argument(
        "--allow-vsync-discard",
        action="store_true",
        help=("allow VSyncCallback only for the reviewed scheduler that discards "
              "its truncated return value"),
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        hits = check_objects(args.objects, forbidden_callbacks(args.allow_vsync_discard))
        if not hits:
            print(f"PORT SYMBOLS OK: {len(args.objects)} object(s), no truncating callback used")
            return 0
        print("PORT SYMBOLS REFUSED: a callback whose return value truncates on 64-bit", file=sys.stderr)
        for path, symbol in hits:
            print(f"  {path.name} references {symbol}", file=sys.stderr)
        print(
            "  Its return value is a pointer squeezed through an int and must not be\n"
            "  stored or reinstalled. See docs/PC-PORT.md.",
            file=sys.stderr,
        )
        return 1
    except RetailError as exc:
        print(f"check_port_symbols: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
