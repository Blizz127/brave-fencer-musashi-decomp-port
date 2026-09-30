from __future__ import annotations

import struct
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import build_candidate  # noqa: E402
from retail_common import RetailError  # noqa: E402


class ToolchainTests(unittest.TestCase):
    """The PSY-Q release an executable was built with implies its ASPSX version.

    Pairings come from the PSY-Q/compiler/ASPSX release table; getting the ASPSX
    version wrong reproduces the wrong macro expansions and silently prevents a
    match, so the mapping is pinned rather than passed in loosely.
    """

    def test_known_toolchain_carries_its_aspsx_version(self) -> None:
        self.assertEqual(build_candidate.resolve_toolchain("gcc-2.7.2-psx").aspsx_version, "2.56")
        self.assertEqual(build_candidate.resolve_toolchain("gcc-2.7.2-cdk-psx").aspsx_version, "2.67")

    def test_registry_spans_the_psyq_releases_either_side_of_1998(self) -> None:
        """Candidates must bracket the target, not just cluster on the guess.

        BFM's executable is dated 1998-08-25. Stopping the registry at 2.7.2
        would make a 2.7.2 result unfalsifiable, so the later releases the 1999
        Square titles used are included as well.
        """

        self.assertEqual(build_candidate.resolve_toolchain("gcc-2.6.0-psx").aspsx_version, "2.34")
        self.assertEqual(build_candidate.resolve_toolchain("gcc-2.8.0-psx").aspsx_version, "2.77")
        self.assertEqual(build_candidate.resolve_toolchain("gcc-2.8.1-psx").aspsx_version, "2.79")
        self.assertEqual(build_candidate.resolve_toolchain("gcc-2.91.66-psx").aspsx_version, "2.81")

    def test_unknown_toolchain_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            build_candidate.resolve_toolchain("clang-19")

    def test_available_names_are_reported_in_the_error(self) -> None:
        with self.assertRaises(RetailError) as caught:
            build_candidate.resolve_toolchain("nope")
        self.assertIn("gcc-2.7.2-psx", str(caught.exception))


class DefaultPathTests(unittest.TestCase):
    """Vendored tool locations are repo-relative, not derived from other options.

    maspsx used to be resolved as the toolchain root's sibling, so pointing
    --toolchain-root at a directory elsewhere silently broke the assembler
    lookup. The two are independent and resolve independently.
    """

    def test_maspsx_default_is_repo_relative(self) -> None:
        repo = Path("/somewhere/bfm")
        self.assertEqual(
            build_candidate.default_maspsx_path(repo),
            repo / "tools" / "maspsx" / "maspsx.py",
        )

    def test_maspsx_default_ignores_the_toolchain_root(self) -> None:
        repo = Path("/somewhere/bfm")
        first = build_candidate.default_maspsx_path(repo)
        # Resolving again must not depend on any toolchain-root state.
        self.assertEqual(first, build_candidate.default_maspsx_path(repo))
        self.assertNotIn("psyq", str(first))

    def test_include_dir_is_on_the_default_search_path(self) -> None:
        """Decompiled sources share PSX typedefs from include/.

        cpp runs with -nostdinc, so src/shared/types.h is unusable in decomp
        sources: it pulls in stdint.h. include/ carries freestanding typedefs
        instead, and must be searched without every caller passing -I.
        """

        repo = Path("/somewhere/bfm")
        self.assertIn(repo / "include", build_candidate.default_include_dirs(repo))

    def test_toolchain_root_default_is_repo_relative(self) -> None:
        repo = Path("/somewhere/bfm")
        self.assertEqual(
            build_candidate.default_toolchain_root(repo), repo / "tools" / "psyq"
        )


class GnuAssemblerTests(unittest.TestCase):
    def test_only_gnu_is_accepted(self) -> None:
        self.assertEqual(build_candidate.ASSEMBLERS, ("gnu",))
        with self.assertRaises(SystemExit):
            build_candidate.build_parser().parse_args(
                ["x.c", "--symbol", "f", "--output", "o", "--assembler", "aspsx"])

    def test_default_is_the_maspsx_path(self) -> None:
        args = build_candidate.build_parser().parse_args(["x.c", "--symbol", "f", "--output", "o"])
        self.assertIsNone(args.assembler)

    def test_set_volatile_markers_are_stripped(self) -> None:
        text = "\tlw\t$2,D_1\n\t#.set\tvolatile\n\tsw\t$3,4($5)\n\t#.set\tnovolatile\n\t.set\tvolatile\n"
        out = build_candidate.strip_set_volatile(text)
        self.assertNotIn("volatile", out)
        self.assertIn("lw\t$2,D_1", out)
        self.assertIn("sw\t$3,4($5)", out)

    def test_move_is_spelled_as_addu_for_gnu_as(self) -> None:
        """Retail encodes `move` as addu rd,rs,$0; current binutils would use or."""
        out = build_candidate.gnu_as_source("\tmove\t$16,$5\n\t#.set\tvolatile\n\tmove\t$4, $2\n")
        self.assertIn("addu\t$16,$5,$0", out)
        self.assertIn("addu\t$4,$2,$0", out)
        self.assertNotIn("move", out)
        self.assertNotIn("volatile", out)

    def test_gnu_as_command_uses_reordering_optimiser(self) -> None:
        cmd = build_candidate.gnu_as_command(Path("as"), Path("u.s"), Path("u.o"))
        for flag in ("-EL", "-G0", "-march=r3000", "-O2", "-no-pad-sections"):
            self.assertIn(flag, cmd)
        self.assertEqual(cmd[-3:], ["-o", "u.o", "u.s"])


class SymbolDefinitionTests(unittest.TestCase):
    """splat emits undefined symbols already in linker-assignment syntax.

    config/undefined_syms.auto.txt and undefined_funcs.auto.txt hold
    `name = 0xADDR;` lines, which feed a linker script directly. There are
    ~2700 of them, far past what --defsym arguments can carry, so they are
    rendered into a script instead.
    """

    def test_parses_a_hex_assignment(self) -> None:
        self.assertEqual(
            build_candidate.parse_symbol_definitions("func_80010204 = 0x80010204;\n"),
            {"func_80010204": 0x80010204},
        )

    def test_parses_many_assignments(self) -> None:
        text = "a = 0x1;\nb = 0x2;\nc = 0x3;\n"
        self.assertEqual(build_candidate.parse_symbol_definitions(text), {"a": 1, "b": 2, "c": 3})

    def test_blank_and_malformed_lines_are_skipped(self) -> None:
        text = "\n// a comment\ngarbage without equals\nd = 0x4;\ne = notanumber;\n"
        self.assertEqual(build_candidate.parse_symbol_definitions(text), {"d": 4})

    def test_trailing_semicolon_is_optional(self) -> None:
        self.assertEqual(build_candidate.parse_symbol_definitions("f = 0x5\n"), {"f": 5})

    def test_later_definition_wins(self) -> None:
        self.assertEqual(build_candidate.parse_symbol_definitions("g = 0x1;\ng = 0x2;\n"), {"g": 2})


class DerivedAddressTests(unittest.TestCase):
    """splat names carry their own address, which covers in-split callees.

    undefined_funcs.auto.txt only lists symbols splat could not place. A call
    to a function inside the split is undefined in the *object* but absent from
    that file, so its address has to come from the name.
    """

    def test_function_name_yields_its_address(self) -> None:
        self.assertEqual(build_candidate.derive_symbol_address("func_80042610"), 0x80042610)

    def test_data_name_yields_its_address(self) -> None:
        self.assertEqual(build_candidate.derive_symbol_address("D_800747C0"), 0x800747C0)

    def test_lowercase_hex_is_accepted(self) -> None:
        self.assertEqual(build_candidate.derive_symbol_address("func_80042610"), 0x80042610)
        self.assertEqual(build_candidate.derive_symbol_address("D_800747c0"), 0x800747C0)

    def test_a_name_without_an_embedded_address_yields_nothing(self) -> None:
        self.assertIsNone(build_candidate.derive_symbol_address("memcpy"))
        self.assertIsNone(build_candidate.derive_symbol_address("func_notahexnumber"))

    def test_a_wrong_length_address_is_rejected(self) -> None:
        """Eight hex digits exactly; anything else is not a splat address."""

        self.assertIsNone(build_candidate.derive_symbol_address("func_8004"))


class UndefinedSymbolTests(unittest.TestCase):
    def test_parses_undefined_symbols_from_nm(self) -> None:
        output = "         U func_80042610\n         U D_800747C0\n00000000 T func_80010938\n"
        self.assertEqual(
            build_candidate.parse_undefined_symbols(output), ["D_800747C0", "func_80042610"]
        )

    def test_no_undefined_symbols_yields_an_empty_list(self) -> None:
        self.assertEqual(build_candidate.parse_undefined_symbols("00000000 T only\n"), [])


class LinkCommandTests(unittest.TestCase):
    def test_link_selects_little_endian(self) -> None:
        """ld defaults to big-endian MIPS and rejects the object without -EL."""

        command = build_candidate.ld_command(
            Path("/usr/bin/mips-linux-gnu-ld"), Path("u.ld"), Path("u.o"), Path("u.elf")
        )
        self.assertIn("-EL", command)

    def test_link_uses_the_generated_script(self) -> None:
        command = build_candidate.ld_command(
            Path("/usr/bin/mips-linux-gnu-ld"), Path("u.ld"), Path("u.o"), Path("u.elf")
        )
        self.assertIn("-T", command)
        self.assertIn("u.ld", command)


class StaleOutputTests(unittest.TestCase):
    def test_failed_build_does_not_leave_a_stale_artifact(self) -> None:
        """A failed build must not leave an earlier candidate in place.

        match_function compares whatever file it is given. If a build fails and
        the previous run's bytes survive, the comparison silently reports on
        stale data — the exact false-pass this project exists to prevent.
        """

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            output = root / "candidate.bin"
            output.write_bytes(b"bytes from an earlier run")
            code = build_candidate.main(
                [
                    str(root / "absent.c"),
                    "--symbol",
                    "func_x",
                    "--toolchain",
                    "definitely-not-a-toolchain",
                    "--output",
                    str(output),
                ]
            )
            self.assertEqual(code, 2)
            self.assertFalse(output.exists())


class LinkerScriptTests(unittest.TestCase):
    @unittest.skipUnless(all(shutil.which("mips-linux-gnu-" + tool)
                            for tool in ("as", "ld", "objcopy")), "requires MIPS binutils")
    def test_read_only_table_placement_and_relocations(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            obj, elf, binary = (root / name for name in ("unit.o", "unit.elf", "table.bin"))
            script = root / "unit.ld"
            assembly = (".text\n.align 4\n.set noreorder\nprobe:\njr $31\nnop\n"
                        ".section .rodata\n.align 3\ntable:\n.word probe\n.word table\n")
            subprocess.run(["mips-linux-gnu-as", "-EL", "-march=r3000", "-o", str(obj)],
                           input=assembly, text=True, check=True, capture_output=True)
            for address in (0x800742B4, 0x80075000):
                script.write_text(build_candidate.render_linker_script(0x8005C640, {}, address))
                subprocess.run(build_candidate.ld_command(Path("mips-linux-gnu-ld"), script, obj, elf),
                               check=True, capture_output=True)
                subprocess.run(["mips-linux-gnu-objcopy", "-O", "binary", "--only-section=.rodata",
                                str(elf), str(binary)], check=True, capture_output=True)
                self.assertEqual(struct.unpack("<II", binary.read_bytes()), (0x8005C640, address))

    def test_invalid_read_only_addresses_are_rejected(self) -> None:
        for address in (-4, 0x100000000, 0x800742B5):
            with self.assertRaises(build_candidate.RetailError):
                build_candidate.render_linker_script(0x8005C640, {}, address)

    @unittest.skipUnless(shutil.which("mips-linux-gnu-as") and shutil.which("mips-linux-gnu-ld") and shutil.which("mips-linux-gnu-objcopy"), "requires MIPS binutils")
    def test_internal_jump_uses_exact_word_aligned_retail_base(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            obj, elf, binary = (root / name for name in ("unit.o", "unit.elf", "unit.bin"))
            script = root / "unit.ld"
            assembly = ".text\n.align 4\n.set noreorder\n.globl probe\nprobe:\nj target\nnop\ntarget:\njr $31\nnop\n"
            subprocess.run(["mips-linux-gnu-as", "-EL", "-march=r3000", "-o", str(obj)], input=assembly, text=True, check=True, capture_output=True)
            for base in (0x80010178, 0x80012E6C, 0x80010000):
                with self.subTest(base=hex(base)):
                    script.write_text(build_candidate.render_linker_script(base, {}))
                    subprocess.run(build_candidate.ld_command(Path("mips-linux-gnu-ld"), script, obj, elf), check=True, capture_output=True)
                    subprocess.run(["mips-linux-gnu-objcopy", "-O", "binary", "--only-section=.text", str(elf), str(binary)], check=True, capture_output=True)
                    jump = struct.unpack_from("<I", binary.read_bytes())[0]
                    target = ((base + 4) & 0xF0000000) | ((jump & 0x03FFFFFF) << 2)
                    self.assertEqual(target, base + 8)

    def test_candidate_definition_does_not_override_object_text_symbol(self) -> None:
        definitions = {"func_801281D8": 0x801281D8, "callee": 0x8013E67C}
        build_candidate.discard_candidate_definition(definitions, "func_801281D8")
        self.assertNotIn("func_801281D8", definitions)
        self.assertEqual(definitions["callee"], 0x8013E67C)

    def test_places_text_at_the_function_address(self) -> None:
        script = build_candidate.render_linker_script(0x80012E6C, {})
        self.assertIn("0x80012E6C", script)
        self.assertIn(".text", script)

    def test_defines_every_supplied_symbol(self) -> None:
        script = build_candidate.render_linker_script(0x80010000, {"sym": 0x80074750})
        self.assertIn("sym = 0x80074750;", script)

    def test_symbols_are_emitted_before_sections(self) -> None:
        """Assignments must precede SECTIONS or ld cannot resolve them."""

        script = build_candidate.render_linker_script(0x80010000, {"sym": 0x1})
        self.assertLess(script.index("sym = "), script.index("SECTIONS"))


class SymbolTableTests(unittest.TestCase):
    def test_parses_sized_text_symbols(self) -> None:
        output = "00000000 00000018 T add2\n00000018 00000040 T mul3\n"
        symbols = build_candidate.parse_nm_symbols(output)
        self.assertEqual(symbols["add2"], (0x0, 0x18))
        self.assertEqual(symbols["mul3"], (0x18, 0x40))

    def test_symbol_without_a_size_is_recorded_as_unsized(self) -> None:
        symbols = build_candidate.parse_nm_symbols("00000000 T bare\n")
        self.assertEqual(symbols["bare"], (0x0, None))

    def test_non_text_symbols_are_ignored(self) -> None:
        output = (
            "00000000 00000018 T keep\n"
            "00000000 00000004 D data_sym\n"
            "         U undefined_sym\n"
            "00000000 00000008 B bss_sym\n"
        )
        symbols = build_candidate.parse_nm_symbols(output)
        self.assertEqual(list(symbols), ["keep"])

    def test_local_text_symbols_are_kept(self) -> None:
        symbols = build_candidate.parse_nm_symbols("00000020 00000010 t static_helper\n")
        self.assertEqual(symbols["static_helper"], (0x20, 0x10))

    def test_blank_and_malformed_lines_are_skipped(self) -> None:
        symbols = build_candidate.parse_nm_symbols("\n\ngarbage\n00000000 00000004 T ok\n")
        self.assertEqual(list(symbols), ["ok"])


class SliceTests(unittest.TestCase):
    SECTION = struct.pack("<8I", *range(8))

    def test_slices_the_symbol_range(self) -> None:
        got = build_candidate.slice_symbol(self.SECTION, 0x8, 0x8)
        self.assertEqual(got, struct.pack("<2I", 2, 3))

    def test_range_past_section_end_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            build_candidate.slice_symbol(self.SECTION, 0x18, 0x20)

    def test_zero_size_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            build_candidate.slice_symbol(self.SECTION, 0x0, 0)

    def test_unaligned_size_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            build_candidate.slice_symbol(self.SECTION, 0x0, 6)

    def test_unaligned_offset_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            build_candidate.slice_symbol(self.SECTION, 0x2, 0x4)


class SubprocessInvariantTests(unittest.TestCase):
    """Child stdin must be explicit, never inherited.

    maspsx picks its input source with sys.stdin.isatty(): with a non-tty stdin
    it calls sys.stdin.readlines() and blocks forever if the parent supplies no
    input. Inheriting stdin therefore makes the harness work interactively and
    hang under CI. Both helpers also carry a timeout so a hang fails loudly.
    """

    def test_run_closes_child_stdin_and_sets_a_timeout(self) -> None:
        with mock.patch.object(build_candidate.subprocess, "run") as runner:
            runner.return_value = subprocess.CompletedProcess([], 0, b"", b"")
            build_candidate._run(["/bin/true"], "probe")
        _, kwargs = runner.call_args
        self.assertEqual(kwargs["stdin"], subprocess.DEVNULL)
        self.assertIsNotNone(kwargs.get("timeout"))

    def test_capture_closes_child_stdin_and_sets_a_timeout(self) -> None:
        with mock.patch.object(build_candidate.subprocess, "run") as runner:
            runner.return_value = subprocess.CompletedProcess([], 0, "", "")
            build_candidate._capture(["/bin/true"], "probe")
        _, kwargs = runner.call_args
        self.assertEqual(kwargs["stdin"], subprocess.DEVNULL)
        self.assertIsNotNone(kwargs.get("timeout"))

    def test_timeout_is_reported_as_a_retail_error(self) -> None:
        with mock.patch.object(build_candidate.subprocess, "run") as runner:
            runner.side_effect = subprocess.TimeoutExpired(cmd=["x"], timeout=1)
            with self.assertRaises(RetailError) as caught:
                build_candidate._run(["/bin/true"], "probe")
        self.assertIn("timed out", str(caught.exception).lower())


class CommandTests(unittest.TestCase):
    def test_cc1_command_carries_optimisation_and_gp_flags(self) -> None:
        command = build_candidate.cc1_command(
            Path("/tc/cc1"), Path("in.i"), Path("out.s"), optimization="-O2", gp="-G0"
        )
        self.assertIn("-O2", command)
        self.assertIn("-G0", command)
        self.assertIn("-mips1", command)
        self.assertIn("-mcpu=3000", command)
        self.assertEqual(command[0], "/tc/cc1")

    def test_maspsx_command_pins_the_aspsx_version_and_runs_the_assembler(self) -> None:
        command = build_candidate.maspsx_command(
            Path("/m/maspsx.py"),
            Path("in.s"),
            Path("out.o"),
            aspsx_version="2.56",
            gnu_as=Path("/usr/bin/mips-linux-gnu-as"),
        )
        self.assertIn("--aspsx-version=2.56", command)
        self.assertIn("--run-assembler", command)
        self.assertIn("-EL", command)

    def test_maspsx_expands_division_by_default(self) -> None:
        """Retail guards every division, so the guard must be emitted by default.

        All 69 division sites in the retail image expand to bnez/nop/break, and
        no candidate emits that without --expand-div. Defaulting it off would
        make any function containing a division unmatchable.
        """

        command = build_candidate.maspsx_command(
            Path("/m/maspsx.py"),
            Path("in.s"),
            Path("out.o"),
            aspsx_version="2.56",
            gnu_as=Path("/usr/bin/mips-linux-gnu-as"),
        )
        self.assertIn("--expand-div", command)

    def test_division_expansion_can_be_disabled_explicitly(self) -> None:
        command = build_candidate.maspsx_command(
            Path("/m/maspsx.py"),
            Path("in.s"),
            Path("out.o"),
            aspsx_version="2.56",
            gnu_as=Path("/usr/bin/mips-linux-gnu-as"),
            expand_div=False,
        )
        self.assertNotIn("--expand-div", command)

    def test_cpp_command_defines_the_psx_target_macros(self) -> None:
        command = build_candidate.cpp_command(Path("/tc/cpp"), Path("in.c"), includes=[Path("/sdk")])
        self.assertIn("-undef", command)
        self.assertIn("-Dmips", command)
        self.assertIn("-I/sdk", command)


class SectionAddressTests(unittest.TestCase):
    """The linked image does not necessarily start at the requested base.

    ld honours the section's alignment, so requesting a base that is not
    sufficiently aligned bumps .text forward. Assuming the image starts at the
    requested base then slices at the wrong offset — and for a function large
    enough to still fit, it does so silently.
    """

    SAMPLE = "\n".join([
        "unit.elf:     file format elf32-tradlittlemips",
        "",
        "Sections:",
        "Idx Name          Size      VMA       LMA       File off  Algn",
        "  0 .text         00000008  8012bf50  8012bf50  00001000  2**3",
        "                  CONTENTS, ALLOC, LOAD, READONLY, CODE",
        "  1 .comment      00000012  00000000  00000000  00001008  2**0",
    ])

    def test_reads_the_actual_section_address(self) -> None:
        self.assertEqual(build_candidate.parse_section_address(self.SAMPLE, ".text"), 0x8012BF50)

    def test_a_missing_section_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            build_candidate.parse_section_address(self.SAMPLE, ".nosuch")

    def test_another_section_is_not_confused_for_text(self) -> None:
        self.assertEqual(build_candidate.parse_section_address(self.SAMPLE, ".comment"), 0)


if __name__ == "__main__":
    unittest.main()
