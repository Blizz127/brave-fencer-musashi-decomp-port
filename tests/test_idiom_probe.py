from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import idiom_probe  # noqa: E402
from retail_common import RetailError  # noqa: E402


DIV = frozenset(("div", "divu"))


class WindowTests(unittest.TestCase):
    def test_window_starts_at_the_anchor(self) -> None:
        mnemonics = ["addiu", "div", "bnez", "nop", "break", "mflo"]
        self.assertEqual(
            idiom_probe.idiom_windows(mnemonics, DIV, 4), [("div", "bnez", "nop", "break")]
        )

    def test_window_is_truncated_at_the_end_of_the_sequence(self) -> None:
        self.assertEqual(idiom_probe.idiom_windows(["div", "bnez"], DIV, 4), [("div", "bnez")])

    def test_every_anchor_yields_a_window(self) -> None:
        mnemonics = ["div", "bnez", "divu", "bnez"]
        self.assertEqual(
            idiom_probe.idiom_windows(mnemonics, DIV, 2),
            [("div", "bnez"), ("divu", "bnez")],
        )

    def test_absent_anchor_yields_nothing(self) -> None:
        self.assertEqual(idiom_probe.idiom_windows(["addiu", "lw"], DIV, 4), [])

    def test_zero_window_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            idiom_probe.idiom_windows(["div"], DIV, 0)


class RetailIdiomTests(unittest.TestCase):
    def test_windows_do_not_cross_function_boundaries(self) -> None:
        functions = {
            "a": ["div", "bnez"],
            "b": ["nop", "break"],
        }
        counted = idiom_probe.retail_idioms(functions, DIV, 4)
        # Without the boundary the window would read "div bnez nop break".
        self.assertEqual(dict(counted), {("div", "bnez"): 1})

    def test_identical_patterns_are_counted_together(self) -> None:
        functions = {
            "a": ["div", "bnez", "nop", "break"],
            "b": ["div", "bnez", "nop", "break"],
            "c": ["divu", "bnez", "nop", "break"],
        }
        counted = idiom_probe.retail_idioms(functions, DIV, 4)
        self.assertEqual(counted[("div", "bnez", "nop", "break")], 2)
        self.assertEqual(counted[("divu", "bnez", "nop", "break")], 1)


class ObjdumpTests(unittest.TestCase):
    SAMPLE = "\n".join(
        [
            "unit.o:     file format elf32-tradlittlemips",
            "",
            "Disassembly of section .text:",
            "",
            "00000000 <probe>:",
            "   0:\t0085001a \tdiv\tzero,a0,a1",
            "   4:\t14a00002 \tbnez\ta1,10 <probe+0x10>",
            "   8:\t00000000 \tnop",
            "   c:\t0007000d \tbreak\t0x7",
            "  10:\t00001012 \tmflo\tv0",
            "",
        ]
    )

    def test_parses_mnemonics_in_order(self) -> None:
        self.assertEqual(
            idiom_probe.parse_objdump_mnemonics(self.SAMPLE),
            ["div", "bnez", "nop", "break", "mflo"],
        )

    def test_headers_and_labels_are_skipped(self) -> None:
        mnemonics = idiom_probe.parse_objdump_mnemonics(self.SAMPLE)
        self.assertNotIn("file", mnemonics)
        self.assertNotIn("probe", mnemonics)

    def test_empty_disassembly_yields_nothing(self) -> None:
        self.assertEqual(idiom_probe.parse_objdump_mnemonics("no instructions here\n"), [])


class DominantTests(unittest.TestCase):
    def test_reports_the_most_common_prefix_and_its_share(self) -> None:
        windows = [
            ("div", "bnez", "nop", "break"),
            ("divu", "bnez", "nop", "break"),
            ("divu", "bnez", "nop", "mflo"),
        ]
        prefix, share = idiom_probe.dominant_prefix(windows, length=3)
        # Normalised on the anchor, two of three agree on bnez/nop after it.
        self.assertEqual(prefix, ("bnez", "nop", "break"))
        self.assertAlmostEqual(share, 2 / 3, places=6)

    def test_empty_input_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            idiom_probe.dominant_prefix([], length=3)


class VerdictTests(unittest.TestCase):
    def test_candidate_reproducing_the_prefix_is_a_match(self) -> None:
        verdict = idiom_probe.compare_idiom(
            candidate=["div", "bnez", "nop", "break", "mflo"],
            retail_prefix=("bnez", "nop", "break"),
            anchors=DIV,
        )
        self.assertTrue(verdict["reproduces"])
        self.assertEqual(verdict["candidate_prefix"], ("bnez", "nop", "break"))

    def test_candidate_with_a_different_expansion_is_not_a_match(self) -> None:
        verdict = idiom_probe.compare_idiom(
            candidate=["div", "mflo", "nop", "nop"],
            retail_prefix=("bnez", "nop", "break"),
            anchors=DIV,
        )
        self.assertFalse(verdict["reproduces"])
        self.assertEqual(verdict["candidate_prefix"], ("mflo", "nop", "nop"))

    def test_candidate_without_the_anchor_reports_absent(self) -> None:
        verdict = idiom_probe.compare_idiom(
            candidate=["addiu", "jr", "nop"],
            retail_prefix=("bnez", "nop", "break"),
            anchors=DIV,
        )
        self.assertFalse(verdict["reproduces"])
        self.assertIsNone(verdict["candidate_prefix"])


if __name__ == "__main__":
    unittest.main()
