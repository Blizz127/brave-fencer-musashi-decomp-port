"""Focused real PsyCross gteRegs/CTC2 owner tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
VENDOR_INCLUDE = ROOT / "tools/third_party/psycross/include"
INLINE = ROOT / "tools/third_party/psycross/src/psx/INLINE_C.C"


class GteOwnerTests(unittest.TestCase):
    def test_shared_bank_transfers_and_leases(self):
        with tempfile.TemporaryDirectory(prefix="musashi-gte-owner-") as td:
            out = Path(td)
            owner_obj = out / "gte_owner.o"
            probe_obj = out / "probe.o"
            inline_obj = out / "inline.o"
            exe = out / "gte_owner_probe"
            common = ["-I", str(ROOT / "pc_port/include"),
                      "-I", str(VENDOR_INCLUDE)]
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                            *common, "-c", str(ROOT / "pc_port/gte_owner.c"),
                            "-o", str(owner_obj)], check=True, timeout=30)
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                            *common, "-c", str(ROOT / "tests/gte_owner_probe.c"),
                            "-o", str(probe_obj)], check=True, timeout=30)
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            "-x", "c++", *common, "-c", str(INLINE),
                            "-o", str(inline_obj)], check=True, timeout=30)
            subprocess.run(["g++", str(owner_obj), str(probe_obj), str(inline_obj),
                            "-o", str(exe)], check=True, timeout=30)
            result = subprocess.run([str(exe)], text=True, capture_output=True,
                                    timeout=30)
            self.assertEqual(result.returncode, 0,
                             result.stdout + result.stderr)
            self.assertIn("GTE_OWNER_PASS", result.stdout)

