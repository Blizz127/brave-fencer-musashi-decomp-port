"""Native lane: checker decisions and generated-table shape.

Only tracked decomp sources are needed (no retail data); skipped without
clang. The differential check against the interpreter is the CTest
native_lane_differential (MUSASHI_NATIVE_LANE=ON)."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile

import unittest

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "tools/native_lane_gen.py"

# Each decision is pinned to the rule that must make it.
EXPECT = {
    0x80019064: (True, None),        # guest globals, absolute addresses
    0x8001C044: (True, None),        # calls a bespoke-binding callee (thunk)
    0x800141F0: (False, "hooks"),    # STARTUP_ENTRY: host-sequenced (enum-named PC)
    0x8001903C: (True, None),        # STARTUP_RECORD_CLEAR: a host intrinsic the lane may run
    0x80015498: (False, "deny"),     # admissible, held out on DENY until a targeted probe covers it
    0x80015208: (True, None),        # register-pinned, only tied empty asm
    0x80015B6C: (False, "calls"),    # empty asm with an untied output
    0x80014CF8: (True, None),        # tied empty asm ("" : "=r"(x) : "0"(x)) is an identity
    0x80015310: (False, "memory"),   # volatile device access
    0x8002A088: (False, "abi"),      # passes 0 of 1 arguments to func_8002A108
    0x8001CFDC: (True, None),        # bounded read-only table search (walks memory)
    0x80018384: (False, "irq"),      # for (;;); : a loop that reads nothing new
}
# Sony PsyQ entry points never enter the lane (config/psyq_ranges.txt).
PSYQ = (0x800425B0, 0x8003B08C, 0x80046ABC)
CALLEES = (0x8002A108,)  # the arity rule needs the admitted callee present



class NativeLaneGeneratorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.clang = shutil.which("clang")
        if not cls.clang:
            raise unittest.SkipTest("clang unavailable")
        cls._tmp = tempfile.TemporaryDirectory(prefix="musashi-lane-")
        cls.out = Path(cls._tmp.name)
        only = cls.out / "only.txt"
        only.write_text("".join(f"{pc:08x}\n" for pc in (*EXPECT, *PSYQ, *CALLEES)))
        subprocess.run([sys.executable, str(GEN), "--out", str(cls.out), "--clang", cls.clang,
                        "--jobs", "2", "--only", str(only)], check=True, timeout=300,
                       capture_output=True)
        cls.report = json.loads((cls.out / "report.json").read_text())
        cls.table = (cls.out / "native_lane_table.c").read_text()

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_decisions(self):
        got = {f["vram"]: f for f in self.report["functions"]}
        for pc, (admitted, rule) in EXPECT.items():
            entry = got[pc]
            self.assertEqual(entry["admitted"], admitted, (hex(pc), entry))
            if not admitted:
                self.assertEqual(entry["rule"], rule, (hex(pc), entry))
        for pc in PSYQ:
            self.assertNotIn(pc, got)
        self.assertGreaterEqual(self.report["psyq_excluded"], 1)

    def test_table_shape(self):
        # Admitted entry, marshalled through the register ABI.
        self.assertIn('{0x80019064u, 36u, wrap_80019064, "func_80019064", 1u, 0u},', self.table)
        # func_80016714 has a bespoke binding, so natively compiled callers
        # reach its guest words through the interpreter thunk.
        self.assertIn("musashi_native_guest_call(0x80016714u, 2u, args, NULL)", self.table)
        # Guest symbols are absolute guest addresses, never host relocations.
        lane_ir = (self.out / "ir" / "80019064.lane.ll").read_text()
        self.assertIn("inttoptr (i64 2147960768 to ptr)", lane_ir)  # D_800747C0
        self.assertNotIn("@D_800747C0", lane_ir)

    def test_table_compiles(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("cc unavailable")
        subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "pc_port/include"), "-c",
                        str(self.out / "native_lane_table.c"), "-o", str(self.out / "table.o")],
                       check=True, timeout=60)


class EmptyAsmTest(unittest.TestCase):
    def test_only_identity_empty_asm_is_dropped(self):
        sys.path.insert(0, str(ROOT / "tools"))
        import native_lane_gen as g
        for text in ('__asm__("" : : "r"(i), "r"(c));', '__asm__("" : "=r"(p) : "0"(p));',
                     '__asm__("" : "=r"(a), "=r"(b) : "0"(a), "1"(b));',
                     '__asm__ __volatile__("" ::: "memory");'):
            self.assertEqual(g.strip_empty_asm(text)[1], 1, text)
        self.assertIsNone(g.strip_empty_asm('__asm__("" : "=r"(a));'))        # untied output
        self.assertIsNone(g.strip_empty_asm('__asm__("" : "=r"(a) : "0"(b));'))  # tied to another value
        self.assertEqual(g.strip_empty_asm('__asm__("mtc2 $1, $2");')[1], 0)


class NativeLaneSourceTest(unittest.TestCase):
    def test_formatter_consults_lane_before_fetch(self):
        source = (ROOT / "pc_port/mips_formatter.c").read_text()
        step = source[source.index(
            "static int formatter_step(MusashiBootMemory *memory, FormatterCpu *cpu) {"):]
        self.assertLess(step.index("lane_try(memory, cpu)"),
                        step.index("formatter_fetch(memory, cpu, &instruction)"))
        # Tracing is opt-in and only observes.
        self.assertIn('getenv("MUSASHI_TRACE_FUNCS")', source)

    def test_no_retail_words_compiled_into_the_formatter(self):
        """Run off the disc: word arrays carry only their shape."""
        import re
        source = (ROOT / "pc_port/mips_formatter.c").read_text()
        self.assertNotRegex(source, r'#include "[^"]*_words\.inc"')
        for m in re.finditer(r"^static const uint32_t (k\w*Words)(\[[^\]]*\])+(.*)$",
                             source, flags=re.M):
            self.assertTrue(m.group(3).startswith(";"), m.group(0)[:100])
        self.assertFalse((ROOT / "pc_port/800cf02c_overlay_words.inc").exists())
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertNotIn("extract_asm_words.py", cmake)


if __name__ == "__main__":
    unittest.main()
