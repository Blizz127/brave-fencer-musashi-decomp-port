"""Exercise decomp-owned DMA registration through checked synthetic devices."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x8006CBC4
CONTROL_CELL = BASE - 4
CONTROL = 0x1F8010F4


class Memory(C.Structure):
    _fields_ = [("bytes", C.c_ubyte * 0x200000)]


READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32))
WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)


class Device(C.Structure):
    _fields_ = [("userdata", C.c_void_p), ("read", READ), ("write", WRITE)]


class DmaRegistrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-dma-registration-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "dma_registration.so"
        subprocess.run([
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-DMUSASHI_NATIVE_DMA_REGISTRATION=1",
            "-DMUSASHI_NATIVE_CALLBACK_DISPATCH=1",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/libetc_dma_registration.c"), str(ROOT / "pc_port/libetc_dma_veneer.c"),
            str(ROOT / "pc_port/boot_memory.c"),
            "-lcrypto", "-o", str(library),
        ], check=True, stdin=subprocess.DEVNULL, timeout=60, capture_output=True)
        cls.library = C.CDLL(str(library))
        cls.call = cls.library.musashi_boot_call_80042f8c
        cls.call.argtypes = [C.POINTER(Memory), C.POINTER(Device), C.c_int32,
                             C.c_uint32, C.POINTER(C.c_uint32)]
        cls.call.restype = C.c_int
        cls.dispatch = cls.library.musashi_boot_call_800425e0
        cls.dispatch.argtypes = cls.call.argtypes
        cls.dispatch.restype = C.c_int

    def setUp(self):
        self.memory = Memory()
        self.trace = []
        self.control = 0xFE123456
        self.refuse = None
        self.after_read = None
        self.write_word(CONTROL_CELL, CONTROL)

        def read(_, address, value):
            self.trace.append(("read", address, self.word(self.slot)))
            value[0] = self.control
            if self.after_read:
                self.after_read()
            return self.refuse != "read"

        def write(_, address, value):
            self.trace.append(("write", address, value))
            if self.refuse == "write":
                return 0
            self.control = value
            return 1

        self.device = Device(None, READ(read), WRITE(write))

    def write_word(self, address, value):
        struct.pack_into("<I", self.memory, address - 0x80000000, value)

    def word(self, address):
        return struct.unpack_from("<I", self.memory, address - 0x80000000)[0]

    def run_call(self, index, callback, device=True):
        self.slot = (BASE + index * 4) & 0xFFFFFFFF
        previous = C.c_uint32(0xDEADBEEF)
        status = self.call(self.memory, C.byref(self.device) if device else None,
                           index, callback, C.byref(previous))
        return status, previous.value

    def test_all_eight_slots_enable_disable_and_preserve_surrounding_ram(self):
        for index in range(8):
            for disabled in (False, True):
                with self.subTest(index=index, disabled=disabled):
                    self.setUp()
                    old, new = 0x80100000 + index * 4, 0 if disabled else 0x80042E08
                    self.write_word(BASE + index * 4, old)
                    before = bytearray(self.memory)
                    struct.pack_into("<I", before, BASE + index * 4 - 0x80000000, new)
                    bit = 1 << (index + 16)
                    masked = (0xFE123456 & 0xFFFFFF) | 0x800000
                    expected = masked & ~bit if disabled else masked | bit
                    self.assertEqual(self.run_call(index, new), (1, old))
                    self.assertEqual(bytes(self.memory), before)
                    self.assertEqual(self.trace, [("read", CONTROL, new),
                                                 ("write", CONTROL, expected)])

    def test_unchanged_callback_needs_no_device_or_valid_control_pointer(self):
        self.write_word(CONTROL_CELL, 1)
        self.write_word(BASE, 0xFFFFFFFF)
        before = bytes(self.memory)
        self.assertEqual(self.run_call(0, 0xFFFFFFFF, device=False), (1, 0xFFFFFFFF))
        self.assertEqual(bytes(self.memory), before)
        self.assertEqual(self.trace, [])

    def test_control_pointer_is_snapshotted_before_aliasing_slot_publication(self):
        # -1 names the pointer cell itself. Both accesses still use its old word.
        self.after_read = lambda: self.write_word(CONTROL_CELL, 0x1F801074)
        self.assertEqual(self.run_call(-1, 0x80042E08), (1, CONTROL))
        self.assertEqual(self.trace, [("read", CONTROL, 0x80042E08),
                                     ("write", CONTROL, 0x0092B456)])
        self.assertEqual(self.word(CONTROL_CELL), 0x1F801074)

    def test_device_refusal_preserves_publication_without_publishing_return(self):
        for refused in ("read", "write"):
            self.setUp()
            self.refuse = refused
            self.assertEqual(self.run_call(2, 9), (0, 0xDEADBEEF))
            self.assertEqual(self.word(BASE + 8), 9)
            self.assertEqual(self.control, 0xFE123456)
            self.assertEqual(self.trace[-1][0], refused)
        self.setUp()
        self.assertEqual(self.run_call(2, 9, device=False), (0, 0xDEADBEEF))
        self.assertEqual(self.word(BASE + 8), 9)
        self.assertEqual(self.trace, [])

    def test_shift_count_wraps_and_disabling_channel_seven_clears_master_bit(self):
        self.assertEqual(self.run_call(32, 9), (1, 0))
        self.assertEqual(self.control, 0x00933456)  # (32+16)&31 = 16
        self.assertEqual(self.word(BASE + 128), 9)
        self.write_word(BASE + 28, 9)
        self.assertEqual(self.run_call(7, 0), (1, 9))
        self.assertEqual(self.control, 0x00133456)

    def test_bad_slot_and_misaligned_device_stop_at_their_actual_access(self):
        before = bytes(self.memory)
        self.assertEqual(self.run_call(0x10000000, 9), (0, 0xDEADBEEF))
        self.assertEqual(bytes(self.memory), before)
        self.write_word(CONTROL_CELL, CONTROL + 1)
        self.assertEqual(self.run_call(0, 9), (0, 0xDEADBEEF))
        self.assertEqual(self.word(BASE), 9)
        self.assertEqual(self.trace, [])

    def test_dispatch_follows_the_live_table_slot_and_returns_previous(self):
        self.write_word(0x8006CB84, 0x80060000)
        self.write_word(0x80060004, 0x80042F8C)
        self.write_word(BASE + 8, 42)
        self.slot = BASE + 8
        previous = C.c_uint32(0xDEADBEEF)
        self.assertEqual(self.dispatch(self.memory, self.device, 2, 9, previous), 1)
        self.assertEqual(previous.value, 42)
        self.assertEqual(self.word(BASE + 8), 9)
        self.assertEqual(self.control, 0x00963456)

    def test_unknown_dispatch_target_cannot_fall_back_to_a_known_function(self):
        self.write_word(0x8006CB84, 0x80060000)
        self.write_word(0x80060004, 0x80042F90)
        before = bytes(self.memory)
        previous = C.c_uint32(0xDEADBEEF)
        self.assertEqual(self.dispatch(self.memory, self.device, 2, 9, previous), 0)
        self.assertEqual(previous.value, 0xDEADBEEF)
        self.assertEqual(bytes(self.memory), before)
        self.assertEqual(self.trace, [])
