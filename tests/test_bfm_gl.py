"""`gl` renderer backend (pc_port/platform/backends/renderer_gl.c).

Three levels, each skipped when its inputs are missing:
  core    CPU side (bfm_gl_core.c): CLUT decode, batching, PS1 blend formulas
          vs the GL blend state, dither, hor+ routing, HD replacement lookup,
          the reference rasteriser, the render-feedback tile map.
          Always runs, no GL or display needed.
  exec    The GL 3.3 executor on a headless OSMesa (llvmpipe) context,
          compared pixel by pixel with the CPU reference. Needs the Khronos
          GL headers and libOSMesa (both dlopened/compile-time only).
  sdl     The whole backend through SDL2 and the bfm_plat renderer
          interface, on SDL's offscreen driver with Mesa EGL. On a desktop
          with a display the backend opens a real window instead.
Synthetic data only.
"""
import ctypes.util
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PLAT = ROOT / "pc_port/platform"
CFLAGS = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O0", "-g"]
DEPS = Path(os.environ.get("BFM_HOST_DEPS", Path.home() / "opt/host-port-deps/usr"))
DEPS_LIB = DEPS / "lib/x86_64-linux-gnu"
PLATFORM_SOURCES = sorted(str(p) for p in PLAT.glob("bfm_plat*.c")) + [
    str(PLAT / "psyq/bfm_psyq_compat.c"), str(PLAT / "psyq/bfm_psyq_libgs.c"),
    str(PLAT / "psyq/bfm_psyq_libgte.c")]
CORE = [str(PLAT / "backends/gl/bfm_gl_core.c")]
EXEC = [str(PLAT / "backends/gl/bfm_gl_exec.c")]


def gl_include():
    for d in (os.environ.get("BFM_GL_INCLUDE"), DEPS / "include", "/usr/include"):
        if d and (Path(d) / "GL/glcorearb.h").is_file() and (Path(d) / "KHR/khrplatform.h").is_file():
            return str(d)
    return None


def sdl_include():
    for d in (DEPS / "include/SDL2", Path("/usr/include/SDL2")):
        if (d / "SDL.h").is_file():
            return str(d)
    return None


def osmesa_lib():
    env = os.environ.get("BFM_OSMESA")
    if env:
        return env
    for name in ("libOSMesa.so.8", "libOSMesa.so.6", "libOSMesa.so"):
        if (DEPS_LIB / name).is_file():
            return str(DEPS_LIB / name)
    return ctypes.util.find_library("OSMesa")


class GlRendererTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="bfm-gl-")
        cls.addClassCleanup(temp.cleanup)
        cls.tmp = Path(temp.name)

    def build(self, name, extra, sources, libs=()):
        out = self.tmp / name
        subprocess.run(["cc", *CFLAGS, *extra, "-I", str(PLAT), *PLATFORM_SOURCES, *sources,
                        "-o", str(out), *libs, "-ldl", "-lm"],
                       check=True, stdin=subprocess.DEVNULL, timeout=180)
        return out

    def run_bin(self, cmd, env=None):
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=300,
                              env=env, stdin=subprocess.DEVNULL)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        return proc.stdout

    def test_core(self):
        probe = self.build("gl_core", [], CORE + [str(ROOT / "tests/bfm_gl_probe.c")])
        out = self.run_bin([str(probe), "core"])
        self.assertIn("ok core", out)
        # add/subtract modes are bit-exact; the halving modes within one step
        self.assertIn("blend mode 1: 1024/1024 exact", out)
        self.assertIn("blend mode 2: 1024/1024 exact", out)

    def test_exec_osmesa(self):
        inc, lib = gl_include(), osmesa_lib()
        if not inc or not lib:
            self.skipTest("GL headers or libOSMesa not available")
        probe = self.build("gl_exec", ["-DBFM_GL_PROBE_EXEC", "-I", inc],
                           CORE + EXEC + [str(ROOT / "tests/bfm_gl_probe.c")])
        env = dict(os.environ, LD_LIBRARY_PATH=str(DEPS_LIB), LIBGL_ALWAYS_SOFTWARE="1")
        out = self.run_bin([str(probe), "exec", lib], env=env)
        if "skip exec" in out:
            self.skipTest(out.strip())
        self.assertIn("ok exec", out)
        self.assertIn("textures 4/8/15-bit: 3072 texels, 3072 exact", out)
        self.assertIn("gl blend mode 1: vs PS1 992/992 exact", out)
        # mask bit and render feedback, all 16 bits vs the reference rasteriser
        for line in ("gl mask 1x: 6144 px bit-exact", "gl mask 3x: 6144 px bit-exact",
                     "gl feedback 4-bit + CLUT: 1024 px bit-exact",
                     "gl feedback frame buffer: 76800 px bit-exact",
                     "gl whole VRAM 1x: 524288 px bit-exact",
                     "gl whole VRAM 3x: 524288 px bit-exact"):
            self.assertIn(line, out)

    def test_bench_runs(self):
        # tools/gl_bench: builds at -O2 -Werror and runs a tiny frame set
        if not gl_include() or not (DEPS_LIB / "libOSMesa.so.8").exists():
            self.skipTest("GL headers or libOSMesa not available")
        env = dict(os.environ, BFM_HOST_DEPS=str(DEPS), TMPDIR=str(self.tmp))
        out = self.run_bin(["sh", str(ROOT / "tools/gl_bench/run.sh"), "--frames", "2",
                            "--prims", "400", "--scales", "1,2", "--feedback", "--mask",
                            "--size", "320x240"], env=env)
        if out.startswith("skip"):
            self.skipTest(out.strip())
        self.assertIn("ok bench", out)
        self.assertNotIn("FAIL", out)

    def test_headless_boot_script(self):
        # tools/run_headless_boot.sh: the capture shim builds; the script
        # refuses missing inputs and an --out inside the tracked tree
        script = ROOT / "tools/run_headless_boot.sh"
        shim = self.tmp / "cap.so"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", "-shared", "-fPIC",
                        "-I", str(PLAT), str(ROOT / "tools/headless/bfm_swap_capture.c"),
                        str(PLAT / "bfm_plat_image.c"), "-o", str(shim), "-ldl"],
                       check=True, stdin=subprocess.DEVNULL, timeout=60)
        proc = subprocess.run(["sh", str(script), "--binary", str(self.tmp / "nope"),
                               "--disc", str(self.tmp)], capture_output=True, text=True,
                              timeout=60, stdin=subprocess.DEVNULL)
        self.assertEqual(proc.returncode, 2)
        self.assertIn("missing", proc.stderr)
        if not (DEPS_LIB / "libEGL_mesa.so.0").exists():
            return
        disc = self.tmp / "disc"
        (disc / "files").mkdir(parents=True, exist_ok=True)
        for f in ("files/SLUS_007.26", "disc.cue", "disc.bin"):
            (disc / f).write_bytes(b"synthetic")
        fake = self.tmp / "fake_boot"
        fake.write_text("#!/bin/sh\nexit 0\n")
        fake.chmod(0o755)
        inside = ROOT / "pc_port/.headless-guard-test"
        try:
            proc = subprocess.run(["sh", str(script), "--binary", str(fake), "--disc", str(disc),
                                   "--out", str(inside)], capture_output=True, text=True,
                                  timeout=60, stdin=subprocess.DEVNULL,
                                  env=dict(os.environ, BFM_HOST_DEPS=str(DEPS)))
            self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
            self.assertIn("not ignored", proc.stderr)
            self.assertFalse(inside.exists())   # refused before creating anything
        finally:
            if inside.exists():
                shutil.rmtree(inside)
        # a scratch --out runs the fake binary and summarises
        out = self.tmp / "hl"
        proc = subprocess.run(["sh", str(script), "--binary", str(fake), "--disc", str(disc),
                               "--out", str(out), "--timeout", "20"], capture_output=True,
                              text=True, timeout=120, stdin=subprocess.DEVNULL,
                              env=dict(os.environ, BFM_HOST_DEPS=str(DEPS)))
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("exit: 0", proc.stdout)
        for f in ("egl.json", "alsa.conf", "bfm_swap_capture.so", "out.log"):
            self.assertTrue((out / f).exists(), f)

    def test_sdl_backend(self):
        inc, sdl = gl_include(), sdl_include()
        if not inc or not sdl or not (DEPS_LIB / "libSDL2.so").exists():
            self.skipTest("SDL2 / GL headers not available")
        smoke = self.build(
            "gl_sdl", ["-DBFM_PLAT_WITH_GL", "-D_REENTRANT", "-I", inc, "-I", sdl],
            CORE + EXEC + [str(PLAT / "backends/renderer_gl.c"), str(ROOT / "tests/bfm_gl_sdl_smoke.c")],
            ["-L", str(DEPS_LIB), f"-Wl,-rpath-link={DEPS_LIB}", "-Wl,--allow-shlib-undefined", "-lSDL2"])
        env = dict(os.environ, LD_LIBRARY_PATH=str(DEPS_LIB))
        if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
            egl = DEPS_LIB / "libEGL_mesa.so.0"
            if not egl.exists():
                self.skipTest("no display and no Mesa EGL for SDL's offscreen driver")
            vendor = self.tmp / "50_mesa.json"
            vendor.write_text('{"file_format_version":"1.0.0","ICD":{"library_path":"%s"}}' % egl)
            # (LIBGL_ALWAYS_SOFTWARE here crashes Mesa 25.0's EGL inside
            # SDL_CreateWindow; without it Mesa falls back to llvmpipe itself)
            env.update(SDL_VIDEODRIVER="offscreen", __EGL_VENDOR_LIBRARY_FILENAMES=str(vendor),
                       EGL_PLATFORM="surfaceless")
            env.pop("LIBGL_ALWAYS_SOFTWARE", None)
        out = self.run_bin([str(smoke)], env=env)
        if out.startswith("skip"):
            self.skipTest(out.strip())
        self.assertIn("ok gl_sdl", out)


if __name__ == "__main__":
    unittest.main()
