#!/usr/bin/env python3
"""Check a partial native startup and its refusal gate; never certify a menu."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile


def main():
    binary, exe = (Path(arg).resolve() for arg in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="musashi-native-startup-") as temp:
        cwd = Path(temp)
        wrong = cwd / "wrong-exe"
        wrong.write_bytes(b"PS-X EXE" + bytes(32))
        rejected = subprocess.run([str(binary), str(wrong)], cwd=cwd,
                                  stdin=subprocess.DEVNULL, capture_output=True,
                                  text=True, timeout=10)
        if (rejected.returncode != 2 or "pinned retail EXE rejected before startup"
                not in rejected.stderr or "Initialising Psy-X" in rejected.stdout):
            print("native startup: bad input was not rejected before display", file=sys.stderr)
            return 1
        xvfb = shutil.which("xvfb-run")
        if not xvfb:
            print("native startup: no isolated display; NOT_RUN", file=sys.stderr)
            return 77
        env = os.environ.copy()
        # Keep the graphics driver independent of the operator's Wayland session.
        env["SDL_VIDEODRIVER"] = "x11"
        env["XDG_RUNTIME_DIR"] = str(cwd)
        env["MUSASHI_NATIVE_GPU_TRACE"] = "1"
        # Guest-cycle device time: host-paced time refuses under load
        # (IRQ source-overflow) and makes the stop state depend on host speed.
        env["MUSASHI_GUEST_CLOCK"] = "1"
        run = subprocess.run([xvfb, "-a", str(binary), str(exe)], cwd=cwd,
                             env=env, stdin=subprocess.DEVNULL, capture_output=True,
                             text=True, timeout=30)
        output = run.stdout + run.stderr
        # The installed xvfb-run merges the child's stderr into stdout.
        fields = dict(re.findall(r"(\w+)=([\w]+)", output))
        expected = dict(status="0", pc="80044dbc", target="80044dbc",
                        IRQ_installed="1", I_MASK="000d",
                        DPCR="33333b33", DICR="00000000",
                        custom_vblank="1", guard="0001",
                        enabled="000d", slot0="80042ce8", slot3="80042e08",
                        bios_cd_installed="0", bios_event_used="8", guest_irq_enabled="1",
                        irq_faulted="0", scheduler_removed="1", startup="PARTIAL",
                        incoming_ra="NATIVE_ZERO", menu="NOT_REACHED",
                        visual_check="REQUIRED",
                        GPUREAD="00000400", DMA2_CHCR="00000401",
                        graphics_init="RETURNED", blank_presented="1", gpu_removed="1",
                        card_initialized="1", card_active="0", card_pad_started="0",
                        card_hook_installed="1", card_removed="1", card_started="1",
                        card_sio_installed="1", card_delay_calls="3",
                        SIO_MODE="000d", SIO_BAUD="0088", SIO_CONTROL="0000",
                        memory_cards="DISCONNECTED", sio_tx_bytes="2", bu_stage="3",
                        bu_directory_zero="960", bu_broken_ff="40", bu_status_zero="5",
                        card_flags="1111", card_action="0", card_last_port="1",
                        card_operation_step="0", card_irq_installed="0", bu_removed="1",
                        bu_return_port="1", bu_return_last_port="1", bu_return_flags="1111",
                        input_removed="1", frame_busy="0", frame_faulted="0")
        # Ordered retail observation through reset, then following blanking.
        writes = re.findall(r"GPU_TRACE address=(\w+) width=(\d+) value=(\w+)", output)
        expected_writes = [
            ("1f801810", "4", "00072740"), ("1f801074", "2", "00000000"),
            ("1f8010a8", "4", "00000401"), ("1f8010f0", "4", "33333b33"),
            ("1f801814", "4", "00000000"), ("1f801074", "2", "00000009"),
            ("1f801814", "4", "10000007"), ("1f801810", "4", "e1003000"),
            ("1f801814", "4", "03000001"),
        ]
        card_writes = re.findall(r"CARD_TRACE address=(\w+) width=(\d+) value=(\w+)", output)
        expected_card_writes = [
            ("1f80104a", "2", "00000040"), ("1f80104e", "2", "00000088"),
            ("1f801048", "2", "0000000d"), ("1f80104a", "2", "00000000"),
            ("1f80104a", "2", "00000002"), ("1f80104a", "2", "00002002"),
            ("1f80104a", "2", "00000000"), ("1f801074", "4", "00000009"),
        ]
        for port in range(2):
            select = port << 13
            expected_card_writes += [
                ("1f80104a", "2", f"{select | 0x12:08x}"),
                ("1f80104a", "2", f"{select | 0x1003:08x}"),
                ("1f801040", "1", "00000081"),
                ("1f80104a", "2", f"{select | 0x1013:08x}"),
                ("1f801070", "4", "ffffff7f"), ("1f801070", "4", "ffffff7f"),
                ("1f801074", "4", "00000089"),
                ("1f801070", "4", "ffffff7f"), ("1f801074", "4", "00000009"),
                ("1f80104a", "2", "00000000"), ("1f80104a", "2", "00000040"),
                ("1f80104e", "2", "00000088"), ("1f801048", "2", "0000000d"),
                ("1f80104a", "2", "00000000"),
            ]
        # Separate source-backed BIOS VBlank acknowledgement from the exact
        # serial transaction order; host scheduling can expose policy1 edges.
        bios_acks = [w for w in card_writes if w == ("1f801070", "4", "fffffffe")]
        card_effects = [w for w in card_writes if w != ("1f801070", "4", "fffffffe")]
        policy_states = re.findall(r"CARD_STATE at=(\w+) pad_policy=(\d+) "
                                  r"card_started=(\d+) card_guest_irq=(\d+)", output)
        snapshots = {}
        for pc, npc, hi, lo, registers in re.findall(
                r"CPU_TRACE pc=(\w+) npc=(\w+) hi=(\w+) lo=(\w+) regs=([0-9a-f,]+)", output):
            values = [int(word, 16) for word in registers.split(",")]
            if len(values) != 32:
                print("native startup: malformed CPU checkpoint", file=sys.stderr)
                return 1
            key = (pc, values[31])
            if key in snapshots:
                print("native startup: duplicate caller checkpoint", file=sys.stderr)
                return 1
            snapshots[key] = values
        # Source-defined CRT/caller values; inherited BIOS scratch registers
        # intentionally are not asserted equal to the native zero policy.
        required_cpu = {
            "80010178": {2: 0x1FFFF8, 3: 0xC00, 4: 0x800C7F08, 5: 0x1374F0,
                         28: 0x80074750, 29: 0x801FFFF8, 30: 0x801FFFF8, 31: 0x8001009C},
            "800141f0": {28: 0x80074750, 29: 0x801FFFC0, 30: 0x801FFFC0, 31: 0x800101C8},
            "800101c8": {28: 0x80074750, 29: 0x801FFFC0, 30: 0x801FFFC0, 31: 0x800101C8},
            "8005fc68": {4: 0, 29: 0x801FFFC0, 30: 0x801FFFC0, 31: 0x800101D4},
            "8005cf08": {2: 0, 29: 0x801FFF78, 31: 0x800622F0},
            "800622f0": {2: 1, 29: 0x801FFF78, 31: 0x800622F0},
            "800627d8": {4: 0, 29: 0x801FFF78, 31: 0x8006230C},
            "800627e8": {2: 1, 4: 1, 29: 0x801FFF78, 30: 0x801FFFC0, 31: 0x80062340},
            "80062340": {2: 1, 28: 0x80074750, 29: 0x801FFF78, 30: 0x801FFFC0, 31: 0x80062340},
            "80061f38": {2: 1, 4: 2, 28: 0x80074750, 29: 0x801FFF90, 30: 0x801FFFC0, 31: 0x8006188C},
            "8006188c": {2: 0, 28: 0x80074750, 29: 0x801FFF90, 30: 0x801FFFC0, 31: 0x8006188C},
            "8006189c": {2: 0xFFFFFFFF, 28: 0x80074750, 29: 0x801FFFA8, 30: 0x801FFFC0, 31: 0x8005FCF8},
        }
        cpu_ok = all((pc, wanted[31]) in snapshots and
                     all(snapshots[(pc, wanted[31])][reg] == value for reg, value in wanted.items())
                     for pc, wanted in required_cpu.items())
        # SYS2 after InitCARD executed addiu a0,2 and preserved its v0=0;
        # the following SYS1 wrapper is reached with that actual caller state.
        second_sys1 = snapshots.get(("8005cf08", 0x80062338), [])
        cpu_ok = cpu_ok and len(second_sys1) == 32 and second_sys1[2] == 0 and second_sys1[4] == 2
        cpu_ok = cpu_ok and ("8005fcf8", 0x8005FCF8) in snapshots and all(
            snapshots[("8005fcf8", 0x8005FCF8)][r] == v for r, v in {
                2: 1, 28: 0x80074750, 29: 0x801FFFA8, 30: 0x801FFFC0, 31: 0x8005FCF8
            }.items())
        event_rows = re.findall(r"EVENT_STATE index=(\d+) handle=(\w+) flag=(\w+) "
            r"class=(\w+) status=(\w+) spec=(\w+) mode=(\w+) callback=(\w+)", output)
        expected_events = [(str(i), f"{0xf1000000+i:08x}", "00000000",
            "f4000001" if i < 4 else "f0000011", "00002000",
            f"{[4, 0x8000, 0x100, 0x2000][i%4]:08x}", "00001000",
            f"{0x800617cc+20*i:08x}") for i in range(8)]
        event_open = re.findall(r"EVENT_OPEN class=(\w+) spec=(\w+) mode=(\w+) callback=(\w+) handle=(\w+)", output)
        event_enable = re.findall(r"EVENT_ENABLE handle=(\w+) result=(-?\d+)", output)
        event_test = re.findall(r"EVENT_TEST handle=(\w+) result=(-?\d+)", output)
        events_ok = (event_rows == expected_events and
            event_open == [(r[3], r[5], r[6], r[7], r[1]) for r in expected_events] and
            event_enable == [(r[1], "1") for r in expected_events] and
            event_test == [(r[1], "0") for r in expected_events])
        input_values = [(int(a,16), int(v,16)) for a,v in re.findall(
            r"INPUT_RAM at=800101e4 address=(\w+) value=(\w+)", output)]
        wanted_input = {0x80078988:0x74a8, 0x8007898c:0x8005d734, 0x80078990:0x8005d6cc,
            0x80078994:0, 0x80072994:1, 0x800729ac:0, 0x800729b0:1,
            0x80072990:0x80078a48, 0x80072984:0, 0x80078998:0, 0x8007899c:0,
            0x800729c4:0, 0x800729a0:0, 0x8007299c:0}
        input_ok = len(input_values) == len(wanted_input) and dict(input_values) == wanted_input
        input_ok = input_ok and re.findall(r"INPUT_C003 priority=(\d+) descriptor=(\w+) result=(\w+)",output) == [("2","80078988","00000000")]
        input_ok = input_ok and re.findall(r"INPUT_C002 priority=(\d+) descriptor=(\w+) result=(\w+)",output) == [("2","80078988","00000000")]
        input_ok = input_ok and re.findall(r"INPUT_C00A channel=(\d+) value=(\w+) result=(\w+)",output) == [("3","00000000","00000000")]
        # The acknowledge/mask pair must be the only 32-bit IRQ writes, in this
        # order. The guest also performs 16-bit zero writes to the same two
        # registers during input-kernel setup; those are bounded here rather
        # than ignored, so a 32-bit write of another value still fails.
        input_mmio = re.findall(r"INPUT_MMIO address=(\w+) width=(\d+) value=(\w+)", output)
        input_ok = input_ok and [e for e in input_mmio if e[1] == "4"] == [
            ("1f801070","4","fffffffe"),("1f801074","4","00000009")]
        input_ok = input_ok and all(
            e[0] in ("1f801070","1f801074") and e[1] == "2" and e[2] == "00000000"
            for e in input_mmio if e[1] != "4")
        input_ok = input_ok and "INPUT_OWNER at=800101e4 installed=1 faulted=0 frame_busy=0 frame_faulted=0 timer3=0" in output
        input_ok = input_ok and "INPUT_CALLBACK_REFUSED" not in output
        for pc, regs in {
            "8005d8b4":{2:1,28:0x80074750,29:0x801fff88,30:0x801fffc0,31:0x8005d108},
            "800101e4":{2:1,28:0x80074750,29:0x801fffc0,30:0x801fffc0,31:0x800101e4},
        }.items():
            actual = snapshots.get((pc,regs[31]), [])
            input_ok = input_ok and len(actual)==32 and all(actual[r]==v for r,v in regs.items())
        # Host timing can supply a VBlank during startup. Only recovered game
        # callbacks may advance the tick, once per delivered IRQ on this path.
        delivered = int(fields.get("deliveries", "-1"))
        tick = int(fields.get("tick", "-1"), 16)
        hook_calls = int(fields.get("card_hook_calls", "-1"))
        card_calls = int(fields.get("card_vblank_calls", "-1"))
        maintenance_calls = int(fields.get("card_maintenance_calls", "-1"))
        video_ok = "VIDEO_SOURCE mode=0 policy=NTSC selected_before_worker=1" in output
        clock = re.search(r"SOURCE_CLOCK cycles=(\d+) faulted=0 "
                          r"scope=STARTUP_AND_INPUT_SOURCE_ONLY devices_advanced=DEVICE_EPOCH", output)
        clock_ok = clock is not None and int(clock.group(1)) > 0
        epoch = re.search(r"DEVICE_EPOCH cycles=(\d+) source_edges=(\d+) cuts=(\d+) "
                          r"waits=(\d+) frequency=(\d+) faulted=0 profile=PACED_HOST_TIME cd_advanced=0", output)
        timer = re.search(r"TIMER2 count=([0-9a-f]+) mode=([0-9a-f]+) target=0000 "
                          r"epoch=0 profile=PACED_EVENT_SERVICED irq_enabled=0", output)
        video = re.search(r"VIDEO_TIMING timer1_cycle=(\d+) count=([0-9a-f]+) "
                          r"subcycle=(\d+) running=1 faulted=0 profile=NTSC_2146_EVENT_SERVICED "
                          r"gpu_parity=([01]) gpu_reset_sequence=(\d+)", output)
        sio = re.search(r"SIO profile=DIGITAL_PORT1_CARDS_ABSENT bound=1 "
                        r"cycle=(\d+) transmitted=2 keyboard_polls=0 ack_pending=0 faulted=0", output)
        bu_clock = re.search(r"BU_CLOCK start=(\d+) end=(\d+) source_start=(\d+) source_end=(\d+)", output)
        clock_ok = (clock_ok and timer is not None and epoch is not None
                    and bu_clock is not None and sio is not None and video is not None)
        if clock_ok:
            clock_ok = (int(timer.group(1), 16) == int(epoch.group(1)) % 0xffff
                        and int(timer.group(2), 16) in (0x400, 0x1400)
                        and int(epoch.group(1)) >= int(clock.group(1))
                        and sio.group(1) == epoch.group(1)
                        and video.group(1) == epoch.group(1)
                        and 0 <= int(video.group(2), 16) < 0xffff
                        and int(video.group(3)) < 2146
                        and fields.get("TIMER1_MODE") in ("0507", "1507")
                        and int(video.group(5)) <= int(epoch.group(2))
                        and int(video.group(4)) == ((int(epoch.group(2)) - int(video.group(5))) & 1)
                        and int(fields.get("GPUSTAT", "-1"), 16) == (0x14802000 | (int(video.group(4)) << 31))
                        and int(epoch.group(2)) >= delivered
                        and int(epoch.group(3)) > 0 and 0 < int(epoch.group(5)) <= 0xffffffff
                        and int(bu_clock.group(2)) > int(bu_clock.group(1))
                        and bu_clock.group(3) == bu_clock.group(4))
        if (run.returncode != 2 or any(fields.get(k) != v for k, v in expected.items())
                or delivered < 4 or tick != delivered or writes != expected_writes
                or fields.get("I_STAT") not in ("0000", "0001") or not cpu_ok
                or card_calls < 4 or maintenance_calls != card_calls
                or fields.get("card_port") != str(card_calls % 2)
                or card_effects != expected_card_writes or len(bios_acks) > card_calls
                or "BU_RETURN result=0 " not in output
                or policy_states != [("80062340", "1", "1", "1"), ("80061f38", "0", "1", "1")]
                or not events_ok or not input_ok or not video_ok or not clock_ok
                or hook_calls < 9 + card_calls or hook_calls > 9 + delivered + card_calls):
            print(output, file=sys.stderr)
            print("native startup: expected continuous entry/card boundary not observed", file=sys.stderr)
            return 1
        print(output, end="")
        print("NATIVE INPUT registration verified; first CD MMIO at80044DBC unsupported; "
              "menu=NOT_REACHED; presented frame still needs a human visual check")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
