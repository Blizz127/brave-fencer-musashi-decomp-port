"""Isolated console semantics; no BIOS or native boot acceptance implied."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ConsoleTests(unittest.TestCase):
    def test_byte_classification_tab_expansion_and_newline_order(self):
        with tempfile.TemporaryDirectory(prefix="musashi-console-") as directory:
            probe = Path(directory) / "probe"
            subprocess.run([
                "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "include"),
                str(ROOT / "tests/console_probe.c"), "-o", str(probe),
            ], check=True, stdin=subprocess.DEVNULL, timeout=60)
            subprocess.run([str(probe)], check=True,
                           stdin=subprocess.DEVNULL, timeout=10)
