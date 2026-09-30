"""No Sony PsyQ code in the port (config/psyq_ranges.txt, tools/psyq_manifest.py)."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import psyq_manifest  # noqa: E402


class PsyqExclusionTest(unittest.TestCase):
    def test_manifest_covers_the_libraries_and_not_game_code(self):
        table = psyq_manifest.ranges()
        self.assertEqual(psyq_manifest.psyq_segment(0x800425B0, table), "libetc")
        self.assertEqual(psyq_manifest.psyq_segment(0x8005C388, table), "libc2_1")
        self.assertEqual(psyq_manifest.psyq_segment(0x8003A444, table), "snd1")
        # Game code, including the sgap_* islands between sound blocks.
        for vram in (0x80010000, 0x8001903C, 0x8003C438, 0x8003FA54, 0x80057928):
            self.assertIsNone(psyq_manifest.psyq_segment(vram, table), hex(vram))
        for lo, hi, _ in table:
            self.assertLess(lo, hi)

    def check(self, *sources):
        return subprocess.run(
            [sys.executable, str(ROOT / "tools/psyq_manifest.py"), "check",
             "--allow", str(ROOT / "config/psyq_pending_replacement.txt"), *sources],
            cwd=ROOT, capture_output=True, text=True)

    def test_check_refuses_new_psyq_sources(self):
        run = self.check("src/main/8004239c.c")
        self.assertEqual(run.returncode, 1)
        self.assertIn("FORBIDDEN src/main/8004239c.c", run.stderr)

    def test_check_accepts_game_code_and_pending_bindings_only(self):
        self.assertEqual(self.check("src/main/8001903c.c").returncode, 0)
        # Replaced bindings never come back.
        for replaced in ("800425b0", "80042c64", "8005c388", "800427f4", "80042e08", "8005bd7c", "8005c1c0", "800426fc", "800616d0", "8006291c", "80062988"):
            self.assertEqual(self.check(f"src/main/{replaced}.c").returncode, 1, replaced)

    def test_no_psyq_binding_is_pending(self):
        pending = [l for l in (ROOT / "config/psyq_pending_replacement.txt").read_text().splitlines()
                   if l.strip() and not l.startswith("#")]
        self.assertEqual(pending, [])

    def test_native_lane_never_takes_psyq_candidates(self):
        import native_lane_gen
        excluded = []
        for m in native_lane_gen.candidates(excluded=excluded):
            self.assertIsNone(psyq_manifest.psyq_segment(m["vram"]), m["name"])
        self.assertIn(0x800425B0, excluded)


if __name__ == "__main__":
    unittest.main()
