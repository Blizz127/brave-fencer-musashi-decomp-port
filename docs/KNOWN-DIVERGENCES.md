# Known divergences from retail

This is the public list of every known place where the PC port is not yet
1:1 with the retail North American PlayStation release. The standard is in
[ARCHITECTURE-PORT.md, "Purpose: preservation"](ARCHITECTURE-PORT.md#purpose-preservation).

Rules for this file:
- An entry stays until objective evidence closes it: a byte-matched C path,
  an emulator or video differential with identical output, or measured
  numbers. Evidence gets linked in the entry, and a closed entry moves to
  "Closed" with that link.
- Anything nobody has compared is **untested**, not "matches".
- Opt-in mods, cheats and debug switches that are off by default are
  listed under "Opt-in (off by default)". Everything else is a defect to fix.

Status values: **open** (differs, or is known to differ), **untested** (no
comparison yet), **opt-in** (off by default).

Seeded 2026-09-28 by reading the code on `port/platform-layer` (merged
`port/native-lane` 24d270872 and `main` f0299da37). Run the measurement
with `tools/port_matched_ratio.py`.

## Measured share (2026-09-28)

`tools/port_matched_ratio.py` was run on a headless boot to the title
screen:
- The build was lane-ON; the trace was taken with the lane off (so it is
  complete), on the guest clock, with exact per-PC counts from
  `tools/patches/trace-insns.patch`.
- It stopped at the naturally rendered Press Start prompt
  (`MUSASHI_PAUSE_AT_START_SCREEN`): 1109 frames, about 3500 s wall.
- 455 distinct guest functions ran, for 295.4 M interpreted instructions.
  195.0 M of those are the VSync wait loop (func_800424E4). Shares are
  given with that idle loop excluded (100.3 M instructions).

The shares:
- **Byte-matched C (native lane): 0.5%** of instructions, 62 functions.
- **Hand-written port code: 0.2%**, 42 functions: 9 PsyQ HLE, 4
  PsyCross, and 29 guest functions the host skips or intercepts.
- **Interpreted retail: 99.3%**, 351 functions. 212 of them have matched C
  that isn't run natively:
  - 86 are in PsyQ library ranges, which the lane policy keeps out even
    when matched;
  - 47 fail the lane's hooks rule;
  - 46 are PsyQ stub entries;
  - the rest fail other lane rules.
By domain, excluding idle, classified against the B1 lane table
(29477eed8). Overlay code is attributed to the resident MAIN.CD member
(73b802be3). That attribution is inferred from traced entries, and only
45 K instructions needed the global tie-break. The next run records exact
residency (tools/patches/trace-resident.patch).
- **Game code** (17.3 M instructions, 186 functions):
  - byte-matched C *runs* for **4.7%** (71 functions), up from 1.2% before
    B1;
  - byte-matched C *exists* for **99.85%**;
  - the gap is main_0003's matched C, interpreted because the lane has
    no overlay support yet, plus the lane-refused main-EXE functions
    (hooks and other rules).
- **PsyQ/SDK** (82.6 M, 268 functions):
  - shims run 0.2%;
  - the rest is interpreted retail: unregistered libcd (46 M, including
    the 43 M sector-copy loop at 80046C38), libpress in main_0003
    (21.4 M, D-023), libpad (6.4 M) and libetc (2.2 M).
  - Under the owner's rule, PsyQ/SDK stays platform code. The target is
    shims proven faithful, not matched C.

Shadowing:
- 31 active PsyQ shims shadow a registered matched-C function. 9 of
  them ran: memcpy, ClearOTagR, CdIntToPos, CdPosToInt, CatPrim, memset,
  SetDefDispEnv, GsInitCoordinate2 and GsSetLightMode.
- A further 15 guest functions with matched C are skipped by the host,
  for example 80016714 (startup environment clear, 3008 calls) and
  8005C388 (console putchar).

## Game logic and state

| ID | Divergence | Where | Why | Status |
|----|------------|-------|-----|--------|
| D-001 | Guest RAM was forced by default once the game was in play: player `unk4D` 2 → 1, the mode word 0x800B99F0 = 9, and every frame player `+0xA9 = 0x41` (pad type) and `+0x4D = 1`. **Fixed on `port/native-lane` in b3b33befd.** Nothing is forced by default now. The same pokes run only with `MUSASHI_DEBUG_FORCE=1` (see D-005), and `MUSASHI_NO_FORCE` is removed. | `pc_port/native_boot.c` | Workaround from before the boot path was complete. | fixed on port/native-lane (merge into this branch pending) |
| D-002 | The auto-start and auto-dismiss paths (`MUSASHI_BOOT_AUTO_START`, `MUSASHI_AUTO_DISMISS`, set by `--headless` and `--frames`) inject Start and dismiss presses. That is input only, like a player pressing buttons. Their `unk4D` RAM write is now behind `MUSASHI_DEBUG_FORCE` (b3b33befd). | `native_boot.c` | For headless tests. | opt-in (input only) |
| D-003 | The interpreter replaced the retail pad-record getter at 0x8005F704 with host code. **Fixed on `port/native-lane` in 57199ef9c**: the retail getter runs. The retail code returns 0x80078A48 (addiu sign-extends) and adds 0xF0 whenever a0 & 0xF0 is nonzero, so the override differed only for port 2 (+0x10 where retail gives +0xF0). | `pc_port/mips_formatter.c` | Boot-path workaround. | fixed on port/native-lane (merge pending) |
| D-004 | Memory cards are always disconnected. Saving and loading are unavailable, and the game takes its no-card path. | `pc_port/sio_controller.c` (`disconnected_cards = 1`) | No card image backend yet. | open |
| D-005 | `MUSASHI_DEBUG_FORCE=1` (debug switch) pokes guest RAM after SCENE1_REACHED: player+0x4D 2 → 1 (control unlock), 0x800B99F0 = 9 (game mode), player+0xA9 = 0x41 (pad type). It prints a DIVERGENCE line at startup and once per poke. | `native_boot.c` (b3b33befd) | Debugging only. | opt-in (off by default) |
| D-006 | `MUSASHI_GAMEPLAY_ACTION` (scripted test driver) writes pad words straight into guest RAM in gameplay (player +0xAA/+0xAC/+0xAE, 0x80078DCA, 0x80078DD2) and writes 0x800AE658. It prints a DIVERGENCE line when set (a6e3766f9). | `native_boot.c` | Automated gameplay tests. | opt-in (off by default) |
| D-007 | **Unknown cause:** why the player's pad-type byte (+0xA9) needed forcing to 0x41 for control. The D-003 override returned the retail value for port 1, so it does not explain this by itself. Static trace (2026-09-28): `func_80174714` advances player state only when `func_800CF8CC()` is false; that matched helper returns true exactly while mode `0x800B99F0` is 10. Mode 10 dispatches through member0010's `D_800D3430` table; its slot 10 handler (`0x801281B8`) reaches `func_80128714`, which polls `func_800D19F0`. That helper waits for `func_8001BE30`, whose state machine checks the CD queue and current CD request sector. This makes an incomplete CD request a plausible reason the mode-10 path stays active, but does not establish a runtime stall or an XA voice-stream cause. The nine listed functions in `provenance/matches.json` were freshly re-verified byte-exact against retail (9/9); the slot mapping was read from the extracted member0010 bytes. | pad/SIO path | Not yet known; static trace only. | open (untested) |

## Timing

| ID | Divergence | Where | Why | Status |
|----|------------|-------|-----|--------|
| D-010 | On the default host clock, if the game waits in `VSync` (0x800424E4 polling 0x8006CBB8) and the tick hasn't advanced, the host latches VBlank in I_STAT. The VBlank then follows host pacing, not guest CPU cycles. `MUSASHI_GUEST_CLOCK=1` (the default for `--headless`) paces VBlank by guest cycles instead. | `native_boot.c` `vsync_wait_raise_tick` | Interactive runs are host-paced. Headless on the host clock, a boot refuses at irq-dispatch (pc 80018DF4) after about 21 s with all frames black (2026-09-28). | open (host clock); untested (guest clock vs hardware cycle counts) |
| D-011 | **Guest time differs from retail when the native lane is on.** A native-lane call charges a fixed cost: 2 × (size / 4) cycles for generated functions, 16 for PsyQ entries. The retail instructions it replaces would have cost their interpreted cycle count. So with the lane on, VBlank-relative timing, and anything else counted in guest cycles, drifts from the all-interpreted run. On top of that, the guest clock charges the interpreter's per-instruction cost and does not model the R3000A's cache, load delays or memory wait states. Fix direction: the lane should charge the interpreted cycle count (for example a per-function count measured on the interpreter, or the retail count from a cycle model), so lane-on timing is 1:1 with lane-off. | `mips_formatter.c` `lane_try` (`cost_cycles`); `tools/native_lane_gen.py` (`cost = 2 * (size // 4)`); `native_boot.c` guest clock | Not modelled yet. | open |
| D-012 | Retail runs at 30 fps game logic. `[video] frame_rate = 60` is accepted by the config but has no effect. | `bfm_plat_config.c` | Design note only. | opt-in (no effect) |

## PsyQ library code (hand-written, not byte-matched)

| ID | Divergence | Where | Why | Status |
|----|------------|-------|-----|--------|
| D-020 | PsyQ compat entries run hand-written C instead of the retail library instructions at their PCs. This is by design: the port's PsyQ layer is PsyCross or its own code, never decompiled PsyQ. By default (`MUSASHI_PSYQ_LANE=pure`) that means every non-stub entry except the device entries and those whose body holds a port-sequenced PC. Each shim must be proven faithful by `tests/psyq_shim_probe.c` (retail on the interpreter vs the shim, same inputs, comparing v0 and guest RAM). The proven ones are in the table below. Seven were fixed after the lane probe (7935600a2). The GTE functions RotMatrix, RotMatrixYXZ/X/Y/Z and ratan2 read the game's own tables (193b41e86). All other entries have no per-entry differential yet. | `pc_port/platform/psyq/bfm_psyq_compat.def`, `pc_port/native_lane_psyq.c` | Owner rule: no decompiled PsyQ in the port. | open for every entry not proven below |
| D-021 | Entries with the `PSYCROSS` status call PsyCross (MIT, a PsyQ reimplementation), whose GTE and libgpu semantics are not retail. | `pc_port/platform/psyq/psyq_psycross.c` | Same as D-020. | untested |
| D-022 | Shims set v0 (and write memory) exactly, but leave the other caller-saved registers (v1, a0-a3, t0-t9) as they were on entry, where the retail code clobbers them. Under the o32 ABI no C caller can observe this; hand-written asm callers could. | `native_lane_psyq.c`, `mips_formatter.c` `lane_try` | Wrappers work on the live register file. | open (believed invisible; not measured) |
| D-023 | main_0003 (the intro movie player) contains Sony libpress DecDCTvlcSize/DecDCTvlc at 0x800D3204-0x800D3574: handwritten SDK assembly that can never be C. It runs as interpreted retail, unmatched. For the port it is platform territory: the faithful replacement is a PsyCross or port-own DecDCTvlc, proven by a differential against interpreted retail (same bitstream in, same RAM out). | `tools/port_matched_ratio.py` `SDK_OVERLAY_RANGES` | Planned. | open (interpreted retail today, so faithful by construction until replaced) |

### PsyQ shim differential status (`tests/psyq_shim_probe.c`, 96 inputs each)

| Shim | PC | Result | Evidence |
|------|----|--------|----------|
| memcpy | 8005C324 | same on 96/96 after fixing overlapping forward copy, NULL dst and n <= 0 | f986ce7a8 |
| memset | 8005C358 | same on 96/96 after fixing NULL dst and the return of 0 for n <= 0 | f986ce7a8 |
| bzero | 8005C2C8 | same on 96/96 (v0 too) after the same fix | f986ce7a8 |
| CatPrim | 80058CE4 | same on 96/96 | 1a8944bb4 |
| CdIntToPos | 80043A18 | same on 96/96 (including negative and large values) | 1a8944bb4 |
| CdPosToInt | 80043B1C | same on 96/96 (valid and invalid BCD) | 1a8944bb4 |
| SetDefDispEnv | 80058B04 | same on 96/96 | 1a8944bb4 |
| GsInitCoordinate2 | 80052D90 | same on 96/96 (NULL and non-NULL super) | 1a8944bb4 |
| GsSetLightMode | 800538EC | same on 96/96 | 1a8944bb4 |
| ClearOTagR | 80059BFC | **untested**. Retail programs DMA6, which the device-less probe cannot run. The shim was corrected from the asm: retail relinks ot[0] to the terminator at 0x80072844, where the shim wrote 0x00FFFFFF, and it prints at debug level >= 2. It needs a device-backed differential. | f986ce7a8 |

"Same on N/N" is evidence for the inputs tried, not a proof for all
inputs.

## Video

| ID | Divergence | Where | Why | Status |
|----|------------|-------|-----|--------|
| D-030 | The default CPU rasteriser (`gpu_controller.c`) is an approximation. Line clipping and edge rules are not proven against hardware, and Gouraud lines use the start colour for the whole line. | `pc_port/gpu_controller.c` | Not yet hardware-accurate. | open (lines); untested (polygon edge and fill rules) |
| D-031 | Semi-transparency, mask bit and STP were fixed against an 8-case repro (`tools/headless/gpu_semi_repro.c`, 056abd331). That repro is a unit comparison, not a hardware capture. | `gpu_controller.c` | | untested against hardware |
| D-032 | `video.gpu = bfm_plat` (the GPU bridge to the bfm_plat renderer) is verified only through the splash and fade-in at 125 s. The title and gameplay haven't been compared. | `pc_port/platform/bfm_plat_gpu_bridge.c` | New path. | opt-in; untested beyond the splash |
| D-033 | The `gl` renderer blends in 8-bit float: modes 0 (B/2+F/2) and 3 (B+F/4) can differ by ±1 in a 5-bit channel. Dithering is off above 1x resolution. | `pc_port/platform/backends/gl/` | GPU precision. | opt-in (`renderer = gl`) |
| D-034 | The `psycross` renderer draws through PsyCross's GL path with PGXP; its fidelity to the PS1 GPU hasn't been compared. | `backends/renderer_psycross.c` | | opt-in; untested |
| D-035 | Widescreen (hor+ / anamorphic), HD texture replacement and texture restyle change the picture. | `bfm_plat_widescreen.c`, gl renderer, mods | Enhancements. | opt-in (off by default) |

## Audio

| ID | Divergence | Where | Why | Status |
|----|------------|-------|-----|--------|
| D-040 | SPU and XA/CD-DA output come from the port's own SPU core. Neither sample accuracy nor timing has been compared with hardware or an emulator. Headless runs use `MUSASHI_AUDIO=null`, which produces no sound. | `pc_port/spu_*`, `native_boot.c` | | untested |
| D-041 | Pre-title audio is silent in the headless run. 1200 s runs (native lane, guest clock) reach the Squaresoft logo, about 25-28 s of guest time. The only KON writes are libspu SpuInit's two passes over all 24 voices, at volume 0 and ADSR 0, on the silent block 0x0200. The sink receives about 340k frames, all zero. Whether retail plays anything during the publisher and Squaresoft logos is **untested**, pending an emulator or video reference. | SPU path | | untested |

## Opt-in (off by default)

These change retail behaviour only when a user asks for them:
- Cheats (`pc_port/mods/examples/cheat_pack`) and mods (docs/MODDING.md).
- The Port Dev Menu (`BFM_DEV_MENU=1` or `--dev-menu`, `pc_port/dev_menu.c`): cheats that write the game's own HP/BP/Drans state once per VBlank, and warps once tested. Off, it writes nothing, reads no input and draws nothing (`tests/dev_menu_probe.c`); while open, the game reads a neutral pad.
- `MUSASHI_DEBUG_FORCE=1` (D-005) and `MUSASHI_GAMEPLAY_ACTION` (D-006): both write guest RAM and log DIVERGENCE.
- `MUSASHI_HOLD_R1`, `MUSASHI_DISMISS_KEY`, `MUSASHI_PAUSE_*`, and the trace and dump switches. The trace and dump switches only observe.
- Widescreen, the gl renderer, HD textures, fast-forward.

## Closed

(none yet)
