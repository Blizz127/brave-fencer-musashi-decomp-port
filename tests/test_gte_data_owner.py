"""Focused real shared-GTE data30/data31 owner tests."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / "pc_port" / "gte_owner.c"
PROBE = ROOT / "tests" / "gte_data_owner_probe.c"
VENDOR_INCLUDE = ROOT / "tools" / "third_party" / "psycross" / "include"
INLINE = ROOT / "tools" / "third_party" / "psycross" / "src" / "psx" / "INLINE_C.C"


class GteDataOwnerTests(unittest.TestCase):
    def _build_and_run(self, out: Path, owner_source: Path, sanitizer=False):
        out.mkdir(parents=True, exist_ok=True)
        cc = shutil.which("clang") if sanitizer else shutil.which("cc")
        cxx = shutil.which("clang++") if sanitizer else shutil.which("g++")
        self.assertIsNotNone(cc)
        self.assertIsNotNone(cxx)
        common = ["-I", str(ROOT / "pc_port" / "include"),
                  "-I", str(VENDOR_INCLUDE)]
        flags = ["-Wall", "-Wextra", "-Werror"]
        if sanitizer:
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        owner_obj = out / "gte_owner.o"
        status_obj = out / "cpu_status.o"
        probe_obj = out / "probe.o"
        inline_obj = out / "inline.o"
        exe = out / "gte_data_owner_probe"
        subprocess.run([cc, "-std=c99", "-DMUSASHI_CPU_STATUS_TESTING", *flags,
                        *common, "-c",
                        str(owner_source), "-o", str(owner_obj)],
                       check=True, timeout=30)
        subprocess.run([cc, "-std=c99", "-DMUSASHI_CPU_STATUS_TESTING", *flags,
                        *common, "-c", str(ROOT / "pc_port" / "cpu_status.c"),
                        "-o", str(status_obj)], check=True, timeout=30)
        subprocess.run([cc, "-std=c99", "-DMUSASHI_CPU_STATUS_TESTING", *flags,
                        *common, "-c", str(PROBE),
                        "-o", str(probe_obj)], check=True, timeout=30)
        subprocess.run([cxx, "-std=c++11", *flags, "-x", "c++", *common,
                        "-c", str(INLINE), "-o", str(inline_obj)],
                       check=True, timeout=30)
        subprocess.run([cxx, str(owner_obj), str(status_obj), str(probe_obj),
                        str(inline_obj),
                        *(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                          if sanitizer else []), "-o", str(exe)],
                       check=True, timeout=30)
        result = subprocess.run([str(exe)], text=True, capture_output=True,
                                timeout=60)
        self.assertEqual(result.returncode, 0,
                         result.stdout + result.stderr)
        self.assertIn("GTE_DATA_OWNER_PASS", result.stdout)

    def test_signed_data_owner_against_independent_oracle(self):
        with tempfile.TemporaryDirectory(prefix="musashi-gte-data-") as td:
            self._build_and_run(Path(td), OWNER)

    def test_clang_address_undefined_sanitizers(self):
        with tempfile.TemporaryDirectory(prefix="musashi-gte-data-asan-") as td:
            self._build_and_run(Path(td), OWNER, sanitizer=True)

    def test_meaningful_data_path_mutants_fail(self):
        source = OWNER.read_text()
        sign_mutant = re.sub(
            r"(normalized\s*=\s*[^;]*\?)\s*~value",
            r"\1 value", source, count=1)
        self.assertNotEqual(sign_mutant, source,
                            "owner sign-normalization expression changed")
        bank_mutant = source.replace("gteRegs.CP2D", "gteRegs.CP2C")
        self.assertNotEqual(bank_mutant, source,
                            "owner data-bank access disappeared")
        with tempfile.TemporaryDirectory(prefix="musashi-gte-mutants-") as td:
            root = Path(td)
            for name, text in (("wrong-sign.c", sign_mutant),
                               ("control-bank.c", bank_mutant)):
                mutant = root / name
                mutant.write_text(text)
                with self.subTest(mutant=name):
                    with self.assertRaises(AssertionError):
                        self._build_and_run(root / name.removesuffix(".c"), mutant)


if __name__ == "__main__":
    unittest.main()
