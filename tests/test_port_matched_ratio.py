"""tools/port_matched_ratio.py: classification and shares on a synthetic run.

A made-up registry, PsyQ table, lane report and trace (no game data) with
one function in each class. Checks the exact counts, the shares, the idle
exclusion and the shadow list.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/port_matched_ratio.py"

REGISTRY = {"matches": [
    {"name": "lane_fn", "vram": 0x80020000, "size": 16, "region": "main", "recovery": "c"},
    {"name": "shim_fn", "vram": 0x80020010, "size": 32, "region": "main", "recovery": "c"},
    {"name": "stub_fn", "vram": 0x80020030, "size": 8, "region": "main", "recovery": "c"},
    {"name": "asm_fn", "vram": 0x80020038, "size": 8, "region": "main", "recovery": "assembly"},
    {"name": "skip_fn", "vram": 0x80020040, "size": 8, "region": "main", "recovery": "c"},
    {"name": "idle_fn", "vram": 0x80020048, "size": 8, "region": "main", "recovery": "c"},
    {"name": "ovl_fn", "vram": 0x80180000, "size": 8, "region": "sc01_0000", "recovery": "c"},
    {"name": "ovl_other", "vram": 0x80180000, "size": 8, "region": "sc09_0001", "recovery": "assembly"},
    {"name": "ovl_other2", "vram": 0x80180100, "size": 8, "region": "sc09_0001", "recovery": "assembly"},
]}
PSYQ = """BFM_PSYQ(0x80020010, memcpy, BFM_PSYQ_HLE, 3, 1, w_memcpy)
BFM_PSYQ(0x80020030, SsEnd, BFM_PSYQ_STUB, 0, 0, bfm_psyq_stub)
BFM_PSYQ(0x80020050, VSync, BFM_PSYQ_PSYCROSS, 1, 1, w_vsync)
"""
REPORT = {"psyq_hooked": [], "functions": [
    {"vram": 0x80020000, "admitted": True, "rule": None},
    {"vram": 0x80020040, "admitted": False, "rule": "hooks"},
    {"vram": 0x80020048, "admitted": False, "rule": "irq"},
]}
# target calls kind caller lane
COUNTS = """# target count first_kind first_caller lane
80020000 10 j 80020048 interp
80020010 5 j 80020048 interp
80020030 2 j 80020048 interp
80020038 1 j 80020048 interp
80020040 4 j 80020048 interp
80020048 1 d 00000000 interp
80180000 3 j 80020048 interp
000000a0 7 j 80020048 interp
"""
# steps per PC: lane 10x4, shim 5x8, stub 2x2, asm 1x2, skip 0 (host), idle
# 1000, overlay 3x2, one unattributed PC
INSNS = """# pc interpreted_steps
80020000 20
80020004 20
80020010 40
80020014 40
80020030 4
80020038 2
80020048 1000
8002004c 0
80180000 6
80030000 8
"""


class PortMatchedRatio(unittest.TestCase):
    def run_tool(self, d, *extra):
        out = Path(d) / "r.json"
        p = subprocess.run([sys.executable, str(TOOL), "--trace-counts", str(Path(d) / "c"),
                            "--lane-report", str(Path(d) / "rep.json"),
                            "--registry", str(Path(d) / "reg.json"), "--psyq-def", str(Path(d) / "p.def"),
                            "--formatter", str(Path(d) / "none.c"), "--idle", "80020048",
                            "--config-dir", str(Path(d) / "noconfig"),
                            "--json", str(out), *extra], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        return json.loads(out.read_text()), p.stderr

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        (d / "reg.json").write_text(json.dumps(REGISTRY))
        (d / "p.def").write_text(PSYQ)
        (d / "rep.json").write_text(json.dumps(REPORT))
        (d / "c").write_text(COUNTS)
        (d / "i").write_text(INSNS)

    def tearDown(self):
        self.tmp.cleanup()

    def test_exact_classes(self):
        r, summary = self.run_tool(self.tmp.name, "--trace-insns", str(Path(self.tmp.name) / "i"))
        c = r["classes"]
        self.assertEqual(c["matched_c"]["functions"], 1)
        self.assertEqual(c["matched_c"]["instructions"], 40)
        # memcpy (HLE), BIOS A0, host-skipped 80020040
        self.assertEqual(c["port_shim"]["functions"], 3)
        self.assertEqual(set(c["port_shim"]["by_reason"]), {"psyq_hle", "bios_hle", "host_skipped"})
        self.assertEqual(c["port_shim"]["instructions"], 80)
        reasons = c["interpreted"]["by_reason"]
        self.assertEqual(reasons["matched_c_unused"]["functions"], 3)  # stub, idle, overlay
        self.assertEqual(reasons["retail_asm"]["functions"], 1)
        self.assertEqual(r["totals"]["instructions"], 1140)
        self.assertEqual(r["totals"]["unattributed_instructions"], 8)
        self.assertEqual(r["totals"]["idle_instructions"], 1000)
        self.assertAlmostEqual(c["matched_c"]["instructions_share_excluding_idle"], round(40 / 140, 4))
        att = r["overlay_attribution"]
        self.assertIn("INFERRED", att["method"])
        # both members have a function at 80180000; the traced entry ties, so
        # the member with more entry hits... is equal here: attribution must
        # still pick exactly one member for the 6 overlay steps
        self.assertEqual(sum(att["instructions_by_member"].values()), 6)
        self.assertEqual([s["pc"] for s in r["shadowed_executed"]], ["80020010", "80020040"])
        self.assertEqual(r["shadowed_static"], ["80020010 memcpy"])
        dom = r["by_domain"]
        self.assertEqual(dom["game"]["matched_c"]["functions"], 1)
        self.assertEqual(dom["psyq"]["port_shim"]["functions"], 1)   # memcpy
        self.assertEqual(dom["bios"]["port_shim"]["functions"], 1)   # A0
        self.assertIn("exact", r["instructions_basis"])
        self.assertIn("matched C", summary)

    def test_estimate_is_labelled(self):
        r, summary = self.run_tool(self.tmp.name)
        self.assertIn("ESTIMATE", r["instructions_basis"])
        self.assertIn("ESTIMATED", summary)
        self.assertEqual(r["classes"]["matched_c"]["instructions"], 10 * 16 // 4)


class ResidentMember(unittest.TestCase):
    def test_entered_function_wins_over_hit_count(self):
        sys.path.insert(0, str(ROOT / "tools"))
        import port_matched_ratio as pmr
        matches = [
            {"name": "a_fn", "vram": 0x800D0000, "size": 0x100, "region": "main_0003", "recovery": "c"},
            {"name": "b_big", "vram": 0x800CFF00, "size": 0x400, "region": "main_0010", "recovery": "c"},
            {"name": "b_1", "vram": 0x800D1000, "size": 8, "region": "main_0010", "recovery": "c"},
            {"name": "b_2", "vram": 0x800D1010, "size": 8, "region": "main_0010", "recovery": "c"},
        ]
        traced = {0x800D0000, 0x800D1000, 0x800D1010}
        reg = pmr.Registry(matches, traced)
        self.assertEqual(reg.hits, {"main_0003": 1, "main_0010": 2})
        self.assertEqual(reg.find(0x800D0010)["name"], "a_fn")    # entered, fewer hits
        self.assertEqual(reg.find(0x800D0200)["name"], "b_big")   # only b covers it
        self.assertIsNone(reg.find(0x800E0000))


class MemberMap(unittest.TestCase):
    def test_asm_only_routine_attributed_to_its_member(self):
        sys.path.insert(0, str(ROOT / "tools"))
        import port_matched_ratio as pmr
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "overlay_main_0003.yaml").write_text(
                "segments:\n  - name: main_0003\n    type: code\n    start: 0x0\n    vram: 0x800D0000\n"
                "    subsegments:\n      - [0x0, asm, main_0003]\n      - [0x100, data, w]\n"
                "      - [0x104, asm, main_0003_mdec]\n      - [0x200, data, tail]\n  - [0x300]\n")
            (Path(d) / "symbol_addrs.main_0003.auto.txt").write_text(
                "func_800D0000 = 0x800D0000; // type:func\nfunc_800D0104 = 0x800D0104; // type:func\n")
            members = pmr.load_members(d)
        self.assertEqual(members["main_0003"][0], [(0x800D0000, 0x800D0100), (0x800D0104, 0x800D0200)])
        matches = [
            {"name": "c_fn", "vram": 0x800D0000, "size": 0x100, "region": "main_0003", "recovery": "c"},
            {"name": "other", "vram": 0x800D0100, "size": 0x200, "region": "main_0010", "recovery": "c"},
        ]
        reg = pmr.Registry(matches, {0x800D0000, 0x800D0104, 0x800D0100}, (), members)
        f = reg.find(0x800D0150)   # inside the asm-only routine, entered
        self.assertEqual((f["region"], f["vram"], f.get("unregistered")), ("main_0003", 0x800D0104, True))
        self.assertEqual(reg.find(0x800D0010)["name"], "c_fn")
        self.assertIsNone(reg.find(0x800D0100 - 0x1000))


class Residency(unittest.TestCase):
    def test_digest_names_the_member_exactly(self):
        sys.path.insert(0, str(ROOT / "tools"))
        import port_matched_ratio as pmr
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "config").mkdir()
            (root / "extracted").mkdir()
            for name, fill in (("main_0003", 0x11), ("main_0010", 0x22)):
                (root / "config" / f"overlay_{name}.yaml").write_text(
                    f"  target_path: extracted/{name}.bin\nsegments:\n  - name: {name}\n    type: code\n"
                    "    start: 0x0\n    vram: 0x800D0000\n    subsegments:\n      - [0x0, asm, x]\n  - [0x400]\n")
                (root / "extracted" / f"{name}.bin").write_bytes(bytes([fill]) * 0x400)
            members = pmr.load_members(root / "config")
            digest = pmr.fnv1a64(bytes([0x22]) * 32)
            (root / "res").write_text("# h\n800d0100 %016x 7\n800d0200 %016x 1\n" % (digest, 12345))
            res, unmatched = pmr.resolve_resident(root / "res", members, root)
        self.assertEqual(dict(res[0x800D0100]), {"main_0010": 7})
        self.assertEqual(unmatched, 1)


if __name__ == "__main__":
    unittest.main()
