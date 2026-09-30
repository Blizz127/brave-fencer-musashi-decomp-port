"""Checked relative source execution clock ownership and overflow behavior."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SourceClock(C.Structure):
    _fields_ = [("cycles", C.c_uint64), ("faulted", C.c_int)]


class SourceClockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-source-clock-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "source_clock.so"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-shared", "-fPIC", "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/source_clock.c"), "-o", str(library),
        ], check=True, stdin=subprocess.DEVNULL, timeout=30)
        cls.lib = C.CDLL(str(library))
        cls.lib.musashi_source_clock_init.argtypes = [C.POINTER(SourceClock)]
        cls.lib.musashi_source_clock_advance.argtypes = [
            C.POINTER(SourceClock), C.c_uint32,
        ]

    def test_init_and_successful_advances_are_monotonic_including_zero(self):
        clock = SourceClock(123, 1)
        self.lib.musashi_source_clock_init(clock)
        self.assertEqual((clock.cycles, clock.faulted), (0, 0))
        for amount, expected in ((0, 0), (1, 1), (0xFFFFFFFF, 0x100000000)):
            self.assertEqual(
                self.lib.musashi_source_clock_advance(clock, amount), 1)
            self.assertEqual(clock.cycles, expected)
            self.assertEqual(clock.faulted, 0)

    def test_maximum_representable_cycle_then_overflow_refuses_and_faults(self):
        clock = SourceClock()
        self.lib.musashi_source_clock_init(clock)
        clock.cycles = C.c_uint64(-1).value - 1
        self.assertEqual(self.lib.musashi_source_clock_advance(clock, 1), 1)
        self.assertEqual(clock.cycles, C.c_uint64(-1).value)
        self.assertEqual(clock.faulted, 0)
        before = bytes(clock)
        self.assertEqual(self.lib.musashi_source_clock_advance(clock, 1), 0)
        self.assertEqual(clock.cycles, C.c_uint64(-1).value)
        self.assertEqual(clock.faulted, 1)
        self.assertNotEqual(bytes(clock), before)
        before = bytes(clock)
        self.assertEqual(self.lib.musashi_source_clock_advance(clock, 0), 0)
        self.assertEqual(bytes(clock), before)

    def test_null_and_independent_owners_are_refused_or_independent(self):
        self.assertEqual(self.lib.musashi_source_clock_advance(None, 1), 0)
        first, second = SourceClock(), SourceClock()
        self.lib.musashi_source_clock_init(first)
        self.lib.musashi_source_clock_init(second)
        self.assertEqual(self.lib.musashi_source_clock_advance(first, 7), 1)
        self.assertEqual(first.cycles, 7)
        self.assertEqual(second.cycles, 0)
        self.assertEqual(self.lib.musashi_source_clock_advance(second, 11), 1)
        self.assertEqual(first.cycles, 7)
        self.assertEqual(second.cycles, 11)

    def test_fault_is_sticky_and_init_can_prepare_a_fresh_owner(self):
        clock = SourceClock(C.c_uint64(-1).value, 1)
        self.assertEqual(self.lib.musashi_source_clock_advance(clock, 1), 0)
        self.assertEqual((clock.cycles, clock.faulted), (C.c_uint64(-1).value, 1))
        self.lib.musashi_source_clock_init(clock)
        self.assertEqual((clock.cycles, clock.faulted), (0, 0))
        self.assertEqual(self.lib.musashi_source_clock_advance(clock, 3), 1)
        self.assertEqual(clock.cycles, 3)


if __name__ == "__main__":
    unittest.main()
