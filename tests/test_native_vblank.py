"""Real shared VBlank chain with explicitly synthetic request/completion code."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INDEX = 0x80072A34
PENDING = 0x80078C80
SAVED = 0x80078CC4
COMPLETION = 0x80078CC0
TARGETS = 0x80078D78
TICK = 0x8006CBB8


class Memory(C.Structure):
    _fields_ = [("bytes", C.c_ubyte * 0x200000)]


ONE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Memory), C.c_uint32,
                  C.c_uint32, C.POINTER(C.c_uint32))
TWO = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Memory), C.c_uint32,
                  C.c_uint32, C.c_uint32)
ROUTE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Memory), C.c_uint32)


class Executor(C.Structure):
    _fields_ = [("userdata", C.c_void_p), ("one", ONE), ("two", TWO)]


class NativeVblankTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-vblank-native-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "vblank.so"
        subprocess.run([
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-DMUSASHI_NATIVE_VBLANK=1", "-DMUSASHI_NATIVE_CALLBACK_HANDLER=1",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/libmcrd_vblank.c"),
            str(ROOT / "pc_port/libetc_vsync_handler.c"),
            str(ROOT / "pc_port/vblank_bindings.c"), str(ROOT / "pc_port/boot_memory.c"),
            "-lcrypto", "-o", str(library),
        ], check=True, stdin=subprocess.DEVNULL, timeout=60, capture_output=True)
        cls.library = C.CDLL(str(library))
        cls.handler = cls.library.musashi_boot_call_80042ce8
        cls.handler.argtypes = [C.POINTER(Memory), ROUTE, C.c_void_p]
        cls.handler.restype = C.c_int
        cls.route = ROUTE(("musashi_boot_execute_vblank_callback", cls.library))

    def setUp(self):
        self.memory = Memory()
        self.put(0x8006CBB4, 0x800616D0)  # observed slot-seven target
        self.put(TARGETS, 0x800617CC)
        self.put(COMPLETION, 0x80061808)
        self.put(PENDING, 17)
        self.put(PENDING + 4, 23)
        self.trace = []
        self.request_result = 0
        self.request_effect = None
        self.refuse = None

        def one(_, memory, target, argument, result):
            self.trace.append(("one", target, argument))
            if self.request_effect:
                self.request_effect()
            result[0] = self.request_result
            return self.refuse != "one"

        def two(_, memory, target, first, second):
            self.trace.append(("two", target, first, second, self.get(PENDING),
                               self.get(PENDING + 4), self.get(PENDING + 8)))
            return self.refuse != "two"

        self.executor = Executor(None, ONE(one), TWO(two))

    def put(self, address, value):
        struct.pack_into("<I", self.memory, address - 0x80000000, value)

    def get(self, address):
        return struct.unpack_from("<I", self.memory, address - 0x80000000)[0]

    def call(self, executor=True):
        return self.handler(self.memory, self.route,
                            C.byref(self.executor) if executor else None)

    def test_idle_chain_needs_no_external_executor_and_only_changes_tick(self):
        self.put(INDEX, 0xFFFFFFFF)
        self.put(TICK, 0x93)
        expected = bytearray(self.memory)
        struct.pack_into("<I", expected, TICK - 0x80000000, 0x94)
        self.assertEqual(self.call(executor=False), 1)
        self.assertEqual(bytes(self.memory), expected)

    def test_busy_request_keeps_pending_values_and_does_not_complete(self):
        self.assertEqual(self.call(), 1)
        self.assertEqual(self.trace, [("one", 0x800617CC, 0x80078D38)])
        self.assertEqual((self.get(INDEX), self.get(PENDING), self.get(PENDING + 4)), (0, 17, 23))
        self.assertEqual(self.get(PENDING + 8), 0)

    def test_success_reads_live_pending_state_then_publishes_and_clears_before_completion(self):
        self.request_result = 1
        self.request_effect = lambda: self.put(PENDING, 0xFFFFFFFF)
        self.assertEqual(self.call(), 1)
        self.assertEqual(self.get(INDEX), 0xFFFFFFFF)
        self.assertEqual((self.get(SAVED), self.get(SAVED + 4)), (0xFFFFFFFF, 23))
        self.assertEqual(self.trace[-1], ("two", 0x80061808, 0xFFFFFFFF, 23, 0, 0, 1))

    def test_decrement_reloads_the_changed_index_and_wraps_as_a_guest_word(self):
        self.request_result = 1
        for changed, expected in ((2, 1), (0x80000000, 0x7FFFFFFF)):
            self.put(INDEX, 0)
            self.trace.clear()
            self.request_effect = lambda: self.put(INDEX, changed)
            self.assertEqual(self.call(), 1)
            self.assertEqual(self.get(INDEX), expected)
            self.assertEqual(len(self.trace), 1)

    def test_refusal_never_becomes_a_completed_request_or_callback(self):
        self.refuse = "one"
        self.request_result = 1
        self.request_effect = lambda: self.put(PENDING, 99)
        self.assertEqual(self.call(), 0)
        self.assertEqual((self.get(INDEX), self.get(PENDING), self.get(PENDING + 8)), (0, 99, 0))
        self.assertEqual(len(self.trace), 1)
        self.refuse = "two"
        self.assertEqual(self.call(), 0)
        self.assertEqual((self.get(INDEX), self.get(PENDING), self.get(PENDING + 8)), (0xFFFFFFFF, 0, 1))

    def test_missing_executor_and_bad_table_address_refuse_without_completion(self):
        self.assertEqual(self.call(executor=False), 0)
        self.assertEqual(self.get(INDEX), 0)
        self.put(INDEX, 0x10000000)
        self.assertEqual(self.call(), 0)
        self.assertEqual(self.trace, [])
        self.assertEqual(self.get(PENDING), 17)

    def test_unknown_outer_target_cannot_use_the_known_vblank_callback(self):
        before = bytes(self.memory)
        self.assertEqual(self.route(None, self.memory, 0x800616D4), 0)
        self.assertEqual(bytes(self.memory), before)
