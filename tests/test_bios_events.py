"""Bounded BIOS event table services with explicit callback execution."""
import ctypes as C
from pathlib import Path
import hashlib
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CAPACITY = 22
EVENT_WORDS = 7
EVENT_BYTES = CAPACITY * EVENT_WORDS * 4


class Event(C.Structure):
    _fields_ = [("class_word", C.c_uint32), ("status", C.c_uint32),
                ("spec", C.c_uint32), ("mode", C.c_uint32),
                ("callback", C.c_uint32), ("reserved0", C.c_uint32),
                ("reserved1", C.c_uint32)]


class Events(C.Structure):
    _fields_ = [("records", Event * CAPACITY),
                ("allocation_serial", C.c_uint64 * CAPACITY)]


EXECUTE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)


class BiosEventTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-bios-events-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "bios_events.so"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/bios_events.c"), "-o", str(library),
        ], check=True, stdin=subprocess.DEVNULL, timeout=30)
        cls.lib = C.CDLL(str(library))
        cls.lib.musashi_bios_events_init.argtypes = [C.POINTER(Events)]
        cls.lib.musashi_bios_events_open.argtypes = [
            C.POINTER(Events), C.c_uint32, C.c_uint32, C.c_uint32,
            C.c_uint32, C.POINTER(C.c_int32)]
        for name in ("close", "enable", "disable", "test"):
            function = getattr(cls.lib, "musashi_bios_events_" + name)
            function.argtypes = [C.POINTER(Events), C.c_int32,
                                 C.POINTER(C.c_int32)]
        cls.lib.musashi_bios_events_deliver.argtypes = [
            C.POINTER(Events), C.c_uint32, C.c_uint32, EXECUTE, C.c_void_p]
        cls.lib.musashi_bios_events_undeliver.argtypes = [
            C.POINTER(Events), C.c_uint32, C.c_uint32]

    def setUp(self):
        self.events = Events()
        self.lib.musashi_bios_events_init(self.events)

    def open(self, class_word=0xF0000011, spec=0x40, mode=0x1000,
             callback=0):
        result = C.c_int32(0x12345678)
        accepted = self.lib.musashi_bios_events_open(
            self.events, class_word, spec, mode, callback, C.byref(result))
        return accepted, result.value

    def service(self, name, handle):
        result = C.c_int32(0x12345678)
        accepted = getattr(self.lib, "musashi_bios_events_" + name)(
            self.events, handle, C.byref(result))
        return accepted, result.value

    def record_bytes(self):
        return bytes(self.events.records)

    def test_init_and_open_write_only_the_first_five_words(self):
        self.assertEqual(self.record_bytes(), bytes(EVENT_BYTES))
        self.assertEqual(list(self.events.allocation_serial), [0] * CAPACITY)
        accepted, handle = self.open(0xF4000001, 0x8000, 0x1000, 0x800617CC)
        self.assertEqual((accepted, C.c_uint32(handle).value), (1, 0xF1000000))
        self.assertEqual(self.events.allocation_serial[0], 1)
        self.assertEqual(tuple(getattr(self.events.records[0], field)
                               for field, _ in Event._fields_),
                         (0xF4000001, 0x1000, 0x8000, 0x1000,
                          0x800617CC, 0, 0))
        self.events.records[0].reserved0 = 0xA5A5A5A5
        self.events.records[0].reserved1 = 0x5A5A5A5A
        self.assertEqual(self.service("close", handle), (1, 1))
        accepted, reopened = self.open(0xF0000011, 0x10, 0x2000, 0x80061808)
        self.assertEqual((accepted, C.c_uint32(reopened).value), (1, 0xF1000000))
        self.assertEqual(self.events.allocation_serial[0], 2)
        self.assertEqual(tuple(getattr(self.events.records[0], field)
                               for field, _ in Event._fields_),
                         (0xF0000011, 0x1000, 0x10, 0x2000,
                          0x80061808, 0xA5A5A5A5, 0x5A5A5A5A))

    def test_exhaustion_and_reuse_use_lowest_free_slot_and_preserve_metadata(self):
        handles = []
        for index in range(CAPACITY):
            accepted, handle = self.open(spec=index + 1)
            self.assertEqual(accepted, 1)
            self.assertEqual(C.c_uint32(handle).value, 0xF1000000 | index)
            handles.append(handle)
        before = bytes(self.events)
        accepted, result = self.open(spec=0xDEAD)
        self.assertEqual((accepted, result), (1, -1))
        self.assertEqual(bytes(self.events), before)
        self.events.records[7].reserved0 = 0xAABBCCDD
        self.events.records[7].reserved1 = 0x11223344
        self.assertEqual(self.service("close", C.c_int32(0xABCD0007).value), (1, 1))
        accepted, handle = self.open(spec=0xBEEF, mode=0x2000, callback=9)
        self.assertEqual((accepted, C.c_uint32(handle).value), (1, 0xF1000007))
        self.assertEqual(self.events.allocation_serial[7], 2)
        self.assertEqual(tuple(getattr(self.events.records[7], field)
                               for field, _ in Event._fields_),
                          (0xF0000011, 0x1000, 0xBEEF, 0x2000, 9,
                          0xAABBCCDD, 0x11223344))
        self.assertEqual(self.service("enable", handles[7]), (1, 1))
        self.assertEqual(self.events.records[7].status, 0x2000)

    def test_close_enable_disable_and_test_event_apply_status_rules(self):
        _, handle = self.open()
        self.assertEqual(self.service("test", handle), (1, 0))
        self.assertEqual(self.service("enable", handle), (1, 1))
        self.assertEqual(self.events.records[0].status, 0x2000)
        self.events.records[0].status = 0x4000
        self.assertEqual(self.service("test", C.c_int32(0xABCD0000).value), (1, 1))
        self.assertEqual(self.events.records[0].status, 0x2000)
        self.assertEqual(self.service("test", handle), (1, 0))
        self.assertEqual(self.service("disable", handle), (1, 1))
        self.assertEqual(self.events.records[0].status, 0x1000)
        self.assertEqual(self.service("close", handle), (1, 1))
        self.assertEqual(self.service("enable", handle), (1, 1))
        self.assertEqual(self.events.records[0].status, 0)

    def test_invalid_low16_indices_are_refused_without_output_or_mutation(self):
        _, handle = self.open()
        before = bytes(self.events)
        for bad_handle in (22, 0xABCD0016, -1, 0x7FFFFFFF):
            for name in ("close", "enable", "disable", "test"):
                result = C.c_int32(0x12345678)
                accepted = getattr(self.lib, "musashi_bios_events_" + name)(
                    self.events, bad_handle, C.byref(result))
                self.assertEqual((accepted, result.value), (0, 0x12345678),
                                 (name, bad_handle))
                self.assertEqual(bytes(self.events), before)
        self.assertEqual(handle, C.c_int32(0xF1000000).value)

    def test_null_arguments_are_refused_and_outputs_are_unchanged(self):
        self.lib.musashi_bios_events_init(None)
        result = C.c_int32(0x12345678)
        self.assertEqual(self.lib.musashi_bios_events_open(
            None, 1, 2, 3, 4, C.byref(result)), 0)
        self.assertEqual(result.value, 0x12345678)
        self.assertEqual(self.lib.musashi_bios_events_open(
            self.events, 1, 2, 3, 4, None), 0)
        for name in ("close", "enable", "disable", "test"):
            result.value = 0x12345678
            self.assertEqual(getattr(self.lib, "musashi_bios_events_" + name)(
                None, 0, C.byref(result)), 0)
            self.assertEqual(result.value, 0x12345678)
            self.assertEqual(getattr(self.lib, "musashi_bios_events_" + name)(
                self.events, 0, None), 0)
        self.assertEqual(self.lib.musashi_bios_events_deliver(
            None, 1, 2, EXECUTE(), None), 0)
        self.assertEqual(self.lib.musashi_bios_events_undeliver(None, 1, 2), 0)

    def test_allocation_serial_overflow_refuses_selected_slot_without_effects(self):
        self.events.allocation_serial[0] = C.c_uint64(-1).value
        before = bytes(self.events)
        result = C.c_int32(0x12345678)
        self.assertEqual(self.lib.musashi_bios_events_open(
            self.events, 1, 2, 0x1000, 3, C.byref(result)), 0)
        self.assertEqual(result.value, 0x12345678)
        self.assertEqual(bytes(self.events), before)

    def test_deliver_polls_and_runs_callbacks_in_live_index_order(self):
        first = self.events.records[0]
        first.class_word, first.spec, first.status, first.mode = 7, 9, 0x2000, 0x2000
        second = self.events.records[1]
        second.class_word, second.spec, second.status, second.mode = 7, 9, 0x2000, 0x1000
        second.callback = 0x800617CC
        third = self.events.records[2]
        third.class_word, third.spec, third.status, third.mode = 7, 9, 0x2000, 0x4000
        fourth = self.events.records[3]
        fourth.class_word, fourth.spec, fourth.status, fourth.mode = 7, 8, 0x2000, 0x2000
        fifth = self.events.records[4]
        fifth.class_word, fifth.spec, fifth.status, fifth.mode = 7, 9, 0x2000, 0x1000
        calls = []

        def execute(_, target):
            calls.append(target)
            self.events.records[1].status = 0x4000
            return 1

        callback = EXECUTE(execute)
        self.assertEqual(self.lib.musashi_bios_events_deliver(
            self.events, 7, 9, callback, None), 1)
        self.assertEqual(calls, [0x800617CC])
        self.assertEqual((first.status, second.status, third.status, fourth.status,
                          fifth.status),
                         (0x4000, 0x4000, 0x2000, 0x2000, 0x2000))
        self.assertEqual(self.lib.musashi_bios_events_undeliver(self.events, 7, 9), 1)
        self.assertEqual((first.status, second.status), (0x2000, 0x4000))
        self.assertEqual(self.lib.musashi_bios_events_undeliver(self.events, 7, 8), 1)
        self.assertEqual(fourth.status, 0x2000)

    def test_deliver_refusal_preserves_prior_effects_and_observes_live_mutation(self):
        for index in range(3):
            record = self.events.records[index]
            record.class_word, record.spec, record.status = 3, 4, 0x2000
        self.events.records[0].mode = 0x2000
        self.events.records[1].mode = 0x1000
        self.events.records[1].callback = 0x80060000
        self.events.records[2].mode = 0x2000

        def refuse(_, target):
            self.assertEqual(target, 0x80060000)
            return 0

        callback = EXECUTE(refuse)
        self.assertEqual(self.lib.musashi_bios_events_deliver(
            self.events, 3, 4, callback, None), 0)
        self.assertEqual((self.events.records[0].status,
                          self.events.records[1].status,
                          self.events.records[2].status), (0x4000, 0x2000, 0x2000))

        self.events.records[1].callback = 0x80060100

        def mutate_later(_, target):
            self.assertEqual(target, 0x80060100)
            self.events.records[2].status = 0
            return 1

        callback = EXECUTE(mutate_later)
        self.assertEqual(self.lib.musashi_bios_events_deliver(
            self.events, 3, 4, callback, None), 1)
        self.assertEqual((self.events.records[0].status,
                          self.events.records[1].status,
                          self.events.records[2].status), (0x4000, 0x2000, 0))

    def test_sync_spu_testevent_completes_hwspu_poll_without_dma_irq(self):
        """8003A84C is a synchronous FIFO; retail DMA IRQ 8003AA18 would
        DeliverEvent(F0000009, 0x20). TestEvent of that poll handle must
        observe completion so 8003C4F0 can return 1. Other classes stay
        ordinary TestEvent misses."""
        fn = getattr(self.lib, "musashi_bios_events_test_sync_spu", None)
        self.assertIsNotNone(fn)
        fn.argtypes = [C.POINTER(Events), C.c_int32, C.POINTER(C.c_int32)]
        fn.restype = C.c_int
        accepted, handle = self.open(0xF0000009, 0x20, 0x2000, 0)
        self.assertEqual((accepted, C.c_uint32(handle).value), (1, 0xF1000000))
        self.assertEqual(self.service("enable", handle), (1, 1))
        self.assertEqual(self.events.records[0].status, 0x2000)
        self.assertEqual(self.service("test", handle), (1, 0))
        result = C.c_int32(0x12345678)
        self.assertEqual(fn(self.events, handle, C.byref(result)), 1)
        self.assertEqual(result.value, 1)
        self.assertEqual(self.events.records[0].status, 0x2000)
        self.assertEqual(self.service("test", handle), (1, 0))
        accepted, cd_handle = self.open(0xF0000003, 0x20, 0x2000, 0)
        self.assertEqual(accepted, 1)
        self.assertEqual(self.service("enable", cd_handle), (1, 1))
        result.value = 0x12345678
        self.assertEqual(fn(self.events, cd_handle, C.byref(result)), 1)
        self.assertEqual(result.value, 0)
        self.assertEqual(self.events.records[1].status, 0x2000)
        result.value = 0x12345678
        self.assertEqual(fn(None, handle, C.byref(result)), 0)
        self.assertEqual(result.value, 0x12345678)
        self.assertEqual(fn(self.events, handle, None), 0)

    def test_retail_snapshot_closures_match_captured_after_state(self):
        before_path = ROOT / "artifacts/bios-remove-20260905/retail-run3/before.ram"
        after_path = ROOT / "artifacts/bios-remove-20260905/retail-run3/after.ram"
        if not before_path.exists() or not after_path.exists():
            self.skipTest("retail BIOS snapshot fixture is not available")
        before = before_path.read_bytes()
        after = after_path.read_bytes()
        self.assertEqual(hashlib.sha256(before).hexdigest(),
                         "2516dfa6a77d505e088210d1fe02e52cb5f087fc483bc8ae74c892cde2f7467b")
        self.assertEqual(hashlib.sha256(after).hexdigest(),
                         "c1838bc592abe991577ef93dd5d29cd9f5e366d941d91a96c26ba08d8a6fff3a")
        C.memmove(C.addressof(self.events.records),
                  before[0xE028:0xE028 + EVENT_BYTES], EVENT_BYTES)
        for index in range(5):
            self.assertEqual(self.service("close", C.c_int32(0xF1000000 | index).value),
                             (1, 1))
        self.assertEqual(self.record_bytes(), after[0xE028:0xE028 + EVENT_BYTES])


if __name__ == "__main__":
    unittest.main()
