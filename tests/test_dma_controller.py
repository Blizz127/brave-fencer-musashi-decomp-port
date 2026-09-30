"""DMA control registers: no transfer or game-completion fixture."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DPCR, DICR, GPU_CHCR = 0x1F8010F0, 0x1F8010F4, 0x1F8010A8


class Irq(C.Structure):
    _fields_ = [("status", C.c_uint16), ("mask", C.c_uint16)]


class Dma(C.Structure):
    _fields_ = [("control", C.c_uint32), ("interrupt", C.c_uint32),
                ("irq", C.POINTER(Irq)), ("gpu_channel_control", C.c_uint32),
                ("gpu_owner", C.c_void_p)]


class DmaControllerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-dma-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "dma.so"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/dma_controller.c"), "-o", str(library),
        ], check=True, stdin=subprocess.DEVNULL, timeout=30)
        cls.lib = C.CDLL(str(library))
        cls.lib.musashi_dma_controller_init.argtypes = [C.POINTER(Dma), C.POINTER(Irq)]
        cls.lib.musashi_dma_controller_read32.argtypes = [C.POINTER(Dma), C.c_uint32, C.POINTER(C.c_uint32)]
        cls.lib.musashi_dma_controller_write32.argtypes = [C.POINTER(Dma), C.c_uint32, C.c_uint32]

    def setUp(self):
        self.irq = Irq(1, 9)
        self.dma = Dma()
        self.lib.musashi_dma_controller_init(self.dma, self.irq)

    def read(self, address):
        out = C.c_uint32(0xDEADBEEF)
        self.assertEqual(self.lib.musashi_dma_controller_read32(self.dma, address, out), 1)
        return out.value

    def write(self, address, value):
        return self.lib.musashi_dma_controller_write32(self.dma, address, value)

    def test_priority_enable_word_roundtrips_without_starting_a_transfer(self):
        self.assertEqual(self.read(DPCR), 0x07654321)
        for value in (0x33333333, 0xFFFFFFFF, 0):
            self.assertEqual(self.write(DPCR, value), 1)
            self.assertEqual(self.read(DPCR), value)
            self.assertEqual(self.read(DICR), 0)
            self.assertEqual((self.irq.status, self.irq.mask), (1, 9))

    def test_dicr_writable_bits_cannot_fabricate_channel_completion(self):
        self.assertEqual(self.write(DICR, 0xFF7F7FFF), 1)
        self.assertEqual(self.read(DICR), 0x007F007F)
        self.assertEqual(self.irq.status, 1)

    def test_gpu_idle_configuration_does_not_start_or_complete_dma(self):
        self.assertEqual(self.read(GPU_CHCR), 0)
        for value in (0x401, 0):
            self.assertEqual(self.write(GPU_CHCR, value), 1)
            self.assertEqual(self.read(GPU_CHCR), value)
            self.assertEqual(self.read(DICR), 0)
            self.assertEqual((self.irq.status, self.irq.mask), (1, 9))
        self.assertEqual(self.write(GPU_CHCR, 0x401), 1)
        before = bytes(self.dma), bytes(self.irq)
        for value in (0x01000401, 0x10000401, 0x11000401, 0xFFFFFFFF, 0x201):
            self.assertEqual(self.write(GPU_CHCR, value), 0)
            self.assertEqual((bytes(self.dma), bytes(self.irq)), before)

    def test_installed_owner_or_busy_channel_cannot_be_bypassed(self):
        self.assertFalse(self.dma.gpu_owner)
        for lease, chcr in ((1, 0), (1, 0x401), (0, 0x01000401)):
            self.dma.gpu_owner = lease
            self.dma.gpu_channel_control = chcr
            before = bytes(self.dma), bytes(self.irq)
            for value in (0, 0x401, 0x01000401):
                self.assertEqual(self.write(GPU_CHCR, value), 0)
                self.assertEqual((bytes(self.dma), bytes(self.irq)), before)

    def test_force_irq_is_edge_triggered_and_acknowledgements_are_separate(self):
        self.assertEqual(self.write(DICR, 0x8000), 1)
        self.assertEqual(self.read(DICR), 0x80008000)
        self.assertEqual(self.irq.status, 9)
        self.irq.status = 1  # Synthetic guest I_STAT acknowledgement.
        self.assertEqual(self.write(DICR, 0x8000), 1)
        self.assertEqual(self.irq.status, 1)
        self.assertEqual(self.write(DICR, 0), 1)
        self.assertEqual(self.write(DICR, 0x8000), 1)
        self.assertEqual(self.irq.status, 9)
        self.assertEqual(self.write(DICR, 0), 1)
        self.assertEqual(self.irq.status, 9)

    def test_pending_flags_survive_mask_changes_until_write_one_to_clear(self):
        # Synthetic prior device completion, not a production transfer.
        self.dma.interrupt = 0x05000000
        self.assertEqual(self.write(DICR, 0x00800000), 1)
        self.assertEqual(self.read(DICR), 0x85800000)
        self.assertEqual(self.irq.status, 9)
        self.assertEqual(self.write(DICR, 0x01800000), 1)
        self.assertEqual(self.read(DICR), 0x84800000)
        self.assertEqual(self.write(DICR, 0x04800000), 1)
        self.assertEqual(self.read(DICR), 0x00800000)

    def test_unknown_registers_and_missing_device_refuse_without_effects(self):
        before = bytes(self.dma), bytes(self.irq)
        for address in (DPCR + 1, DICR + 2, GPU_CHCR + 1, 0x1F8010A0, 0x80010000):
            out = C.c_uint32(0xDEADBEEF)
            self.assertEqual(self.write(address, 0xFFFFFFFF), 0)
            self.assertEqual(self.lib.musashi_dma_controller_read32(self.dma, address, out), 0)
            self.assertEqual(out.value, 0xDEADBEEF)
            self.assertEqual((bytes(self.dma), bytes(self.irq)), before)
        self.assertEqual(self.lib.musashi_dma_controller_write32(None, DPCR, 0), 0)
        self.assertEqual(self.lib.musashi_dma_controller_read32(self.dma, DPCR, None), 0)


if __name__ == "__main__":
    unittest.main()
