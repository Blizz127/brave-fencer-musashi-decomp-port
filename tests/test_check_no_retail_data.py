from __future__ import annotations

import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import check_no_retail_data as check  # noqa: E402

TOOLCHAIN = ROOT / "tools/psyq/gcc-2.7.2-psx/cc1"
HAVE_TOOLCHAIN = TOOLCHAIN.is_file() and shutil.which("mips-linux-gnu-objdump") is not None

CLEAN = """#include "psx_types.h"
extern s32 D_800A0000;
extern const char D_800A1000[];
extern void func_80010000(const char *);
s32 func_80020000(s32 a0) {
    func_80010000(D_800A1000);
    return D_800A0000 + a0;
}
"""

CONST_TABLE = """#include "psx_types.h"
const s32 D_800A2000[] = { 1, 2, 3, 4 };
s32 func_80020000(s32 i) { return D_800A2000[i]; }
"""

STRING_LITERAL = """#include "psx_types.h"
extern void func_80010000(const char *);
void func_80020000(void) { func_80010000("retail text"); }
"""

JUMP_TABLE = """#include "psx_types.h"
extern s32 D_800A0000;
s32 func_80020000(s32 a0) {
    switch (a0) {
    case 0: return D_800A0000;
    case 1: return D_800A0000 + 3;
    case 2: return D_800A0000 * 5;
    case 3: return D_800A0000 - 7;
    case 4: return D_800A0000 ^ 9;
    case 5: return D_800A0000 | 11;
    }
    return 0;
}
"""


class ParserTests(unittest.TestCase):
    def test_section_sizes(self) -> None:
        text = """
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00000040  00000000  00000000  00000040  2**2
  1 .data         00000000  00000000  00000000  00000080  2**0
  2 .rodata       00000018  00000000  00000000  00000080  2**2
"""
        self.assertEqual(check.section_sizes(text), {".text": 0x40, ".data": 0, ".rodata": 0x18})

    def test_jump_table_needs_a_text_relocation_on_every_word(self) -> None:
        relocs = [(0, "R_MIPS_32", ".text"), (4, "R_MIPS_32", ".text"), (8, "R_MIPS_32", ".text")]
        self.assertTrue(check.is_jump_table(12, relocs))
        self.assertFalse(check.is_jump_table(16, relocs))  # one plain data word
        self.assertFalse(check.is_jump_table(12, relocs[:2] + [(8, "R_MIPS_32", "D_800A0000")]))
        self.assertFalse(check.is_jump_table(0, []))

    def test_alignment_padding_between_two_tables_is_allowed(self) -> None:
        # table A: words 0..8 (3 entries), zero pad at 0xC, table B at 0x10 (8-aligned)
        relocs = [(o, "R_MIPS_32", ".text") for o in (0, 4, 8, 0x10, 0x14)]
        content = bytes.fromhex("40000000" "50000000" "60000000" "00000000" "70000000" "80000000")
        self.assertTrue(check.is_jump_table(0x18, relocs, content))
        # the same gap holding data is not padding
        self.assertFalse(check.is_jump_table(0x18, relocs, content[:12] + b"ABCD" + content[16:]))
        # without content the gap cannot be proven to be padding
        self.assertFalse(check.is_jump_table(0x18, relocs))


@unittest.skipUnless(HAVE_TOOLCHAIN, "Psy-Q toolchain or mips binutils not installed")
class CompiledFixtureTests(unittest.TestCase):
    def _check(self, body: str) -> list:
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / "fixture.c"
            source.write_text(body)
            return check.check_source(source, "func_80020000", 0x80020000)

    def test_clean_source_has_no_data(self) -> None:
        self.assertEqual(self._check(CLEAN), [])

    def test_const_initializer_is_reported(self) -> None:
        findings = self._check(CONST_TABLE)
        self.assertEqual(len(findings), 1)
        self.assertRegex(findings[0].section, r"^\.r(o)?data")
        self.assertEqual(findings[0].size, 16)
        self.assertIn("D_800A2000", findings[0].symbols)

    def test_string_literal_is_reported(self) -> None:
        findings = self._check(STRING_LITERAL)
        self.assertEqual(len(findings), 1)
        self.assertGreaterEqual(findings[0].size, len("retail text") + 1)

    def test_switch_jump_table_is_allowed(self) -> None:
        self.assertEqual(self._check(JUMP_TABLE), [])


if __name__ == "__main__":
    unittest.main()
