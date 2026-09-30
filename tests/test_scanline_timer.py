"""Timer 1 startup mode, driven only by explicit synthetic video edges."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Timer(C.Structure):
    _fields_ = [("count", C.c_uint16), ("mode", C.c_uint16),
                ("target", C.c_uint16), ("running", C.c_int),
                ("configured", C.c_int), ("last_cycles", C.c_uint64),
                ("subcycle", C.c_uint32), ("paced", C.c_int),
                ("faulted", C.c_int)]


class ScanlineTimerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-scanline-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "timer.so"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-shared", "-fPIC", "-I", str(ROOT / "pc_port/include"),
                        str(ROOT / "pc_port/scanline_timer.c"), "-o", str(library)],
                       check=True, stdin=subprocess.DEVNULL, timeout=30)
        cls.lib = C.CDLL(str(library))
        cls.lib.musashi_scanline_timer_write32.argtypes = [C.POINTER(Timer), C.c_uint32, C.c_uint32]
        cls.lib.musashi_scanline_timer_read16.argtypes = [C.POINTER(Timer), C.c_uint32, C.POINTER(C.c_uint16)]
        cls.lib.musashi_scanline_timer_init_paced.argtypes = [
            C.POINTER(Timer), C.c_uint64]
        cls.lib.musashi_scanline_timer_advance.argtypes = [C.c_void_p, C.c_uint64]
        cls.lib.musashi_scanline_timer_vblank_at.argtypes = [
            C.POINTER(Timer), C.c_uint64]
        for name in ("vblank", "hblank"):
            getattr(cls.lib, "musashi_scanline_timer_" + name).argtypes = [C.POINTER(Timer)]

    def setUp(self):
        self.timer = Timer()

    def write(self, value, address=0x1F801114):
        return self.lib.musashi_scanline_timer_write32(self.timer, address, value)

    def read(self, address=0x1F801110):
        out = C.c_uint16()
        self.assertEqual(self.lib.musashi_scanline_timer_read16(self.timer, address, out), 1)
        return out.value

    def paced_init(self, cycle=0):
        return self.lib.musashi_scanline_timer_init_paced(
            C.byref(self.timer), cycle)

    def advance(self, cycle):
        return self.lib.musashi_scanline_timer_advance(
            C.byref(self.timer), cycle)

    def vblank_at(self, cycle):
        return self.lib.musashi_scanline_timer_vblank_at(
            C.byref(self.timer), cycle)

    def test_retail_mode_waits_for_first_vblank_then_counts_hblank_edges(self):
        self.assertEqual(self.write(0x107), 1)
        self.assertEqual(self.read(0x1F801114), 0x507)
        for _ in range(3): self.lib.musashi_scanline_timer_hblank(self.timer)
        self.assertEqual(self.read(), 0)
        self.lib.musashi_scanline_timer_vblank(self.timer)
        self.assertEqual(self.read(), 0)
        for _ in range(3): self.lib.musashi_scanline_timer_hblank(self.timer)
        self.assertEqual(self.read(), 3)
        self.lib.musashi_scanline_timer_vblank(self.timer)
        self.lib.musashi_scanline_timer_hblank(self.timer)
        self.assertEqual(self.read(), 4)

    def test_mode_rewrite_restarts_synchronization_and_clears_counter_flags(self):
        self.write(0x107)
        self.lib.musashi_scanline_timer_vblank(self.timer)
        self.timer.count = 0xFFFE
        self.timer.target = 0xFFFF
        self.lib.musashi_scanline_timer_hblank(self.timer)
        self.assertEqual(self.read(0x1F801114), 0x1D07)
        self.assertEqual(self.read(0x1F801114), 0x507)
        self.lib.musashi_scanline_timer_hblank(self.timer)
        self.assertEqual(self.read(), 0)
        self.write(0x107)
        self.assertEqual((self.timer.count, self.timer.mode, self.timer.running), (0, 0x507, 0))
        self.assertEqual(self.timer.target, 0xFFFF)

    def test_unimplemented_modes_and_addresses_fail_without_mutation(self):
        self.write(0x107)
        before = bytes(self.timer)
        for mode in (0, 0x100, 0x117, 0x127, 0x207):
            self.assertEqual(self.write(mode), 0)
            self.assertEqual(bytes(self.timer), before)
        for address in (0x1F801110, 0x1F801104, 0x1F801115, 0x80000000):
            self.assertEqual(self.write(0x107, address), 0)
            self.assertEqual(bytes(self.timer), before)
        out = C.c_uint16(0xABCD)
        self.assertEqual(self.lib.musashi_scanline_timer_read16(self.timer, 0x1F801104, out), 0)
        self.assertEqual(out.value, 0xABCD)

    def test_paced_init_zeroes_fresh_state_and_waits_for_first_vblank(self):
        self.timer.count = 99
        self.timer.mode = 0x1D07
        self.timer.target = 0xFFFF
        self.timer.running = 1
        self.timer.configured = 1
        self.timer.last_cycles = 123
        self.timer.subcycle = 77
        self.timer.faulted = 1
        self.assertEqual(self.paced_init(100), 1)
        self.assertEqual((self.timer.count, self.timer.mode, self.timer.target,
                          self.timer.running, self.timer.configured,
                          self.timer.last_cycles, self.timer.subcycle,
                          self.timer.paced, self.timer.faulted),
                         (0, 0, 0, 0, 0, 100, 0, 1, 0))
        self.assertEqual(self.advance(1000), 1)
        self.assertEqual((self.timer.count, self.timer.subcycle,
                          self.timer.last_cycles), (0, 0, 1000))
        self.assertEqual(self.write(0x107), 1)
        self.assertEqual(self.vblank_at(1500), 1)
        self.assertEqual((self.timer.count, self.timer.subcycle,
                          self.timer.running, self.timer.last_cycles),
                         (0, 0, 1, 1500))

    def test_paced_fractional_cycles_are_partition_invariant(self):
        one = Timer()
        split = Timer()
        self.assertEqual(self.lib.musashi_scanline_timer_init_paced(
            C.byref(one), 0), 1)
        self.assertEqual(self.lib.musashi_scanline_timer_init_paced(
            C.byref(split), 0), 1)
        for timer in (one, split):
            self.assertEqual(self.lib.musashi_scanline_timer_write32(
                C.byref(timer), 0x1F801114, 0x107), 1)
            self.assertEqual(self.lib.musashi_scanline_timer_vblank_at(
                C.byref(timer), 0), 1)
        total = 2146 * 5 + 7
        self.assertEqual(self.lib.musashi_scanline_timer_advance(
            C.byref(one), total), 1)
        for cycle in (1000, 2146, total):
            self.assertEqual(self.lib.musashi_scanline_timer_advance(
                C.byref(split), cycle), 1)
        self.assertEqual((one.count, one.subcycle, one.last_cycles),
                         (split.count, split.subcycle, split.last_cycles))
        self.assertEqual((one.count, one.subcycle), (5, 7))

    def test_paced_vblank_rearms_only_the_first_gate_edge(self):
        self.assertEqual(self.paced_init(10), 1)
        self.assertEqual(self.advance(100), 1)
        self.assertEqual(self.write(0x107), 1)
        self.assertEqual((self.timer.last_cycles, self.timer.count,
                          self.timer.subcycle, self.timer.running),
                         (100, 0, 0, 0))
        self.assertEqual(self.vblank_at(100), 1)
        self.assertEqual(self.advance(100 + 2 * 2146 + 9), 1)
        self.assertEqual((self.timer.count, self.timer.subcycle), (2, 9))
        self.assertEqual(self.vblank_at(100 + 2 * 2146 + 9), 1)
        self.assertEqual((self.timer.count, self.timer.subcycle), (2, 9))

    def test_paced_target_zero_overflows_at_ffff_without_target_flag(self):
        self.assertEqual(self.paced_init(), 1)
        self.assertEqual(self.write(0x107), 1)
        self.assertEqual(self.vblank_at(0), 1)
        self.assertEqual(self.advance(2146 * 65534), 1)
        self.assertEqual(self.timer.count, 0xFFFE)
        self.assertEqual(self.timer.mode & 0x1800, 0)
        self.assertEqual(self.advance(2146 * 65535), 1)
        self.assertEqual(self.timer.count, 0)
        self.assertEqual(self.timer.mode & 0x1000, 0x1000)
        self.assertEqual(self.timer.mode & 0x0800, 0)

    def test_paced_reads_do_not_advance_and_clear_status_flags_only(self):
        self.assertEqual(self.paced_init(), 1)
        self.assertEqual(self.write(0x107), 1)
        self.assertEqual(self.vblank_at(0), 1)
        self.assertEqual(self.advance(2146 + 3), 1)
        before = (self.timer.count, self.timer.mode, self.timer.last_cycles,
                  self.timer.subcycle)
        self.assertEqual(self.read(), 1)
        self.assertEqual((self.timer.count, self.timer.mode,
                          self.timer.last_cycles, self.timer.subcycle), before)
        self.timer.mode = 0x1507
        self.assertEqual(self.read(0x1F801114), 0x1507)
        self.assertEqual(self.timer.mode & 0x1800, 0)

    def test_paced_absolute_boundaries_and_backward_time_fault_stickily(self):
        self.assertEqual(self.paced_init(0), 1)
        self.assertEqual(self.write(0x107), 1)
        self.assertEqual(self.vblank_at(0), 1)
        self.assertEqual(self.advance(0xFFFFFFFFFFFFFFFF), 1)
        self.assertEqual(self.timer.last_cycles, 0xFFFFFFFFFFFFFFFF)
        snapshot = bytes(self.timer)
        self.assertEqual(self.advance(0xFFFFFFFFFFFFFFFE), 0)
        self.assertEqual(self.timer.faulted, 1)
        self.assertEqual(self.timer.last_cycles, 0xFFFFFFFFFFFFFFFF)
        self.assertEqual(bytes(self.timer)[:32], snapshot[:32])

    def test_paced_invalid_profile_and_legacy_clock_mix_refuse(self):
        self.assertEqual(self.paced_init(), 1)
        self.timer.target = 1
        before = bytes(self.timer)
        self.assertEqual(self.advance(1), 0)
        self.assertEqual(self.timer.faulted, 1)
        self.assertEqual(self.timer.last_cycles, 0)
        self.assertEqual(bytes(self.timer)[:32], before[:32])

        fresh = Timer()
        self.assertEqual(self.lib.musashi_scanline_timer_init_paced(
            C.byref(fresh), 0), 1)
        self.lib.musashi_scanline_timer_hblank(C.byref(fresh))
        self.assertEqual(fresh.faulted, 1)
        self.assertEqual(self.lib.musashi_scanline_timer_advance(
            C.byref(fresh), 1), 0)

    def test_paced_corrupt_bounded_state_faults_before_mutating_time_or_output(self):
        self.assertEqual(self.paced_init(), 1)
        self.timer.subcycle = 2146
        self.assertEqual(self.advance(1), 0)
        self.assertEqual((self.timer.faulted, self.timer.last_cycles), (1, 0))

        self.assertEqual(self.paced_init(), 1)
        self.assertEqual(self.write(0x107), 1)
        self.assertEqual(self.vblank_at(0), 1)
        self.timer.mode = 0x707
        output = C.c_uint16(0xABCD)
        self.assertEqual(self.lib.musashi_scanline_timer_read16(
            C.byref(self.timer), 0x1F801110, output), 0)
        self.assertEqual((self.timer.faulted, output.value), (1, 0xABCD))

        self.assertEqual(self.paced_init(), 1)
        self.assertEqual(self.write(0x107), 1)
        self.assertEqual(self.vblank_at(0), 1)
        self.timer.count = 0xFFFF
        self.assertEqual(self.write(0x107), 0)
        self.assertEqual((self.timer.faulted, self.timer.count), (1, 0xFFFF))

        self.assertEqual(self.paced_init(), 1)
        self.assertEqual(self.write(0x107), 1)
        self.timer.subcycle = 1
        self.assertEqual(self.vblank_at(0), 0)
        self.assertEqual((self.timer.faulted, self.timer.last_cycles), (1, 0))

        self.assertEqual(self.paced_init(), 1)
        self.timer.configured = 2
        self.assertEqual(self.advance(0), 0)
        self.assertEqual(self.timer.faulted, 1)

    def test_paced_unsupported_mmio_preserves_output_and_state(self):
        self.assertEqual(self.paced_init(), 1)
        before = bytes(self.timer)
        for address, value in ((0x1F801110, 0x107),
                               (0x1F801114, 0x117),
                               (0x80000000, 0x107)):
            self.assertEqual(self.write(value, address), 0)
            self.assertEqual(bytes(self.timer), before)
        output = C.c_uint16(0xABCD)
        self.assertEqual(self.lib.musashi_scanline_timer_read16(
            C.byref(self.timer), 0x1F801110, output), 0)
        self.assertEqual(output.value, 0xABCD)


if __name__ == "__main__":
    unittest.main()
