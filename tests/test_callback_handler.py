"""Exercise the matched handler independently of the guest callback boundary."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CallbackHandlerTests(unittest.TestCase):
    def test_live_slot_reads_counter_wrap_and_eight_slot_limit(self):
        with tempfile.TemporaryDirectory(prefix="musashi-handler-") as directory:
            probe = Path(directory) / "probe"
            subprocess.run([
                "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "include"),
                str(ROOT / "tests/callback_handler_probe.c"), "-o", str(probe),
            ], check=True, stdin=subprocess.DEVNULL, timeout=60)
            subprocess.run([str(probe)], check=True,
                           stdin=subprocess.DEVNULL, timeout=10)
