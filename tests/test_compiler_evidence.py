from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import identify_compiler  # noqa: E402


def asm(*lines: str) -> str:
    return "\n".join(lines) + "\n"


# Splat renders a delay-slot instruction with one extra leading space, but that is
# cosmetic. These fixtures keep the convention while the parser derives delay slots
# structurally from the preceding branch, so a formatting change cannot skew results.
FILLED_AND_EMPTY = asm(
    'glabel func_80010000',
    '    /* 800 80010000 0780023C */  lui        $v0, %hi(D_80074750)',
    '    /* 804 80010004 FCFF2014 */  bnez       $at, .L80010010',
    '    /* 808 80010008 21100000 */   addu       $v0, $zero, $zero',
    '    /* 80C 8001000C 0800E003 */  jr         $ra',
    '    /* 810 80010010 00000000 */   nop',
    '',
    'glabel func_80010020',
    '    /* 820 80010020 0800E003 */  jr         $ra',
    '    /* 824 80010024 00000000 */   nop',
)


class ParseTests(unittest.TestCase):
    def test_parses_each_glabel_into_its_own_function(self) -> None:
        functions = identify_compiler.parse_disassembly(FILLED_AND_EMPTY)
        self.assertEqual(list(functions), ["func_80010000", "func_80010020"])
        self.assertEqual(len(functions["func_80010000"]), 5)
        self.assertEqual(len(functions["func_80010020"]), 2)

    def test_records_vram_and_mnemonic(self) -> None:
        functions = identify_compiler.parse_disassembly(FILLED_AND_EMPTY)
        first = functions["func_80010000"][0]
        self.assertEqual(first.vram, 0x80010000)
        self.assertEqual(first.mnemonic, "lui")

    def test_ignores_local_branch_labels_and_directives(self) -> None:
        text = asm(
            '.set noreorder',
            'glabel func_80010000',
            '  .L80010010:',
            '    /* 800 80010000 00000000 */  nop',
        )
        functions = identify_compiler.parse_disassembly(text)
        self.assertEqual(list(functions), ["func_80010000"])
        self.assertEqual(len(functions["func_80010000"]), 1)


class DelaySlotTests(unittest.TestCase):
    def test_counts_filled_and_empty_delay_slots(self) -> None:
        functions = identify_compiler.parse_disassembly(FILLED_AND_EMPTY)
        measured = identify_compiler.measure_delay_slots(functions)
        # Three control transfers: bnez (filled), jr (nop), jr (nop).
        self.assertEqual(measured["total"], 3)
        self.assertEqual(measured["filled"], 1)
        self.assertEqual(measured["nop"], 2)

    def test_fill_rate_is_a_ratio_of_total(self) -> None:
        functions = identify_compiler.parse_disassembly(FILLED_AND_EMPTY)
        measured = identify_compiler.measure_delay_slots(functions)
        self.assertAlmostEqual(measured["fill_rate"], 1 / 3, places=6)

    def test_no_control_transfers_yields_zero_rate_without_dividing_by_zero(self) -> None:
        functions = identify_compiler.parse_disassembly(
            asm('glabel func_80010000', '    /* 800 80010000 00000000 */  nop')
        )
        measured = identify_compiler.measure_delay_slots(functions)
        self.assertEqual(measured["total"], 0)
        self.assertEqual(measured["fill_rate"], 0.0)


class ReturnDelaySlotTests(unittest.TestCase):
    """The aggregate fill rate does not discriminate compilers; the return slot does.

    GCC 2.8.1 fills the delay slot after `jr $ra`, 2.7.2 leaves it as a nop, so
    this is measured separately from branch delay slots rather than blended in.
    """

    RETURNS = asm(
        'glabel func_80010000',
        '    /* 800 80010000 0800E003 */  jr         $ra',
        '    /* 804 80010004 00000000 */   nop',
        '',
        'glabel func_80010020',
        '    /* 820 80010020 0800E003 */  jr         $ra',
        '    /* 824 80010024 21100000 */   addu       $v0, $zero, $zero',
        '',
        'glabel func_80010040',
        '    /* 840 80010040 08002003 */  jr         $t9',
        '    /* 844 80010044 00000000 */   nop',
    )

    def test_counts_only_returns_not_indirect_jumps(self) -> None:
        functions = identify_compiler.parse_disassembly(self.RETURNS)
        measured = identify_compiler.measure_return_delay_slots(functions)
        self.assertEqual(measured["total"], 2)
        self.assertEqual(measured["filled"], 1)
        self.assertEqual(measured["nop"], 1)
        self.assertAlmostEqual(measured["fill_rate"], 0.5, places=6)

    def test_no_returns_yields_zero_rate_without_dividing_by_zero(self) -> None:
        functions = identify_compiler.parse_disassembly(
            asm('glabel func_80010000', '    /* 800 80010000 00000000 */  nop')
        )
        measured = identify_compiler.measure_return_delay_slots(functions)
        self.assertEqual(measured["total"], 0)
        self.assertEqual(measured["fill_rate"], 0.0)

    def test_evidence_reports_return_slots_and_flags_the_aggregate_as_weak(self) -> None:
        functions = identify_compiler.parse_disassembly(self.RETURNS)
        evidence = identify_compiler.build_evidence(functions, toolchain_strings=[])
        self.assertEqual(evidence["metrics"]["return_delay_slots"]["total"], 2)
        caveats = " ".join(evidence["caveats"]).lower()
        self.assertIn("not diagnostic", caveats)


class IdiomTests(unittest.TestCase):
    def test_detects_div_break_zero_guard(self) -> None:
        functions = identify_compiler.parse_disassembly(
            asm(
                'glabel func_80010000',
                '    /* 800 80010000 1A004300 */  div        $zero, $v0, $v1',
                '    /* 804 80010004 0D000700 */  break      7',
            )
        )
        self.assertEqual(identify_compiler.count_div_zero_guards(functions), 1)

    def test_div_without_break_is_not_a_guard(self) -> None:
        functions = identify_compiler.parse_disassembly(
            asm(
                'glabel func_80010000',
                '    /* 800 80010000 1A004300 */  div        $zero, $v0, $v1',
                '    /* 804 80010004 12100000 */  mflo       $v0',
            )
        )
        self.assertEqual(identify_compiler.count_div_zero_guards(functions), 0)

    def test_detects_inline_gte_coprocessor_instructions(self) -> None:
        functions = identify_compiler.parse_disassembly(
            asm(
                'glabel func_80010000',
                '    /* 800 80010000 00000048 */  ctc2       $a0, $0',
                '    /* 804 80010004 0100004A */  rtps',
                '    /* 808 80010008 21100000 */  addu       $v0, $zero, $zero',
            )
        )
        self.assertEqual(identify_compiler.count_gte_instructions(functions), 2)

    def test_trailing_nop_padding_is_measured_per_function(self) -> None:
        functions = identify_compiler.parse_disassembly(
            asm(
                'glabel func_80010000',
                '    /* 800 80010000 0800E003 */  jr         $ra',
                '    /* 804 80010004 00000000 */   nop',
                '    /* 808 80010008 00000000 */  nop',
                '    /* 80C 8001000C 00000000 */  nop',
            )
        )
        histogram = identify_compiler.measure_trailing_padding(functions)
        # jr + its delay-slot nop, then two nops of alignment padding.
        self.assertEqual(histogram, {2: 1})


class EvidenceTests(unittest.TestCase):
    def test_evidence_never_names_a_compiler(self) -> None:
        functions = identify_compiler.parse_disassembly(FILLED_AND_EMPTY)
        evidence = identify_compiler.build_evidence(functions, toolchain_strings=[])
        self.assertEqual(evidence["status"], "unresolved")
        self.assertIsNone(evidence["compiler"])
        self.assertIn("no toolchain conclusion", evidence["reason"].lower())

    def test_evidence_records_the_string_scan_negative_result(self) -> None:
        functions = identify_compiler.parse_disassembly(FILLED_AND_EMPTY)
        evidence = identify_compiler.build_evidence(functions, toolchain_strings=[])
        self.assertEqual(evidence["toolchain_strings"], [])
        self.assertEqual(evidence["metrics"]["function_count"], 2)

    def test_evidence_stays_unresolved_even_when_strings_are_present(self) -> None:
        functions = identify_compiler.parse_disassembly(FILLED_AND_EMPTY)
        evidence = identify_compiler.build_evidence(functions, toolchain_strings=["GCC: (GNU) 2.7.2"])
        self.assertEqual(evidence["status"], "unresolved")
        self.assertIsNone(evidence["compiler"])
        self.assertEqual(evidence["toolchain_strings"], ["GCC: (GNU) 2.7.2"])


class FailClosedTests(unittest.TestCase):
    def _identity(self, root: Path, exe: Path) -> Path:
        path = root / "exe_identity.json"
        path.write_text(
            json.dumps(
                {
                    "status": "observed",
                    "path": str(exe),
                    "sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
                }
            ),
            encoding="utf-8",
        )
        return path

    def test_hash_mismatch_stops_before_writing_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            exe = root / "SLUS_007.26"
            exe.write_bytes(b"original-bytes")
            identity = self._identity(root, exe)
            exe.write_bytes(b"tampered-bytes-of-different-length")

            output = root / "compiler_evidence.json"
            result = identify_compiler.main(
                ["--identity", str(identity), "--disassembly", str(root / "missing.s"), "--output", str(output)]
            )
            self.assertEqual(result, 2)
            self.assertFalse(output.exists())

    def test_unresolved_identity_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            identity = root / "exe_identity.json"
            identity.write_text(json.dumps({"status": "unresolved", "path": None, "sha256": None}), encoding="utf-8")
            output = root / "compiler_evidence.json"
            result = identify_compiler.main(
                ["--identity", str(identity), "--disassembly", str(root / "missing.s"), "--output", str(output)]
            )
            self.assertEqual(result, 2)
            self.assertFalse(output.exists())

    def test_verified_exe_writes_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            exe = root / "SLUS_007.26"
            exe.write_bytes(b"original-bytes")
            identity = self._identity(root, exe)
            disassembly = root / "main.s"
            disassembly.write_text(FILLED_AND_EMPTY, encoding="utf-8")

            output = root / "compiler_evidence.json"
            result = identify_compiler.main(
                ["--identity", str(identity), "--disassembly", str(disassembly), "--output", str(output)]
            )
            self.assertEqual(result, 0)
            written = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(written["status"], "unresolved")
            self.assertEqual(written["metrics"]["function_count"], 2)


if __name__ == "__main__":
    unittest.main()
