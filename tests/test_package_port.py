"""Data-free packaging: tools/package_port.sh + tools/retail_guard.py.

Everything "retail" here is synthetic random data standing in for the user's
EXE and extracted disc files.
"""
import os
from pathlib import Path
import random
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GUARD = ROOT / "tools/retail_guard.py"
PACKAGE = ROOT / "tools/package_port.sh"


def c_array(data: bytes) -> str:
    return ",".join(str(b) for b in data)


class PackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="bfm-package-")
        cls.addClassCleanup(temp.cleanup)
        cls.tmp = Path(temp.name)
        rng = random.Random(726)
        # Synthetic "retail" EXE: random code-like bytes plus a zero-filled block.
        cls.exe_bytes = rng.randbytes(48 * 1024) + bytes(4096) + rng.randbytes(8 * 1024)
        cls.exe = cls.tmp / "SLUS_007.26"
        cls.exe.write_bytes(cls.exe_bytes)
        cls.extracted = cls.tmp / "extracted"
        (cls.extracted / "DATA").mkdir(parents=True)
        cls.data_file = cls.extracted / "DATA" / "FIELD.DAT"
        cls.data_file.write_bytes(rng.randbytes(20000))
        (cls.extracted / "SYSTEM.CNF").write_bytes(b"BOOT = cdrom:\\SLUS_007.26;1\r\n")

    def build_binary(self, name, embed=b""):
        src = self.tmp / f"{name}.c"
        body = (f"static const unsigned char blob[] = {{{c_array(embed)}}};\n"
                if embed else "static const unsigned char blob[] = {0};\n")
        src.write_text(body + "#include <stdio.h>\nint main(void){printf(\"%d\\n\", (int)blob[0]);return 0;}\n")
        out = self.tmp / name
        subprocess.run(["cc", "-O0", str(src), "-o", str(out)], check=True, timeout=60)
        return out

    def package(self, binary, *extra, refs=True):
        out = self.tmp / f"dist-{binary.name}"
        args = ["sh", str(PACKAGE), "--binary", str(binary), "--out", str(out), "--version", "0.0.1"]
        if refs:
            args += ["--exe", str(self.exe), "--extracted", str(self.extracted)]
        args += list(extra)
        env = dict(os.environ, TMPDIR=str(self.tmp))
        proc = subprocess.run(args, capture_output=True, text=True, timeout=300, env=env)
        return proc, out

    def test_clean_package_contents(self):
        proc, out = self.package(self.build_binary("clean"))
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        tars = list(out.glob("*.tar.gz"))
        self.assertEqual(len(tars), 1)
        with tarfile.open(tars[0]) as t:
            names = t.getnames()
        top = names[0].split("/")[0]
        for want in ("bin/bfm-port", "README.txt", "docs/ARCHITECTURE-PORT.md",
                     "docs/MODDING.md", "licenses/THIRD-PARTY.txt", "licenses/LICENSE-NOTES.md",
                     "mods/README.md", "mods/examples/hello/hello_plugin.c",
                     "mods/examples/lua_hello/main.lua", "mods/examples/cheat_pack/cheats.ini",
                     "include/bfm_plugin.h", "disc/PUT-YOUR-DISC-HERE.txt"):
            self.assertIn(f"{top}/{want}", names)
        for n in names:
            self.assertNotRegex(n, r"(?i)\.(bin|cue|iso|chd|mcd)$|SLUS_|SYSTEM\.CNF")

    def test_binary_embedding_exe_bytes_is_refused(self):
        run = self.exe_bytes[1000:1100]                    # 100 bytes of "retail" code
        proc, out = self.package(self.build_binary("leaky", run))
        self.assertEqual(proc.returncode, 1, proc.stdout)
        self.assertIn("RETAIL CONTENT: bin/bfm-port: 100 bytes", proc.stdout)
        self.assertFalse(out.exists() and list(out.glob("*.tar.gz")))

    def test_short_coincidence_and_padding_pass(self):
        # 40 bytes (below the 64-byte threshold) and long zero runs are allowed.
        proc, _ = self.package(self.build_binary("short", self.exe_bytes[5000:5040] + bytes(3000)))
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def test_extra_file_identical_to_disc_file_is_refused(self):
        copy = self.tmp / "notes.dat"
        copy.write_bytes(self.data_file.read_bytes())
        proc, _ = self.package(self.build_binary("clean2"), "--extra", str(copy))
        self.assertEqual(proc.returncode, 1)
        self.assertIn("notes.dat: identical to retail file", proc.stdout)

    def test_retail_file_names_are_refused(self):
        named = self.tmp / "SLUS_007.26.txt"
        named.write_text("not really")
        cue = self.tmp / "game.cue"
        cue.write_text('FILE "game.bin" BINARY\n')
        proc, _ = self.package(self.build_binary("clean3"), "--extra", str(cue), refs=False)
        self.assertEqual(proc.returncode, 2)               # needs a reference or the flag
        proc, _ = self.package(self.build_binary("clean4"), "--extra", str(cue),
                               "--allow-no-reference", refs=False)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("game.cue: disc image / retail file name", proc.stdout)

    def test_guard_run_threshold_exact(self):
        stage = self.tmp / "stage-threshold"
        stage.mkdir()
        for n, want in ((64, 1), (63, 0)):
            (stage / "f").write_bytes(b"\x01\x02prefix" + self.exe_bytes[20000:20000 + n] + b"suffix!!")
            proc = subprocess.run(["python3", str(GUARD), "--stage", str(stage), "--exe", str(self.exe)],
                                  capture_output=True, text=True, timeout=120)
            self.assertEqual(proc.returncode, want, (n, proc.stdout))


if __name__ == "__main__":
    unittest.main()
