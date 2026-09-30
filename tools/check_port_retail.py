#!/usr/bin/env python3
"""CTest step: the built port carries no retail content.

Stages the shippable build outputs (musashi_native_boot, the port static
libraries, the native-lane archive and table library when built, the X11
helper) and runs tools/retail_guard.py over them against the developer's own
SLUS_007.26 and every extracted MAIN.CD overlay member. Exit 77 (CTest skip)
when no local disc extract is present."""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHIPPED = ("musashi_native_boot", "libmusashi_pc_port.a", "libmusashi_shared.a",
           "libbfm_plat.a", "libmusashi_native_lane.a", "libmusashi_x11_nograb.so",
           "generated/native_lane/libmusashi_native_lane.a")


def main():
    if len(sys.argv) != 2:
        print("usage: check_port_retail.py <build dir>", file=sys.stderr)
        return 2
    build = Path(sys.argv[1])
    exe = ROOT / "extracted/disc/files/SLUS_007.26"
    if not exe.is_file():
        print("retail check: no local disc extract; NOT_RUN", file=sys.stderr)
        return 77
    with tempfile.TemporaryDirectory(prefix="musashi-retail-stage-") as temp:
        stage = Path(temp)
        staged = 0
        for name in SHIPPED:
            path = build / name
            if path.is_file():
                shutil.copy2(path, stage / name.replace("/", "_"))
                staged += 1
        if not staged:
            print("retail check: nothing built to check", file=sys.stderr)
            return 1
        cmd = [sys.executable, str(ROOT / "tools/retail_guard.py"), "--stage", str(stage),
               "--exe", str(exe)]
        for member in sorted((ROOT / "extracted/overlays/main").glob("*.bin")):
            cmd += ["--run-source", str(member)]
        return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
