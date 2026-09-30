from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import verify_registry  # noqa: E402
from retail_common import RetailError  # noqa: E402

TARGETS = {
    "main": {"kind": "executable", "identity": "provenance/exe_identity.json"},
    "ov": {"kind": "blob", "file": "extracted/overlays/main/0007.bin", "base": 0x800CEDF8, "sha256": "ab" * 32},
}
MAIN_ENTRY = {"name": "func_80012AB0", "vram": 0x80012AB0, "size": 12, "region": "main", "source": "src/main/80012ab0.c"}
OV_ENTRY = {"name": "func_800CF3B0", "vram": 0x800CF3B0, "size": 88, "region": "ov", "source": "src/overlays/x.c"}


class PlanTests(unittest.TestCase):
    def test_build_links_at_the_function_address(self) -> None:
        build, _ = verify_registry.plan(MAIN_ENTRY, TARGETS, Path("/scratch/c.bin"))
        self.assertIn("--link-base", build)
        self.assertEqual(build[build.index("--link-base") + 1], "0x80012AB0")
        self.assertEqual(build[build.index("--symbol") + 1], "func_80012AB0")

    def test_optimization_defaults_when_the_entry_omits_it(self) -> None:
        """Every entry recorded before this field existed must still build.

        The identified default for the executable is -O2, so an entry with no
        recorded optimization is assumed to have used it.
        """

        build, _ = verify_registry.plan(MAIN_ENTRY, TARGETS, Path("/scratch/c.bin"))
        self.assertNotIn("--optimization", build)

    def test_optimization_is_passed_through_when_the_entry_records_one(self) -> None:
        """func_80010A98 matches only at -O0, not the identified -O2 default."""

        entry = {**MAIN_ENTRY, "optimization": "-O0"}
        build, _ = verify_registry.plan(entry, TARGETS, Path("/scratch/c.bin"))
        self.assertIn("--optimization=-O0", build)

    def test_executable_match_uses_no_blob_arguments(self) -> None:
        _, match = verify_registry.plan(MAIN_ENTRY, TARGETS, Path("/scratch/c.bin"))
        self.assertNotIn("--retail-file", match)
        self.assertEqual(match[match.index("--size") + 1], "0xC")
        self.assertEqual(match[match.index("--candidate") + 1], "/scratch/c.bin")

    def test_blob_match_carries_file_base_and_hash(self) -> None:
        _, match = verify_registry.plan(OV_ENTRY, TARGETS, Path("/scratch/c.bin"))
        self.assertEqual(match[match.index("--retail-file") + 1], "extracted/overlays/main/0007.bin")
        self.assertEqual(match[match.index("--base") + 1], "0x800CEDF8")
        self.assertEqual(match[match.index("--sha256") + 1], "ab" * 32)

    def test_default_entries_carry_no_assembler_argument(self) -> None:
        """An entry without the field builds through maspsx exactly as before."""

        build, _ = verify_registry.plan(MAIN_ENTRY, TARGETS, Path("/scratch/c.bin"))
        self.assertNotIn("--assembler", build)

    def test_gnu_assembler_round_trips_through_plan(self) -> None:
        """StopRCnt (8005D06C) is a Sony object GNU as assembled; the gate must say so."""

        entry = {**MAIN_ENTRY, "assembler": "gnu", "optimization": "-O2"}
        build, _ = verify_registry.plan(entry, TARGETS, Path("/scratch/c.bin"))
        self.assertEqual(build[build.index("--assembler") + 1], "gnu")
        self.assertIn("--optimization=-O2", build)
        args = verify_registry.build_candidate.build_parser().parse_args(build)
        self.assertEqual(args.assembler, "gnu")

    def test_an_unknown_assembler_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            verify_registry.plan({**MAIN_ENTRY, "assembler": "aspsx9"}, TARGETS, Path("/s"))

    def test_an_unknown_region_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            verify_registry.plan({**MAIN_ENTRY, "region": "nowhere"}, TARGETS, Path("/s"))

    def test_an_unknown_target_kind_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            verify_registry.plan(MAIN_ENTRY, {"main": {"kind": "hologram"}}, Path("/s"))


class QuietTests(unittest.TestCase):
    """A malformed argv must fail as one entry, not abort the whole sweep.

    argparse calls sys.exit() rather than returning on a bad argument list.
    An unquoted "-O0"-shaped optimization value hit exactly this once and
    silently killed the run with zero output.
    """

    def test_a_system_exit_becomes_a_normal_failure_code(self) -> None:
        def raises(argv):
            raise SystemExit(2)

        code, out = verify_registry._quiet(raises, [])
        self.assertEqual(code, 2)

    def test_output_before_the_exit_is_still_captured(self) -> None:
        def raises(argv):
            print("partial output")
            raise SystemExit(2)

        _, out = verify_registry._quiet(raises, [])
        self.assertIn("partial output", out)

    def test_a_non_integer_exit_code_still_counts_as_failure(self) -> None:
        def raises(argv):
            raise SystemExit("some usage message")

        code, _ = verify_registry._quiet(raises, [])
        self.assertNotEqual(code, 0)


if __name__ == "__main__":
    unittest.main()
