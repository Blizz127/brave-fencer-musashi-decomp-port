"""Retail 80042E08 DMA IRQ handler against a checked synthetic DICR device."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CONTROL_CELL = 0x8006CBC0
CONTROL = 0x1F8010F4
SLOTS = 0x8006CBC4


class Memory(C.Structure):
    _fields_ = [("bytes", C.c_ubyte * 0x200000)]


READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32))
WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)
CALL = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Memory), C.c_uint32)


class Device(C.Structure):
    _fields_ = [("userdata", C.c_void_p), ("read", READ), ("write", WRITE)]


class DmaIrqHandlerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-dma-irq-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "dma_irq.so"
        subprocess.run([
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-DMUSASHI_NATIVE_DMA_IRQ_HANDLER=1",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/libetc_dma_irq.c"),
            str(ROOT / "pc_port/boot_memory.c"),
            "-lcrypto", "-o", str(library),
        ], check=True, stdin=subprocess.DEVNULL, timeout=60, capture_output=True)
        cls.library = C.CDLL(str(library))
        cls.call = cls.library.musashi_boot_call_80042e08
        cls.call.argtypes = [C.POINTER(Memory), C.POINTER(Device), CALL, C.c_void_p]
        cls.call.restype = C.c_int

    def setUp(self):
        self.memory = Memory()
        self.trace = []
        self.calls = []
        self.dicr = 0x00900000
        self.write_word(CONTROL_CELL, CONTROL)

        def read(_, address, value):
            self.trace.append(("read", address, self.dicr))
            if address != CONTROL:
                return 0
            value[0] = self.dicr
            return 1

        def write(_, address, value):
            self.trace.append(("write", address, value, self.dicr))
            if address != CONTROL:
                return 0
            flags = self.dicr & 0x7F000000 & ~value
            nxt = (value & 0x00FF807F) | flags
            if (nxt & 0x8000) or ((nxt & 0x00800000) and (nxt & 0x7F000000)):
                nxt |= 0x80000000
            else:
                nxt &= 0x7FFFFFFF
            self.dicr = nxt
            return 1

        def execute(_, memory, target):
            self.calls.append(target)
            return target == 0x8004AA18

        self.read, self.write, self.execute = READ(read), WRITE(write), CALL(execute)
        self.device = Device(None, self.read, self.write)

    def write_word(self, address, value):
        struct.pack_into("<I", self.memory, address - 0x80000000, value)

    def word(self, address):
        return struct.unpack_from("<I", self.memory, address - 0x80000000)[0]

    def run_call(self, execute=True):
        return self.call(self.memory, self.device,
                         self.execute if execute else CALL(), None)

    def test_idle_dicr_returns_without_callback_or_store(self):
        self.assertEqual(self.run_call(execute=False), 1)
        self.assertEqual(self.calls, [])
        self.assertEqual([event[0] for event in self.trace], ["read", "read"])
        self.assertEqual(self.dicr, 0x00900000)

    def test_channel_four_flag_acknowledges_then_runs_the_live_slot(self):
        self.dicr = 0x10900000
        self.write_word(SLOTS + 16, 0x8004AA18)
        self.assertEqual(self.run_call(), 1)
        self.assertEqual(self.calls, [0x8004AA18])
        self.assertEqual(self.dicr, 0x00900000)
        self.assertEqual(self.trace[0], ("read", CONTROL, 0x10900000))
        self.assertEqual(self.trace[1][0], "read")
        self.assertEqual(self.trace[2][0], "write")
        self.assertEqual(self.trace[2][1], CONTROL)
        self.assertEqual(self.trace[2][2], 0x10900000 & ((1 << 28) | 0xFFFFFF))

    def test_callback_refusal_keeps_the_acknowledge(self):
        self.dicr = 0x10900000
        self.write_word(SLOTS + 16, 0x80001000)
        self.assertEqual(self.run_call(), 0)
        self.assertEqual(self.calls, [0x80001000])
        self.assertEqual(self.dicr, 0x00900000)

    def test_missing_device_or_misaligned_pointer_refuses_before_dicr(self):
        self.assertEqual(self.call(self.memory, None, CALL(), None), 0)
        self.write_word(CONTROL_CELL, CONTROL + 1)
        self.assertEqual(self.run_call(execute=False), 0)
        self.assertEqual(self.trace, [])
        self.assertEqual(self.dicr, 0x00900000)


if __name__ == "__main__":
    unittest.main()
