from __future__ import annotations

import json
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import progress  # noqa: E402
from retail_common import RetailError  # noqa: E402


_next_vram = [0x80012AB0]


def entry(name: str, size: int, **kw):
    """A well-formed entry. Each gets a distinct vram, since repeats are refused."""
    _next_vram[0] += 0x10
    base = {
        "name": name,
        "vram": _next_vram[0],
        "size": size,
        "source": f"src/main/{name}.c",
        "region": "main",
    }
    base.update(kw)
    return base


class SummaryTests(unittest.TestCase):
    """Legacy aggregate keys count registered ranges, irrespective of recovery."""

    def test_counts_functions_and_bytes(self) -> None:
        summary = progress.summarise([entry("a", 0xC), entry("b", 0x20)])
        self.assertEqual(summary["function_count"], 2)
        self.assertEqual(summary["matched_bytes"], 0xC + 0x20)

    def test_groups_by_region(self) -> None:
        summary = progress.summarise(
            [entry("a", 4, region="main"), entry("b", 4, region="main_0012")]
        )
        self.assertEqual(summary["by_region"], {"main": 1, "main_0012": 1})

    def test_entry_without_a_region_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            progress.summarise([{"name": "a", "vram": 0, "size": 4, "source": "x.c"}])

    def test_empty_registry_summarises_to_zero(self) -> None:
        summary = progress.summarise([])
        self.assertEqual(summary["function_count"], 0)
        self.assertEqual(summary["matched_bytes"], 0)

    def test_a_non_positive_size_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            progress.summarise([entry("a", 0, region="main")])

    def test_duplicate_addresses_are_refused(self) -> None:
        """The same function counted twice would inflate progress."""
        with self.assertRaises(RetailError):
            progress.summarise([entry("a", 4, vram=0x80001000), entry("b", 4, vram=0x80001000)])


class AssemblyCategoryTests(unittest.TestCase):
    def test_overlap_and_fake_matches_have_honest_subsets(self):
        matches = [
            entry("c", 16, vram=0x80010000, recovery="c", extent="function"),
            entry("asm", 16, vram=0x80010008, recovery="assembly", extent="function"),
            entry("fake", 64, vram=0x80010100, recovery="c", extent="function", fakematch=True),
        ]
        summary = progress.summarise(matches)
        self.assertEqual(summary["unique_c_bytes"], 16)
        self.assertEqual(summary["unique_assembly_bytes"], 16)
        self.assertEqual(summary["unique_qualifying_bytes"], 24)
        self.assertEqual(summary["fakematch_count"], 1)
        self.assertEqual(summary["fakematch_bytes"], 64)

    def test_fake_tag_must_be_boolean(self):
        with self.assertRaises(RetailError):
            progress.summarise([entry("fake", 16, fakematch="false")])

    def test_fake_assembly_is_also_excluded(self):
        summary = progress.summarise([
            entry("fake", 64, recovery="assembly", extent="function", fakematch=True),
        ])
        self.assertEqual(summary["unique_qualifying_bytes"], 0)
        self.assertEqual(summary["unique_assembly_bytes"], 0)


class LoadTests(unittest.TestCase):
    def test_reads_a_registry_file(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "matches.json"
            path.write_text(json.dumps({"schema": 1, "matches": [entry("a", 4, region="main")]}))
            self.assertEqual(len(progress.load_registry(path)), 1)

    def test_missing_registry_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            progress.load_registry(Path("/nonexistent/matches.json"))

    def test_a_registry_without_a_matches_list_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "matches.json"
            path.write_text(json.dumps({"schema": 1}))
            with self.assertRaises(RetailError):
                progress.load_registry(path)


class RecoveryTests(unittest.TestCase):
    def test_old_entries_are_unclassified_even_with_c_filename(self) -> None:
        summary = progress.summarise([entry("old", 32)])
        self.assertEqual(summary["by_recovery"], {"unclassified": 1})
        self.assertEqual(summary["by_extent"], {"unclassified": 1})

    def test_source_kind_and_range_extent_are_independent(self) -> None:
        summary = progress.summarise([
            entry("leaf", 12, recovery="c", extent="function"),
            entry("crt", 112, recovery="assembly", extent="function"),
            entry("prefix", 24, recovery="mixed", extent="partial"),
        ])
        self.assertEqual(summary["by_recovery"], {"c": 1, "assembly": 1, "mixed": 1})
        self.assertEqual(summary["bytes_by_recovery"], {"c": 12, "assembly": 112, "mixed": 24})
        self.assertEqual(summary["by_extent"], {"function": 2, "partial": 1})

    def test_unknown_classifications_are_refused(self) -> None:
        for field, value in (("recovery", "probably-c"), ("extent", "whole-game"),
                             ("recovery", None), ("extent", [])):
            with self.subTest(field=field, value=value), self.assertRaises(RetailError):
                progress.summarise([entry("bad", 12, **{field: value})])

    def test_cli_does_not_promote_registry_claims_to_verified_c(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "matches.json"
            path.write_text(json.dumps({"matches": [entry("fragment", 4)]}))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(progress.main(["--registry", str(path)]), 0)
            text = output.getvalue()
            self.assertIn("REGISTERED 1 ranges", text)
            self.assertIn("unclassified", text)
            self.assertIn("verify_registry.py", text)
            self.assertNotIn("functions rebuilt from C", text)
            self.assertNotIn("substantive", text)


class TrivialityTests(unittest.TestCase):
    """A count that treats a two-instruction stub as a function will mislead.

    Most functions matched early are setters, getters and empty stubs. They are
    genuine matches, but reporting only a total invites the same inflation the
    rr-decomp retraction was about, so stubs are counted separately.
    """

    def test_a_two_instruction_function_is_trivial(self) -> None:
        summary = progress.summarise([entry("stub", 8)])
        self.assertEqual(summary["trivial_count"], 1)
        self.assertEqual(summary["substantive_count"], 0)

    def test_a_larger_function_is_substantive(self) -> None:
        summary = progress.summarise([entry("real", 0x58)])
        self.assertEqual(summary["trivial_count"], 0)
        self.assertEqual(summary["substantive_count"], 1)

    def test_the_threshold_is_explicit(self) -> None:
        self.assertEqual(progress.TRIVIAL_MAX_BYTES, 8)

    def test_counts_partition_the_total(self) -> None:
        summary = progress.summarise([entry("a", 8), entry("b", 0x20), entry("c", 8)])
        self.assertEqual(
            summary["trivial_count"] + summary["substantive_count"], summary["function_count"]
        )


class ClassifyRecoveryTests(unittest.TestCase):
    """Promotion-time labels come from the PS1 implementation, not the filename."""

    def test_ordinary_c_is_c(self) -> None:
        source = "int func_80012AB0(int a, int b) { return (a - b) & 0xFFF; }\n"
        self.assertEqual(progress.classify_recovery(source), "c")

    def test_empty_stub_is_still_c(self) -> None:
        self.assertEqual(progress.classify_recovery("void func_x(void) {}\n"), "c")

    def test_native_export_without_c_body_is_unclassified(self) -> None:
        source = (
            "#ifdef MUSASHI_NATIVE_MIPS_WORD_EXPORT\n"
            "MUSASHI_NATIVE_MIPS_WORD(0x03e00008)\n"
            "#else\n"
            "/* No matched C implementation is claimed for this export. */\n"
            "#endif\n"
        )
        self.assertEqual(progress.classify_recovery(source), "unclassified")

    def test_empty_register_constraint_stays_c(self) -> None:
        source = (
            "u8 func_80014CF8(s32 port, s32 axis) {\n"
            '    __asm__("" : "=r"(record), "=r"(axis) : "0"(record), "1"(axis));\n'
            "    return record[0x32];\n"
            "}\n"
        )
        self.assertEqual(progress.classify_recovery(source), "c")

    def test_instruction_asm_overlay_is_assembly(self) -> None:
        source = (
            '__asm__(\n'
            '    ".set noreorder\\n"\n'
            '    "jr $ra\\n"\n'
            '    "nop\\n"\n'
            ');\n'
        )
        self.assertEqual(progress.classify_recovery(source), "assembly")

    def test_verbatim_word_dump_is_unclassified_not_assembly(self) -> None:
        """Retail bytes pasted as .word are transcription, not decompilation."""

        source = (
            '__asm__(\n'
            '    ".set noreorder\\n"\n'
            '    "func_x:\\n"\n'
            '    ".word 0x3C028007\\n"\n'
            '    ".word 0x24424750\\n"\n'
            '    ".word 0x03E00008\\n"\n'
            '    ".word 0x00000000\\n"\n'
            ');\n'
        )
        self.assertEqual(progress.classify_recovery(source), "unclassified")

    def test_gte_mnemonic_assembly_stays_assembly(self) -> None:
        source = (
            '__asm__(\n'
            '    "ctc2 $t0, $0\\n"\n'
            '    "mvmva 0, 0, 0, 3, 0\\n"\n'
            '    "jr $ra\\n"\n'
            '    "nop\\n"\n'
            ');\n'
        )
        self.assertEqual(progress.classify_recovery(source), "assembly")

    def test_native_word_export_does_not_turn_c_into_assembly(self) -> None:
        source = (
            "#ifdef MUSASHI_NATIVE_MIPS_WORD_EXPORT\n"
            "MUSASHI_NATIVE_MIPS_WORD(0x03e00008)\n"
            "MUSASHI_NATIVE_MIPS_WORD(0x00000000)\n"
            "#endif\n"
            "void func_800291A0(s32 selector, s32 value) { D_800BA1B8[selector] = value; }\n"
        )
        self.assertEqual(progress.classify_recovery(source), "c")

    def test_c_in_native_export_else_branch_is_c(self) -> None:
        source = (
            "#ifdef MUSASHI_NATIVE_MIPS_WORD_EXPORT\n"
            "MUSASHI_NATIVE_MIPS_WORD(0x03e00008)\n"
            "#else\n"
            "void func_80010A98(void) { func_800595FC(); }\n"
            "#endif\n"
        )
        self.assertEqual(progress.classify_recovery(source), "c")

    def test_c_body_plus_instruction_asm_is_mixed(self) -> None:
        source = (
            "int func_x(int a) {\n"
            "    if (a) return a;\n"
            '    __asm__("jr $ra\\nnop\\n");\n'
            "}\n"
        )
        self.assertEqual(progress.classify_recovery(source), "mixed")


class FillMissingClassificationsTests(unittest.TestCase):
    def test_fills_c_function_from_source_and_splat_size(self) -> None:
        matches = [entry("leaf", 12)]
        sources = {matches[0]["source"]: "int leaf(int a, int b) { return a - b; }\n"}
        complete = {("main", matches[0]["vram"], 12)}
        filled = progress.fill_missing_classifications(
            matches, read_source=sources.__getitem__, complete_functions=complete
        )
        self.assertEqual(filled, 2)
        self.assertEqual(matches[0]["recovery"], "c")
        self.assertEqual(matches[0]["extent"], "function")

    def test_does_not_overwrite_reviewed_labels(self) -> None:
        matches = [entry("crt", 112, recovery="assembly", extent="function")]
        sources = {matches[0]["source"]: "int crt(void) { return 0; }\n"}
        filled = progress.fill_missing_classifications(
            matches, read_source=sources.__getitem__, complete_functions=set()
        )
        self.assertEqual(filled, 0)
        self.assertEqual(matches[0]["recovery"], "assembly")
        self.assertEqual(matches[0]["extent"], "function")

    def test_size_mismatch_stays_unclassified_extent(self) -> None:
        matches = [entry("prefix", 24)]
        sources = {matches[0]["source"]: "int prefix(void) { return 1; }\n"}
        filled = progress.fill_missing_classifications(
            matches, read_source=sources.__getitem__, complete_functions=set()
        )
        self.assertEqual(matches[0]["extent"], "unclassified")
        self.assertEqual(filled, 2)


class UniqueQualifyingCoverageTests(unittest.TestCase):
    """The 30% bar is unique union of reviewed complete C/assembly functions.

    Summing sizes double-counts overlapping vram spans. Counting every
    registry row treats stubs, prefixes, and unclassified drafts as
    recovered game code. The shipped reporter has to refuse both.
    """

    def test_identified_code_bytes_are_the_split_regions(self) -> None:
        self.assertEqual(progress.IDENTIFIED_REGION_BYTES["main"], 411648)
        self.assertEqual(progress.IDENTIFIED_REGION_BYTES["main_0007"], 9600)
        self.assertEqual(progress.IDENTIFIED_REGION_BYTES["main_0003"], 18084)
        self.assertEqual(progress.IDENTIFIED_REGION_BYTES["main_0001"], 1516)
        self.assertEqual(progress.IDENTIFIED_REGION_BYTES["main_0008"], 768)
        self.assertEqual(progress.IDENTIFIED_REGION_BYTES["main_0009"], 2436)
        self.assertEqual(progress.IDENTIFIED_REGION_BYTES["main_0010"], 17932)
        self.assertEqual(progress.IDENTIFIED_REGION_BYTES["main_0011"], 17284)
        self.assertEqual(progress.IDENTIFIED_REGION_BYTES["main_0012"], 385024)
        md = sum(v for k, v in progress.IDENTIFIED_REGION_BYTES.items()
                 if k.startswith("main_") and "0013" <= k[-4:] <= "0047")
        self.assertEqual(md, 100180)
        sc_md = sum(v for k, v in progress.IDENTIFIED_REGION_BYTES.items() if k.startswith("sc"))
        self.assertEqual(sc_md, 216372)
        self.assertEqual(
            progress.IDENTIFIED_CODE_BYTES,
            411648 + 1516 + 768 + 2436 + 18084 + 9600 + 17932 + 17284 + 385024 + md + sc_md,
        )
        self.assertEqual(progress.IDENTIFIED_CODE_BYTES, 1180844)
        self.assertEqual(progress.COVERAGE_THRESHOLD_BYTES, 354254)
        self.assertEqual(
            progress.COVERAGE_THRESHOLD_BYTES,
            (progress.IDENTIFIED_CODE_BYTES * 3 + 9) // 10,
        )

    def test_overlapping_ranges_in_one_region_count_once(self) -> None:
        matches = [
            entry("outer", 200, vram=0x80001000, recovery="c", extent="function"),
            entry("inner", 100, vram=0x80001040, recovery="c", extent="function"),
        ]
        self.assertEqual(progress.unique_qualifying_bytes(matches), 200)

    def test_adjacent_ranges_merge_without_a_gap(self) -> None:
        matches = [
            entry("a", 100, vram=0x80001000, recovery="c", extent="function"),
            entry("b", 50, vram=0x80001000 + 100, recovery="assembly", extent="function"),
        ]
        self.assertEqual(progress.unique_qualifying_bytes(matches), 150)

    def test_gapped_ranges_do_not_fill_the_hole(self) -> None:
        matches = [
            entry("a", 40, vram=0x80001000, recovery="c", extent="function"),
            entry("b", 40, vram=0x80001080, recovery="c", extent="function"),
        ]
        self.assertEqual(progress.unique_qualifying_bytes(matches), 80)

    def test_same_vram_in_different_regions_counts_separately(self) -> None:
        matches = [
            entry("exe", 64, vram=0x80001000, region="main", recovery="c", extent="function"),
            entry("ovl", 64, vram=0x80001000, region="main_0012", recovery="c", extent="function"),
        ]
        self.assertEqual(progress.unique_qualifying_bytes(matches), 128)

    def test_partial_unclassified_and_non_function_extents_are_excluded(self) -> None:
        matches = [
            entry("ok", 40, recovery="c", extent="function"),
            entry("prefix", 200, recovery="c", extent="partial"),
            entry("asm_partial", 80, recovery="assembly", extent="partial"),
            entry("mixed_fn", 120, recovery="mixed", extent="function"),
            entry("unknown_fn", 300, recovery="unclassified", extent="function"),
            entry("unknown_both", 400),
            entry("asm_ok", 16, recovery="assembly", extent="function"),
        ]
        self.assertEqual(progress.unique_qualifying_bytes(matches), 56)

    def test_a_sub_threshold_fixture_fails_the_30_percent_bar(self) -> None:
        matches = [
            entry(
                "short",
                progress.COVERAGE_THRESHOLD_BYTES - 1,
                recovery="c",
                extent="function",
            )
        ]
        coverage = progress.qualifying_coverage(matches)
        self.assertEqual(
            coverage["unique_qualifying_bytes"],
            progress.COVERAGE_THRESHOLD_BYTES - 1,
        )
        self.assertEqual(coverage["identified_bytes"], progress.IDENTIFIED_CODE_BYTES)
        self.assertFalse(coverage["meets_threshold"])

    def test_a_threshold_fixture_passes_the_30_percent_bar(self) -> None:
        matches = [
            entry(
                "enough",
                progress.COVERAGE_THRESHOLD_BYTES,
                recovery="c",
                extent="function",
            )
        ]
        coverage = progress.qualifying_coverage(matches)
        self.assertEqual(
            coverage["unique_qualifying_bytes"],
            progress.COVERAGE_THRESHOLD_BYTES,
        )
        self.assertTrue(coverage["meets_threshold"])

    def test_word_dump_assembly_labels_cannot_manufacture_the_threshold(self) -> None:
        """A registry row stamped assembly still does not count if the source is a .word dump.

        Coverage is a pure function of registry records; callers must store
        classify_recovery() output. A dump mislabeled assembly is refused
        when the test drives classify_recovery then unique_qualifying_bytes.
        """

        dump = (
            '__asm__(".word 0x03E00008\\n.word 0x00000000\\n");\n'
        )
        matches = [
            entry(
                "dump",
                progress.COVERAGE_THRESHOLD_BYTES,
                recovery=progress.classify_recovery(dump),
                extent="function",
            )
        ]
        coverage = progress.qualifying_coverage(matches)
        self.assertEqual(matches[0]["recovery"], "unclassified")
        self.assertEqual(coverage["unique_qualifying_bytes"], 0)
        self.assertFalse(coverage["meets_threshold"])

    def test_unclassified_bytes_cannot_manufacture_the_threshold(self) -> None:
        matches = [
            entry("bulk", progress.COVERAGE_THRESHOLD_BYTES + 1000),
        ]
        coverage = progress.qualifying_coverage(matches)
        self.assertEqual(coverage["unique_qualifying_bytes"], 0)
        self.assertFalse(coverage["meets_threshold"])

    def test_summarise_exposes_unique_qualifying_coverage(self) -> None:
        summary = progress.summarise([
            entry("ok", 40, recovery="c", extent="function"),
            entry("skip", 200, recovery="c", extent="partial"),
        ])
        self.assertEqual(summary["unique_qualifying_bytes"], 40)
        self.assertEqual(summary["identified_bytes"], 1180844)
        self.assertFalse(summary["meets_coverage_threshold"])

    def test_c_only_subset_excludes_assembly(self) -> None:
        matches = [
            entry("c_fn", 40, vram=0x80001000, recovery="c", extent="function"),
            entry("asm_fn", 100, vram=0x80002000, recovery="assembly", extent="function"),
            entry("mixed_fn", 60, vram=0x80003000, recovery="mixed", extent="function"),
        ]
        self.assertEqual(progress.unique_qualifying_bytes(matches), 140)
        self.assertEqual(
            progress.unique_qualifying_bytes(matches, progress.PORTABLE_RECOVERIES), 40
        )

    def test_same_vram_in_two_overlay_regions_is_allowed(self) -> None:
        summary = progress.summarise([
            entry("a", 40, vram=0x800CEF04, region="main_0003", recovery="c", extent="function"),
            entry("b", 40, vram=0x800CEF04, region="main_0011", recovery="c", extent="function"),
        ])
        self.assertEqual(summary["unique_qualifying_bytes"], 80)

    def test_scene_overlays_are_outside_the_headline(self) -> None:
        self.assertIn("sc01_0000", progress.SCENE_OVERLAY_REGION_BYTES)
        self.assertNotIn("sc01_0000", progress.IDENTIFIED_REGION_BYTES)
        matches = [entry("s", 40, region="sc01_0000", recovery="c", extent="function")]
        self.assertEqual(progress.unique_qualifying_bytes(matches), 0)
        self.assertEqual(
            progress.unique_qualifying_bytes(
                matches, progress.PORTABLE_RECOVERIES, progress.SCENE_OVERLAY_REGION_BYTES),
            40,
        )

    def test_regions_outside_the_denominator_do_not_count(self) -> None:
        matches = [
            entry("in", 40, recovery="c", extent="function"),
            entry("out", 100, region="main_0099", recovery="c", extent="function"),
        ]
        self.assertEqual(progress.unique_qualifying_bytes(matches), 40)

    def test_c_only_subset_unions_overlaps_per_region(self) -> None:
        matches = [
            entry("outer", 200, vram=0x80001000, recovery="c", extent="function"),
            entry("inner", 100, vram=0x80001040, recovery="c", extent="function"),
            entry("asm", 50, vram=0x80001000 + 200, recovery="assembly", extent="function"),
        ]
        coverage = progress.qualifying_coverage(matches)
        self.assertEqual(coverage["unique_qualifying_bytes"], 250)
        self.assertEqual(coverage["unique_c_bytes"], 200)

    def test_summarise_exposes_c_only_subset(self) -> None:
        summary = progress.summarise([
            entry("ok", 40, recovery="c", extent="function"),
            entry("asm_ok", 16, recovery="assembly", extent="function"),
        ])
        self.assertEqual(summary["unique_c_bytes"], 40)

    def test_cli_prints_both_qualifying_and_c_only_numbers(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "matches.json"
            path.write_text(json.dumps({"matches": [
                entry("ok", 40, vram=0x80001000, recovery="c", extent="function"),
                entry("asm_ok", 16, vram=0x80002000, recovery="assembly", extent="function"),
            ]}))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(progress.main(["--registry", str(path)]), 0)
            text = output.getvalue()
            self.assertIn("56 / 1,180,844", text)
            self.assertIn("Portable C-only subset", text)
            self.assertIn("40 / 1,180,844", text)

    def test_cli_prints_unique_qualifying_coverage_not_a_raw_percentage_of_all_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "matches.json"
            path.write_text(json.dumps({"matches": [
                entry("ok", 40, recovery="c", extent="function"),
                entry("noise", 400),
            ]}))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(progress.main(["--registry", str(path)]), 0)
            text = output.getvalue()
            self.assertIn("40 / 1,180,844", text)
            self.assertIn("354,254", text)
            self.assertNotIn("440 / 1,180,844", text)

    def test_every_registered_sc_region_is_counted_once(self) -> None:
        self.assertFalse(set(progress.SCENE_OVERLAY_REGION_BYTES) & set(progress.IDENTIFIED_REGION_BYTES))
        tables = set(progress.SCENE_OVERLAY_REGION_BYTES) | set(progress.IDENTIFIED_REGION_BYTES)
        registry = json.loads((ROOT / "provenance/matches.json").read_text())
        regions = {m["region"] for m in registry["matches"] if m["region"].startswith("sc")}
        self.assertEqual(regions - tables, set())

    def test_cli_names_sc_regions_missing_from_the_tables(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "matches.json"
            path.write_text(json.dumps({"matches": [
                entry("new", 40, region="sc99_0001", recovery="c", extent="function"),
            ]}))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(progress.main(["--registry", str(path)]), 0)
            self.assertIn("NOT COUNTED: 1 registered SC regions missing from the tables: sc99_0001",
                          output.getvalue())


if __name__ == "__main__":
    unittest.main()
