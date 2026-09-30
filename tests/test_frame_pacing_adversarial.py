import os
import re
import subprocess
import threading
import time
from pathlib import Path
import pytest

ROOT = Path(__file__).resolve().parents[1]

def test_spiral_climb_continuous_frame_pacing():
    boot_bin = ROOT / "build/musashi_native_boot"
    assert boot_bin.exists(), f"binary not found: {boot_bin}"

    cmd = [
        str(boot_bin),
        "--headless",
        "--frames", "120",
        "--action", "spiral_climb"
    ]

    env = os.environ.copy()
    env["MUSASHI_GUEST_CLOCK"] = "1"
    env["MUSASHI_UNTHROTTLED"] = "1"

    frame_timestamps = {}
    actions = []
    refusals = []
    terrain_positions = {}
    all_stderr_lines = []

    start_wall = time.perf_counter()
    proc = subprocess.Popen(
        cmd,
        cwd=str(ROOT),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )

    pattern_climb = re.compile(r"native_boot:\s+TERRAIN_CLIMB\s+frame=(\d+)\s+pos=\(([-\d]+),([-\d]+),([-\d]+)\)\s+vy=([-\d]+)")
    pattern_action = re.compile(r"native_boot:\s+MUSASHI_ACTION\s+prev=(\d+)\s+curr=(\d+)\s+anim=(\d+)\s+hp=(\d+)\s+frame=(\d+)")
    pattern_refusal = re.compile(r"STEP_REFUSED|ACCESS_REFUSED|OVERLAY_WORD_MISMATCH")

    def read_stderr():
        for line in proc.stderr:
            t = time.perf_counter()
            all_stderr_lines.append(line)
            if pattern_refusal.search(line):
                refusals.append(line.strip())

            m_climb = pattern_climb.search(line)
            if m_climb:
                f_num = int(m_climb.group(1))
                x, y, z = int(m_climb.group(2)), int(m_climb.group(3)), int(m_climb.group(4))
                vy = int(m_climb.group(5))
                frame_timestamps[f_num] = t
                terrain_positions[f_num] = (x, y, z, vy)

            m_act = pattern_action.search(line)
            if m_act:
                actions.append((int(m_act.group(5)), int(m_act.group(1)), int(m_act.group(2))))

    reader_thread = threading.Thread(target=read_stderr, daemon=True)
    reader_thread.start()

    timeout_seconds = 30
    try:
        proc.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        reader_thread.join(timeout=2.0)
        pytest.fail(
            f"musashi_native_boot timed out after {timeout_seconds}s. "
            f"Captured frames: {len(frame_timestamps)}/120. "
            f"Last 10 stderr lines:\n" + "".join(all_stderr_lines[-10:])
        )

    reader_thread.join(timeout=5.0)
    total_duration = time.perf_counter() - start_wall

    # Verify exit status
    # 77 indicates PsyCross display unavailable in headless test environment
    if proc.returncode == 77:
        pytest.skip("PsyCross display unavailable; startup not observed (exit 77)")
    # 143 is 128 + SIGTERM (clean termination on reaching target frames)
    assert proc.returncode in (0, 143), f"Unexpected returncode {proc.returncode}"

    # Confirm zero refusals
    assert len(refusals) == 0, f"Observed CPU execution refusals: {refusals}"

    # Verify 120 frames reached
    assert len(frame_timestamps) == 120, f"Expected 120 frames, got {len(frame_timestamps)}"

    # Opening stair off the spawn landing. Sky is -Y, and this heading steps
    # down that stair: floor height moves toward zero while vertical speed stays 0.
    y_values = [terrain_positions[f][1] for f in sorted(terrain_positions.keys())]
    assert y_values[0] in (-1057, -1056), f"Starting Y was {y_values[0]}"
    assert y_values[-1] > y_values[0], f"Expected the opening stair to step down, Y {y_values[0]} -> {y_values[-1]}"
    for i in range(1, len(y_values)):
        assert y_values[i] >= y_values[i - 1], f"Left the stair at frame {i+1}: {y_values[i]} < {y_values[i-1]}"

    # Verify ground clamping: vy must be 0 at all times
    for f, pos in terrain_positions.items():
        assert pos[3] == 0, f"Frame {f} had non-zero vy: {pos[3]}"

    # Frame delta analysis
    deltas = []
    frame_keys = sorted(frame_timestamps.keys())
    for i in range(1, len(frame_keys)):
        d = frame_timestamps[frame_keys[i]] - frame_timestamps[frame_keys[i - 1]]
        deltas.append(d)

    mean_delta = sum(deltas) / len(deltas)
    variance = sum((d - mean_delta) ** 2 for d in deltas) / len(deltas)
    std_dev = variance ** 0.5
    min_delta = min(deltas)
    max_delta = max(deltas)

    print(f"\n--- Frame Pacing Adversarial Metrics ---")
    print(f"Total Traversal Frames: {len(frame_timestamps)}")
    print(f"Total Duration: {total_duration:.3f} s")
    print(f"Mean Inter-Frame Delta: {mean_delta*1000:.2f} ms")
    print(f"Min / Max Inter-Frame Delta: {min_delta*1000:.2f} ms / {max_delta*1000:.2f} ms")
    print(f"Standard Deviation: {std_dev*1000:.2f} ms")
    print(f"Zero Stalls (>200ms in-game): {all(d < 0.200 for d in deltas)}")
    print(f"Opening stair: {y_values[0]} -> {y_values[-1]} (clamped vy=0)")

    # Assert no extreme stalls (>200ms) during active in-game climb
    assert max_delta < 0.200, f"Frame stall detected: max_delta = {max_delta*1000:.2f} ms"

if __name__ == "__main__":
    test_spiral_climb_continuous_frame_pacing()
