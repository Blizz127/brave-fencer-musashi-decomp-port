"""bfm_plat_gpu_bridge: GP0/GP1 port protocol -> bfm_plat renderer.

Always: synthetic GP0/GP1 streams through the bridge into a reference
renderer backend (bfm_gl_ref_draw), checked against the PS1 formulas,
under ASan/UBSan. When native_boot's GPU controller source is available
(BFM_GPU_CONTROLLER_DIR = a pc_port directory, default the main checkout
next to this one), the same streams also run through that controller and
the two VRAM images are compared region by region: everything the
controller implements must match (semi-transparent regions once it
blends); its known gaps are reported. Synthetic data only.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PLAT = ROOT / "pc_port/platform"
CFLAGS = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O0", "-g"]
SOURCES = sorted(str(p) for p in PLAT.glob("bfm_plat*.c")) + [
    str(PLAT / "psyq/bfm_psyq_compat.c"), str(PLAT / "psyq/bfm_psyq_libgs.c"),
    str(PLAT / "psyq/bfm_psyq_libgte.c"), str(PLAT / "backends/gl/bfm_gl_core.c"),
    str(ROOT / "tests/bfm_gpu_bridge_probe.c")]


def controller_dir():
    env = os.environ.get("BFM_GPU_CONTROLLER_DIR")
    for d in ([Path(env)] if env else []) + [ROOT.parent / "brave-fencer-musashi-decomp/pc_port"]:
        if (d / "gpu_controller.c").is_file() and (d / "include/musashi_gpu_controller.h").is_file():
            return d
    return None


class GpuBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="bfm-bridge-")
        cls.addClassCleanup(temp.cleanup)
        cls.tmp = Path(temp.name)

    def build_run(self, name, extra, sources):
        out = self.tmp / name
        subprocess.run(["cc", *CFLAGS, *extra, "-I", str(PLAT), *sources, "-o", str(out),
                        "-ldl", "-lm"], check=True, stdin=subprocess.DEVNULL, timeout=180)
        proc = subprocess.run([str(out)], capture_output=True, text=True, timeout=120,
                              stdin=subprocess.DEVNULL,
                              env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("ok bridge", proc.stdout)
        return proc.stdout

    def test_bridge_protocol_and_pixels(self):
        out = self.build_run("bridge", ["-fsanitize=address,undefined",
                                        "-fno-sanitize-recover=all"], SOURCES)
        self.assertIn("15 prims", out)   # incl. the read-back tile and the gouraud polyline

    def test_bridge_matches_controller(self):
        d = controller_dir()
        if not d:
            self.skipTest("native_boot gpu_controller.c not available (BFM_GPU_CONTROLLER_DIR)")
        out = self.build_run("bridge_ctl", ["-DBFM_BRIDGE_WITH_CONTROLLER", "-I", str(d / "include")],
                             SOURCES + [str(d / "gpu_controller.c")])
        self.assertIn("fade quad 0x2A (abr 2)", out)
        self.assertNotIn(" DIFFER", out)


if __name__ == "__main__":
    unittest.main()
