"""Execute the native RAM boundary; these are not game-boot acceptance tests."""
import ctypes as C
import hashlib
import json
import os
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAM_SIZE = 0x200000


class Memory(C.Structure):
    _fields_ = [("bytes", C.c_ubyte * RAM_SIZE)]


class EntryState(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ("stack", "heap", "size")]


class PrefixStop(C.Structure):
    _fields_ = [("call", C.c_uint32), ("target", C.c_uint32)]


class BootMemoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="musashi-boot-memory-")
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / "boot_memory.so"
        subprocess.run([
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
            "-shared", "-fPIC", "-I", str(ROOT / "pc_port/include"),
            "-I", str(ROOT / "include"), "-DMUSASHI_NATIVE_MEMORY_BINDING=1",
            "-DMUSASHI_NATIVE_CONSOLE_BINDING=1",
            "-DMUSASHI_NATIVE_MEMSET_BINDING=1",
            "-DMUSASHI_NATIVE_TABLE_CLEAR_BINDING=1",
            str(ROOT / "pc_port/boot_memory.c"),
            str(ROOT / "pc_port/startup_bindings.c"),
            "-lcrypto", "-o", str(library),
            str(ROOT / "src/main/80016714.c"),
            str(ROOT / "src/main/80029044.c"),
            str(ROOT / "src/main/8002906c.c"),
        ], check=True, stdin=subprocess.DEVNULL, timeout=60)
        cls.lib = C.CDLL(str(library))
        cls.lib.musashi_boot_ram_span.argtypes = [C.POINTER(Memory), C.c_uint32, C.c_size_t]
        cls.lib.musashi_boot_ram_span.restype = C.c_void_p
        cls.lib.musashi_boot_read32.argtypes = [C.POINTER(Memory), C.c_uint32, C.POINTER(C.c_uint32)]
        cls.lib.musashi_boot_write32.argtypes = [C.POINTER(Memory), C.c_uint32, C.c_uint32]

    def setUp(self):
        self.memory = Memory()

    def halfword_accessors(self):
        read, write = self.lib.musashi_boot_read16, self.lib.musashi_boot_write16
        read.argtypes = [C.POINTER(Memory), C.c_uint32, C.POINTER(C.c_uint16)]
        write.argtypes = [C.POINTER(Memory), C.c_uint32, C.c_uint16]
        return read, write

    def test_halfword_values_share_word_and_byte_views(self):
        read, write = self.halfword_accessors()
        value = C.c_uint16()
        address = 0x8006BAFC  # callback initialization guard
        offset = address - 0x80000000
        self.memory.bytes[offset - 1] = 0xA5
        self.memory.bytes[offset + 2] = 0x5A
        for expected in range(0x10000):
            self.assertEqual(write(self.memory, address, expected), 1)
            self.assertEqual(read(self.memory, address, C.byref(value)), 1)
            self.assertEqual(value.value, expected)
            self.assertEqual(bytes(self.memory.bytes[offset:offset + 2]),
                             expected.to_bytes(2, "little"))
        self.assertEqual(self.memory.bytes[offset - 1], 0xA5)
        self.assertEqual(self.memory.bytes[offset + 2], 0x5A)
        self.assertEqual(self.lib.musashi_boot_write32(self.memory, address, 0xA1B2C3D4), 1)
        self.assertEqual(read(self.memory, address + 2, C.byref(value)), 1)
        self.assertEqual(value.value, 0xA1B2)
        self.assertEqual(write(self.memory, address, 0x5678), 1)
        word = C.c_uint32()
        self.assertEqual(self.lib.musashi_boot_read32(self.memory, address, C.byref(word)), 1)
        self.assertEqual(word.value, 0xA1B25678)

    def test_halfword_boundaries_and_failures_preserve_state(self):
        read, write = self.halfword_accessors()
        value = C.c_uint16(0xABCD)
        for address in (0x80000000, 0x801FFFFE):
            self.assertEqual(write(self.memory, address, 0x1234), 1)
            self.assertEqual(read(self.memory, address, C.byref(value)), 1)
            self.assertEqual(value.value, 0x1234)
        before = bytes(self.memory)
        for address in (0, 0x7FFFFFFE, 0x80000001, 0x8006BAFD,
                        0x801FFFFF, 0x80200000, 0xFFFFFFFF,
                        0x1F801074, 0x9F801074, 0xA0000000):
            value.value = 0xABCD
            self.assertEqual(read(self.memory, address, C.byref(value)), 0)
            self.assertEqual(value.value, 0xABCD)
            self.assertEqual(write(self.memory, address, 0xFFFF), 0)
        self.assertEqual(read(None, 0x80000000, C.byref(value)), 0)
        self.assertEqual(value.value, 0xABCD)
        self.assertEqual(read(self.memory, 0x80000000, None), 0)
        self.assertEqual(write(None, 0x80000000, 0xFFFF), 0)
        self.assertEqual(bytes(self.memory), before)

    def test_matched_search_guest_addresses_and_rejected_spans(self):
        search = self.lib.musashi_boot_search_bytes
        search.argtypes = [C.POINTER(Memory), C.c_uint32, C.c_int32,
                           C.c_int32, C.POINTER(C.c_uint32)]
        self.memory.bytes[RAM_SIZE - 4:] = b"\x80\xFF\x00\xFF"
        before = bytes(self.memory)
        result = C.c_uint32(123)
        for address, target, count, expected in [
            (0x801FFFFC, -1, 4, 0x801FFFFD),
            (0x801FFFFC, 0x180, 4, 0x801FFFFC),
            (0x801FFFFC, 0, 4, 0x801FFFFE),
            (0x801FFFFF, -1, 1, 0x801FFFFF),
            (0x801FFFFC, 0, 2, 0),
            (0, 0, 2147483647, 0),
            (0xFFFFFFFF, 0, -2147483648, 0),
            (0xFFFFFFFF, 0, 0, 0),
        ]:
            self.assertEqual(search(self.memory, address, target, count,
                                    C.byref(result)), 1)
            self.assertEqual(result.value, expected)
        for address, count in [(0x801FFFFF, 2), (0x80200000, 1),
                               (0x1F801814, 1), (0xFFFFFFFF, 1),
                               (0x80000000, 2147483647)]:
            result.value = 123
            self.assertEqual(search(self.memory, address, 0, count,
                                    C.byref(result)), 0)
            self.assertEqual(result.value, 123)
        self.assertEqual(search(None, 0, 0, 0, C.byref(result)), 0)
        self.assertEqual(search(self.memory, 0, 0, 0, None), 0)
        self.assertEqual(bytes(self.memory), before)

    def test_overlapping_retail_record_views_share_bytes(self):
        span = self.lib.musashi_boot_ram_span
        record = span(self.memory, 0x80078D98, 0x4C)
        buffer = span(self.memory, 0x80078DA0, 2)
        self.assertEqual(buffer, record + 8)
        C.memmove(buffer, b"\x12\xFE", 2)
        self.assertEqual(C.string_at(record + 8, 2), b"\x12\xFE")
        self.assertEqual(span(self.memory, 0x80078DE4, 0x4C), record + 0x4C)

    def test_span_boundaries_and_unsupported_regions(self):
        span = self.lib.musashi_boot_ram_span
        self.assertEqual(span(self.memory, 0x80000000, RAM_SIZE), C.addressof(self.memory))
        self.assertEqual(span(self.memory, 0x801FFFFF, 1), C.addressof(self.memory) + RAM_SIZE - 1)
        for address, size in [(0x80000000, 0), (0x801FFFFF, 2),
                              (0x80200000, 1), (0x7FFFFFFF, 2),
                              (0x80000000, C.c_size_t(-1).value),
                              (0xFFFFFFFF, 4), (0x1F801814, 4),
                              (0x9F801814, 4), (0x1F800000, 4),
                              (0xBFC00000, 4), (0, 4), (0xA0000000, 4)]:
            with self.subTest(address=hex(address), size=size):
                self.assertIsNone(span(self.memory, address, size))
        self.assertIsNone(span(None, 0x80000000, 4))

    def test_little_endian_words_and_rejected_accesses_are_atomic(self):
        read, write = self.lib.musashi_boot_read32, self.lib.musashi_boot_write32
        self.assertEqual(write(self.memory, 0x8006CBE8, 0xFEDCBA98), 1)
        self.assertEqual(bytes(self.memory.bytes[0x6CBE8:0x6CBEC]), b"\x98\xBA\xDC\xFE")
        value = C.c_uint32(123)
        self.assertEqual(read(self.memory, 0x8006CBE8, C.byref(value)), 1)
        self.assertEqual(value.value, 0xFEDCBA98)
        before = bytes(self.memory)
        for address in (0x8006CBE9, 0x801FFFFE, 0x1F801814, 0xFFFFFFFF):
            self.assertEqual(write(self.memory, address, 0), 0)
            self.assertEqual(read(self.memory, address, C.byref(value)), 0)
            self.assertEqual(value.value, 0xFEDCBA98)
        self.assertEqual(read(self.memory, 0x80000000, None), 0)
        self.assertEqual(write(None, 0x80000000, 0), 0)
        self.assertEqual(read(None, 0x80000000, C.byref(value)), 0)
        self.assertEqual(bytes(self.memory), before)

    def loader(self):
        fn = self.lib.musashi_boot_load_entry
        fn.argtypes = [C.POINTER(Memory), C.c_void_p, C.c_size_t,
                       C.c_uint32, C.POINTER(EntryState)]
        return fn

    def retail_image(self):
        identity = json.loads((ROOT / "provenance/exe_identity.json").read_text())
        path = ROOT / identity["path"]
        if not path.exists():
            if os.environ.get("MUSASHI_REQUIRE_RETAIL_BOOT_DATA") == "1":
                self.fail("required retail boot-data payload unavailable")
            self.skipTest("registered retail payload unavailable; native boot NOT_RUN")
        image = path.read_bytes()
        self.assertEqual(hashlib.sha256(image).hexdigest(), identity["sha256"])
        self.assertEqual(len(image), identity["file_size_bytes"])
        return image

    def test_required_retail_mode_cannot_silently_skip(self):
        with patch.object(Path, "exists", return_value=False), patch.dict(
            os.environ, {"MUSASHI_REQUIRE_RETAIL_BOOT_DATA": "1"}
        ):
            with self.assertRaisesRegex(AssertionError, "required retail boot-data payload unavailable"):
                self.retail_image()

    def test_matched_exchange_uses_shared_ram_and_preserves_other_bytes(self):
        call = self.lib.musashi_boot_call_80043060
        call.argtypes = [C.POINTER(Memory), C.c_int32, C.POINTER(C.c_int32)]
        other = Memory()
        for memory, original, argument in [(self.memory, 0xFEDCBA98, 0x12345678),
                                            (other, 0x7FFFFFFF, -2147483648),
                                            (self.memory, 0, -1)]:
            self.assertEqual(self.lib.musashi_boot_write32(memory, 0x8006CBE8, original), 1)
            expected = bytearray(bytes(memory))
            struct.pack_into("<I", expected, 0x6CBE8, argument & 0xFFFFFFFF)
            previous = C.c_int32()
            self.assertEqual(call(memory, argument, C.byref(previous)), 1)
            self.assertEqual(previous.value, C.c_int32(original).value)
            self.assertEqual(bytes(memory), expected)
        self.assertEqual(bytes(other.bytes[0x6CBE8:0x6CBEC]), b"\x00\x00\x00\x80")
        before = bytes(self.memory)
        previous = C.c_int32(123)
        self.assertEqual(call(self.memory, 0, None), 0)
        self.assertEqual(call(None, 0, C.byref(previous)), 0)
        self.assertEqual(previous.value, 123)
        self.assertEqual(bytes(self.memory), before)

    def test_prefix_preserves_nonzero_guard_and_stops_before_next_call(self):
        run = self.lib.musashi_boot_run_prefix
        run.argtypes = [C.POINTER(Memory), C.POINTER(PrefixStop)]
        for guard in (0, 1, 0xFFFFFFFF, 0x12345678):
            with self.subTest(guard=hex(guard)):
                C.memset(C.byref(self.memory), 0xA5, RAM_SIZE)
                self.lib.musashi_boot_write32(self.memory, 0x80062998, guard)
                expected = bytearray(bytes(self.memory))
                struct.pack_into("<I", expected, 0x62998, guard or 1)
                struct.pack_into("<I", expected, 0x6CBE8, 0)
                struct.pack_into("<I", expected, 0x74778, 0x3E0)
                stop = PrefixStop()
                self.assertEqual(run(self.memory, C.byref(stop)), 1)  # unsupported, not boot success
                self.assertEqual((stop.call, stop.target), (0x800101C0, 0x800141F0))
                self.assertEqual(bytes(self.memory), expected)
                self.assertEqual(run(self.memory, C.byref(stop)), 1)
                self.assertEqual(bytes(self.memory), expected)
        before = bytes(self.memory)
        stop = PrefixStop(11, 22)
        self.assertEqual(run(None, C.byref(stop)), 0)
        self.assertEqual((stop.call, stop.target), (11, 22))
        self.assertEqual(run(self.memory, None), 0)
        self.assertEqual(bytes(self.memory), before)

    def test_pinned_load_then_prefix_changes_only_three_words(self):
        image = self.retail_image()
        state = EntryState()
        self.assertEqual(self.loader()(self.memory, image, len(image), 0x12345678,
                                       C.byref(state)), 1)
        expected = bytearray(bytes(self.memory))
        for offset, value in ((0x62998, 1), (0x6CBE8, 0), (0x74778, 0x3E0)):
            struct.pack_into("<I", expected, offset, value)
        run = self.lib.musashi_boot_run_prefix
        run.argtypes = [C.POINTER(Memory), C.POINTER(PrefixStop)]
        stop = PrefixStop()
        self.assertEqual(run(self.memory, C.byref(stop)), 1)
        self.assertEqual((stop.call, stop.target), (0x800101C0, 0x800141F0))
        self.assertEqual(bytes(self.memory), expected)

    def test_map_only_load_preserves_ram_outside_exe_without_applying_crt(self):
        image = self.retail_image()
        mapper = self.lib.musashi_boot_map_exe
        mapper.argtypes = [C.POINTER(Memory), C.c_void_p, C.c_size_t]
        C.memset(C.byref(self.memory), 0xA5, RAM_SIZE)
        expected = bytearray(bytes(self.memory))
        expected[0x10000:0x74800] = image[0x800:0x65000]
        self.assertEqual(mapper(self.memory, image, len(image)), 1)
        self.assertEqual(bytes(self.memory), expected)
        # Corruption must reject before even text/data is mapped.
        wrong = bytearray(image)
        wrong[0x800] ^= 1
        self.assertEqual(mapper(self.memory, bytes(wrong), len(wrong)), 0)
        self.assertEqual(bytes(self.memory), expected)

    def test_matched_byte_fill_preserves_bounds_and_truncates_value(self):
        fill = self.lib.musashi_boot_call_8005c29c
        fill.argtypes = [C.POINTER(Memory), C.c_uint32, C.c_int32, C.c_uint32]
        for address, count, value in [(0x80072788, 0x80, 0),
                                       (0x80072798, 0x5C, -1),
                                       (0x800727F4, 0x14, -1),
                                       (0x80078874, 0x100, 0),
                                       (0x800C5510, 0x1800, 0),
                                       (0x801FFFFF, 1, 0x1234),
                                       (0x80000001, 7, -257),
                                       (0x801FFFFF, 0, 0)]:
            with self.subTest(address=hex(address), count=count, value=value):
                C.memset(C.byref(self.memory), 0xA5, RAM_SIZE)
                expected = bytearray(bytes(self.memory))
                offset = address - 0x80000000
                expected[offset:offset + count] = bytes([value & 255]) * count
                self.assertEqual(fill(self.memory, address, value, count), 1)
                self.assertEqual(bytes(self.memory), expected)
        before = bytes(self.memory)
        for address, count in [(0x801FFFFF, 2), (0x80200000, 0),
                               (0x1F801810, 4), (0x80000000, 0xFFFFFFFF)]:
            self.assertEqual(fill(self.memory, address, 0, count), 0)
            self.assertEqual(bytes(self.memory), before)
        self.assertEqual(fill(None, 0x80000000, 0, 1), 0)

    def test_environment_clear_uses_checked_guest_span(self):
        clear = self.lib.musashi_boot_call_80016714
        clear.argtypes = [C.POINTER(Memory), C.c_uint32, C.c_uint32]
        for address, count in ((0x80072788, 0x80),
                               (0x80072789, 0x10),
                               (0x800BA118, 0x98),
                               (0x80000001, 7),
                               (0x801FFFFF, 0)):
            with self.subTest(address=hex(address), count=count):
                C.memset(C.byref(self.memory), 0xA5, RAM_SIZE)
                expected = bytearray(bytes(self.memory))
                offset = address - 0x80000000
                expected[offset:offset + count] = b"\0" * count
                self.assertEqual(clear(self.memory, address, count), 1)
                self.assertEqual(bytes(self.memory), expected)
        before = bytes(self.memory)
        for address, count in ((0x801FFFFF, 2), (0x80200000, 0),
                               (0x1F801810, 4), (0x80000000, 0xFFFFFFFF)):
            self.assertEqual(clear(self.memory, address, count), 0)
            self.assertEqual(bytes(self.memory), before)
        self.assertEqual(clear(None, 0x80000000, 1), 0)

    def test_record_clear_composes_exact_8001903c_boundary(self):
        clear = self.lib.musashi_boot_call_8001903c
        clear.argtypes = [C.POINTER(Memory)]
        C.memset(C.byref(self.memory), 0xA5, RAM_SIZE)
        before = bytearray(bytes(self.memory))
        before[0x747C0:0x747C8] = b"\0" * 8
        self.assertEqual(clear(self.memory), 1)
        self.assertEqual(bytes(self.memory), before)
        before = bytes(self.memory)
        self.assertEqual(clear(None), 0)
        self.assertEqual(bytes(self.memory), before)

    def test_142c8_table_clears_use_checked_guest_spans(self):
        clear_a = self.lib.musashi_boot_call_80029044
        clear_b = self.lib.musashi_boot_call_8002906c
        clear_a.argtypes = [C.POINTER(Memory)]
        clear_b.argtypes = [C.POINTER(Memory)]
        C.memset(C.byref(self.memory), 0xA5, RAM_SIZE)
        expected = bytearray(bytes(self.memory))
        expected[0xAE648:0xAE688] = b"\0" * 0x40
        expected[0xBA1B8:0xBA2B8] = b"\0" * 0x100
        self.assertEqual(clear_a(self.memory), 1)
        self.assertEqual(clear_b(self.memory), 1)
        self.assertEqual(bytes(self.memory), expected)
        before = bytes(self.memory)
        self.assertEqual(clear_a(None), 0)
        self.assertEqual(clear_b(None), 0)
        self.assertEqual(bytes(self.memory), before)

    def test_invalid_image_leaves_all_state_unchanged(self):
        load = self.loader()
        C.memset(C.byref(self.memory), 0xA5, RAM_SIZE)
        state = EntryState(11, 22, 33)
        image = C.create_string_buffer(0x65000)
        before = bytes(self.memory)
        for size in (0, 0x7FF, 0x64FFF, 0x65000, 0x65001, C.c_size_t(-1).value):
            self.assertEqual(load(self.memory, image, size, 0xDEADBEEF, C.byref(state)), 0)
        self.assertEqual(load(None, image, 0x65000, 0, C.byref(state)), 0)
        self.assertEqual(load(self.memory, None, 0x65000, 0, C.byref(state)), 0)
        self.assertEqual(load(self.memory, image, 0x65000, 0, None), 0)
        self.assertEqual(bytes(self.memory), before)
        self.assertEqual((state.stack, state.heap, state.size), (11, 22, 33))

    def test_pinned_entry_effects_match_full_memory_image(self):
        image = self.retail_image()
        # Poison is synthetic pre-entry RAM, not a retail boot observation.
        for incoming_ra in (0x12345678, 0xFEDCBA98):
            C.memset(C.byref(self.memory), 0xA5, RAM_SIZE)
            state = EntryState()
            self.assertEqual(self.loader()(self.memory, image, len(image), incoming_ra,
                                           C.byref(state)), 1)
            expected = bytearray(b"\xA5" * RAM_SIZE)
            expected[0x10000:0x74800] = image[0x800:]
            expected[0x74750:0xC7F08] = bytes(0x537B8)
            struct.pack_into("<I", expected, 0x74750, incoming_ra)
            struct.pack_into("<II", expected, 0x6299C, 0x800C7F08, 0x1374F0)
            self.assertEqual(bytes(self.memory), expected)
            self.assertEqual((state.stack, state.heap, state.size),
                             (0x801FFFF8, 0x800C7F08, 0x1374F0))

    def test_rejects_modified_retail_header_code_data_and_padding(self):
        image = bytearray(self.retail_image())
        state = EntryState(11, 22, 33)
        before = bytes(self.memory)
        for offset in (0, 0x10, 0x18, 0x1C, 0x7FF, 0x800, 0x52DBC, 0x64FFF):
            with self.subTest(offset=hex(offset)):
                changed = image.copy()
                changed[offset] ^= 1
                self.assertEqual(self.loader()(self.memory, bytes(changed), len(changed),
                                               0, C.byref(state)), 0)
                self.assertEqual(bytes(self.memory), before)
                self.assertEqual((state.stack, state.heap, state.size), (11, 22, 33))

    def test_image_may_overlap_destination_storage(self):
        image = self.retail_image()
        C.memset(C.byref(self.memory), 0x5A, RAM_SIZE)
        C.memmove(C.byref(self.memory), image, len(image))
        expected = bytearray(bytes(self.memory))
        expected[0x10000:0x74800] = image[0x800:]
        expected[0x74750:0xC7F08] = bytes(0x537B8)
        struct.pack_into("<I", expected, 0x74750, 0x89ABCDEF)
        struct.pack_into("<II", expected, 0x6299C, 0x800C7F08, 0x1374F0)
        state = EntryState()
        self.assertEqual(self.loader()(self.memory, C.byref(self.memory), len(image),
                                       0x89ABCDEF, C.byref(state)), 1)
        self.assertEqual(bytes(self.memory), expected)
