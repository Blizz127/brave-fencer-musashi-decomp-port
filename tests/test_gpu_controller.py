"""Checked initial GP0/GP1 frontend with an explicit renderer backend."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GPUREAD = 0x1F801810
GPUSTAT = 0x1F801814
UINT32 = C.c_uint32

RESET = C.CFUNCTYPE(C.c_int, C.c_void_p)
DRAW_MODE = C.CFUNCTYPE(C.c_int, C.c_void_p, UINT32)
DISPLAY_ENABLE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
CLEAR_FIFO = C.CFUNCTYPE(C.c_int, C.c_void_p)
READY = C.CFUNCTYPE(C.c_int, C.c_void_p)
ENVIRONMENT = C.CFUNCTYPE(C.c_int, C.c_void_p, UINT32)
DISPLAY = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)


class DisplayState(C.Structure):
    _fields_ = [
        ("origin", UINT32), ("horizontal", UINT32), ("vertical", UINT32),
        ("mode", UINT32), ("x", C.c_uint16), ("y", C.c_uint16),
        ("width", C.c_uint16), ("height", C.c_uint16),
        ("h_start", C.c_uint16), ("h_end", C.c_uint16),
        ("v_start", C.c_uint16), ("v_end", C.c_uint16),
        ("dot_divisor", C.c_uint8),
    ]


class Fill(C.Structure):
    _fields_ = [("command", UINT32), ("xy", UINT32), ("wh", UINT32),
                ("x", C.c_uint16), ("y", C.c_uint16),
                ("width", C.c_uint16), ("height", C.c_uint16),
                ("color", C.c_uint16)]


FILL = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Fill))
STORE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint16, C.c_uint16, C.c_uint16)
READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint16, C.c_uint16, C.POINTER(C.c_uint16))


class Backend(C.Structure):
    _fields_ = [("userdata", C.c_void_p), ("reset", RESET),
                ("draw_mode", DRAW_MODE), ("display_enable", DISPLAY_ENABLE),
                ("clear_fifo", CLEAR_FIFO), ("ready", READY),
                ("environment", ENVIRONMENT), ("display", DISPLAY),
                ("fill_vram", FILL), ("store_vram", STORE), ("read_vram", READ)]


class Controller(C.Structure):
    _fields_ = [("backend", Backend), ("read_latch", UINT32),
                ("draw_mode", UINT32), ("dma_direction", UINT32),
                ("display_disabled", C.c_int), ("faulted", C.c_int),
                ("executing", C.c_int), ("vblank_parity", UINT32),
                ("texture_window", UINT32), ("drawing_area_start", UINT32),
                ("drawing_area_end", UINT32), ("drawing_offset", UINT32),
                ("mask_flags", UINT32), ("accepted_gp0_words", C.c_uint64),
                ("display", DisplayState), ("fill_command", UINT32), ("fill_xy", UINT32),
                ("fill_words", C.c_uint8), ("completed_fills", C.c_uint64),
                ("filled_pixels", C.c_uint64), ("last_fill", Fill),
                ("store_command", UINT32), ("store_xy", UINT32),
                ("store_wh", UINT32), ("store_x", C.c_uint16),
                ("store_y", C.c_uint16), ("store_w", C.c_uint16),
                ("store_h", C.c_uint16), ("store_px", C.c_uint16),
                ("store_py", C.c_uint16), ("store_remaining", UINT32),
                ("store_phase", C.c_uint8), ("stored_pixels", C.c_uint64),
                ("copy_command", UINT32), ("copy_src", UINT32),
                ("copy_dst", UINT32), ("copy_words", C.c_uint8),
                ("copied_pixels", C.c_uint64), ("completed_copies", C.c_uint64),
                ("prim_needed", C.c_uint8), ("prim_got", C.c_uint8),
                ("prim_data", UINT32 * 12),
                ("tap", C.c_void_p), ("tap_userdata", C.c_void_p),
                ("raster_disabled", C.c_int), ("drawn_pixels", C.c_uint64),
                ("semi", C.c_uint8), ("semi_mode", C.c_uint8),
                ("semi_textured", C.c_uint8),
                ("poly_active", C.c_uint8), ("poly_gouraud", C.c_uint8),
                ("poly_words", UINT32), ("poly_vertices", UINT32),
                ("poly_command", UINT32), ("poly_color", UINT32),
                ("poly_prev_color", UINT32),
                ("poly_x", C.c_int32), ("poly_y", C.c_int32)]


class GpuControllerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="musashi-gpu-controller-")
        cls.addClassCleanup(temp.cleanup)
        library = Path(temp.name) / "gpu_controller.so"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "pc_port/gpu_controller.c"), "-o", str(library),
        ], check=True, stdin=subprocess.DEVNULL, timeout=30)
        cls.lib = C.CDLL(str(library))
        cls.lib.musashi_gpu_controller_init.argtypes = [
            C.POINTER(Controller), C.POINTER(Backend)]
        cls.lib.musashi_gpu_controller_read32.argtypes = [
            C.POINTER(Controller), UINT32, C.POINTER(UINT32)]
        cls.lib.musashi_gpu_controller_write32.argtypes = [
            C.POINTER(Controller), UINT32, UINT32]
        cls.lib.musashi_gpu_controller_vblank.argtypes = [
            C.POINTER(Controller)]
        cls.lib.musashi_gpu_controller_get_store_image.argtypes = [
            C.POINTER(Controller),
            C.POINTER(C.c_uint16), C.POINTER(C.c_uint16),
            C.POINTER(C.c_uint16), C.POINTER(C.c_uint16),
            C.POINTER(UINT32),
        ]
        cls.lib.musashi_gpu_controller_get_store_image.restype = C.c_int
        cls.lib.musashi_gpu_controller_store_image_phase.argtypes = [
            C.POINTER(Controller)]
        cls.lib.musashi_gpu_controller_store_image_phase.restype = C.c_int
        cls.lib.musashi_gpu_controller_store_image_remaining_words.argtypes = [
            C.POINTER(Controller)]
        cls.lib.musashi_gpu_controller_store_image_remaining_words.restype = UINT32
        cls.lib.musashi_gpu_controller_read_store_image_buffer.argtypes = [
            C.POINTER(Controller),
            C.POINTER(UINT32), UINT32, C.POINTER(UINT32),
        ]
        cls.lib.musashi_gpu_controller_read_store_image_buffer.restype = C.c_int

    def setUp(self):
        self.controller = Controller()
        self.ready_value = 1
        self.calls = []

        def reset(_):
            self.calls.append(("reset",))
            return 1

        def draw_mode(_, word):
            self.calls.append(("draw_mode", word))
            return 1

        def display_enable(_, enabled):
            self.calls.append(("display_enable", enabled))
            return 1

        def clear_fifo(_):
            self.calls.append(("clear_fifo",))
            return 1

        def ready(_):
            self.calls.append(("ready",))
            return self.ready_value

        def environment(_, word):
            self.calls.append(("environment", word))
            return 1

        def display(_, candidate):
            state = C.cast(candidate, C.POINTER(DisplayState)).contents
            self.calls.append(("display", state.mode, state.width, state.height))
            return 1

        self.callbacks = (RESET(reset), DRAW_MODE(draw_mode),
                          DISPLAY_ENABLE(display_enable), CLEAR_FIFO(clear_fifo),
                          READY(ready), ENVIRONMENT(environment), DISPLAY(display))
        self.backend = Backend(None, *self.callbacks)
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(self.controller), C.byref(self.backend)), 1)

    def test_fill_partial_packet_and_real_descriptor(self):
        fills = []
        def accept(_, candidate):
            f = candidate.contents
            fills.append((f.command, f.xy, f.wh, f.x, f.y,
                          f.width, f.height, f.color))
            return 1
        fill = FILL(accept)
        self.controller.backend.fill_vram = fill
        self.assertEqual(self.write(0x02ff00ff), 1)
        self.assertEqual(self.controller.fill_words, 1)
        self.assertEqual(self.controller.accepted_gp0_words, 1)
        self.assertEqual(fills, [])
        self.assertEqual(self.read()[1] & (1 << 26), 0)
        self.assertNotEqual(self.read()[1] & (1 << 28), 0)
        self.assertEqual(self.write(0xe60003ff), 1)  # data, not E6
        self.assertEqual(self.controller.mask_flags, 0)
        self.assertEqual(self.controller.fill_words, 2)
        self.assertEqual(fills, [])
        self.assertEqual(self.write(0x01ff0011), 1)
        self.assertEqual(fills, [(0x02ff00ff, 0xe60003ff, 0x01ff0011,
                                 1008, 0, 32, 511, 0x7c1f)])
        self.assertEqual(self.controller.fill_words, 0)
        self.assertEqual(self.controller.completed_fills, 1)
        self.assertEqual(self.controller.filled_pixels, 32 * 511)
        self.assertEqual(self.controller.accepted_gp0_words, 3)

    def test_fill_reset_refusal_and_accepted_reentry(self):
        for prefix in (1, 2):
            for reset_word in (0, 0x01000000):
                self.setUp()
                calls = []
                fill = FILL(lambda _, f: calls.append(f.contents.color) or 1)
                self.controller.backend.fill_vram = fill
                self.assertEqual(self.write(0x02ffffff), 1)
                if prefix == 2:
                    self.assertEqual(self.write(0), 1)
                self.assertEqual(self.write(reset_word, GPUSTAT), 1)
                self.assertEqual(self.controller.fill_words, 0)
                self.assertEqual(self.controller.accepted_gp0_words, prefix)
                self.assertEqual(calls, [])
                self.assertEqual(self.write(0xe6000003), 1)
                self.assertEqual(self.controller.mask_flags, 3)
        for accept in (False, True):
            self.setUp()
            calls = []
            def backend(_, f):
                calls.append(f.contents.color)
                if accept:
                    self.assertEqual(self.write(0xe1000000), 0)
                return int(accept)
            fill = FILL(backend)
            self.controller.backend.fill_vram = fill
            self.assertEqual(self.write(0x020000ff), 1)
            self.assertEqual(self.write(0), 1)
            self.assertEqual(self.write(0x00010010), 0)
            self.assertEqual(calls, [31])
            self.assertEqual(self.controller.faulted, 1)
            self.assertEqual(self.controller.accepted_gp0_words, 3 if accept else 2)
            self.assertEqual(self.controller.completed_fills, int(accept))
            self.assertEqual(self.controller.fill_words, 0 if accept else 2)

    def test_fill_busy_counter_and_zero_extent(self):
        calls = []
        fill = FILL(lambda _, f: calls.append(f.contents.color) or 1)
        self.controller.backend.fill_vram = fill
        self.assertEqual(self.write(0x0200ff00), 1)
        self.ready_value = 0
        self.assertEqual(self.write(0xe6000003), 0)
        self.assertEqual(self.controller.fill_words, 1)
        self.assertEqual(self.controller.faulted, 0)
        self.ready_value = 1
        self.assertEqual(self.write(0), 1)
        self.controller.completed_fills = (1 << 64) - 1
        self.assertEqual(self.write(0x00010010), 0)
        self.assertEqual(calls, [])
        self.assertEqual(self.controller.fill_words, 2)
        self.controller.completed_fills = 0
        self.assertEqual(self.write(0x02000400), 1)  # both effective sizeszero
        self.assertEqual(calls, [0x3e0])
        self.assertEqual(self.controller.filled_pixels, 0)
        self.assertEqual(self.controller.completed_fills, 1)
        self.assertEqual(self.controller.accepted_gp0_words, 3)

    def read(self, address=GPUSTAT):
        value = UINT32(0xDEADBEEF)
        accepted = self.lib.musashi_gpu_controller_read32(
            C.byref(self.controller), address, C.byref(value))
        return accepted, value.value

    def write(self, value, address=GPUREAD):
        return self.lib.musashi_gpu_controller_write32(
            C.byref(self.controller), address, value)

    def vblank(self, controller=None):
        target = self.controller if controller is None else controller
        return self.lib.musashi_gpu_controller_vblank(C.byref(target))

    def get_store_image(self, controller=None):
        target = self.controller if controller is None else controller
        x = C.c_uint16()
        y = C.c_uint16()
        w = C.c_uint16()
        h = C.c_uint16()
        total = UINT32()
        ok = self.lib.musashi_gpu_controller_get_store_image(
            C.byref(target), C.byref(x), C.byref(y), C.byref(w), C.byref(h), C.byref(total))
        return ok, x.value, y.value, w.value, h.value, total.value

    def store_image_phase(self, controller=None):
        target = self.controller if controller is None else controller
        return self.lib.musashi_gpu_controller_store_image_phase(C.byref(target))

    def store_image_remaining_words(self, controller=None):
        target = self.controller if controller is None else controller
        return self.lib.musashi_gpu_controller_store_image_remaining_words(C.byref(target))

    def test_init_requires_reset_acceptance_and_starts_guest_reset_state(self):
        self.assertEqual(self.controller.read_latch, 0x400)
        self.assertEqual(self.controller.draw_mode, 0)
        self.assertEqual(self.controller.display_disabled, 1)
        self.assertEqual(self.controller.dma_direction, 0)
        self.assertEqual(self.controller.faulted, 0)
        self.assertEqual(self.controller.vblank_parity, 0)
        self.assertEqual(self.calls, [("reset",)])

        failed = Controller()
        self.ready_value = 0
        self.callbacks[0]  # keep callback tuple alive while replacing backend
        refused = RESET(lambda _: 0)
        bad_backend = Backend(None, refused, self.callbacks[1],
                              self.callbacks[2], self.callbacks[3], self.callbacks[4],
                              self.callbacks[5])
        before = bytes(failed)
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(failed), C.byref(bad_backend)), 0)
        self.assertEqual(bytes(failed), before)
        self.assertEqual(failed.faulted, 0)
        self.assertEqual(failed.executing, 0)
        self.assertEqual(failed.accepted_gp0_words, 0)

    def test_gp1_display_05_to_08_commit_ordered_descriptor_and_status(self):
        self.assertEqual(self.controller.display.origin, 0)
        self.assertEqual(self.controller.display.horizontal, 0x00C00200)
        self.assertEqual(self.controller.display.vertical, 0x00040010)
        self.assertEqual(self.controller.display.width, 256)
        self.assertEqual(self.controller.display.height, 240)
        self.assertEqual(self.controller.display.dot_divisor, 10)
        self.assertEqual(self.write(0x05000000, GPUSTAT), 1)
        self.assertEqual(self.write(0x06C60260, GPUSTAT), 1)
        self.assertEqual(self.controller.display.width, 256)
        self.assertEqual(self.write(0x07040010, GPUSTAT), 1)
        self.assertEqual(self.write(0x08000001, GPUSTAT), 1)
        self.assertEqual(self.controller.display.mode, 1)
        self.assertEqual(self.controller.display.width, 320)
        self.assertEqual(self.controller.display.dot_divisor, 8)
        self.assertEqual(self.calls[-1], ("display", 1, 320, 240))
        accepted, status = self.read(GPUSTAT)
        self.assertEqual(accepted, 1)
        self.assertEqual((status >> 17) & 3, 1)
        self.assertEqual(status & (0xf << 19), 0)

    def test_gp1_display_rejects_unsupported_range_or_mode_transactionally(self):
        before = bytes(self.controller)
        self.assertEqual(self.write(0x07040020, GPUSTAT), 0)
        self.assertEqual(bytes(self.controller), before)
        self.assertEqual(self.write(0x08000008, GPUSTAT), 0)
        self.assertEqual(bytes(self.controller), before)

    def test_interlaced_480_line_scanout_covers_both_fields(self):
        self.assertEqual(self.write(0x06C60260, GPUSTAT), 1)
        for mode, height in ((0x03, 240), (0x07, 240), (0x23, 240), (0x27, 480), (0x01, 240)):
            self.assertEqual(self.write(0x08000000 | mode, GPUSTAT), 1)
            self.assertEqual(self.controller.display.height, height, hex(mode))
            self.assertEqual(self.calls[-1][3], height)

    def test_gp1_display_mode_map_and_status_bits_for_all_byte_values(self):
        supported = {}
        for mode in range(256):
            if mode & 0x98:
                continue
            if (mode & 0x40) and (mode & 3):
                continue
            if mode & 0x40:
                supported[mode] = 7
            else:
                supported[mode] = {0: 10, 1: 8, 2: 5, 3: 4}[mode & 3]
        for mode in range(256):
            before = bytes(self.controller)
            accepted = self.write(0x08000000 | mode, GPUSTAT)
            if mode not in supported:
                self.assertEqual(accepted, 0, hex(mode))
                self.assertEqual(bytes(self.controller), before)
                continue
            self.assertEqual(accepted, 1, hex(mode))
            self.assertEqual(self.controller.display.mode, mode)
            self.assertEqual(self.controller.display.dot_divisor, supported[mode])
            _, status = self.read(GPUSTAT)
            self.assertEqual((status >> 17) & 3, mode & 3)
            self.assertEqual((status >> 19) & 0xf, (mode >> 2) & 0xf)
            self.assertEqual((status >> 16) & 1, (mode >> 6) & 1)

    def test_gp1_display_origin_masks_upper_payload_bits(self):
        self.assertEqual(self.write(0x05081234, GPUSTAT), 1)
        self.assertEqual(self.controller.display.origin, 0x1234)
        self.assertEqual(self.controller.display.x, 0x234)
        self.assertEqual(self.controller.display.y, 0x4)

    def test_gp1_display_horizontal_rounding_and_bounds_refuse_atomically(self):
        self.assertEqual(self.write(0x06C60260, GPUSTAT), 1)
        self.assertEqual(self.write(0x06020002, GPUSTAT), 1)
        self.assertEqual(self.controller.display.width, 4)
        before = bytes(self.controller)
        for payload in (0x00020200, 0x00200200):
            self.assertEqual(self.write(0x06000000 | payload, GPUSTAT), 0)
            self.assertEqual(bytes(self.controller), before)
        self.assertEqual(self.write(0x060C0260, GPUSTAT), 0)
        self.assertEqual(bytes(self.controller), before)

    def test_gp1_display_clear_fifo_preserves_and_reset_restores_descriptor(self):
        for value in (0x05000123, 0x06C60260, 0x07040010, 0x08000001):
            self.assertEqual(self.write(value, GPUSTAT), 1)
        before = bytes(self.controller.display)
        self.assertEqual(self.write(0x01000000, GPUSTAT), 1)
        self.assertEqual(bytes(self.controller.display), before)
        self.assertEqual(self.write(0x00000000, GPUSTAT), 1)
        self.assertEqual(self.controller.display.origin, 0)
        self.assertEqual(self.controller.display.horizontal, 0x00C00200)
        self.assertEqual(self.controller.display.width, 256)

    def test_gp1_display_missing_capability_refuses_without_mutation(self):
        fresh = Controller()
        backend = Backend(None, self.callbacks[0], self.callbacks[1],
                          self.callbacks[2], self.callbacks[3], self.callbacks[4],
                          self.callbacks[5])
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        before = bytes(fresh)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUSTAT, 0x05000123), 0)
        self.assertEqual(bytes(fresh), before)

    def test_gp1_display_provider_refusal_preserves_frontend_descriptor(self):
        fresh = Controller()

        def refuse(_, candidate):
            return 0

        refused = DISPLAY(refuse)
        backend = Backend(None, self.callbacks[0], self.callbacks[1],
                          self.callbacks[2], self.callbacks[3], self.callbacks[4],
                          self.callbacks[5], refused)
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        before = bytes(fresh.display)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUSTAT, 0x05000123), 0)
        self.assertEqual(bytes(fresh.display), before)
        self.assertEqual(fresh.faulted, 1)

    def test_gp1_display_ready_reentry_refuses_before_provider(self):
        fresh = Controller()
        nested = []

        def reentrant_ready(_):
            nested.append(self.lib.musashi_gpu_controller_write32(
                C.byref(fresh), GPUSTAT, 0x05000123))
            return 1

        callback = READY(reentrant_ready)
        backend = Backend(None, self.callbacks[0], self.callbacks[1],
                          self.callbacks[2], self.callbacks[3], callback,
                          self.callbacks[5], self.callbacks[6])
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        before = bytes(fresh.display)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUSTAT, 0x05000123), 0)
        self.assertEqual(nested, [0])
        self.assertEqual(bytes(fresh.display), before)
        self.assertEqual(fresh.faulted, 1)

    def test_gp1_display_provider_reentry_retains_accepted_descriptor_and_fault(self):
        fresh = Controller()
        nested = []

        def reentrant_display(_, candidate):
            nested.append(self.lib.musashi_gpu_controller_write32(
                C.byref(fresh), GPUSTAT, 0x05000123))
            return 1

        callback = DISPLAY(reentrant_display)
        backend = Backend(None, self.callbacks[0], self.callbacks[1],
                          self.callbacks[2], self.callbacks[3], self.callbacks[4],
                          self.callbacks[5], callback)
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUSTAT, 0x05000123), 0)
        self.assertEqual(nested, [0])
        self.assertEqual(fresh.display.origin, 0x123)
        self.assertEqual(fresh.faulted, 1)

    def test_constructor_reset_reentry_refuses_publication(self):
        fresh = Controller()
        holder = {}
        nested = []

        def nested_reset(_):
            nested.append(self.lib.musashi_gpu_controller_init(
                C.byref(fresh), C.byref(holder["backend"])))
            return 1

        reset_callback = RESET(nested_reset)
        holder["backend"] = Backend(None, reset_callback, self.callbacks[1],
                                     self.callbacks[2], self.callbacks[3],
                                     self.callbacks[4], self.callbacks[5])
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(holder["backend"])), 0)
        self.assertEqual(nested, [0])
        self.assertEqual(fresh.faulted, 1)
        self.assertEqual(fresh.executing, 0)
        self.assertEqual(fresh.accepted_gp0_words, 0)

    def test_gpu_read_latch_and_info_index_seven_leave_400_unchanged(self):
        self.assertEqual(self.read(GPUREAD), (1, 0x400))
        self.assertEqual(self.write(0x10000007, GPUSTAT), 1)
        self.assertEqual(self.calls, [("reset",)])
        self.assertEqual(self.read(GPUREAD), (1, 0x400))
        self.assertEqual(self.calls[-1], ("reset",))

    def test_gp0_nop_does_not_call_renderer_or_reset(self):
        before = bytes(self.controller)
        self.assertEqual(self.write(0x0007924C), 1)
        self.assertNotEqual(bytes(self.controller), before)
        self.assertEqual(self.controller.accepted_gp0_words, 1)
        self.assertEqual(self.calls, [("reset",), ("ready",)])

    def test_gp0_draw_mode_uses_backend_and_maps_only_supported_status_bits(self):
        word = 0xE1F23ABC
        self.assertEqual(self.write(word), 1)
        self.assertEqual(self.calls[-2:], [("ready",), ("draw_mode", word)])
        self.assertEqual(self.controller.draw_mode, word & 0x3FFF)
        accepted, status = self.read()
        self.assertEqual(accepted, 1)
        self.assertEqual(status & 0x7FF, word & 0x7FF)
        self.assertEqual(status & 0x8000, 0x8000 if word & 0x800 else 0)
        self.assertEqual(status & 0x1000, 0)

    def test_environment_words_commit_raw_latches_and_status_mask_bits(self):
        words = (0xE2123456, 0xE30ABCDE, 0xE41ABCDE,
                 0xE5123456, 0xE6000003)
        for word in words:
            self.assertEqual(self.write(word), 1)
        self.assertEqual(self.controller.texture_window, words[0] & 0xFFFFF)
        self.assertEqual(self.controller.drawing_area_start, words[1] & 0xFFFFF)
        self.assertEqual(self.controller.drawing_area_end, words[2] & 0xFFFFF)
        self.assertEqual(self.controller.drawing_offset, words[3] & 0x3FFFFF)
        self.assertEqual(self.controller.mask_flags, 3)
        self.assertEqual(self.controller.accepted_gp0_words, 5)
        self.assertEqual(self.write(0x10000002, GPUSTAT), 1)
        self.assertEqual(self.read(GPUREAD), (1, words[0] & 0xFFFFF))
        self.assertEqual(self.write(0x10000005, GPUSTAT), 1)
        self.assertEqual(self.read(GPUREAD), (1, words[3] & 0x3FFFFF))
        _, status = self.read()
        self.assertEqual(status & ((1 << 11) | (1 << 12)),
                         (1 << 11) | (1 << 12))

    def test_e2_preserves_noncontiguous_masks_and_offsets_outside_mask(self):
        raw = ((0b10101) | (0b01010 << 5) |
               (0b11100 << 10) | (0b00011 << 15))
        self.assertEqual(self.write(0xE2000000 | raw), 1)
        self.assertEqual(self.controller.texture_window, raw)
        self.assertEqual(self.write(0x10000002, GPUSTAT), 1)
        self.assertEqual(self.read(GPUREAD), (1, raw))

    def test_e5_exercises_every_independent_signed_11_bit_component(self):
        for x in range(2048):
            self.assertEqual(self.write(0xE5000000 | x), 1)
            self.assertEqual(self.controller.drawing_offset, x)
        for y in range(2048):
            self.assertEqual(self.write(0xE5000000 | (y << 11)), 1)
            self.assertEqual(self.controller.drawing_offset, y << 11)
        self.assertEqual((2047 if 2047 < 1024 else 2047 - 2048), -1)
        self.assertEqual((1023 if 1023 < 1024 else 1023 - 2048), 1023)

    def test_e6_all_force_and_check_flag_combinations_map_gpu_status(self):
        for flags in range(4):
            self.assertEqual(self.write(0xE6000000 | flags), 1)
            _, status = self.read()
            self.assertEqual((status >> 11) & 3, flags)

    def test_e3_e4_raw_queries_keep_twenty_bits_and_latch_until_next_query(self):
        start = 0x80000 | (7 << 10) | 2
        end = (511 << 10) | 1023
        self.assertEqual(self.write(0xE3000000 | start), 1)
        self.assertEqual(self.write(0xE4000000 | end), 1)
        self.assertEqual(self.write(0x10000003, GPUSTAT), 1)
        self.assertEqual(self.read(GPUREAD), (1, start))
        self.assertEqual(self.write(0xE3000000 | 0x123), 1)
        self.assertEqual(self.read(GPUREAD), (1, start))
        self.assertEqual(self.write(0x10000004, GPUSTAT), 1)
        self.assertEqual(self.read(GPUREAD), (1, end))

    def test_gp1_reset_preserves_accepted_count_but_clears_environment(self):
        self.assertEqual(self.write(0xE2123456), 1)
        self.assertEqual(self.write(0x00000000, GPUSTAT), 1)
        self.assertEqual(self.controller.accepted_gp0_words, 1)
        self.assertEqual(self.controller.texture_window, 0)
        self.assertEqual(self.controller.mask_flags, 0)

    def test_environment_without_backend_callback_refuses_without_mutation(self):
        fresh = Controller()
        backend = Backend(None, self.callbacks[0], self.callbacks[1],
                          self.callbacks[2], self.callbacks[3], self.callbacks[4],
                          ENVIRONMENT())
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        before = bytes(fresh)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUREAD, 0xE2000000), 0)
        self.assertEqual(bytes(fresh), before)

    def test_environment_backend_refusal_is_sticky_and_preserves_raw_register(self):
        fresh = Controller()

        def refuse_environment(_, word):
            self.calls.append(("environment_refused", word))
            return 0

        callback = ENVIRONMENT(refuse_environment)
        backend = Backend(None, self.callbacks[0], self.callbacks[1],
                          self.callbacks[2], self.callbacks[3], self.callbacks[4],
                          callback)
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUREAD, 0xE2000001), 0)
        self.assertEqual(fresh.faulted, 1)
        self.assertEqual(fresh.texture_window, 0)
        self.assertEqual(fresh.accepted_gp0_words, 0)

    def test_environment_backend_reentry_commits_word_but_faults_outer_call(self):
        fresh = Controller()
        nested = []

        def environment(_, word):
            nested.append(self.lib.musashi_gpu_controller_write32(
                C.byref(fresh), GPUREAD, 0xE1000000))
            return 1

        callback = ENVIRONMENT(environment)
        backend = Backend(None, self.callbacks[0], self.callbacks[1],
                          self.callbacks[2], self.callbacks[3], self.callbacks[4],
                          callback)
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUREAD, 0xE2000001), 0)
        self.assertEqual(nested, [0])
        self.assertEqual(fresh.faulted, 1)
        self.assertEqual(fresh.texture_window, 1)
        self.assertEqual(fresh.accepted_gp0_words, 1)

    def test_ready_reentry_refuses_without_accepting_gp0_word(self):
        fresh = Controller()
        nested = []

        def ready_reentrant(_):
            nested.append(self.lib.musashi_gpu_controller_write32(
                C.byref(fresh), GPUREAD, 0xE1000000))
            return 1

        callback = READY(ready_reentrant)
        backend = Backend(None, self.callbacks[0], self.callbacks[1],
                          self.callbacks[2], self.callbacks[3], callback,
                          self.callbacks[5])
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUREAD, 0xE1000000), 0)
        self.assertEqual(nested, [0])
        self.assertEqual(fresh.faulted, 1)
        self.assertEqual(fresh.accepted_gp0_words, 0)

    def test_gp0_counter_overflow_refuses_before_backend_callback(self):
        self.controller.accepted_gp0_words = (1 << 64) - 1
        before_calls = list(self.calls)
        before = bytes(self.controller)
        self.assertEqual(self.write(0xE1000000), 0)
        self.assertEqual(self.calls, before_calls)
        self.assertEqual(bytes(self.controller), before)

    def test_busy_gp0_refuses_without_command_effects_and_ready_bits_clear(self):
        self.ready_value = 0
        before = bytes(self.controller)
        self.assertEqual(self.write(0xE1001234), 0)
        self.assertEqual(bytes(self.controller), before)
        accepted, status = self.read()
        self.assertEqual(accepted, 1)
        self.assertEqual(status & ((1 << 26) | (1 << 28)), 0)

    def test_status_maps_display_dma_and_backend_ready_state(self):
        self.assertEqual(self.write(0x03000000, GPUSTAT), 1)
        self.assertEqual(self.controller.display_disabled, 0)
        self.assertEqual(self.write(0x04000002, GPUSTAT), 1)
        _, status = self.read()
        self.assertEqual(status & (1 << 23), 0)
        self.assertEqual((status >> 29) & 3, 2)
        self.assertEqual(status & (1 << 25), 1 << 25)
        self.ready_value = 0
        _, status = self.read()
        self.assertEqual(status & ((1 << 26) | (1 << 28) | (1 << 25)), 0)
        self.assertEqual(self.write(0x03000001, GPUSTAT), 1)
        self.assertEqual(self.controller.display_disabled, 1)
        self.assertEqual(self.calls[-1], ("display_enable", 0))

    def test_gp1_reset_fifo_ack_display_and_dma_controls_are_checked(self):
        self.controller.read_latch = 99
        self.controller.draw_mode = 0x3FFF
        self.controller.display_disabled = 0
        self.controller.dma_direction = 3
        self.assertEqual(self.write(0x00000000, GPUSTAT), 1)
        self.assertEqual(self.controller.read_latch, 0x400)
        self.assertEqual(self.controller.draw_mode, 0)
        self.assertEqual(self.controller.display_disabled, 1)
        self.assertEqual(self.controller.dma_direction, 0)
        self.assertEqual(self.write(0x00000000, GPUSTAT), 1)
        self.assertEqual(self.write(0x00000000, GPUSTAT), 1)
        self.assertEqual(self.write(0x00000000, GPUSTAT), 1)

    def test_vblank_toggles_gpu_status_odd_even_bit_once_per_edge(self):
        self.assertEqual(self.read()[1] & (1 << 31), 0)
        self.assertEqual(self.vblank(), 1)
        self.assertEqual(self.controller.vblank_parity, 1)
        self.assertEqual(self.read()[1] & (1 << 31), 1 << 31)
        self.assertEqual(self.vblank(), 1)
        self.assertEqual(self.controller.vblank_parity, 0)
        self.assertEqual(self.read()[1] & (1 << 31), 0)

    def test_two_coalesced_source_edges_toggle_parity_twice(self):
        self.assertEqual(self.vblank(), 1)
        self.assertEqual(self.vblank(), 1)
        self.assertEqual(self.controller.vblank_parity, 0)
        self.assertEqual(self.read()[1] & (1 << 31), 0)

    def test_vblank_does_not_require_ready_backend_for_blank_edge(self):
        self.ready_value = 0
        self.assertEqual(self.calls, [("reset",)])
        self.assertEqual(self.vblank(), 1)
        self.assertEqual(self.calls, [("reset",), ("ready",)])
        accepted, status = self.read()
        self.assertEqual(accepted, 1)
        self.assertEqual(status & (1 << 31), 1 << 31)
        self.assertEqual(status & ((1 << 26) | (1 << 28) | (1 << 25)), 0)

    def test_gp1_reset_clears_vblank_parity_to_source_reset_value(self):
        self.assertEqual(self.vblank(), 1)
        self.assertEqual(self.write(0x00000000, GPUSTAT), 1)
        self.assertEqual(self.controller.vblank_parity, 0)
        accepted, status = self.read()
        self.assertEqual(accepted, 1)
        self.assertEqual(status, 0x14802000)

    def test_vblank_refuses_invalid_owner_without_moving_prior_parity(self):
        self.controller.vblank_parity = 1
        self.controller.faulted = 1
        self.assertEqual(self.vblank(), 0)
        self.assertEqual(self.controller.vblank_parity, 1)
        self.controller.faulted = 0
        self.controller.executing = 1
        self.assertEqual(self.vblank(), 0)
        self.assertEqual(self.controller.vblank_parity, 1)

        fresh = Controller()
        self.assertEqual(self.vblank(fresh), 0)
        self.assertEqual(fresh.vblank_parity, 0)

        def refuse_ready(_):
            self.calls.append(("ready_refused",))
            return -1

        refused_ready = READY(refuse_ready)
        backend = Backend(None, self.callbacks[0], self.callbacks[1],
                          self.callbacks[2], self.callbacks[3], refused_ready)
        refused = Controller()
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(refused), C.byref(backend)), 1)
        refused.vblank_parity = 1
        self.assertEqual(self.lib.musashi_gpu_controller_vblank(
            C.byref(refused)), 0)
        self.assertEqual(refused.faulted, 1)
        self.assertEqual(refused.vblank_parity, 1)
        output = UINT32(0xDEADBEEF)
        self.assertEqual(self.lib.musashi_gpu_controller_read32(
            C.byref(refused), GPUSTAT, C.byref(output)), 0)
        self.assertEqual(refused.faulted, 1)
        self.assertEqual(output.value, 0xDEADBEEF)
        self.assertEqual(refused.vblank_parity, 1)

    def test_backend_refusal_faults_controller_and_blocks_later_access(self):
        def refuse_draw(_, word):
            self.calls.append(("draw_refused", word))
            return 0

        refusal = DRAW_MODE(refuse_draw)
        backend = Backend(None, self.callbacks[0], refusal, self.callbacks[2],
                          self.callbacks[3], self.callbacks[4])
        fresh = Controller()
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUREAD, 0xE1000000), 0)
        self.assertEqual(fresh.faulted, 1)
        output = UINT32(0xDEADBEEF)
        self.assertEqual(self.lib.musashi_gpu_controller_read32(
            C.byref(fresh), GPUSTAT, C.byref(output)), 0)
        self.assertEqual(output.value, 0xDEADBEEF)

    def test_unsupported_commands_and_addresses_refuse_without_effects(self):
        before = bytes(self.controller)
        for value, address in ((0x1F000000, GPUREAD),
                               (0x09000000, GPUSTAT)):
            self.assertEqual(self.write(value, address), 0)
            self.assertEqual(bytes(self.controller), before)
        self.assertEqual(self.write(0x06000001, GPUSTAT), 0)
        self.assertEqual(bytes(self.controller), before)
        output = UINT32(0xDEADBEEF)
        self.assertEqual(self.lib.musashi_gpu_controller_read32(
            C.byref(self.controller), 0x1F801818, C.byref(output)), 0)
        self.assertEqual(output.value, 0xDEADBEEF)
        self.assertEqual(self.write(0, 0x1F801818), 0)

    def test_uninitialized_controller_refuses_without_calling_unbound_backend(self):
        fresh = Controller()
        output = UINT32(0xDEADBEEF)
        self.assertEqual(self.lib.musashi_gpu_controller_read32(
            C.byref(fresh), GPUSTAT, C.byref(output)), 0)
        self.assertEqual(output.value, 0xDEADBEEF)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUSTAT, 0x02000000), 0)

    def test_backend_callbacks_cannot_reenter_writes_and_see_ready_during_execution(self):
        fresh = Controller()
        nested = []

        def nested_draw(_, word):
            fresh.dma_direction = 1
            nested.append(self.lib.musashi_gpu_controller_write32(
                C.byref(fresh), GPUREAD, word))
            output = UINT32(0xDEADBEEF)
            self.assertEqual(self.lib.musashi_gpu_controller_read32(
                C.byref(fresh), GPUSTAT, C.byref(output)), 0)
            self.assertEqual(output.value, 0xDEADBEEF)
            return 1

        callback = DRAW_MODE(nested_draw)
        backend = Backend(None, self.callbacks[0], callback, self.callbacks[2],
                          self.callbacks[3], self.callbacks[4], self.callbacks[5])
        self.assertEqual(self.lib.musashi_gpu_controller_init(
            C.byref(fresh), C.byref(backend)), 1)
        self.assertEqual(self.lib.musashi_gpu_controller_write32(
            C.byref(fresh), GPUREAD, 0xE1001234), 0)
        self.assertEqual(nested, [0])
        self.assertEqual(fresh.faulted, 1)


    def test_gp0_clear_cache_is_nop_like_ready_word(self):
        before_calls = list(self.calls)
        self.assertEqual(self.write(0x01000000), 1)
        self.assertEqual(self.controller.accepted_gp0_words, 1)
        self.assertEqual(self.controller.store_phase, 0)
        self.assertEqual(self.calls, before_calls + [("ready",)])

    def test_gp0_cpu_to_vram_writes_each_texel_through_store_vram(self):
        stored = []

        def accept(_, x, y, pixel):
            stored.append((x, y, pixel))
            return 1

        self.store_cb = STORE(accept)
        self.controller.backend.store_vram = self.store_cb
        self.assertEqual(self.write(0xA0000000), 1)
        self.assertEqual(self.controller.store_phase, 1)
        self.assertEqual(self.write(0x00020010), 1)  # y=2 x=16
        self.assertEqual(self.write(0x00020002), 1)  # h=2 w=2
        self.assertEqual(self.controller.store_remaining, 4)
        self.assertEqual(stored, [])
        self.assertEqual(self.write(0x22221111), 1)
        self.assertEqual(stored, [(16, 2, 0x1111), (17, 2, 0x2222)])
        self.assertEqual(self.write(0x44443333), 1)
        self.assertEqual(stored, [
            (16, 2, 0x1111), (17, 2, 0x2222),
            (16, 3, 0x3333), (17, 3, 0x4444)])
        self.assertEqual(self.controller.store_phase, 0)
        self.assertEqual(self.controller.stored_pixels, 4)
        self.assertEqual(self.controller.accepted_gp0_words, 5)

    def test_gp0_cpu_to_vram_payload_is_not_parsed_as_fill(self):
        stored = []

        def accept(_, x, y, pixel):
            stored.append((x, y, pixel))
            return 1

        self.store_cb = STORE(accept)
        self.controller.backend.store_vram = self.store_cb
        self.assertEqual(self.write(0xA0000000), 1)
        self.assertEqual(self.write(0), 1)
        self.assertEqual(self.write(0x00020002), 1)
        self.assertEqual(self.write(0x02000200), 1)
        self.assertEqual(stored, [(0, 0, 0x0200), (1, 0, 0x0200)])
        self.assertEqual(self.controller.fill_words, 0)
        self.assertEqual(self.controller.store_remaining, 2)
        self.assertEqual(self.write(0xA000A000), 1)
        self.assertEqual(stored, [
            (0, 0, 0x0200), (1, 0, 0x0200),
            (0, 1, 0xA000), (1, 1, 0xA000)])
        self.assertEqual(self.controller.store_phase, 0)

    def test_gp0_poly_f4_rasterizes_through_store_vram(self):
        stored = []

        def accept(_, x, y, pixel):
            stored.append((x, y, pixel))
            return 1

        self.store_cb = STORE(accept)
        self.controller.backend.store_vram = self.store_cb
        # 0x2A is semi-transparent: black back buffer + additive (E1 abr=1).
        self.read_cb = READ(lambda _, x, y, out: out.__setitem__(0, 0) or 1)
        self.controller.backend.read_vram = self.read_cb
        self.assertEqual(self.write(0xE1000020), 1)
        self.assertEqual(self.write(0xE3000000), 1)
        self.assertEqual(self.write(0xE403C13F), 1)  # 319,240
        self.assertEqual(self.write(0x2AFFFFFF), 1)
        self.assertEqual(self.controller.prim_needed, 5)
        self.assertEqual(self.write(0x00000000), 1)
        self.assertEqual(self.write(0x00000008), 1)
        self.assertEqual(self.write(0x00080000), 1)
        self.assertEqual(self.write(0x00080008), 1)
        self.assertEqual(self.controller.prim_needed, 0)
        self.assertGreater(len(stored), 0)
        self.assertTrue(all(p == 0x7FFF for _, _, p in stored))
        self.assertEqual(self.controller.drawn_pixels, len(stored))

    def test_gp0_poly_f3_flat_triangle(self):
        for opcode in (0x20, 0x21, 0x22, 0x23):
            self.setUp()
            stored = []
            self.store_cb = STORE(lambda _, x, y, p: stored.append((x, y, p)) or 1)
            self.controller.backend.store_vram = self.store_cb
            # Semi-transparent variants (command bit 1) read the back buffer.
            # With a black back buffer and additive blending (E1 abr=1),
            # B+F == F, so the colours below hold for every variant.
            back_reads = []
            self.read_cb = READ(lambda _, x, y, out: back_reads.append((x, y))
                                or out.__setitem__(0, 0) or 1)
            self.controller.backend.read_vram = self.read_cb
            if opcode & 2:
                self.assertEqual(self.write(0xE1000020), 1)
            self.assertEqual(self.write(0xE3000000), 1)
            self.assertEqual(self.write(0xE403C13F), 1)  # 319, 240
            cmd = (opcode << 24) | 0x000000F8  # pure red (r=248 -> 31 -> 0x001F)
            self.assertEqual(self.write(cmd), 1)
            self.assertEqual(self.controller.prim_needed, 4)
            self.assertEqual(self.controller.prim_got, 1)
            self.assertEqual(self.write(0x00000000), 1)  # v0 (0, 0)
            self.assertEqual(self.write(0x00000008), 1)  # v1 (8, 0)
            self.assertEqual(self.write(0x00080000), 1)  # v2 (0, 8)
            self.assertEqual(self.controller.prim_needed, 0)
            self.assertGreater(len(stored), 0)
            self.assertTrue(all(p == 0x001F for _, _, p in stored))
            self.assertEqual(self.controller.drawn_pixels, len(stored))

            self.assertEqual(bool(back_reads), bool(opcode & 2))
    def _polyline(self, words):
        stored = []
        self.store_cb = STORE(lambda _, x, y, p: stored.append((x, y, p)) or 1)
        self.controller.backend.store_vram = self.store_cb
        self.assertEqual(self.write(0xE3000000), 1)
        self.assertEqual(self.write(0xE403C13F), 1)
        for w in words:
            self.assertEqual(self.write(w), 1, hex(w))
        return stored

    def test_gp0_flat_polyline_until_terminator(self):
        # 0x48: colour, then vertices until a 0x5xxx5xxx word.
        stored = self._polyline([0x480000F8, 0x00000000, 0x00000008,
                                 0x00080008, 0x55555555])
        self.assertFalse(self.controller.poly_active)
        pts = {(x, y) for x, y, _ in stored}
        self.assertIn((0, 0), pts); self.assertIn((8, 0), pts); self.assertIn((8, 8), pts)
        self.assertTrue(all(p == 0x001F for _, _, p in stored))
        # The next word is a command again, not a vertex.
        self.assertEqual(self.write(0xE1000000), 1)
        self.assertEqual(self.controller.draw_mode, 0)

    def test_gp0_gouraud_polyline_terminator_in_colour_slot(self):
        # 0x58: c0|cmd, v0, c1, v1, c2, v2, terminator (colour position).
        # A vertex that looks like a terminator is still a vertex.
        # Its segments are 1024+ wide and not drawn (psx-spx), so the only
        # pixels come from the last in-range segment (16,16)-(0,16).
        stored = self._polyline([0x580000F8, 0x00000000, 0x0000F800, 0x50005000,
                                 0x00F80000, 0x00100010, 0x000000F8, 0x00100000,
                                 0x50005000])
        self.assertFalse(self.controller.poly_active)
        self.assertEqual({(x, y) for x, y, _ in stored}, {(x, 16) for x in range(17)})
        self.assertEqual(self.write(0xE1000000), 1)

    def test_gp0_line_clipped_to_drawing_area(self):
        # E3/E4 = (0,0)-(4,4): the (0,2)-(10,2) line keeps x 0..4 only.
        stored = []
        self.store_cb = STORE(lambda _, x, y, p: stored.append((x, y, p)) or 1)
        self.controller.backend.store_vram = self.store_cb
        self.assertEqual(self.write(0xE3000000), 1)
        self.assertEqual(self.write(0xE4001004), 1)
        for w in (0x400000F8, 0x00020000, 0x0002000A):
            self.assertEqual(self.write(w), 1)
        self.assertEqual({(x, y) for x, y, _ in stored}, {(x, 2) for x in range(5)})

    def test_gp0_polyline_reset_by_gp1(self):
        self._polyline([0x480000F8, 0x00000000])
        self.assertTrue(self.controller.poly_active)
        self.assertEqual(self.write(0x01000000, GPUSTAT), 1)
        self.assertFalse(self.controller.poly_active)

    def test_gp0_poly_f4_flat_quad_five_words(self):
        for opcode in (0x28, 0x29, 0x2A, 0x2B):
            self.setUp()
            stored = []
            self.store_cb = STORE(lambda _, x, y, p: stored.append((x, y, p)) or 1)
            self.controller.backend.store_vram = self.store_cb
            # Semi-transparent variants (command bit 1) read the back buffer.
            # With a black back buffer and additive blending (E1 abr=1),
            # B+F == F, so the colours below hold for every variant.
            back_reads = []
            self.read_cb = READ(lambda _, x, y, out: back_reads.append((x, y))
                                or out.__setitem__(0, 0) or 1)
            self.controller.backend.read_vram = self.read_cb
            if opcode & 2:
                self.assertEqual(self.write(0xE1000020), 1)
            self.assertEqual(self.write(0xE3000000), 1)
            self.assertEqual(self.write(0xE403C13F), 1)  # 319, 240
            cmd = (opcode << 24) | 0x0000F800  # pure green (g=248 -> 31 -> 0x03E0)
            self.assertEqual(self.write(cmd), 1)
            self.assertEqual(self.controller.prim_needed, 5)
            self.assertEqual(self.controller.prim_got, 1)
            self.assertEqual(self.write(0x00000000), 1)  # v0 (0, 0)
            self.assertEqual(self.write(0x00000008), 1)  # v1 (8, 0)
            self.assertEqual(self.write(0x00080000), 1)  # v2 (0, 8)
            self.assertEqual(self.write(0x00080008), 1)  # v3 (8, 8)
            self.assertEqual(self.controller.prim_needed, 0)
            self.assertGreater(len(stored), 0)
            self.assertTrue(all(p == 0x03E0 for _, _, p in stored))
            self.assertEqual(self.controller.drawn_pixels, len(stored))

            self.assertEqual(bool(back_reads), bool(opcode & 2))
    def test_gp0_poly_g3_gouraud_triangle(self):
        for opcode in (0x30, 0x31, 0x32, 0x33):
            self.setUp()
            stored = {}
            self.store_cb = STORE(lambda _, x, y, p: stored.__setitem__((x, y), p) or 1)
            self.controller.backend.store_vram = self.store_cb
            # Semi-transparent variants (command bit 1) read the back buffer.
            # With a black back buffer and additive blending (E1 abr=1),
            # B+F == F, so the colours below hold for every variant.
            back_reads = []
            self.read_cb = READ(lambda _, x, y, out: back_reads.append((x, y))
                                or out.__setitem__(0, 0) or 1)
            self.controller.backend.read_vram = self.read_cb
            if opcode & 2:
                self.assertEqual(self.write(0xE1000020), 1)
            self.assertEqual(self.write(0xE3000000), 1)
            self.assertEqual(self.write(0xE403C13F), 1)  # 319, 240
            cmd = (opcode << 24) | 0x000000F8  # v0 red (r=248 -> 31)
            self.assertEqual(self.write(cmd), 1)
            self.assertEqual(self.controller.prim_needed, 6)
            self.assertEqual(self.controller.prim_got, 1)
            self.assertEqual(self.write(0x00000000), 1)  # v0 (0, 0)
            self.assertEqual(self.write(0x0000F800), 1)  # c1 green (g=248 -> 31)
            self.assertEqual(self.write(0x00000010), 1)  # v1 (16, 0)
            self.assertEqual(self.write(0x00F80000), 1)  # c2 blue (b=248 -> 31)
            self.assertEqual(self.write(0x00100000), 1)  # v2 (0, 16)
            self.assertEqual(self.controller.prim_needed, 0)
            self.assertGreater(len(stored), 0)
            self.assertEqual(stored.get((0, 0)), 0x001F)  # pure red
            self.assertEqual(stored.get((16, 0)), 0x03E0)  # pure green
            self.assertEqual(stored.get((0, 16)), 0x7C00)  # pure blue
            mid = stored.get((8, 0))
            self.assertIsNotNone(mid)
            self.assertGreater(mid & 0x1F, 0)
            self.assertGreater((mid >> 5) & 0x1F, 0)
            self.assertEqual((mid >> 10) & 0x1F, 0)

            self.assertEqual(bool(back_reads), bool(opcode & 2))
    def test_gp0_poly_g4_gouraud_quad(self):
        for opcode in (0x38, 0x39, 0x3A, 0x3B):
            self.setUp()
            stored = {}
            self.store_cb = STORE(lambda _, x, y, p: stored.__setitem__((x, y), p) or 1)
            self.controller.backend.store_vram = self.store_cb
            # Semi-transparent variants (command bit 1) read the back buffer.
            # With a black back buffer and additive blending (E1 abr=1),
            # B+F == F, so the colours below hold for every variant.
            back_reads = []
            self.read_cb = READ(lambda _, x, y, out: back_reads.append((x, y))
                                or out.__setitem__(0, 0) or 1)
            self.controller.backend.read_vram = self.read_cb
            if opcode & 2:
                self.assertEqual(self.write(0xE1000020), 1)
            self.assertEqual(self.write(0xE3000000), 1)
            self.assertEqual(self.write(0xE403C13F), 1)  # 319, 240
            cmd = (opcode << 24) | 0x000000F8  # v0 red
            self.assertEqual(self.write(cmd), 1)
            self.assertEqual(self.controller.prim_needed, 8)
            self.assertEqual(self.controller.prim_got, 1)
            self.assertEqual(self.write(0x00000000), 1)  # v0 (0, 0)
            self.assertEqual(self.write(0x0000F800), 1)  # c1 green
            self.assertEqual(self.write(0x00000010), 1)  # v1 (16, 0)
            self.assertEqual(self.write(0x00F80000), 1)  # c2 blue
            self.assertEqual(self.write(0x00100000), 1)  # v2 (0, 16)
            self.assertEqual(self.write(0x00F8F8F8), 1)  # c3 white
            self.assertEqual(self.write(0x00100010), 1)  # v3 (16, 16)
            self.assertEqual(self.controller.prim_needed, 0)
            self.assertGreater(len(stored), 0)
            self.assertEqual(stored.get((0, 0)), 0x001F)    # red
            self.assertEqual(stored.get((16, 0)), 0x03E0)   # green
            self.assertEqual(stored.get((0, 16)), 0x7C00)   # blue
            self.assertEqual(stored.get((16, 16)), 0x7FFF)  # white

            self.assertEqual(bool(back_reads), bool(opcode & 2))
    def test_gp0_poly_gt3_textured_gouraud_triangle(self):
        for opcode in (0x34, 0x35):
            self.setUp()
            stored = []
            self.store_cb = STORE(lambda _, x, y, p: stored.append((x, y, p)) or 1)
            self.read_cb = READ(lambda _, x, y, out: out.__setitem__(0, 0x7FFF) or 1)
            self.controller.backend.store_vram = self.store_cb
            self.controller.backend.read_vram = self.read_cb
            self.assertEqual(self.write(0xE3000000), 1)
            self.assertEqual(self.write(0xE403C13F), 1)
            tpage = 0x0100
            cmd = (opcode << 24) | 0x00808080
            self.assertEqual(self.write(cmd), 1)
            self.assertEqual(self.controller.prim_needed, 9)
            self.assertEqual(self.write(0x00000000), 1)  # v0 (0, 0)
            self.assertEqual(self.write(0x00000000), 1)  # uv0=(0,0), clut=0
            self.assertEqual(self.write(0x00808080), 1)  # c1
            self.assertEqual(self.write(0x00000008), 1)  # v1 (8, 0)
            self.assertEqual(self.write((tpage << 16) | 0x0008), 1)  # uv1=(8,0), tpage
            self.assertEqual(self.write(0x00808080), 1)  # c2
            self.assertEqual(self.write(0x00080000), 1)  # v2 (0, 8)
            self.assertEqual(self.write(0x00000800), 1)  # uv2=(0,8)
            self.assertEqual(self.controller.prim_needed, 0)
            self.assertGreater(len(stored), 0)

    def test_gp0_poly_gt4_textured_gouraud_quad(self):
        for opcode in (0x3C, 0x3D):
            self.setUp()
            stored = []
            self.store_cb = STORE(lambda _, x, y, p: stored.append((x, y, p)) or 1)
            self.read_cb = READ(lambda _, x, y, out: out.__setitem__(0, 0x7FFF) or 1)
            self.controller.backend.store_vram = self.store_cb
            self.controller.backend.read_vram = self.read_cb
            self.assertEqual(self.write(0xE3000000), 1)
            self.assertEqual(self.write(0xE403C13F), 1)
            tpage = 0x0100
            cmd = (opcode << 24) | 0x00808080
            self.assertEqual(self.write(cmd), 1)
            self.assertEqual(self.controller.prim_needed, 12)
            self.assertEqual(self.write(0x00000000), 1)  # v0 (0, 0)
            self.assertEqual(self.write(0x00000000), 1)  # uv0=(0,0), clut=0
            self.assertEqual(self.write(0x00808080), 1)  # c1
            self.assertEqual(self.write(0x00000008), 1)  # v1 (8, 0)
            self.assertEqual(self.write((tpage << 16) | 0x0008), 1)  # uv1=(8,0), tpage
            self.assertEqual(self.write(0x00808080), 1)  # c2
            self.assertEqual(self.write(0x00080000), 1)  # v2 (0, 8)
            self.assertEqual(self.write(0x00000800), 1)  # uv2=(0,8)
            self.assertEqual(self.write(0x00808080), 1)  # c3
            self.assertEqual(self.write(0x00080008), 1)  # v3 (8, 8)
            self.assertEqual(self.write(0x00000808), 1)  # uv3=(8,8)
            self.assertEqual(self.controller.prim_needed, 0)
            self.assertGreater(len(stored), 0)

    def test_gp0_cpu_to_vram_without_backend_refuses(self):
        before = bytes(self.controller)
        self.assertEqual(self.write(0xA0000000), 0)
        self.assertEqual(bytes(self.controller), before)

    def test_gp0_store_image_packet_parsing_and_header_length(self):
        self.read_cb = READ(lambda _, x, y, out: out.__setitem__(0, 0x1234) or 1)
        self.controller.backend.read_vram = self.read_cb
        self.assertEqual(self.store_image_phase(), 0)

        # Word 0: Command 0xC0000000
        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.controller.faulted, 0)
        self.assertEqual(self.store_image_phase(), 1)
        self.assertEqual(self.controller.accepted_gp0_words, 1)
        self.assertEqual(self.read()[1] & (1 << 26), 0)
        self.assertEqual(self.read()[1] & (1 << 27), 0)

        # Word 1: VRAM X=16, Y=2
        self.assertEqual(self.write(0x00020010), 1)
        self.assertEqual(self.controller.faulted, 0)
        self.assertEqual(self.store_image_phase(), 2)
        self.assertEqual(self.controller.accepted_gp0_words, 2)
        self.assertEqual(self.read()[1] & (1 << 26), 0)

        # Word 2: Width=2, Height=2 (total 4 halfwords = 2 32-bit words)
        self.assertEqual(self.write(0x00020002), 1)
        self.assertEqual(self.controller.faulted, 0)
        self.assertEqual(self.store_image_phase(), 3)
        self.assertEqual(self.controller.accepted_gp0_words, 3)
        self.assertNotEqual(self.read()[1] & (1 << 26), 0)
        self.assertNotEqual(self.read()[1] & (1 << 27), 0)

        # Verify next word is treated as a new command (NOP)
        self.assertEqual(self.write(0x00000000), 1)
        self.assertEqual(self.controller.accepted_gp0_words, 4)
        self.assertEqual(self.controller.faulted, 0)

    def test_gp0_store_image_transfer_parameters(self):
        self.read_cb = READ(lambda _, x, y, out: out.__setitem__(0, 0x5555) or 1)
        self.controller.backend.read_vram = self.read_cb

        # Test parameter extraction for (x=100, y=50, w=16, h=8)
        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write((50 << 16) | 100), 1)
        self.assertEqual(self.write((8 << 16) | 16), 1)
        self.assertEqual(self.controller.faulted, 0)

        ok, x, y, w, h, total = self.get_store_image()
        self.assertEqual(ok, 1)
        self.assertEqual(x, 100)
        self.assertEqual(y, 50)
        self.assertEqual(w, 16)
        self.assertEqual(h, 8)
        self.assertEqual(total, 128)
        self.assertEqual(self.store_image_remaining_words(), 64)

        # Test odd halfword count (w=3, h=3 -> 9 halfwords -> 5 32-bit words)
        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write((20 << 16) | 10), 1)
        self.assertEqual(self.write((3 << 16) | 3), 1)
        self.assertEqual(self.controller.faulted, 0)

        ok, x, y, w, h, total = self.get_store_image()
        self.assertEqual(ok, 1)
        self.assertEqual(x, 10)
        self.assertEqual(y, 20)
        self.assertEqual(w, 3)
        self.assertEqual(h, 3)
        self.assertEqual(total, 9)
        self.assertEqual(self.store_image_remaining_words(), 5)

        # Test opcodes in range 0xC0..0xDF
        for op in (0xC0, 0xC1, 0xC8, 0xD0, 0xDF):
            self.assertEqual(self.write((op << 24) | 0x123456), 1)
            self.assertEqual(self.write(0x00010002), 1)
            self.assertEqual(self.write(0x00010002), 1)
            self.assertEqual(self.controller.faulted, 0)
            ok, x, y, w, h, total = self.get_store_image()
            self.assertEqual(ok, 1)
            self.assertEqual(w, 2)
            self.assertEqual(h, 1)
            self.assertEqual(total, 2)

    def test_gp0_store_image_roundtrip_with_load_image(self):
        vram = {}

        def mock_store(_, x, y, pixel):
            vram[(x, y)] = pixel
            return 1

        def mock_read(_, x, y, out):
            out[0] = vram.get((x, y), 0)
            return 1

        self.store_cb = STORE(mock_store)
        self.read_cb = READ(mock_read)
        self.controller.backend.store_vram = self.store_cb
        self.controller.backend.read_vram = self.read_cb

        # 1. Load a 4x4 image at (32, 48) with unique colors
        test_pixels = [0x1000 + i * 0x111 for i in range(16)]
        self.assertEqual(self.write(0xA0000000), 1)
        self.assertEqual(self.write((48 << 16) | 32), 1)
        self.assertEqual(self.write((4 << 16) | 4), 1)
        for i in range(0, 16, 2):
            word = test_pixels[i] | (test_pixels[i + 1] << 16)
            self.assertEqual(self.write(word), 1)
        self.assertEqual(self.controller.faulted, 0)
        self.assertEqual(len(vram), 16)

        # 2. StoreImage at (32, 48) with size 4x4
        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write((48 << 16) | 32), 1)
        self.assertEqual(self.write((4 << 16) | 4), 1)
        self.assertEqual(self.controller.faulted, 0)
        self.assertEqual(self.store_image_remaining_words(), 8)

        # 3. Read back 8 words from GPUREAD and verify round-trip pixels
        read_pixels = []
        for _ in range(8):
            ok, val = self.read(GPUREAD)
            self.assertEqual(ok, 1)
            read_pixels.append(val & 0xFFFF)
            read_pixels.append((val >> 16) & 0xFFFF)
        self.assertEqual(read_pixels, test_pixels)
        self.assertEqual(self.controller.faulted, 0)
        self.assertEqual(self.store_image_remaining_words(), 0)

        # 4. Reading past end returns latched last word
        expected_last_word = test_pixels[14] | (test_pixels[15] << 16)
        ok, val = self.read(GPUREAD)
        self.assertEqual(ok, 1)
        self.assertEqual(val, expected_last_word)

    def test_gp0_store_image_roundtrip_with_fill(self):
        vram = {}

        def mock_fill(_, f):
            candidate = f.contents
            for r in range(candidate.height):
                for c in range(candidate.width):
                    vram[(candidate.x + c, candidate.y + r)] = candidate.color
            return 1

        def mock_read(_, x, y, out):
            out[0] = vram.get((x, y), 0)
            return 1

        self.fill_cb = FILL(mock_fill)
        self.read_cb = READ(mock_read)
        self.controller.backend.fill_vram = self.fill_cb
        self.controller.backend.read_vram = self.read_cb

        # Fill 16x16 with color
        self.assertEqual(self.write(0x02ff0000), 1)
        self.assertEqual(self.write(0x00000000), 1)
        self.assertEqual(self.write((16 << 16) | 16), 1)
        self.assertEqual(self.controller.faulted, 0)

        # StoreImage over filled area
        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write(0x00000000), 1)
        self.assertEqual(self.write((16 << 16) | 16), 1)
        self.assertEqual(self.controller.faulted, 0)

        # Read back 128 words (256 halfwords)
        for _ in range(128):
            ok, val = self.read(GPUREAD)
            self.assertEqual(ok, 1)
            p0 = val & 0xFFFF
            p1 = (val >> 16) & 0xFFFF
            self.assertEqual(p0, 0x7C00)
            self.assertEqual(p1, 0x7C00)
        self.assertEqual(self.controller.faulted, 0)

    def test_gp0_store_image_boundary_handling(self):
        vram = {}
        for cy in range(508, 512):
            for cx in range(1020, 1024):
                vram[(cx, cy)] = 0x1234

        accessed = []

        def mock_read(_, x, y, out):
            accessed.append((x, y))
            out[0] = vram.get((x, y), 0)
            return 1

        self.read_cb = READ(mock_read)
        self.controller.backend.read_vram = self.read_cb

        # StoreImage spanning past bottom-right: x=1020, y=510, w=8, h=4
        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write((510 << 16) | 1020), 1)
        self.assertEqual(self.write((4 << 16) | 8), 1)
        self.assertEqual(self.controller.faulted, 0)

        for ax, ay in accessed:
            self.assertLess(ax, 1024)
            self.assertLess(ay, 512)

        pixels = []
        for _ in range(16):
            ok, val = self.read(GPUREAD)
            self.assertEqual(ok, 1)
            pixels.append(val & 0xFFFF)
            pixels.append((val >> 16) & 0xFFFF)

        for r in range(4):
            for c in range(8):
                idx = r * 8 + c
                if r < 2 and c < 4:
                    self.assertEqual(pixels[idx], 0x1234)
                else:
                    self.assertEqual(pixels[idx], 0)
        self.assertEqual(self.controller.faulted, 0)

    def test_gp0_store_image_backend_refusal_and_missing_callback(self):
        before = bytes(self.controller)
        self.assertEqual(self.write(0xC0000000), 0)
        self.assertEqual(bytes(self.controller), before)

        self.read_cb = READ(lambda *_: 0)
        self.controller.backend.read_vram = self.read_cb
        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write(0x00000000), 1)
        self.assertEqual(self.write(0x00010001), 0)
        self.assertEqual(self.controller.faulted, 1)

        self.setUp()
        self.read_cb = READ(lambda _, x, y, out: out.__setitem__(0, 1) or 1)
        self.controller.backend.read_vram = self.read_cb
        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write(0x00000000), 1)
        self.ready_value = 0
        self.assertEqual(self.write(0x00010001), 0)
        self.assertEqual(self.controller.faulted, 0)

    def test_gp0_store_image_gp1_reset_and_clear_fifo(self):
        self.read_cb = READ(lambda _, x, y, out: out.__setitem__(0, 0xABCD) or 1)
        self.controller.backend.read_vram = self.read_cb

        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write(0x00000000), 1)
        self.assertEqual(self.write(0x00020002), 1)
        self.assertEqual(self.store_image_remaining_words(), 2)

        self.assertEqual(self.write(0x00000000, GPUSTAT), 1)
        self.assertEqual(self.store_image_phase(), 0)
        self.assertEqual(self.store_image_remaining_words(), 0)
        self.assertEqual(self.controller.read_latch, 0x400)

        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.store_image_phase(), 1)
        self.assertEqual(self.write(0x01000000, GPUSTAT), 1)
        self.assertEqual(self.store_image_phase(), 0)

    def test_gp0_store_image_dimension_limits(self):
        self.read_cb = READ(lambda _, x, y, out: out.__setitem__(0, 1) or 1)
        self.controller.backend.read_vram = self.read_cb

        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write(0x00000000), 1)
        self.assertEqual(self.write((1 << 16) | 1025), 0)
        self.assertEqual(self.controller.faulted, 0)

        self.assertEqual(self.write((513 << 16) | 1), 0)
        self.assertEqual(self.controller.faulted, 0)

    def test_gp0_store_image_dma_direction_status(self):
        self.read_cb = READ(lambda _, x, y, out: out.__setitem__(0, 0x42) or 1)
        self.controller.backend.read_vram = self.read_cb

        self.assertEqual(self.write(0x04000003, GPUSTAT), 1)
        self.assertEqual(self.read()[1] & (1 << 25), 0)

        self.assertEqual(self.write(0xC0000000), 1)
        self.assertEqual(self.write(0x00000000), 1)
        self.assertEqual(self.write(0x00010004), 1)

        status = self.read()[1]
        self.assertNotEqual(status & (1 << 25), 0)
        self.assertNotEqual(status & (1 << 27), 0)

        self.read(GPUREAD)
        self.read(GPUREAD)

        status = self.read()[1]
        self.assertEqual(status & (1 << 25), 0)
        self.assertEqual(status & (1 << 27), 0)


if __name__ == "__main__":
    unittest.main()
