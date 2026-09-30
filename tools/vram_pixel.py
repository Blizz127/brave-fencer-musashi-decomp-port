#!/usr/bin/env python3
"""PS1 VRAM pixel packing, and the render check's report format.

VRAM is 16 bits per pixel: red in bits 0-4, green in 5-9, blue in 10-14, and
bit 15 as the mask/semi-transparency flag. Pinning this matters because getting
the order backwards compares blue against red and still looks plausible.

The render target itself runs under a display and prints the pixels it sampled;
this module is the pure half, so the packing and the verdict are unit-tested
without needing a GPU.
"""

from __future__ import annotations

import re

from retail_common import RetailError

COMPONENT_MAX = 31
MASK_BIT = 0x8000

REPORT_RE = re.compile(r"^PIXEL\s+(?P<name>\w+)\s+0x(?P<value>[0-9a-fA-F]{1,4})\s*$")
REQUIRED_SAMPLES = ("inside", "outside")


def decode(value: int) -> tuple[int, int, int]:
    """Split a 16-bit VRAM pixel into 5-bit red, green and blue."""

    return (value & 0x1F, (value >> 5) & 0x1F, (value >> 10) & 0x1F)


def encode(red: int, green: int, blue: int) -> int:
    """Pack 5-bit components into a VRAM pixel, without the mask bit."""

    for name, component in (("red", red), ("green", green), ("blue", blue)):
        if not 0 <= component <= COMPONENT_MAX:
            raise RetailError(f"{name} component {component} is outside 0..{COMPONENT_MAX}")
    return red | (green << 5) | (blue << 10)


def from_rgb8(red: int, green: int, blue: int) -> int:
    """Pack an 8-bit colour, as passed to setRGB0, into a VRAM pixel.

    The GPU keeps the top five bits of each component, so 248 and 255 both
    become 31 and a test must compare against the narrowed value.
    """

    return encode(red >> 3, green >> 3, blue >> 3)


def parse_report(output: str) -> dict[str, int]:
    """Read the render target's `PIXEL <name> 0x<value>` lines."""

    samples: dict[str, int] = {}
    for line in output.splitlines():
        match = REPORT_RE.match(line.strip())
        if match:
            samples[match.group("name")] = int(match.group("value"), 16)
    missing = [name for name in REQUIRED_SAMPLES if name not in samples]
    if missing:
        raise RetailError(
            f"render report is missing {', '.join(missing)}; got {sorted(samples) or 'nothing'}"
        )
    return samples


# Colours round-trip 8-bit -> 5-bit VRAM -> 8-bit GL and back, so an exact
# comparison is the wrong bar: red 248 came back as 241 on llvmpipe. Judging is
# a per-channel tolerance in 5-bit units.
#
# An earlier version asked for one dominant channel and near-zero others. That
# works for a red quad on blue and is wrong for anything else: the target model
# is authored in a beige, whose three channels are all substantial. A tolerance
# covers both cases without special-casing either.
CHANNEL_TOLERANCE = 3


def _matches(actual: int, expected: int) -> bool:
    got = decode(actual)
    want = decode(expected)
    return all(abs(got[channel] - want[channel]) <= CHANNEL_TOLERANCE for channel in range(3))


def judge(samples: dict[str, int], expect_inside: int, expect_outside: int) -> list[str]:
    """Return a problem per sample that does not show the expected colour.

    An empty list is a pass. The render target only samples and prints; this
    is where the verdict is made, so it stays unit-tested without a display.
    """

    problems = []
    for name, expected in (("inside", expect_inside), ("outside", expect_outside)):
        actual = samples[name]
        if not _matches(actual, expected):
            problems.append(
                f"{name}: expected rgb5 {decode(expected)} but sampled {decode(actual)} (0x{actual:04X})"
            )
    return problems


def parse_rgb8(text: str) -> tuple[int, int, int]:
    """Read an `R,G,B` triple of 8-bit components from the command line."""

    parts = [part.strip() for part in text.split(",")]
    if len(parts) != 3:
        raise RetailError(f"expected three comma-separated components, got {text!r}")
    values = []
    for part in parts:
        if not part.isdigit():
            raise RetailError(f"colour component {part!r} is not a number")
        value = int(part)
        if not 0 <= value <= 255:
            raise RetailError(f"colour component {value} is outside 0..255")
        values.append(value)
    return (values[0], values[1], values[2])
