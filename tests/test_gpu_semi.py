"""PS1 semi-transparency in the GPU controller (tests/gpu_semi_probe.c)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GpuSemiTransparencyTests(unittest.TestCase):
    def test_all_modes_and_stp_gate(self):
        with tempfile.TemporaryDirectory(prefix="musashi-gpu-semi-") as temp:
            probe = Path(temp) / "probe"
            subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "pc_port/include"),
                            str(ROOT / "tests/gpu_semi_probe.c"),
                            str(ROOT / "pc_port/gpu_controller.c"), "-o", str(probe)],
                           check=True, timeout=120)
            run = subprocess.run([str(probe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("GPU_SEMI_PASS", run.stdout)


if __name__ == "__main__":
    unittest.main()
