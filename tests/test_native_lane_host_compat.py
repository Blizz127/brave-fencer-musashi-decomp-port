"""native_lane_gen host-compat rules: matching-only MIPS constructs that
carry no C semantics are accepted, and anything with semantics still is not.

- `register T x asm("$n")` register pins are stripped in a host-only copy;
- an empty `__asm__("")` is accepted only when every output is tied to an
  input (an identity); an untied output would be undefined;
- `long long` (64-bit on both targets) passes the source rule, a lone
  `long` does not;
- calls through guest function pointers become lane_icall stubs that hand
  the guest address to the interpreter, as jalr does.
No game data.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import native_lane_gen as g  # noqa: E402


class HostCompat(unittest.TestCase):
    def test_register_pins_stripped(self):
        src = ('register s32 a asm("$5") = 1;\n'
               'register s32 (*fn)(void *) __asm__("$3") = *(void **)p;\n'
               'register u8 *q __asm__ ( "$16" );\n'
               'asm volatile("nop");\n')
        out = g.host_source(src)
        self.assertNotIn('"$', out)
        self.assertIn("register s32 a = 1;", out)
        self.assertIn("register s32 (*fn)(void *) = *(void **)p;", out)
        self.assertIn('asm volatile("nop");', out)  # real asm is not touched

    def test_empty_asm_identity(self):
        ok = g.empty_asm_identity
        self.assertTrue(ok('%7 = tail call { ptr, i32 } asm "", "=r,=r,0,1,~{dirflag},~{fpsr},~{flags}"(ptr %6, i32 %1)'))
        self.assertTrue(ok('call void asm sideeffect "", "~{memory},~{dirflag}"()'))
        self.assertTrue(ok('call void asm sideeffect "", "r,r,r"(i32 %a, i32 %b, i32 %c)'))
        self.assertFalse(ok('%x = call i32 asm "", "=r"()'))            # untied output
        self.assertFalse(ok('call void asm sideeffect "nop", ""()'))    # an instruction
        self.assertFalse(ok('%y = call i32 @f(i32 1)'))

    def test_indirect_call_rewrite(self):
        ir = ("define i32 @lane_func_80000000(ptr %p) {\n"
              "  %f = load i32, ptr %p\n"
              "  %fp = inttoptr i32 %f to ptr\n"
              "  %r = tail call signext i16 %fp(i32 noundef 5, ptr %p) #3\n"
              "  call void %fp()\n"
              "  %z = call double %fp(double 1.0)\n"
              "  ret i32 0\n}\n")
        out, icalls = g.rewrite_indirect_calls(ir)
        self.assertIn("call signext i16 @lane_icall__ri16s_i32_ptr(i32 %icall.0.t, i32 noundef 5, ptr %p) #3", out)
        self.assertIn("%icall.0.t = trunc i64 %icall.0 to i32", out)
        self.assertIn("declare signext i16 @lane_icall__ri16s_i32_ptr(i32, i32, ptr)", out)
        self.assertIn("call void @lane_icall__rvoid(i32 %icall.1.t)", out)
        self.assertIn("call double %fp(double 1.0)", out)  # no guest ABI form: left for the checker
        self.assertEqual(set(icalls), {"lane_icall__ri16s_i32_ptr", "lane_icall__rvoid"})


if __name__ == "__main__":
    unittest.main()
