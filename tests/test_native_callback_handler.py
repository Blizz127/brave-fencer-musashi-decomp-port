"""Shared-RAM handler semantics; callback execution here is explicitly synthetic."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SLOTS = 0x8006CB98
TICK = 0x8006CBB8


class Memory(C.Structure):
    _fields_ = [("bytes", C.c_ubyte * 0x200000)]


EXECUTE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Memory), C.c_uint32)


class NativeHandlerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-native-handler-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "handler.so"
        subprocess.run([
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-DMUSASHI_NATIVE_CALLBACK_HANDLER=1",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/libetc_vsync_handler.c"), str(ROOT / "pc_port/boot_memory.c"),
            "-lcrypto", "-o", str(library),
        ], check=True, capture_output=True, stdin=subprocess.DEVNULL, timeout=60)
        cls.library = C.CDLL(str(library))
        cls.call = cls.library.musashi_boot_call_80042ce8
        cls.call.argtypes = [C.POINTER(Memory), EXECUTE, C.c_void_p]
        cls.call.restype = C.c_int

    def setUp(self):
        self.memory = Memory()

    def put(self, address, value):
        struct.pack_into("<I", self.memory, address - 0x80000000, value)

    def get(self, address):
        return struct.unpack_from("<I", self.memory, address - 0x80000000)[0]

    def test_empty_slots_wrap_tick_without_executor_and_preserve_all_other_ram(self):
        self.put(TICK, 0xFFFFFFFF)
        before = bytearray(self.memory)
        struct.pack_into("<I", before, TICK - 0x80000000, 0)
        self.assertEqual(self.call(self.memory, EXECUTE(), None), 1)
        self.assertEqual(bytes(self.memory), before)

    def test_callbacks_see_live_ram_and_can_replace_later_slots(self):
        self.put(SLOTS, 0x80001000)
        self.put(SLOTS + 28, 0x80002000)
        self.put(TICK, 41)
        trace = []

        def execute(_, memory, target):
            trace.append((target, self.get(TICK), C.addressof(memory.contents)))
            if target == 0x80001000:
                self.put(SLOTS, 0)
                self.put(SLOTS + 28, 0x80003000)
                self.put(TICK, 99)
                return 1
            return target == 0x80003000

        self.assertEqual(self.call(self.memory, EXECUTE(execute), None), 1)
        self.assertEqual(trace, [(0x80001000, 42, C.addressof(self.memory)),
                                 (0x80003000, 99, C.addressof(self.memory))])
        self.assertEqual(self.get(TICK), 99)

    def test_refused_callback_stops_before_later_slots_and_keeps_prior_effects(self):
        self.put(SLOTS, 0x80001000)
        self.put(SLOTS + 4, 0x80002000)
        trace = []

        def refuse(_, memory, target):
            trace.append(target)
            self.put(0x80070000, 123)
            return 0

        self.assertEqual(self.call(self.memory, EXECUTE(refuse), None), 0)
        self.assertEqual(trace, [0x80001000])
        self.assertEqual(self.get(TICK), 1)
        self.assertEqual(self.get(0x80070000), 123)

    def test_missing_executor_cannot_skip_a_nonzero_slot(self):
        self.put(SLOTS + 28, 0x80003000)
        self.assertEqual(self.call(self.memory, EXECUTE(), None), 0)
        self.assertEqual(self.get(TICK), 1)
        self.assertEqual(self.call(None, EXECUTE(), None), 0)
