"""Fresh, event-serviced Timer 2 owner with checked absolute-cycle updates."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Timer2(C.Structure):
    _fields_ = [
        ("epoch_cycles", C.c_uint64),
        ("last_cycles", C.c_uint64),
        ("count", C.c_uint16),
        ("mode", C.c_uint16),
        ("target", C.c_uint16),
        ("initialized", C.c_int),
    ]


class Timer2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-timer2-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "timer2.so"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-shared", "-fPIC", "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/timer2.c"), "-o", str(library),
        ], check=True, stdin=subprocess.DEVNULL, timeout=30)
        cls.lib = C.CDLL(str(library))
        cls.lib.musashi_timer2_init.argtypes = [C.POINTER(Timer2), C.c_uint64]
        cls.lib.musashi_timer2_advance.argtypes = [
            C.POINTER(Timer2), C.c_uint64,
        ]
        cls.lib.musashi_timer2_read16.argtypes = [
            C.POINTER(Timer2), C.c_uint32, C.POINTER(C.c_uint16),
        ]

    def read(self, timer, address):
        value = C.c_uint16(0xA55A)
        result = self.lib.musashi_timer2_read16(timer, address, C.byref(value))
        return result, value.value

    def test_init_epoch_same_cycle_and_count_boundaries(self):
        timer = Timer2()
        self.lib.musashi_timer2_init(timer, 100)
        self.assertEqual(
            (timer.epoch_cycles, timer.last_cycles, timer.count,
             timer.mode, timer.target, timer.initialized),
            (100, 100, 0, 0x400, 0, 1),
        )
        self.assertEqual(self.lib.musashi_timer2_advance(timer, 100), 1)
        self.assertEqual(timer.count, 0)
        self.assertEqual(self.lib.musashi_timer2_advance(timer, 100 + 0xFFFE), 1)
        self.assertEqual(timer.count, 0xFFFE)
        self.assertEqual(timer.mode, 0x400)
        self.assertEqual(self.lib.musashi_timer2_advance(timer, 100 + 0xFFFF), 1)
        self.assertEqual(timer.count, 0)
        self.assertEqual(timer.mode, 0x1400)
        self.assertEqual(self.lib.musashi_timer2_advance(timer, 100 + 0xFFFF + 1), 1)
        self.assertEqual(timer.count, 1)
        self.assertEqual(timer.mode, 0x1400)

    def test_multiple_wraps_coalesce_sticky_overflow(self):
        timer = Timer2()
        self.lib.musashi_timer2_init(timer, 0)
        self.assertEqual(
            self.lib.musashi_timer2_advance(timer, 0xFFFF * 3 + 2), 1)
        self.assertEqual(timer.count, 2)
        self.assertEqual(timer.mode, 0x1400)
        self.assertEqual(self.lib.musashi_timer2_advance(timer, 0xFFFF * 3 + 2), 1)
        self.assertEqual(timer.count, 2)
        self.assertEqual(timer.mode, 0x1400)

    def test_mode_read_clears_status_only_and_reads_do_not_advance(self):
        timer = Timer2()
        self.lib.musashi_timer2_init(timer, 0)
        self.lib.musashi_timer2_advance(timer, 0xFFFF)
        before = timer.count
        result, mode = self.read(timer, 0x1F801124)
        self.assertEqual((result, mode), (1, 0x1400))
        self.assertEqual(timer.count, before)
        self.assertEqual(timer.mode, 0x400)
        result, mode = self.read(timer, 0x1F801124)
        self.assertEqual((result, mode), (1, 0x400))
        self.assertEqual(self.read(timer, 0x1F801120), (1, before))
        self.assertEqual(self.read(timer, 0x1F801128), (1, 0))

    def test_backwards_and_unsupported_accesses_refuse_without_mutation(self):
        timer = Timer2()
        self.lib.musashi_timer2_init(timer, 10)
        self.assertEqual(self.lib.musashi_timer2_advance(timer, 20), 1)
        before = bytes(timer)
        self.assertEqual(self.lib.musashi_timer2_advance(timer, 19), 0)
        self.assertEqual(bytes(timer), before)
        for address in (0x1F801121, 0x1F801122, 0x1F80112C, 0x80000000):
            output = C.c_uint16(0xBEEF)
            before = bytes(timer)
            self.assertEqual(self.lib.musashi_timer2_read16(
                timer, address, C.byref(output)), 0)
            self.assertEqual(output.value, 0xBEEF)
            self.assertEqual(bytes(timer), before)

    def test_null_and_uninitialized_accesses_refuse_without_output(self):
        timer = Timer2()
        before = bytes(timer)
        output = C.c_uint16(0xCAFE)
        self.assertEqual(self.lib.musashi_timer2_advance(None, 1), 0)
        self.assertEqual(self.lib.musashi_timer2_read16(
            None, 0x1F801120, C.byref(output)), 0)
        self.assertEqual(output.value, 0xCAFE)
        self.assertEqual(self.lib.musashi_timer2_advance(timer, 1), 0)
        self.assertEqual(self.lib.musashi_timer2_read16(
            timer, 0x1F801120, C.byref(output)), 0)
        self.assertEqual(output.value, 0xCAFE)
        self.assertEqual(bytes(timer), before)

    def test_near_uint64_max_is_monotonic_without_wrap(self):
        # A huge catch-up from a nonzero count must not overflow the sum.
        # 2**64-1 is exactly divisible by the selected 2**16-1 period.
        large = Timer2()
        self.lib.musashi_timer2_init(large, 0)
        self.assertEqual(self.lib.musashi_timer2_advance(large, 0xFFFE), 1)
        self.assertEqual(self.lib.musashi_timer2_advance(
            large, C.c_uint64(-1).value), 1)
        self.assertEqual((large.count, large.mode), (0, 0x1400))
        timer = Timer2()
        epoch = C.c_uint64(-1).value - 2
        self.lib.musashi_timer2_init(timer, epoch)
        self.assertEqual(self.lib.musashi_timer2_advance(timer, epoch + 1), 1)
        self.assertEqual(timer.count, 1)
        self.assertEqual(self.lib.musashi_timer2_advance(
            timer, C.c_uint64(-1).value), 1)
        self.assertEqual(timer.count, 2)
        before = bytes(timer)
        self.assertEqual(self.lib.musashi_timer2_advance(timer, epoch), 0)
        self.assertEqual(bytes(timer), before)


if __name__ == "__main__":
    unittest.main()
