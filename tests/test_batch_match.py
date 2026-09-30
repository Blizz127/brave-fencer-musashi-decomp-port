from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import batch_match  # noqa: E402
from retail_common import RetailError  # noqa: E402


ASM = """\
.include "macro.inc"

.section .text, "ax"

glabel func_80012AB0
    /* 38B0 80012AB0 0800E003 */  jr         $ra
    /* 38B4 80012AB4 00000000 */   nop

glabel func_80012AB8
    /* 38B8 80012AB8 E8FFBD27 */  addiu      $sp, $sp, -0x18
    /* 38BC 80012ABC 0800E003 */  jr         $ra
    /* 38C0 80012AC0 00000000 */   nop
"""

FALLTHROUGH_LABEL = """\
.section .text, "ax"

glabel func_80012AF4
    /* 38F4 80012AF4 3412033C */  lui        $v1, 0x1234
    /* 38F8 80012AF8 78566394 */  lhu        $v1, 0x5678($v1)

glabel func_80012AFC
    /* 38FC 80012AFC 0800E003 */  jr         $ra
    /* 3900 80012B00 00000000 */   nop
"""

DIRECT_JUMP_THUNK = """\
.section .text, "ax"

glabel func_8001020C
    /* A0C 8001020C 53420008 */  j          func_8001094C
    /* A10 80010210 00000000 */   nop
"""


class EnumerateTests(unittest.TestCase):
    def test_completed_endlabel_excludes_alignment_padding(self) -> None:
        text = '''glabel first
/* 0 80010000 0800E003 */ jr $ra
/* 4 80010004 00000000 */ nop
endlabel first
/* 8 80010008 00000000 */ nop
glabel second
/* C 8001000C E8FFBD27 */ addiu $sp, $sp, -0x18
/* 10 80010010 0800E003 */ jr $ra
/* 14 80010014 00000000 */ nop
'''
        funcs = batch_match.enumerate_functions(text)
        self.assertEqual([(f.name, f.size) for f in funcs],
                         [("first", 8), ("second", 12)])

    def test_endlabel_before_delay_slot_does_not_truncate(self) -> None:
        text = DIRECT_JUMP_THUNK.replace(
            "    /* A10", "endlabel func_8001020C\n    /* A10")
        funcs = batch_match.enumerate_functions(text)
        self.assertEqual([(f.name, f.size) for f in funcs], [("func_8001020C", 8)])

    def test_label_in_jump_delay_slot_does_not_end_range(self) -> None:
        text = DIRECT_JUMP_THUNK.replace(
            "    /* A10", "glabel delay_slot\n    /* A10")
        funcs = batch_match.enumerate_functions(text)
        self.assertEqual([(f.name, f.size) for f in funcs], [("func_8001020C", 8)])

    def test_missing_jump_delay_slot_is_not_a_complete_function(self) -> None:
        text = DIRECT_JUMP_THUNK.split("    /* A10")[0]
        self.assertEqual(batch_match.enumerate_functions(text), [])

    def test_noncontiguous_instruction_cannot_supply_jump_delay_slot(self) -> None:
        text = DIRECT_JUMP_THUNK.replace("A10 80010210", "A14 80010214")
        self.assertEqual(batch_match.enumerate_functions(text), [])

    def test_forward_target_beyond_input_is_not_a_complete_function(self) -> None:
        text = '''glabel entry
/* 0 80010000 04008010 */ beqz $a0, missing_target
/* 4 80010004 00000000 */ nop
/* 8 80010008 0800E003 */ jr $ra
/* C 8001000C 00000000 */ nop
'''
        self.assertEqual(batch_match.enumerate_functions(text), [])

    def test_uncrossed_jr_ra_splits_even_without_a_second_glabel(self) -> None:
        """Frameless GTE helpers are often one splat label with many jr $ra tails."""

        text = """\
glabel func_8004D504
/* 0 8004D504 00000000 */ nop
/* 4 8004D508 0800E003 */ jr $ra
/* 8 8004D50C 00000000 */ nop
/* C 8004D510 00000000 */ nop
/* 10 8004D514 00008284 */ lh $v0, 0x0($a0)
/* 14 8004D518 0800E003 */ jr $ra
/* 18 8004D51C 00000000 */ nop
"""
        funcs = batch_match.enumerate_functions(text)
        self.assertEqual(
            [(f.name, f.vram, f.size) for f in funcs],
            [
                ("func_8004D504", 0x8004D504, 12),
                ("func_8004D514", 0x8004D514, 12),
            ],
        )

    def test_early_return_with_forward_branch_is_not_split(self) -> None:
        text = """\
glabel entry
/* 0 80010000 04008014 */ bnez $a0, body
/* 4 80010004 00000000 */ nop
/* 8 80010008 0800E003 */ jr $ra
/* C 8001000C 00000000 */ nop
glabel body
/* 10 80010010 21080000 */ addu $t0, $zero, $zero
/* 14 80010014 0800E003 */ jr $ra
/* 18 80010018 00000000 */ nop
"""
        funcs = batch_match.enumerate_functions(text)
        self.assertEqual([(f.name, f.size) for f in funcs], [("entry", 28)])

    def test_forward_branch_keeps_alternate_path_and_shared_return(self) -> None:
        text = '''glabel entry
/* 0 80010000 04008010 */ beqz $a0, shared_return
/* 4 80010004 00000000 */ nop
/* 8 80010008 05400008 */ j shared_return
glabel jump_delay
/* C 8001000C 00000000 */ nop
glabel alternate
/* 10 80010010 00000000 */ nop
alabel shared_return
/* 14 80010014 0800E003 */ jr $ra
/* 18 80010018 00000000 */ nop
glabel next_function
/* 1C 8001001C 0800E003 */ jr $ra
/* 20 80010020 00000000 */ nop
'''
        funcs = batch_match.enumerate_functions(text)
        self.assertEqual([(f.name, f.size) for f in funcs],
                         [("entry", 28), ("next_function", 8)])

    def test_functions_are_found_with_their_addresses(self) -> None:
        funcs = batch_match.enumerate_functions(ASM)
        self.assertEqual([f.name for f in funcs], ["func_80012AB0", "func_80012AB8"])
        self.assertEqual(funcs[0].vram, 0x80012AB0)

    def test_size_is_measured_from_the_instruction_words(self) -> None:
        funcs = batch_match.enumerate_functions(ASM)
        self.assertEqual(funcs[0].size, 8)
        self.assertEqual(funcs[1].size, 12)

    def test_a_file_with_no_functions_yields_nothing(self) -> None:
        self.assertEqual(batch_match.enumerate_functions(".section .text\n"), [])

    def test_a_fallthrough_split_is_coalesced_at_its_real_entry(self) -> None:
        """A fall-through label belongs to the preceding callable entry."""

        funcs = batch_match.enumerate_functions(FALLTHROUGH_LABEL)
        self.assertEqual([f.name for f in funcs], ["func_80012AF4"])
        self.assertEqual(funcs[0].size, 16)

    def test_m2c_input_rewrites_inner_glabels_as_alabels(self) -> None:
        function = batch_match.enumerate_functions(FALLTHROUGH_LABEL)[0]
        rewritten = batch_match.m2c_assembly_text(FALLTHROUGH_LABEL, function)
        self.assertIn("glabel func_80012AF4", rewritten)
        self.assertIn("alabel func_80012AFC", rewritten)

    def test_direct_jump_thunk_is_not_a_c_candidate(self) -> None:
        thunk = batch_match.enumerate_functions(DIRECT_JUMP_THUNK)[0]
        self.assertTrue(thunk.is_direct_jump_thunk)
        self.assertFalse(batch_match.should_attempt(Path("missing.c"), thunk))

    def test_internal_label_rewrite_preserves_instruction_lines(self) -> None:
        function = batch_match.enumerate_functions(FALLTHROUGH_LABEL)[0]
        for text in (FALLTHROUGH_LABEL, FALLTHROUGH_LABEL.replace("\n", "\r\n")):
            with self.subTest(crlf="\r\n" in text):
                rewritten = batch_match.m2c_assembly_text(text, function)
                self.assertEqual(rewritten, text.replace("glabel func_80012AFC", "alabel func_80012AFC"))


class SafetyTests(unittest.TestCase):
    """The harness must never write over a committed source.

    A previous batch overwrote a hand-written, verified source with m2c output,
    failed to build it, and deleted the file. Nothing noticed until the registry
    was re-verified. Refusing to write an existing path is the guard.
    """

    def test_an_existing_source_is_never_overwritten(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            target = Path(scratch) / "8012ab0.c"
            target.write_text("/* hand written and verified */\n")
            with self.assertRaises(RetailError):
                batch_match.promote(Path(scratch) / "candidate.c", target)
            self.assertEqual(target.read_text(), "/* hand written and verified */\n")

    def test_promotion_writes_a_new_path(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            produced = Path(scratch) / "candidate.c"
            produced.write_text("void f(void) {}\n")
            target = Path(scratch) / "new" / "8012ab0.c"
            batch_match.promote(produced, target)
            self.assertEqual(target.read_text(), "void f(void) {}\n")

    def test_an_existing_source_is_skipped_before_any_work(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            target = Path(scratch) / "8012ab0.c"
            target.write_text("x")
            self.assertFalse(batch_match.should_attempt(target))
            self.assertTrue(batch_match.should_attempt(Path(scratch) / "absent.c"))


class HeaderTests(unittest.TestCase):
    """A candidate must never claim verification it has not undergone.

    run_m2c used to stamp every candidate "verified byte-exact" at
    creation time, before the oracle ran. Failed and banked candidates
    kept the claim: 513 promotion-pending files in staging/ asserted a
    verification that never happened. The draft header now says
    unverified; only stamp_verified (called after an oracle MATCH)
    may assert verification."""

    STUB = "import sys; sys.stdout.write('void f(void) {}\\n')\n"

    def _run_m2c(self, scratch: Path) -> Path:
        stub = scratch / "stub_m2c.py"
        stub.write_text(self.STUB)
        out = scratch / "func.c"
        self.assertTrue(
            batch_match.run_m2c(stub, scratch / "x.s", "func", out)
        )
        return out

    def test_candidate_header_claims_no_verification(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            out = self._run_m2c(Path(scratch))
            text = out.read_text()
            self.assertNotIn("verified byte-exact", text)
            self.assertIn("NOT verified", text)

    def test_stamp_verified_replaces_the_draft_header(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            out = self._run_m2c(Path(scratch))
            batch_match.stamp_verified(out, "x.s")
            text = out.read_text()
            self.assertIn("verified byte-exact", text)
            self.assertNotIn("not verified", text)
            self.assertIn("void f(void) {}", text)

    def test_stamp_verified_prepends_when_no_draft_header(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            bare = Path(scratch) / "bare.c"
            bare.write_text("void f(void) {}\n")
            batch_match.stamp_verified(bare, "x.s")
            text = bare.read_text()
            self.assertIn("verified byte-exact", text)
            self.assertIn("void f(void) {}", text)


class SourcePathTests(unittest.TestCase):
    def test_a_main_function_maps_to_its_address(self) -> None:
        path = batch_match.source_path(Path("/repo"), "main", 0x80012AB0)
        self.assertEqual(path, Path("/repo/src/main/80012ab0.c"))

    def test_an_overlay_function_maps_under_its_region(self) -> None:
        path = batch_match.source_path(Path("/repo"), "main_0012", 0x8012AD3C)
        self.assertEqual(path, Path("/repo/src/overlays/main_0012/8012ad3c.c"))


class SanitizeTests(unittest.TestCase):
    """m2c's `?` placeholder for an unseen symbol is not valid C.

    m2c cannot always tell an uncalled data symbol from a called function, so
    the sanitizer decides from the body: a declared name that is never
    followed by `(` anywhere in the function is data, not a callee.
    """

    def test_a_called_name_keeps_its_function_declaration(self) -> None:
        out = batch_match._sanitize(
            "? func_80010204();                                  /* extern */\n\n"
            "void func_80010938(void) {\n    func_80010204();\n}\n"
        )
        self.assertIn("void func_80010204();", out)
        self.assertIn("func_80010204();\n}", out)

    def test_an_uncalled_name_becomes_extern_data(self) -> None:
        """The failure this guards: an unknown symbol only ever assigned to,
        never called, must not be declared as a function -- assigning to a
        function name is not a valid lvalue."""

        out = batch_match._sanitize(
            "? D_80074778();                                     /* static */\n\n"
            "void func_80010AE0(s32 arg0) {\n    D_80074778 = arg0;\n}\n"
        )
        self.assertIn("extern s32 D_80074778;", out)
        self.assertNotIn("D_80074778()", out)

    def test_unknown_pointer_parameter_becomes_void_star(self) -> None:
        out = batch_match._sanitize(
            "? func_80014070(? *);                               /* static */\n\n"
            "void func_800128B4(void) {\n    func_80014070(0);\n}\n"
        )
        self.assertIn("void func_80014070(void *);", out)

    def test_unknown_local_variable_type_becomes_s32(self) -> None:
        out = batch_match._sanitize(
            "void func_800128B4(void) {\n    ? sp10;\n\n    func_80014070(&sp10);\n}\n"
        )
        self.assertIn("    s32 sp10;", out)

    def test_unknown_argument_list_becomes_empty_parens(self) -> None:
        out = batch_match._sanitize(
            "? func_80042610(?);                                 /* static */\n\n"
            "void func_80010938(void) {\n    func_80042610(0);\n}\n"
        )
        self.assertIn("func_80042610()", out)
        self.assertNotIn("(?)", out)

    def test_the_call_site_itself_is_left_untouched(self) -> None:
        out = batch_match._sanitize("void func_80010938(void) {\n    func_80042610(0);\n}\n")
        self.assertIn("func_80042610(0)", out)

    def test_unknown_function_pointer_local_becomes_s32(self) -> None:
        """The 65-128 bucket failure: m2c emits `? (*name)()` locals for
        callbacks (e.g. func_800100A0's ctor-loop `? (*temp_t0)();`), which
        no existing rule matches, so cpp fails on every such function."""

        out = batch_match._sanitize(
            "void func_800100A0(void) {\n    ? (*temp_t0)();\n    temp_t0();\n}\n"
        )
        self.assertIn("s32 (*temp_t0)();", out)
        self.assertNotIn("? (*temp_t0)()", out)

    def test_unknown_extern_data_declaration_becomes_s32(self) -> None:
        """`extern ? D_*;` is the same register-width-unknown data case as
        `? name;` locals, just spelled as an extern declaration."""

        out = batch_match._sanitize("extern ? D_800AF630;\nvoid f(void) {\n}\n")
        self.assertIn("extern s32 D_800AF630;", out)
        self.assertNotIn("extern ? ", out)

    def test_anonymous_function_pointer_parameter_becomes_s32(self) -> None:
        """`? (*)()` with no name — an anonymous callback parameter — is
        the residual shape the 65-128 re-sanitize found (func_8001BC6C,
        func_8001534C, func_8012F568). The named rule's `(\w+)` cannot
        match it, and the `? *` rule needs a literal `*` after `?`."""

        out = batch_match._sanitize(
            "void func_8001BC6C(? (*)(), s32);\nvoid f(void) {\n}\n"
        )
        self.assertIn("void func_8001BC6C(s32 (*)(), s32);", out)
        self.assertNotIn("? (*)()", out)

    def test_named_unknown_parameter_becomes_s32(self) -> None:
        """`? arg1` — m2c names the parameter but not its type — is the
        common residual (func_80020248, func_80045C94, func_8012F14C and
        ~20 more). Same register-width default as the bare form."""

        out = batch_match._sanitize(
            "void func_80020248(? arg1, s32 arg2, ? arg3) {\n}\n"
        )
        self.assertIn("void func_80020248(s32 arg1, s32 arg2, s32 arg3) {", out)
        self.assertNotIn("? arg", out)

    def test_function_definition_with_unknown_return_becomes_s32(self) -> None:
        """`? (*name(void))() {` is m2c's spelling of a definition whose
        return it reads as a function pointer (func_80042C90 returns the
        address func_80042D60). The body returns a register value, so a
        plain s32 definition compiles and generates identical code."""

        out = batch_match._sanitize("? (*func_80042C90(void))() {\n}\n")
        self.assertIn("s32 func_80042C90(void) {", out)

    def test_callback_prototype_keeps_its_shape_with_s32_return(self) -> None:
        """`? (*f(s32))(void *);` declares a callback factory (func_80131CA8
        stores the result in `s32 (*)()` and calls through it). Only the
        `?` becomes s32; the shape stays, so the call through the
        returned pointer is unchanged."""

        out = batch_match._sanitize(
            "? (*func_80131CF4(s32))(void *);\nvoid f(void) {\n}\n"
        )
        self.assertIn("s32 (*func_80131CF4(s32))(void *);", out)
        self.assertNotIn("? (*func_80131CF4", out)

    def test_null_macro_becomes_zero(self) -> None:
        """m2c emits NULL for null pointers (14 files in the 65-128
        bucket), but the Psy-Q chain runs cpp with -nostdinc so no hosted
        header provides it. NULL as a value is always the zero address,
        which 0 spells with identical codegen."""

        out = batch_match._sanitize(
            "s32 f(s32 *p) {\n    if (p == NULL) {\n        return NULL;\n    }\n    return 1;\n}\n"
        )
        self.assertIn("if (p == 0) {", out)
        self.assertIn("return 0;", out)
        self.assertNotIn("NULL", out)

    def test_deref_only_extern_data_becomes_pointer(self) -> None:
        """`*D = v` with no value-use means D holds an address
        (func_80042C90's `*D_8006CBBC = 0x107`). Declaring the pointer
        compiles; the s32 guess cannot dereference."""

        out = batch_match._sanitize(
            "extern s32 D_8006CBBC;\nvoid f(void) {\n    *D_8006CBBC = 0x107;\n}\n"
        )
        self.assertIn("extern s32 *D_8006CBBC;", out)

    def test_mixed_deref_and_assign_stays_s32(self) -> None:
        """`*D` plus a plain `D = v` is contradictory — no declaration
        satisfies both without knowing the layout. Leave it failing
        loudly for hand work rather than guess."""

        out = batch_match._sanitize(
            "extern s32 D;\nvoid f(void) {\n    D = 1;\n    *D = 2;\n}\n"
        )
        self.assertIn("extern s32 D;", out)
        self.assertNotIn("s32 *D;", out)

    def test_called_through_extern_becomes_function_pointer(self) -> None:
        """`(*D)(x)` calls through D, so D is a callback, not data.
        `s32 *D` would not compile there; `s32 (*D)()` does, with the
        same jalr sequence either way."""

        out = batch_match._sanitize(
            "extern s32 D;\nvoid f(void) {\n    (*D)(1);\n}\n"
        )
        self.assertIn("extern s32 (*D)();", out)

    def test_called_but_undeclared_func_gets_void_prototype(self) -> None:
        """m2c sometimes references a callee it never declares
        (D_80072780-class omissions in 4 files). A `void f();` unchecked
        prototype matches the convention used for seen-but-unknown
        callees."""

        out = batch_match._sanitize("void f(void) {\n    func_80072780(1);\n}\n")
        self.assertIn("void func_80072780();", out)

    def test_value_used_but_undeclared_data_gets_extern(self) -> None:
        out = batch_match._sanitize("void f(void) {\n    g(D_80072788);\n}\n")
        self.assertIn("extern s32 D_80072788;", out)

    def test_no_duplicate_decl_for_callback_prototype(self) -> None:
        """`s32 (*f(..))(..);` already declares f: the missing-symbol
        pass must recognise the `(*f` form as a declaration, or it
        would append a conflicting `void f();` and break a file that
        currently compiles."""

        src = "s32 (*func_80131CF4(s32))(void *);\nvoid f(void) {\n}\n"
        out = batch_match._sanitize(src)
        self.assertEqual(out.count("func_80131CF4"), 1)

    def test_no_decl_for_the_defined_function_itself(self) -> None:
        """A recursive call must not conjure a prototype that conflicts
        with the definition's return type."""

        src = "s32 target(s32 x) {\n    return target(x - 1);\n}\n"
        out = batch_match._sanitize(src)
        self.assertNotIn("void target();", out)
        self.assertNotIn("extern", out)

    def test_m2c_warning_lines_are_dropped(self) -> None:
        """m2c prints `Warning: ...` diagnostics into its stdout, which the
        harness captures as the candidate body (15+ files in the 0012
        <=64 bucket, e.g. `missing "jr $ra" in last block`). A warning is
        not C and always breaks compilation; drop those lines."""

        out = batch_match._sanitize(
            'Warning: missing "jr $ra" in last block of func_80128288 (initial).\n'
            "void func_80128288(void) {\n}\n"
        )
        self.assertNotIn("Warning:", out)
        self.assertIn("void func_80128288(void) {", out)

    def test_bare_unknown_parameter_becomes_s32(self) -> None:
        """A lone `?` parameter is an unknown word-sized argument, not the
        C89 unchecked-args `(?)` form, which keeps its own empty-parens
        mapping."""

        out = batch_match._sanitize(
            "void func_8002D4C8(?, ?);\nvoid f(void) {\n}\n"
        )
        self.assertIn("void func_8002D4C8(s32, s32);", out)
        self.assertNotIn("(?,", out)


GTE_SNIPPET = """\
glabel func_8004D504
/* 0 8004D504 00008884 */ lh $t0, 0x0($a0)
/* 4 8004D508 0000C848 */ ctc2 $t0, $0
/* 8 8004D50C 0800E003 */ jr $ra
/* C 8004D510 00000000 */ nop
"""


class GteOverlayTests(unittest.TestCase):
    def test_instruction_words_are_little_endian_retail_words(self) -> None:
        function = batch_match.FunctionRange("func_8004D504", 0x8004D504, 16)
        words = batch_match.instruction_words(GTE_SNIPPET, function)
        self.assertEqual(
            [(addr, word, op) for addr, word, op in words],
            [
                (0x8004D504, 0x84880000, "lh"),
                (0x8004D508, 0x48C80000, "ctc2"),
                (0x8004D50C, 0x03E00008, "jr"),
                (0x8004D510, 0x00000000, "nop"),
            ],
        )

    def test_gte_opcode_is_detected(self) -> None:
        function = batch_match.FunctionRange("func_8004D504", 0x8004D504, 16)
        self.assertTrue(batch_match.function_contains_gte(GTE_SNIPPET, function))
        self.assertFalse(batch_match.function_contains_gte(ASM, batch_match.FunctionRange("func_80012AB0", 0x80012AB0, 8)))

    def test_overlay_source_emits_gte_mnemonics_not_word_dumps(self) -> None:
        function = batch_match.FunctionRange("func_8004D504", 0x8004D504, 16)
        lines = batch_match.instruction_lines(GTE_SNIPPET, function)
        source = batch_match.assembly_overlay_source(function, lines)
        self.assertIn("ctc2 $t0,$0", source)
        self.assertIn("lh $t0,0x0($a0)", source)
        self.assertNotIn(".word 0x", source)
        self.assertIn("func_8004D504:", source)
        self.assertEqual(batch_match.progress.classify_recovery(source), "assembly")

    def test_overlay_uses_word_only_for_dmpsx_fake_ops(self) -> None:
        listing = """\
glabel func_8004D600
/* 0 8004D600 00008848 */ mtc2 $t0, $0
/* 4 8004D604 1260404A */ mvmva 0, 0, 0, 3, 0
/* 8 8004D608 0800E003 */ jr $ra
/* C 8004D60C 00000000 */ nop
"""
        function = batch_match.FunctionRange("func_8004D600", 0x8004D600, 16)
        source = batch_match.assembly_overlay_source(
            function, batch_match.instruction_lines(listing, function), listing=listing
        )
        self.assertIn("mtc2 $t0,$0", source)
        self.assertIn(".word 0x4A406012", source)
        self.assertIn("jr $ra", source)
        self.assertEqual(source.count(".word 0x"), 1)
        self.assertEqual(batch_match.progress.classify_recovery(source), "assembly")

    def test_overlay_compacts_commas_and_evaluates_splat_immediates(self) -> None:
        listing = """\
glabel func_8004D700
/* 0 8004D700 2B104300 */ sltu $v0, $v0, $v1
/* 4 8004D704 0000803C */ lui $at, (0x80000000 >> 16)
/* 8 8004D708 00008848 */ mtc2 $t0, $0
/* C 8004D70C 0800E003 */ jr $ra
/* 10 8004D710 00000000 */ nop
"""
        function = batch_match.FunctionRange("func_8004D700", 0x8004D700, 20)
        source = batch_match.assembly_overlay_source(
            function, batch_match.instruction_lines(listing, function), listing=listing
        )
        self.assertIn("sltu $v0,$v0,$v1", source)
        self.assertIn("lui $at,0x8000", source)
        self.assertNotIn("(0x80000000 >> 16)", source)
        self.assertIn("mtc2 $t0,$0", source)
        self.assertEqual(batch_match.progress.classify_recovery(source), "assembly")

    def test_overlay_encodes_break_as_retail_word(self) -> None:
        listing = """\
glabel func_8004D800
/* 0 8004D800 00008848 */ mtc2 $t0, $0
/* 4 8004D804 0D000700 */ break 7
/* 8 8004D808 0800E003 */ jr $ra
/* C 8004D80C 00000000 */ nop
"""
        function = batch_match.FunctionRange("func_8004D800", 0x8004D800, 16)
        source = batch_match.assembly_overlay_source(
            function, batch_match.instruction_lines(listing, function), listing=listing
        )
        self.assertIn("mtc2 $t0,$0", source)
        self.assertIn(".word 0x0007000D", source)
        self.assertNotIn("break", source)
        self.assertEqual(batch_match.progress.classify_recovery(source), "assembly")

    def test_non_gte_overlay_is_refused(self) -> None:
        function = batch_match.FunctionRange("func_80012AB0", 0x80012AB0, 8)
        words = batch_match.instruction_words(ASM, function)
        with self.assertRaises(batch_match.RetailError):
            batch_match.assembly_overlay_source(function, words)

    def test_gte_opcodes_do_not_include_cpu_priv_ops(self) -> None:
        self.assertNotIn("mfc0", batch_match.GTE_OPCODES)
        self.assertNotIn("mtc0", batch_match.GTE_OPCODES)
        self.assertNotIn("syscall", batch_match.GTE_OPCODES)
        self.assertIn("ctc2", batch_match.GTE_OPCODES)


class PromotedEntryTests(unittest.TestCase):
    def test_promoted_c_is_classified_as_a_complete_c_function(self) -> None:
        function = batch_match.FunctionRange("func_80012AB0", 0x80012AB0, 12)
        entry = batch_match.promoted_entry(
            function,
            "main",
            "src/main/80012ab0.c",
            "int func_80012AB0(int a, int b) { return (a - b) & 0xFFF; }\n",
            "-O2",
        )
        self.assertEqual(entry["recovery"], "c")
        self.assertEqual(entry["extent"], "function")
        self.assertEqual(entry["optimization"], "-O2")

    def test_promoted_asm_overlay_is_classified_as_assembly(self) -> None:
        function = batch_match.FunctionRange("func_800100A0", 0x800100A0, 112)
        entry = batch_match.promoted_entry(
            function,
            "main",
            "src/main/800100a0.c",
            '__asm__("jr $ra\\nnop\\n");\n',
            None,
        )
        self.assertEqual(entry["recovery"], "assembly")
        self.assertEqual(entry["extent"], "function")
        self.assertNotIn("optimization", entry)


class RegisterTests(unittest.TestCase):
    """A kill mid-sweep must not orphan a promoted match from the registry.

    Two live runs were killed by the environment partway through, and each
    time left matched sources on disk with no registry entry, requiring hand
    recovery. The registry is now written after every single promotion
    instead of once at the end, so a kill can only cost the in-flight
    function, never the ones already promoted.
    """

    def test_register_appends_and_persists_immediately(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            registry_path = Path(scratch) / "matches.json"
            registry = {"matches": []}
            entry = {"name": "func_80012AB0", "vram": 0x80012AB0, "size": 12,
                      "region": "main", "source": "src/main/80012ab0.c"}
            batch_match.register(registry, registry_path, entry)
            self.assertTrue(registry_path.is_file())
            reloaded = json.loads(registry_path.read_text())
            self.assertEqual(reloaded["matches"], [entry])

    def test_register_does_not_duplicate_an_existing_entry(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            registry_path = Path(scratch) / "matches.json"
            entry = {"name": "func_80012AB0", "vram": 0x80012AB0, "size": 12,
                      "region": "main", "source": "src/main/80012ab0.c"}
            registry = {"matches": [dict(entry)]}
            batch_match.register(registry, registry_path, entry)
            self.assertEqual(len(registry["matches"]), 1)

    def test_register_keeps_matches_sorted_by_region_then_vram(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            registry_path = Path(scratch) / "matches.json"
            registry = {"matches": [
                {"name": "func_80012AB8", "vram": 0x80012AB8, "size": 4, "region": "main", "source": "a"},
            ]}
            batch_match.register(
                registry, registry_path,
                {"name": "func_80012AB0", "vram": 0x80012AB0, "size": 4, "region": "main", "source": "b"},
            )
            self.assertEqual([m["vram"] for m in registry["matches"]], [0x80012AB0, 0x80012AB8])


class OutcomeTests(unittest.TestCase):
    """Only an exact match is promoted; everything else leaves no trace."""

    def test_a_mismatch_is_not_promoted(self) -> None:
        self.assertFalse(batch_match.Outcome("func_x", "mismatch").promoted)

    def test_a_match_is_promoted(self) -> None:
        self.assertTrue(batch_match.Outcome("func_x", "match").promoted)

    def test_a_build_failure_is_not_promoted(self) -> None:
        self.assertFalse(batch_match.Outcome("func_x", "build-failed").promoted)


class OptimizationFallbackTests(unittest.TestCase):
    def test_attempt_promotes_only_after_the_o0_retry_matches(self) -> None:
        """A failed default build may be recoverable under the retail -O0 lane."""

        function = batch_match.FunctionRange("func_80010000", 0x80010000, 8)
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            asm = repo / "main.s"
            asm.write_text("glabel func_80010000\n/* 0 80010000 00000000 */ nop\n")
            m2c = repo / "m2c.py"
            m2c.write_text("")
            scratch = repo / "scratch"
            scratch.mkdir()

            def fake_m2c(_m2c, _asm, _name, output, **_kwargs):
                output.write_text("void func_80010000(void) {}\n")
                return True

            build_optimizations: list[str] = []
            match_results = iter([1, 0])

            def fake_quiet(fn, argv):
                if fn is batch_match.build_candidate.main:
                    build_optimizations.append(next(item for item in argv if item.startswith("--optimization=")))
                    Path(argv[argv.index("--output") + 1]).write_bytes(b"\0" * 8)
                    return 0
                self.assertIs(fn, batch_match.match_function.main)
                return next(match_results)

            with mock.patch.object(batch_match, "run_m2c", side_effect=fake_m2c), mock.patch.object(
                batch_match, "_quiet", side_effect=fake_quiet
            ):
                outcome = batch_match.attempt(
                    function, "main", {"kind": "executable"}, repo, asm, m2c, scratch
                )

            self.assertTrue(outcome.promoted)
            self.assertEqual(outcome.optimization, "-O0")
            self.assertEqual(build_optimizations, ["--optimization=-O2", "--optimization=-O0"])
            self.assertTrue((repo / "src/main/80010000.c").is_file())


class GteAttemptTests(unittest.TestCase):
    def test_gte_function_promotes_an_overlay_without_m2c(self) -> None:
        function = batch_match.FunctionRange("func_8004D504", 0x8004D504, 16)
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            asm = repo / "main.s"
            asm.write_text(GTE_SNIPPET)
            m2c = repo / "m2c.py"
            m2c.write_text("")
            scratch = repo / "scratch"
            scratch.mkdir()

            def fake_quiet(fn, argv):
                if fn is batch_match.build_candidate.main:
                    Path(argv[argv.index("--output") + 1]).write_bytes(b"\0" * 16)
                    return 0
                self.assertIs(fn, batch_match.match_function.main)
                return 0

            with mock.patch.object(batch_match, "run_m2c") as run_m2c, mock.patch.object(
                batch_match, "_quiet", side_effect=fake_quiet
            ):
                outcome = batch_match.attempt(
                    function, "main", {"kind": "executable"}, repo, asm, m2c, scratch
                )

            run_m2c.assert_not_called()
            self.assertTrue(outcome.promoted)
            self.assertIsNone(outcome.optimization)
            text = (repo / "src/main/8004d504.c").read_text()
            self.assertIn("ctc2 $t0,$0", text)
            self.assertNotIn(".word 0x", text)
            self.assertNotIn("Decompiled by m2c", text)
            self.assertEqual(batch_match.progress.classify_recovery(text), "assembly")


class VerifyExistingTests(unittest.TestCase):
    """Existing src/ files can be registered on MATCH but must never be rewritten."""

    def test_match_does_not_modify_the_existing_source(self) -> None:
        function = batch_match.FunctionRange("func_80010000", 0x80010000, 8)
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            source = repo / "src/main/80010000.c"
            source.parent.mkdir(parents=True)
            original = "void func_80010000(void) {}\n"
            source.write_text(original)
            scratch = repo / "scratch"
            scratch.mkdir()

            def fake_quiet(fn, argv):
                if fn is batch_match.build_candidate.main:
                    Path(argv[argv.index("--output") + 1]).write_bytes(b"\0" * 8)
                    return 0
                self.assertIs(fn, batch_match.match_function.main)
                return 0

            with mock.patch.object(batch_match, "_quiet", side_effect=fake_quiet):
                outcome = batch_match.verify_existing(
                    function, "main", {"kind": "executable"}, source, scratch
                )

            self.assertTrue(outcome.promoted)
            self.assertEqual(outcome.optimization, "-O2")
            self.assertEqual(source.read_text(), original)

    def test_mismatch_is_not_promoted_and_leaves_the_file(self) -> None:
        function = batch_match.FunctionRange("func_80010000", 0x80010000, 8)
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "src.c"
            source.write_text("void func_80010000(void) {}\n")
            scratch = Path(temporary) / "scratch"
            scratch.mkdir()

            def fake_quiet(fn, argv):
                if fn is batch_match.build_candidate.main:
                    Path(argv[argv.index("--output") + 1]).write_bytes(b"\0" * 8)
                    return 0
                return 1

            with mock.patch.object(batch_match, "_quiet", side_effect=fake_quiet):
                outcome = batch_match.verify_existing(
                    function, "main", {"kind": "executable"}, source, scratch
                )

            self.assertFalse(outcome.promoted)
            self.assertEqual(outcome.status, "mismatch")
            self.assertTrue(source.is_file())

    def test_missing_source_is_skipped(self) -> None:
        function = batch_match.FunctionRange("func_80010000", 0x80010000, 8)
        with tempfile.TemporaryDirectory() as temporary:
            outcome = batch_match.verify_existing(
                function,
                "main",
                {"kind": "executable"},
                Path(temporary) / "missing.c",
                Path(temporary),
            )
        self.assertEqual(outcome.status, "skipped-missing")
        self.assertFalse(outcome.promoted)


class CandidateBankTests(unittest.TestCase):
    def test_sanitizer_preserves_typed_context_externs(self) -> None:
        source = (
            'typedef struct State { int field; } State;\n'
            'extern State D_state;\n'
            'extern struct State D_table[2];\n'
            'void func_test(void) { D_state.field = D_table[1].field; }\n'
        )
        self.assertEqual(batch_match._sanitize(source), source)

    def test_banked_candidate_lookup_requires_an_existing_draft(self) -> None:
        function = batch_match.FunctionRange("func_80010000", 0x80010000, 8)
        with tempfile.TemporaryDirectory() as temporary:
            bank = Path(temporary)
            self.assertIsNone(batch_match.banked_candidate_path(None, function))
            self.assertIsNone(batch_match.banked_candidate_path(bank, function))
            candidate = bank / "func_80010000.c"
            candidate.write_text("void func_80010000(void) {}\n")
            self.assertEqual(batch_match.banked_candidate_path(bank, function), candidate)

    def test_banked_draft_is_oracle_checked_without_rerunning_m2c(self) -> None:
        """The candidate cache is only an input optimization, never authority."""

        function = batch_match.FunctionRange("func_80010000", 0x80010000, 8)
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            asm = repo / "main.s"
            asm.write_text("glabel func_80010000\n/* 0 80010000 00000000 */ nop\n")
            m2c = repo / "m2c.py"
            m2c.write_text("")
            scratch = repo / "scratch"
            scratch.mkdir()
            bank = repo / "bank"
            bank.mkdir()
            banked = bank / "func_80010000.c"
            original = "void func_80010000(void) {}\n"
            banked.write_text(original)

            def fake_quiet(fn, argv):
                if fn is batch_match.build_candidate.main:
                    Path(argv[argv.index("--output") + 1]).write_bytes(b"\0" * 8)
                    return 0
                self.assertIs(fn, batch_match.match_function.main)
                return 0

            with mock.patch.object(batch_match, "run_m2c") as run_m2c, mock.patch.object(
                batch_match, "_quiet", side_effect=fake_quiet
            ):
                outcome = batch_match.attempt(
                    function, "main", {"kind": "executable"}, repo, asm, m2c, scratch, bank
                )

            run_m2c.assert_not_called()
            self.assertTrue(outcome.promoted)
            self.assertEqual(banked.read_text(), original)
            self.assertIn("verified byte-exact", (repo / "src/main/80010000.c").read_text())


if __name__ == "__main__":
    unittest.main()
