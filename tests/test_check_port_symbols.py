from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import check_port_symbols  # noqa: E402
from retail_common import RetailError  # noqa: E402


class ForbiddenReferenceTests(unittest.TestCase):
    """LIBETC.C returns a callback pointer through an int, which truncates on
    x86-64. The build downgrades that error so PsyCross compiles, so nothing
    otherwise stops decomp code from storing and re-installing a corrupted
    pointer. This guard catches the reference instead."""

    def test_detects_a_forbidden_undefined_reference(self) -> None:
        nm = "                 U VSyncCallback\n0000000000000000 T musashi_port_init\n"
        self.assertEqual(
            check_port_symbols.find_forbidden_references(nm, {"VSyncCallback"}),
            ["VSyncCallback"],
        )

    def test_a_defined_symbol_of_the_same_name_is_not_a_reference(self) -> None:
        """Only undefined references matter; PsyCross defining it is expected."""

        nm = "0000000000000000 T VSyncCallback\n"
        self.assertEqual(check_port_symbols.find_forbidden_references(nm, {"VSyncCallback"}), [])

    def test_unrelated_undefined_symbols_are_ignored(self) -> None:
        nm = "                 U printf\n                 U rcos\n"
        self.assertEqual(
            check_port_symbols.find_forbidden_references(nm, {"VSyncCallback"}), []
        )

    def test_multiple_hits_are_sorted(self) -> None:
        nm = "                 U VSyncCallback\n                 U ResetCallback\n"
        self.assertEqual(
            check_port_symbols.find_forbidden_references(nm, {"ResetCallback", "VSyncCallback"}),
            ["ResetCallback", "VSyncCallback"],
        )

    def test_empty_output_is_clean(self) -> None:
        self.assertEqual(check_port_symbols.find_forbidden_references("", {"X"}), [])


class DefaultsTests(unittest.TestCase):
    def test_both_truncating_callbacks_are_guarded_by_default(self) -> None:
        self.assertEqual(
            check_port_symbols.TRUNCATING_CALLBACKS, frozenset({"ResetCallback", "VSyncCallback"})
        )

    def test_vsync_exception_is_opt_in_and_keeps_reset_callback_guarded(self) -> None:
        self.assertEqual(
            check_port_symbols.forbidden_callbacks(True), frozenset({"ResetCallback"})
        )
        self.assertEqual(
            check_port_symbols.forbidden_callbacks(False), check_port_symbols.TRUNCATING_CALLBACKS
        )


class CliTests(unittest.TestCase):
    def test_missing_object_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(RetailError):
                check_port_symbols.check_objects([Path(temp) / "absent.o"], {"X"})


if __name__ == "__main__":
    unittest.main()
