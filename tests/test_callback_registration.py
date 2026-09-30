"""Run the shared registration C with checked synthetic device operations."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x8006BB00
MASK_CELL = 0x8006CB8C
IRQ_MASK = 0x1F801074


class Memory(C.Structure):
    _fields_ = [("bytes", C.c_ubyte * 0x200000)]


READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint16))
WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint16)
BIOS_B0 = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32)
BIOS_C0 = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32)


class Device(C.Structure):
    _fields_ = [("userdata", C.c_void_p), ("read", READ), ("write", WRITE),
                ("b0", BIOS_B0), ("c0", BIOS_C0)]


class CallbackRegistrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="musashi-registration-")
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / "registration.so"
        subprocess.run([
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-DMUSASHI_NATIVE_CALLBACK_REGISTRATION=1",
            "-DMUSASHI_NATIVE_CALLBACK_DISPATCH=1",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/libetc_irq_registration.c"), str(ROOT / "pc_port/libetc_irq_veneer.c"),
            str(ROOT / "pc_port/boot_memory.c"),
            "-lcrypto", "-o", str(library),
        ], check=True, stdin=subprocess.DEVNULL, timeout=60, capture_output=True)
        cls.library = C.CDLL(str(library))
        cls.call = cls.library.musashi_boot_call_800429dc
        cls.call.argtypes = [C.POINTER(Memory), C.POINTER(Device), C.c_int32,
                             C.c_uint32, C.POINTER(C.c_uint32)]
        cls.call.restype = C.c_int
        cls.dispatch = cls.library.musashi_boot_call_800425b0
        cls.dispatch.argtypes = cls.call.argtypes
        cls.dispatch.restype = C.c_int

    def setUp(self):
        self.memory = Memory()
        self.trace = []
        self.mask = 0x8200
        self.refuse = None
        self.after_read = None
        self.after_bios = None
        self.write_word(MASK_CELL, IRQ_MASK)
        self.write_word(BASE - 4, 1)

        def read(_, address, out):
            self.trace.append(("read", address))
            out[0] = self.mask
            if self.after_read:
                self.after_read()
            return self.refuse != "read"

        def write(_, address, value):
            self.trace.append(("write", address, value))
            if self.refuse == "write":
                return 0
            self.mask = value
            return 1

        def b0(_, disabled):
            self.trace.append(("b0", disabled, self.word(BASE), self.half(BASE + 44), self.mask))
            if self.after_bios:
                self.after_bios()
            return self.refuse != "b0"

        def c0(_, channel, disabled):
            self.trace.append(("c0", channel, disabled))
            return self.refuse != "c0"

        self.device = Device(None, READ(read), WRITE(write), BIOS_B0(b0), BIOS_C0(c0))

    def write_word(self, address, value):
        struct.pack_into("<I", self.memory, address - 0x80000000, value)

    def word(self, address):
        return struct.unpack_from("<I", self.memory, address - 0x80000000)[0]

    def half(self, address):
        return struct.unpack_from("<H", self.memory, address - 0x80000000)[0]

    def run_call(self, index, callback, device=True):
        previous = C.c_uint32(0xDEADBEEF)
        status = self.call(self.memory, C.byref(self.device) if device else None,
                           index, callback, C.byref(previous))
        return status, previous.value

    def test_all_eleven_slots_enable_disable_bios_routes_and_surrounding_ram(self):
        for index in range(11):
            for disabled in (False, True):
                with self.subTest(index=index, disabled=disabled):
                    self.setUp()
                    old = 0x80123400 + index * 4
                    new = 0 if disabled else 0x80042CE8
                    self.write_word(BASE + index * 4, old)
                    self.write_word(BASE + 44, 0xA55A07FF)
                    expected = bytearray(self.memory)
                    struct.pack_into("<I", expected, BASE + index * 4 - 0x80000000, new)
                    bit = 1 << index
                    enabled = (0x7FF & ~bit) if disabled else (0x7FF | bit)
                    struct.pack_into("<H", expected, BASE + 44 - 0x80000000, enabled)
                    self.assertEqual(self.run_call(index, new), (1, old))
                    self.assertEqual(bytes(self.memory), expected)
                    expected_trace = [("read", IRQ_MASK), ("write", IRQ_MASK, 0)]
                    if index == 0:
                        expected_trace.append(("b0", int(disabled), new, enabled, 0))
                    if index in (0, 4, 5, 6):
                        channel = 3 if index == 0 else index - 4
                        expected_trace.append(("c0", channel, int(disabled)))
                    mask = (0x8200 & ~bit) if disabled else (0x8200 | bit)
                    expected_trace.append(("write", IRQ_MASK, mask))
                    self.assertEqual(self.trace, expected_trace)

    def test_early_returns_need_no_devices_and_preserve_whole_memory(self):
        for guard, old, new in ((1, 9, 9), (0, 9, 4)):
            self.write_word(BASE - 4, guard)
            self.write_word(BASE, old)
            before = bytes(self.memory)
            self.assertEqual(self.run_call(0, new, device=False), (1, old))
            self.assertEqual(bytes(self.memory), before)
            self.assertEqual(self.trace, [])

    def test_irq_pointer_is_snapshotted_for_clear_and_reloaded_for_restore(self):
        self.after_read = lambda: self.write_word(MASK_CELL, 0x1F801070)
        self.after_bios = lambda: self.write_word(MASK_CELL, 0x1F801078)
        self.assertEqual(self.run_call(0, 0x80042CE8), (1, 0))
        self.assertEqual(self.trace[1], ("write", IRQ_MASK, 0))
        self.assertEqual(self.trace[-1], ("write", 0x1F801078, 0x8201))

    def test_refused_bios_preserves_prior_effects_without_publishing_return(self):
        self.refuse = "b0"
        self.assertEqual(self.run_call(0, 0x80042CE8), (0, 0xDEADBEEF))
        self.assertEqual(self.word(BASE), 0x80042CE8)
        self.assertEqual(self.half(BASE + 44), 1)
        self.assertEqual(self.mask, 0)
        self.assertEqual(self.trace[-1][0], "b0")

    def test_missing_bios_and_refused_device_are_not_success(self):
        self.device.c0 = BIOS_C0()
        self.assertEqual(self.run_call(4, 9), (0, 0xDEADBEEF))
        self.assertEqual(self.mask, 0)
        for refusal in ("read", "write"):
            self.setUp()
            self.refuse = refusal
            before = bytes(self.memory)
            self.assertEqual(self.run_call(0, 9), (0, 0xDEADBEEF))
            self.assertEqual(bytes(self.memory), before)

    def test_wrapped_index_and_shift_use_guest_word_rules(self):
        # Index 32 addresses another valid RAM word, but sllv uses shift zero.
        self.assertEqual(self.run_call(32, 7), (1, 0))
        self.assertEqual(self.word(BASE + 128), 7)
        self.assertEqual(self.half(BASE + 44), 1)
        # -1 aliases the guard word; stores must remain visible through it.
        self.assertEqual(self.run_call(-1, 7), (1, 1))
        self.assertEqual(self.word(BASE - 4), 7)
        self.assertEqual(self.mask, 0x8201)  # bit 31 truncates in halfword store

    def test_slot_outside_cached_ram_is_refused_before_effects(self):
        before = bytes(self.memory)
        self.assertEqual(self.run_call(0x10000000, 9), (0, 0xDEADBEEF))
        self.assertEqual(bytes(self.memory), before)
        self.assertEqual(self.trace, [])

    def test_dispatch_loads_the_live_table_and_returns_the_real_previous_word(self):
        self.write_word(0x8006CB84, 0x80060000)
        self.write_word(0x80060008, 0x800429DC)
        self.write_word(BASE + 12, 42)
        previous = C.c_uint32(0xDEADBEEF)
        self.assertEqual(self.dispatch(self.memory, self.device, 3, 9, previous), 1)
        self.assertEqual(previous.value, 42)
        self.assertEqual(self.word(BASE + 12), 9)
        self.assertEqual(self.mask, 0x8208)

    def test_unknown_dispatch_target_refuses_before_state_or_device_effects(self):
        self.write_word(0x8006CB84, 0x80060000)
        self.write_word(0x80060008, 0x800429E0)
        before = bytes(self.memory)
        previous = C.c_uint32(0xDEADBEEF)
        self.assertEqual(self.dispatch(self.memory, self.device, 3, 9, previous), 0)
        self.assertEqual(previous.value, 0xDEADBEEF)
        self.assertEqual(bytes(self.memory), before)
        self.assertEqual(self.trace, [])
