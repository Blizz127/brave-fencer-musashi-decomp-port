"""Exercise the matched search body; this does not prove native boot."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BoundedSearchTests(unittest.TestCase):
    def test_byte_targets_counts_and_returned_address(self):
        with tempfile.TemporaryDirectory(prefix="musashi-search-") as directory:
            library = Path(directory) / "search.so"
            subprocess.run([
                "cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "include"),
                str(ROOT / "src/main/8005ccc8.c"), "-o", str(library),
            ], check=True, stdin=subprocess.DEVNULL, timeout=60)
            module = ctypes.CDLL(str(library))
            search = module.func_8005CCC8
            search.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
            search.restype = ctypes.c_void_p
            values = bytes(range(256)) + bytes(range(255, -1, -1))
            storage = (ctypes.c_ubyte * len(values)).from_buffer_copy(values)
            base = ctypes.addressof(storage)
            for count in (-2147483648, -1, 0, 1, 512, 2147483647):
                self.assertIsNone(search(None, 0, count))
            for count in (-2147483648, -1, 0, 1, 2, 127, 255, 256, 257, 512):
                for target in range(-256, 512):
                    offset = values[:max(count, 0)].find(bytes([target & 255]))
                    expected = None if offset < 0 else base + offset
                    self.assertEqual(search(base, target, count), expected,
                                     (target, count))
