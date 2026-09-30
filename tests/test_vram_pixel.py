from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import vram_pixel  # noqa: E402
from retail_common import RetailError  # noqa: E402


class DecodeTests(unittest.TestCase):
    """PS1 VRAM is 16-bit: r in bits 0-4, g in 5-9, b in 10-14, mask in 15.

    The port's render check compares pixels it reads back, so the packing has to
    be pinned. Getting it backwards would compare blue against red and still
    look plausible.
    """

    def test_decodes_pure_red(self) -> None:
        self.assertEqual(vram_pixel.decode(0x001F), (31, 0, 0))

    def test_decodes_pure_green(self) -> None:
        self.assertEqual(vram_pixel.decode(0x03E0), (0, 31, 0))

    def test_decodes_pure_blue(self) -> None:
        self.assertEqual(vram_pixel.decode(0x7C00), (0, 0, 31))

    def test_ignores_the_mask_bit(self) -> None:
        self.assertEqual(vram_pixel.decode(0x801F), (31, 0, 0))

    def test_black_is_zero(self) -> None:
        self.assertEqual(vram_pixel.decode(0x0000), (0, 0, 0))


class EncodeTests(unittest.TestCase):
    def test_round_trips(self) -> None:
        for value in (0x0000, 0x001F, 0x03E0, 0x7C00, 0x1234):
            self.assertEqual(vram_pixel.encode(*vram_pixel.decode(value)), value & 0x7FFF)

    def test_eight_bit_components_are_narrowed_to_five(self) -> None:
        """A colour given to setRGB0 as 0-255 appears in VRAM as 0-31."""
        self.assertEqual(vram_pixel.from_rgb8(248, 0, 0), 0x001F)

    def test_out_of_range_component_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            vram_pixel.encode(32, 0, 0)


class RenderReportTests(unittest.TestCase):
    """The render target prints what it saw; the harness judges it."""

    def test_parses_a_pixel_report(self) -> None:
        out = "PIXEL inside 0x001F\nPIXEL outside 0x0000\n"
        self.assertEqual(
            vram_pixel.parse_report(out), {"inside": 0x001F, "outside": 0x0000}
        )

    def test_a_missing_sample_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            vram_pixel.parse_report("PIXEL inside 0x001F\n")

    def test_garbage_output_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            vram_pixel.parse_report("segfault\n")


class JudgeTests(unittest.TestCase):
    """The render target only samples; this decides pass or fail.

    Colours round-trip 8-bit -> 5-bit VRAM -> 8-bit GL, so an exact match is
    the wrong bar: red 248 came back as 241 on llvmpipe. The judge asks for the
    expected channel to dominate and the others to be near zero, in 5-bit units.
    """

    RED = vram_pixel.from_rgb8(248, 0, 0)
    BLUE = vram_pixel.from_rgb8(0, 0, 255)

    def test_correct_inside_and_outside_pass(self) -> None:
        samples = {"inside": vram_pixel.from_rgb8(241, 0, 0), "outside": self.BLUE}
        self.assertEqual(vram_pixel.judge(samples, self.RED, self.BLUE), [])

    def test_clear_colour_inside_the_quad_fails(self) -> None:
        samples = {"inside": self.BLUE, "outside": self.BLUE}
        problems = vram_pixel.judge(samples, self.RED, self.BLUE)
        self.assertEqual(len(problems), 1)
        self.assertIn("inside", problems[0])

    def test_quad_colour_bleeding_outside_fails(self) -> None:
        samples = {"inside": self.RED, "outside": self.RED}
        problems = vram_pixel.judge(samples, self.RED, self.BLUE)
        self.assertEqual(len(problems), 1)
        self.assertIn("outside", problems[0])

    def test_small_rounding_is_tolerated(self) -> None:
        nearly_red = vram_pixel.encode(29, 1, 1)
        self.assertEqual(vram_pixel.judge({"inside": nearly_red, "outside": self.BLUE}, self.RED, self.BLUE), [])

    def test_dim_red_is_not_red_enough(self) -> None:
        dim = vram_pixel.encode(12, 0, 0)
        self.assertEqual(len(vram_pixel.judge({"inside": dim, "outside": self.BLUE}, self.RED, self.BLUE)), 1)


class ColourArgTests(unittest.TestCase):
    """The harness serves more than one render target, each with its own colours."""

    def test_parses_an_r_g_b_triple(self) -> None:
        self.assertEqual(vram_pixel.parse_rgb8("240,197,152"), (240, 197, 152))

    def test_whitespace_is_tolerated(self) -> None:
        self.assertEqual(vram_pixel.parse_rgb8(" 0 , 0 , 255 "), (0, 0, 255))

    def test_a_component_out_of_range_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            vram_pixel.parse_rgb8("256,0,0")

    def test_a_malformed_triple_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            vram_pixel.parse_rgb8("240,197")

    def test_non_numeric_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            vram_pixel.parse_rgb8("red,0,0")


class ArbitraryColourTests(unittest.TestCase):
    """A model's authored colour is not a saturated primary.

    An earlier judge asked for one dominant channel and near-zero others, which
    is fine for a red quad and wrong for the beige the target model is authored
    in. Judging is a per-channel tolerance instead, which covers both.
    """

    BEIGE = vram_pixel.from_rgb8(240, 197, 152)
    BLUE = vram_pixel.from_rgb8(0, 0, 255)

    def test_a_beige_model_pixel_passes(self) -> None:
        sampled = vram_pixel.encode(29, 24, 18)   # what the GPU actually returned
        self.assertEqual(vram_pixel.judge({"inside": sampled, "outside": self.BLUE},
                                          self.BEIGE, self.BLUE), [])

    def test_the_clear_colour_where_the_model_should_be_fails(self) -> None:
        problems = vram_pixel.judge({"inside": self.BLUE, "outside": self.BLUE},
                                    self.BEIGE, self.BLUE)
        self.assertEqual(len(problems), 1)
        self.assertIn("inside", problems[0])

    def test_a_channel_beyond_tolerance_fails(self) -> None:
        off = vram_pixel.encode(30, 24, 8)        # blue channel far off
        self.assertEqual(len(vram_pixel.judge({"inside": off, "outside": self.BLUE},
                                              self.BEIGE, self.BLUE)), 1)

    def test_the_tolerance_is_explicit(self) -> None:
        self.assertEqual(vram_pixel.CHANNEL_TOLERANCE, 3)


if __name__ == "__main__":
    unittest.main()
