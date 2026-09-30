"""Native ownership of the BIOS auto-ack switches used by registration."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Policy(C.Structure):
    _fields_ = [("pad", C.c_int32), ("timers", C.c_int32 * 4),
                ("pad_initialized", C.c_int),
                ("timer_initialized", C.c_int * 4)]


class IrqPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-irq-policy-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "policy.so"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-shared", "-fPIC", "-I", str(ROOT / "pc_port/include"),
                        str(ROOT / "pc_port/irq_policy.c"), "-o", str(library)],
                       check=True, stdin=subprocess.DEVNULL, timeout=30)
        cls.lib = C.CDLL(str(library))
        cls.lib.musashi_irq_policy_init.argtypes = [C.POINTER(Policy)]
        cls.lib.musashi_irq_policy_change_pad.argtypes = [C.POINTER(Policy), C.c_int32]
        cls.lib.musashi_irq_policy_exchange_pad.argtypes = [
            C.POINTER(Policy), C.c_int32, C.POINTER(C.c_int32)]
        cls.lib.musashi_irq_policy_change_timer.argtypes = [C.POINTER(Policy), C.c_int32, C.c_int32]
        cls.lib.musashi_irq_policy_exchange_timer.argtypes = [
            C.POINTER(Policy), C.c_int32, C.c_int32, C.POINTER(C.c_int32)]
        cls.lib.musashi_irq_policy_custom_vblank.argtypes = [C.POINTER(Policy)]

    def test_checked_pad_exchange_refuses_unknown_without_writing_output(self):
        p = Policy()
        self.lib.musashi_irq_policy_init(p)
        old = C.c_int32(0x13579BDF)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_pad(p, 7, C.byref(old)), 0)
        self.assertEqual(old.value, 0x13579BDF)
        self.assertEqual(p.pad, -1)
        self.assertEqual(p.pad_initialized, 0)

    def test_checked_pad_exchange_preserves_full_signed32_old_value(self):
        p = Policy()
        self.lib.musashi_irq_policy_init(p)
        self.assertEqual(self.lib.musashi_irq_policy_change_pad(p, -1), 1)
        self.assertEqual(p.pad_initialized, 1)
        old = C.c_int32(0)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_pad(
            p, C.c_int32(0x80000000).value, C.byref(old)), 1)
        self.assertEqual(old.value, -1)
        self.assertEqual(p.pad, -2147483648)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_pad(
            p, C.c_int32(0xFFFFFFFF).value, C.byref(old)), 1)
        self.assertEqual(old.value, -2147483648)
        self.assertEqual(p.pad, -1)

    def test_checked_exchange_requires_output_pointer_and_legacy_wrapper_still_establishes(self):
        p = Policy()
        self.lib.musashi_irq_policy_init(p)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_pad(p, 0, None), 0)
        self.assertEqual(p.pad_initialized, 0)
        self.assertEqual(self.lib.musashi_irq_policy_change_pad(p, 0), 1)
        self.assertEqual(p.pad_initialized, 1)
        old = C.c_int32(99)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_pad(p, -7, C.byref(old)), 1)
        self.assertEqual(old.value, 0)
        self.assertEqual(p.pad, -7)

    def test_both_bios_consumers_must_relinquish_ack_before_custom_delivery(self):
        p = Policy()
        self.lib.musashi_irq_policy_init(p)
        self.assertEqual(self.lib.musashi_irq_policy_custom_vblank(p), 0)
        self.assertEqual(self.lib.musashi_irq_policy_change_pad(p, 0), 1)
        self.assertEqual(self.lib.musashi_irq_policy_custom_vblank(p), 0)
        self.assertEqual(self.lib.musashi_irq_policy_change_timer(p, 3, 0), 1)
        self.assertEqual(self.lib.musashi_irq_policy_custom_vblank(p), 1)
        self.assertEqual(self.lib.musashi_irq_policy_change_pad(p, 1), 1)
        self.assertEqual(self.lib.musashi_irq_policy_custom_vblank(p), 0)
        self.lib.musashi_irq_policy_change_pad(p, 0)
        self.lib.musashi_irq_policy_change_timer(p, 3, 1)
        self.assertEqual(self.lib.musashi_irq_policy_custom_vblank(p), 0)

    def test_counter_policy_slots_are_independent_and_bad_indices_are_refused(self):
        p = Policy()
        self.lib.musashi_irq_policy_init(p)
        self.assertEqual(list(p.timer_initialized), [0, 0, 0, 0])
        for index in range(4):
            self.assertEqual(self.lib.musashi_irq_policy_change_timer(p, index, index + 10), 1)
        self.assertEqual(list(p.timers), [10, 11, 12, 13])
        self.assertEqual(list(p.timer_initialized), [1, 1, 1, 1])
        before = bytes(p)
        for index in (-1, 4, 0x7FFFFFFF):
            self.assertEqual(self.lib.musashi_irq_policy_change_timer(p, index, 0), 0)
            self.assertEqual(bytes(p), before)
        self.assertEqual(self.lib.musashi_irq_policy_change_pad(None, 0), 0)
        self.assertEqual(self.lib.musashi_irq_policy_custom_vblank(None), 0)

    def test_checked_timer_exchange_refuses_unknown_null_and_bad_channels(self):
        p = Policy()
        self.lib.musashi_irq_policy_init(p)
        old = C.c_int32(0x13579BDF)
        before = bytes(p)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_timer(
            p, 1, 7, C.byref(old)), 0)
        self.assertEqual(old.value, 0x13579BDF)
        self.assertEqual(bytes(p), before)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_timer(
            None, 1, 7, C.byref(old)), 0)
        self.assertEqual(old.value, 0x13579BDF)
        for channel in (-1, 4, 0x7FFFFFFF):
            before = bytes(p)
            self.assertEqual(self.lib.musashi_irq_policy_exchange_timer(
                p, channel, 7, C.byref(old)), 0)
            self.assertEqual(old.value, 0x13579BDF)
            self.assertEqual(bytes(p), before)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_timer(
            p, 1, 7, None), 0)
        self.assertEqual(bytes(p), before)

    def test_checked_timer_exchange_preserves_full_signed32_old_value(self):
        p = Policy()
        self.lib.musashi_irq_policy_init(p)
        self.assertEqual(self.lib.musashi_irq_policy_change_timer(p, 1, -1), 1)
        self.assertEqual(p.timer_initialized[1], 1)
        old = C.c_int32(0)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_timer(
            p, 1, C.c_int32(0x80000000).value, C.byref(old)), 1)
        self.assertEqual(old.value, -1)
        self.assertEqual(p.timers[1], -2147483648)
        self.assertEqual(p.timer_initialized[1], 1)
        self.assertEqual(self.lib.musashi_irq_policy_exchange_timer(
            p, 1, C.c_int32(0xFFFFFFFF).value, C.byref(old)), 1)
        self.assertEqual(old.value, -2147483648)
        self.assertEqual(p.timers[1], -1)


if __name__ == "__main__":
    unittest.main()
