"""bfm_plat audio sinks (MUSASHI_AUDIO=bfm_plat): the null backend drains at
the platform clock's audio rate, so a queue-paced SPU never faults on it;
the OpenAL backend (when the OpenAL headers and library are present) plays
through OpenAL Soft's null driver and drains in real time; and the whole
SPU core -> plat_audio_sink -> bfm_plat path turns a CD tone into non-silent
frames at the sink while the BIOS mute stays silent."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLAT = ROOT / "pc_port/platform"
CFLAGS = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-O0", "-g"]


def sources():
    return (sorted(str(p) for p in PLAT.glob("bfm_plat*.c")) +
            [str(PLAT / "psyq" / n) for n in
             ("bfm_psyq_compat.c", "bfm_psyq_libgs.c", "bfm_psyq_libgte.c")])


@unittest.skipUnless(shutil.which("cc"), "cc unavailable")
class AudioSinkTests(unittest.TestCase):
    def build(self, out, *extra):
        subprocess.run(["cc", *CFLAGS, "-D_POSIX_C_SOURCE=199309L", "-I", str(PLAT), *sources(),
                        str(ROOT / "tests/bfm_plat_audio_probe.c"), *extra,
                        "-o", str(out), "-ldl", "-lm"],
                       check=True, stdin=subprocess.DEVNULL, timeout=180)

    def test_null_sink_drains_at_the_audio_rate(self):
        with tempfile.TemporaryDirectory(prefix="bfm-audio-") as d:
            probe = Path(d) / "probe"
            self.build(probe)
            run = subprocess.run([str(probe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("OK null", run.stdout)

    @unittest.skipUnless(Path("/usr/include/AL/al.h").is_file(), "OpenAL headers unavailable")
    def test_openal_sink_plays_and_drains(self):
        with tempfile.TemporaryDirectory(prefix="bfm-audio-") as d:
            probe = Path(d) / "probe"
            self.build(probe, "-DPROBE_OPENAL", str(PLAT / "backends/audio_openal.c"), "-lopenal")
            env = dict(os.environ, ALSOFT_DRIVERS="null")
            run = subprocess.run([str(probe)], capture_output=True, text=True, timeout=60, env=env)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("OK null", run.stdout)
            self.assertTrue("OK openal" in run.stdout or "SKIP openal" in run.stdout, run.stdout)


class SpuToSinkTests(unittest.TestCase):
    """The native_boot MUSASHI_AUDIO=bfm_plat path without the game."""

    def build(self, out, *extra):
        subprocess.run(["cc", *CFLAGS, "-I", str(PLAT), "-I", str(ROOT / "pc_port/include"),
                        "-I", str(ROOT / "include"), *sources(),
                        str(ROOT / "pc_port/spu_cd_audio.c"), str(ROOT / "pc_port/plat_audio_sink.c"),
                        str(ROOT / "tests/plat_audio_sink_probe.c"), *extra,
                        "-o", str(out), "-ldl", "-lm"],
                       check=True, stdin=subprocess.DEVNULL, timeout=180)

    @unittest.skipUnless(shutil.which("cc"), "cc unavailable")
    def test_cd_tone_reaches_the_null_sink(self):
        with tempfile.TemporaryDirectory(prefix="bfm-audio-") as d:
            probe = Path(d) / "probe"
            self.build(probe)
            run = subprocess.run([str(probe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("OK null frames=5145 nonsilent=4410", run.stdout)
            self.assertIn("OK adpcm frames=2940", run.stdout)

    @unittest.skipUnless(shutil.which("cc") and Path("/usr/include/AL/al.h").is_file(),
                         "OpenAL headers unavailable")
    def test_cd_tone_reaches_openal(self):
        with tempfile.TemporaryDirectory(prefix="bfm-audio-") as d:
            probe = Path(d) / "probe"
            self.build(probe, "-DPROBE_OPENAL", str(PLAT / "backends/audio_openal.c"), "-lopenal")
            env = dict(os.environ, ALSOFT_DRIVERS="null")
            run = subprocess.run([str(probe), "openal"], capture_output=True, text=True,
                                 timeout=60, env=env)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertTrue("OK openal" in run.stdout or "SKIP openal" in run.stdout, run.stdout)


if __name__ == "__main__":
    unittest.main()
