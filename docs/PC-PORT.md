# PC port

The port links the decompilation against
[PsyCross](https://github.com/OpenDriver2/PsyCross) (MIT), which reimplements
the Psy-Q libraries for PC on SDL2, OpenAL and OpenGL. It is vendored at a
pinned commit by `tools/fetch_toolchains.sh` and its checkout is ignored.

## Status

Cleared REFUSED `800D18DC` (MAIN10 `[800D18DC,800D1938)` 23 words). Tip xvfb
2026-09-09 ~16:15 CT STOP **`pc=800d1938`** (`CPU_BOUNDARY` `ra=800d1928`
`a0=800ae6a8` `a1=800d19dc`) / `startup=PARTIAL` / `EXIT:2`.
The summary records the proven CPU refusal as **`menu=NOT_REACHED`** and keeps
the pending human look as a separate **`visual_check=REQUIRED`** marker;
`tools/native_boot_check.py` requires both, so a presented frame cannot be
mistaken for a reached menu. Folded SC02 PAC0 `8014E934…80150EC4` (60 leaves /
2404 words from `8e857c7f0`) onto tip with D18 retained. The SC02 hole
`80150EC4…80153C8C` is closed: its 13 differing spans are recovered, each a
single data reference shifted by the member's `+0x888` block offset, and all
re-earned MATCH against the SC02 blob. Remaining SC02 work is 272 known spans
— now 16 known spans (1,416 bytes) where the two members hold different code
rather than a retargeted data reference: `8013C9C4`, `8013D330`, `80164E40`,
`8017BEBC`, `8017BEE4`, `8017BF70`, `8017BFF8`, `8017C080`, `8017C0DC`,
`8017C120`, `8017C24C`, `8017DF18`, `8017E22C`, `8017E254`, `8017E298`,
`8017E324`. For example `8017BFF8` returns `D_801857E8` in member 0012 while
SC02_031 holds an unrelated store/`lwc2` sequence there, so these need their own
SC02 decompilation rather than an adaptation; seven of them have Druthulu
`shared/ov` donor shapes that still need their extern declarations. Next MAIN10
hole `800D1938+`.
Scanout enabled.

**Later the same day** the resident-image walk below moved the tip to
`pc=80020F44` (a GTE bank leaf, `EXECUTION_BOUNDARY reason=REFUSED`); the
`startup=PARTIAL` / `menu=NOT_REACHED` / `visual_check=REQUIRED` markers still
stand. See "Resident-image walk: GTE bank leaves and the second wave".

## SC02 word-lane provenance (2026-09-10)

The SC02 word lane's authority is **the image the guest itself loads**, not
`artifacts/bfm-takeover-20260907/sc01-title-code.bin`. That capture is a
different member: its overlay words match SC01.CD/extracted `sc01` data and
diverge from the resident image at the first data reference (word 48 of
`func_80128288`, `3C02801A` vs the resident `3C02801B`).

`MUSASHI_DUMP_RAM=<path>` captures the guest's 2 MiB main RAM at the boundary
stop, so the resident overlay can be compared directly. Run with the disc
arguments and auto-start to reach it:

```sh
SDL_VIDEODRIVER=x11 MUSASHI_BOOT_AUTO_START=1 MUSASHI_HOLD_WINDOW_MS=1000 \
MUSASHI_DUMP_RAM=/tmp/ram.bin \
xvfb-run -a build/musashi_native_boot extracted/disc/files/SLUS_007.26 \
  extracted/disc/disc.cue extracted/disc/disc.bin
```

Measured against that resident image (2026-09-10, stop `pc=801282ac`):

| streams | vs resident image |
| --- | --- |
| 1613 pre-existing (leaves folded before the batch work) | all match |
| 284 added by the member-0012 retargeting batches | all mismatch |

The port's `OVERLAY_WORD_MISMATCH` guard is why the run refuses there: it
compares each carved word with guest RAM and fails closed on a difference, so
no wrong word can execute. `src/overlays/sc02_0031/80128288.c` has been
re-derived from the resident image (dispatch table `D_80184F08`, oracle MATCH
17/17) as the first repair. All 283 remaining batch streams have since been
re-derived the same way (279 by automatic retargeting, 4 by hand: the
jump-table base `0x801B5BA8` for `func_80154C24` and the data blocks for
`func_8013C9C4`/`func_8013D330`/`func_80164E40`), each re-earning MATCH against
the resident image. **All 1907 wired SC02 streams now equal the resident
image** and the guard has no mismatch to report.

The walk then continues: `func_801612B8` (21/21) and `func_80133784`
(203/203, donor register pins retargeted to this member's boxes and counters)
were carved from the resident image as the next leaves, and the run now clears
the SC02 path entirely. The stops after that were all device-layer gaps, each
closed with exact retail evidence:

- `80047d3c` — `func_80133784` returns here, so its return alias joins the
  audited `gte_47d3c_caller` list.
- `80133da0` — DIV sites are now admitted when the image carries GCC's
  canonical divide-by-zero guard (`BNE divisor,$zero` to the instruction after
  `BREAK 7` with a NOP between); a DIV without the guard is still refused.
- `80133fe8`/`80133fec`/`80133ff0`, `80133ffc`, `8013400c`/`10`/`14` —
  `func_80133CD4`'s vector transform: LWC2 data 9-11, the SQR command, SWC2
  data 25-27. Its own `0x80133FB0` jal clobbers RA, so these are admitted by
  exact PC/word/rt; commands now dispatch on the COP2 command bit rather than
  `rs==18`, which is only MVMVA's own encoding.
- `8014f5d4` — 396 LWL/LWR/SWL/SWR sites inside carved ranges were missing
  from `merge_kind_for`; they are now listed with the resident image's exact
  words, and the first pair executes.

The interleaved copy blocks were closed by listing the 98 missing
pending-LWR successors in `merge_pending_matches` with the resident image's
exact PC, word and register. From there the walk chained through the retail
code and each stop was resolved in turn:

| stop | resolution |
| --- | --- |
| `8014f5dc` | 98 interleaved merge successors admitted |
| `80159be4` | leaf `func_80159BE4` carved 40/40 (donor shape, one data symbol retargeted) |
| `8015444c` | leaf `func_8015444C` carved 363/363 — the per-location interpreter, from the donor's 260-line shared body, switch lowered to `0x801B5ACC` |
| `80155458` | leaf `func_80155458` carved 24/24 (dispatch table `D_80186ADC`) |
| `80029104` | main-exec `func_80029104` had a registry C recovery but no native word export; its eight retail EXE words are now wired (CMake entry range + formatter array/range) |

Two more leaves followed the same recipe: `func_80161A90` (34/34, donor shape
whose two published variants already match this member) and `func_8016F1C4`
(35/35; the variants differ only in the table symbol, and this member needs
`D_8018804C`, `D_800B9A08` and `D_80126B58`). `func_800120DC` had a registry C
recovery but no native word export, so its 20 retail EXE words are now wired
through the CMake entry list and the formatter, exactly as `func_80029104`.

The `pc=800cf8b4` stop was **not** a missing 0007 selection — the loaded image
at that moment is member 0010. A `MUSASHI_DUMP_RAM` capture at the stop shows
guest RAM `@800cedf8` holding the member-0010 signature
(`00000036 3C03800C 946399F0 27BDFFE8`), and the 64 bytes at `0x800cf8b4` match
`extracted/overlays/main/0010.bin` at offset `0x20abc`, i.e. base `0x800aedf8`
(the same base the MAIN10 leaves use). The address simply sat outside the
port's member-0010 ranges. `func_800CF8B4` `[800CF8B4,800CF8CC)` is now carved
from that image (6/6 words, C written from the disassembly since no registry
entry exists) and wired under `g_overlay_0010_words`. The 0007 selector remains
unused, but nothing yet requires it — a 0007-loaded image would say so in a
RAM capture.

Two more SC02 leaves followed: `func_8016F14C` (24/24, donor `edf31e68`) and
`func_801719A4` (24/24, donor `7890648e`, whose `D_8011F738` already matches).

Current stop: `pc=800d2844`, which is past the last wired member-0004 range
(`0x800d24d0`). Next step: capture guest RAM at the stop with
`MUSASHI_DUMP_RAM`, identify which member is loaded and at which base, then
carve the enclosing function from that member's image the same way.

## Gate status

`tools/run_tests.sh` passes on `6b38fb9e4` (and on `536e94e1a` / `21e91dd33`
earlier the same day): **538 tests OK**, out-of-tree configure/build, **22/22 CTests** (including
`native_startup_boundary`), archive symbol checks and the smoke run all clean,
script exit 0. The earlier aborted run is superseded; re-run it after any
further formatter or CMake change, because those are the files this lane edits
most.

The resident-image walk below re-ran it after its formatter and CMake changes:
**538 tests OK** and **22/22 CTests** again, exit 0.

## The walk's second day: the RAM mirror, the GTE bank tables (2026-09-10)

Continuing the walk from `80020F34` turned up three things that were stopping
code the guest really executes, plus a set of site tables that make the loop
self-sustaining.

**`MUSASHI_TRACE_REFUSAL=1`.** Every refusal now names itself: `formatter_step`
prints the guard's line, a refused load or store prints the address, the run
loop prints which of its own gates refused, and the checkpoint prints why. The
trace is off unless the variable is set and changes no gate's decision, so a
walk that stops early can be diagnosed without editing the source.

**Main RAM mirrors.** `musashi_boot_ram_span` deliberately refuses addresses
outside `[0x80000000,0x80200000)`, and its tests pin that. The *CPU bus*,
though, decodes only A0..A20: the 2 MB window repeats every 2 MB across the
first 8 MB of KUSEG/KSEG0/KSEG1, and those three segments alias each other.
Retail member 0004 stores a halfword to `0x80200000` while walking a buffer to
the top of RAM, which hardware aliases to physical 0. `cpu_ram_span` now
applies that alias (after the canonical span, the BIOS frame and the
scratchpad), so the boot-image API stays as strict as it was.

**SWC2 and LWC2 use the data bank.** PsyCross documents the pairing itself
(`MTC2`: "LWC2 is the same kind"; `MFC2`: "SWC2 is the same kind"), so a
hardware SWC2 reads a GTE *data* register. The two earlier admitted SWC2 sites
had been routed through `read_control`; every admitted site, including those
two, now transfers through `read_data`, and the owner's read profile gained
SXY2 (14), SZ3 (19) and ORGB (29) for the raster/colour stores. The write
profile gained the vector slots 2..6 for the LWC2 loads and IRGB (28), which
goes through the vendor helper because it unpacks into IR1..IR3 rather than
storing a raw word.

**Site tables that follow the wired ranges.** `gte_bank_site`, `lwc2_site`,
`swc2_site`, `gte_exported_srav_site`, `merge_kind_for` and the load-successor
tables are now regenerated from the wired ranges by
`tools`-external scripts (`/tmp/bfm-carve/regen.py` in this session): for every
address in a wired range, once per image that covers it — the executable,
member 0031, or each MAIN.CD member whose blob carries that address. Two
members can be resident at the same address with different words, so the merge
table is now an exact `(pc, word)` lookup instead of a `switch` on the pc
alone. Pcs already owned by a per-family table are left out of the residual
tables so their caller pins stay in force.

Carved and wired in this pass: executable `80017758`, `80017778`, `80017E68`,
`80017E8C`, `80048384`, `80049610`, `80058DE8`; member 0031 `80167DBC`,
`80168070`; member 0010 `800D20C0`, `800D23D0`. `800D20C0`/`800D23D0` are the
same addresses member 0004 also loads, which is exactly why the tables are
keyed per image.

A second batch on the same day, after the tables above started following the
ranges: executable `80015954`, `80018094`, `800190AC`, `8002FA3C`, `800491EC`,
`80058C6C`; member 0010 `800D1FC8`, `800D21C4`, `800D22E4`; member 0031
`80128ED8`, `80129010`, `8012956C`, `8013CA14`, `801627E8`, `80162968`,
`80162B1C`, `80165CA0`, `80166054`, `8016E95C`, `801754A8`, `80175798`,
`8017F590`, `8017F5B4`. That batch also split CFC2 out of the data bank in
`gte_bank_site`: slot 5 is a *control* read and requires the `read_control`
binding, because `cfc2 $v0,$N` reads the control bank while `mfc2` reads the
data bank.

Where the walk stands now: with those admissions the run no longer stops inside
60 s. It executes the CD streaming path — `CD_WRITE`/`CD_RESPONSE`, XA audio
decoding, ordering-table DMA, draw packets and vblank waits — until the 400 s
observation window ends. That is a much longer run, **not** a reached menu:
`menu=NOT_REACHED` and `visual_check=REQUIRED` still stand, and a longer run
still has to be shown to end at a real boundary rather than only at the cap.

## Resident-image walk: GTE bank leaves and the second wave (2026-09-10)

The earlier stop at `pc=80047F0C` was neither the checkpoint nor the GTE
load gate: `formatter_step` itself refused there, at the trapping-`SUB` guard
inside `func_80047EC8`. Every admission below is pinned to exact retail bytes;
the temporary refusal tracing used to find them was removed before this
commit.

- `func_80047EC8` (the exported 188-byte LZCR/GPF leaf): its two signed `SUB`s
  and four `SRAV`s are admitted by exact PC/word plus the five retail JAL
  return aliases. The signed overflow trap is unchanged.
- IR0 (`mtc2 $x,$8`) joins the GTE data-owner write profile. The leaf loads the
  interpolation factor into IR0 and the following `GPF` (`4B90003D`) scales
  IR1..IR3 by it; IR0 *reads* stay refused, because no exported site reads it.
  `tests/gte_data_owner_probe.c` now asserts both halves of that contract.
- `func_800479E8` and `func_80047B3C` (camera fixed-point steppers),
  `func_80049324` (LWC2 → MVMVA → SWC2 vector leaf) and the GTE bank leaves
  `func_80048FBC`, `func_80048EAC`, `func_8004D504`, `func_80048B6C`,
  `func_8004864C`, `func_8004914C`/`func_800491AC` are admitted site by site,
  each with the caller-alias list its own retail `jal` sites produce.
- `func_801347A0`'s LWL/LWR pairs and their SWL/SWR stores are added to the
  merge table, and `gte_47d3c_caller` / `gte_48d9c_caller` / `gte_484ec_caller`
  gained the remaining retail return aliases (executable and member 0031).

Carved and wired this pass — word export, CMake entry, formatter table and
fetch range — each verified against the authority named in its file header
(`extracted/disc/files/SLUS_007.26` for the executable,
`artifacts/sc02-resident-20260910/overlay.bin` for member 0031):

| region | ranges |
| --- | --- |
| main exec | `800123F0`, `800126C4`, `80012F74`, `80012FC8`, `80013294`, `800132BC`, `80013328`, `80013350`, `80013ED0`, `80013F98`, `80013FBC`, `80020F34`, `800479E8`, `80047B3C`, `8004864C`, `80048B6C`, `80048FB8`, `80049324`, `80049A18`, `8004D504`, `80054DCC`, `8005C49C` |
| member 0031 | `80129CF8`, `8012A328`, `8012A988`, `801320D0`, `80134510`, `801345F8`, `801347A0`, `80135480`, `80135D20`, `80135EB0`, `80136A94`, `8013E448`, `80142EC0`, `8014305C`, `80165E90`, `8016EE94`, `8017849C`, `8017869C`, `80178B18`, `80178BF8`, `80178D40`, `8017C180`, `801816C0`, `80181A44`, `80181AE0` |

`func_80013ED0` also got a C body: m2c shape `arg0[3*k+r] = argr[k]`, MATCH
27/27 through `tools/match_function.py`. The executable sources that already
existed but carried no word export — `800123F0`, `800126C4`, `80012F74`,
`80012FC8`, `80013294`, `800132BC`, `80013328`, `80013350`, `80013F98`,
`80013FBC`, `80020F34`, `8004864C`, `80048B6C`, `80049324`, `8004D504`,
`8005C49C` — were wrapped in `#ifdef MUSASHI_NATIVE_MIPS_WORD_EXPORT` rather
than rewritten, so their oracle C claims are untouched; each was re-run through
`tools/match_function.py` after the wrap.

Where the walk stands: it now stops at `pc=80020F44` — the `mtc2` pair of
`func_80020F34`, the next GTE bank leaf — with `EXECUTION_BOUNDARY
reason=REFUSED`, `startup=PARTIAL`, `menu=NOT_REACHED`,
`visual_check=REQUIRED`. That is the next unadmitted site of a shape already
handled above, not a proven dead end, and it is **not** evidence that the menu
is reachable: no frame from the boot path has been looked at by a human.

### Third wave: closing the `8001FB8C…80021284` seam (2026-09-11)

The six gaps left between the second-wave carves are closed, so the native
seam now covers `8001FB8C…80021284` contiguously — every span verified
byte-exact against `extracted/disc/files/SLUS_007.26` with the SHA256 in its
file header:

| range | words | shape |
| --- | --- | --- |
| `80020248…80020598` | 212 | word export only (GTE-heavy, calls `80020DA4`) |
| `80020A28…80020DA4` | 223 | word export only |
| `80021008…80021050` | 18 | word export + pre-existing m2c C below `#else` |
| `80021050…8002109C` | 19 | word export + pre-existing m2c C below `#else` |
| `8002109C…80021120` | 33 | word export + pre-existing m2c C below `#else` |
| `80021120…80021174` | 21 | word export + pre-existing m2c C below `#else` |

The four `800210xx` files existed since the Codex import as C-only drafts;
they were wrapped in `#ifdef MUSASHI_NATIVE_MIPS_WORD_EXPORT` rather than
rewritten, so their oracle C claims are untouched (and still unverified —
no MIPS toolchain is installed here to run `tools/match_function.py`).
Each range got its CMake `addr:count` entry, formatter table and fetch
branch. Also fixed while here, pre-existing and unrelated to the carve
work: the `kOverlaySc02_801458E0Words` table had no fetch branch, which
failed `test_bios_event_callbacks.py` under `-Werror` even on HEAD.

### The formatter compile wall, and why it was a correctness risk (2026-09-15)

`pc_port/mips_formatter.c` took **16.7 seconds** to compile at `-O2`. 83
tests in the suite give `cc` a 30-second budget, so under any load the
file blew that budget and the test reported
`subprocess.TimeoutExpired`. That is what the 39 failures and 11 errors in
the 2026-09-14 full run were: not regressions, compile timeouts. A red
suite that is red for a reason unrelated to the code under test is worse
than a slow one, because it stops being evidence.

`-ftime-report` put 83% of the time in the dominator pass, and
`-fdisable-tree-dom1/2/3=<function>` narrowed it to one function.
Three things were wrong, all of them the same mistake in different
clothing — machine-generated *code* where the data belonged in a *table*:

| change | effect |
| --- | --- |
| `merge_pending_matches`: six `\|\|` chains, 225 `(pc && word && reg)` terms, folded into `kMergeRetireSites` (118 unique rows) | 14.2s → 1.8s |
| `formatter_fetch`: 2,077 `else if (pc >= a && pc < b)` branches in 17 runs, replaced by sorted tables and a binary search | 5,659 → 579 lines in that function |
| eight GTE site scans sharing one `noinline` lookup instead of eight unrollable copies | keeps the trip count opaque to the optimizer |

Result: **16.7s → 1.43s**, and the file shrank from 27,087 to 22,356
lines. `tests/test_pvd_irq.py` went from 4 failures to 5 passes.

This mattered beyond the clock. The dispatch was O(n) in the number of
carved ranges at run time as well as compile time, and the seam is meant
to hold all 3,962 functions — roughly three times today's count.

Both rewrites were proven equivalent rather than reviewed and hoped over,
by building the old and new translation units as two shared objects and
comparing them directly:

- `formatter_fetch`: every 4-byte-aligned address in `[80000000,80200000)`
  under all 16 combinations of the four overlay selections —
  **8,388,608 fetches, 0 differences**.
- `merge_pending_matches`: every `(pc, word, reg)` triple named anywhere in
  the old file, crossed with every merge kind, register, pending, delay-slot
  and branch state, plus deliberately mismatched words —
  **162,965 cases, 0 differences**.

A binary search is only equivalent to an ordered chain when the ranges are
disjoint, so the transform refuses any run containing an overlap. That is
what keeps the SC01/0012 overlay alias at `80128158` spelled as ordered
branches, along with every branch carrying a condition beyond its pc range.

## Merge availability and the GTE library path (2026-09-10)

`merge_memory_available` judged availability with `musashi_boot_ram_span`, which
refuses the 1 KB scratchpad, while the access itself uses `cpu_ram_span`, which
maps it. The guest runs its kernel stack at `sp=1f8003xx`, so every
LWL/LWR/SWL/SWR that touched the stack was refused. It now judges with the same
mapping the access uses; MMIO still fails closed.

`func_80012558` is a PSY-Q GTE library routine (26 COP2 sites: CTC2 control 0..4,
MTC2/MFC2 data 9..11, the `4A49E012` command) that the guest reaches by
fall-through, so RA is stale and cannot gate it. Its sites are admitted by exact
PC/word with the transfer binding, its three MFC2 chains were added to both
pending-load successor tables (the `reg/pc/word` one and the PC-only list the run
loop consults before stepping), and the CTC2 sites bypass the index-based caller
checks. Library routines the walk calls but that have no registry entry
(`ratan2` at `8004CFEC`, `func_8001282C`) are exported as words with an explicit
no-C-claim header.
No decomp claim is withdrawn for MAIN member 0012 — those registry entries were
verified against member 0012 and remain valid there; only their SC02_031
retargeting was built from the wrong image.


The new checked descriptor derives width from the horizontal video-clock range
and selected mode, preserving the real transient 256-pixel state before mode 1
selects 320 pixels. Reset restores 256×240. GP1 buffer reset preserves display
registers; GPUSTAT reflects accepted mode bits. The adapter validates raw and
derived fields, ownership and actual queued work before updating `activeDispEnv`
and `currentDispEnv`. Refused commands cannot publish source cache writes.
Supported modes are 0/1/2/3/40, NTSC progressive 15-bit, with the existing fixed
V `[16,256)` timing profile. Other modes/windows and enabled scanout still refuse.
No display setter clears VRAM, invokes a substitute SDK call, or presents a frame.

The preceding real DMA2 transfer remains intact: one header and six ordered GP0
commands reach PsyCross, complete at the declared device deadline, and return
through the source queue with `v0=0` and `I_MASK=0D`. E2..E6 drawing-register
ownership, inclusive bounds, signed offsets, raw info latches and accepted-prefix
accounting are retained. Timing remains the declared native reference model;
physical GPU/FIFO/raster timing and hardware parity are unproven.

Validation so far: 38 GPU controller tests pass, including all 256 mode encodings,
refusal and reentry boundaries. Three source tests execute the actual display
instructions and cache copy, cover four refusal prefixes, and reject compiled
store mutations; their Clang ASan/UBSan probe passes. The fresh full regression
passes 957 tests plus 92 subtests in 521.76 seconds. All 22 graphics tests, 11
headless tests and symbol checks pass. The earlier shared fixture initializer
failure is fixed; its failure log remains in the previous checkpoint. Independent
source/owner/native review accepts this bounded display milestone.

Current native evidence: `artifacts/spu-startup-20260906/native/gpu-registers-final/`.
Aggregate verification: `artifacts/spu-startup-20260906/gpu-register-verification.json`.
The immutable `gpu-display-verification.json` retains the prior GP1(05) refusal;
`draw-verification.json` retains the earlier no-DMA boundary. Existing 18 source
streams remain 1147 EXE/assembly-exact exported words (1047 mapped). These exports
do not claim newly matched C, full BIOS parity or a complete decompilation.

Next: execute the real BIOS exception-prefix patch and FlushCache publication,
then initialize persistent CP0/GTE state through source instructions. The
source audit identifies 108 additional words and 12 CTC2 writes; these are not
yet bound. See `gpu-gte-init-source-plan.md` and the CP0 startup authority follow-up
in the artifact directory. The audit corrects the earlier short plan: controls
21–23 are far color, not background color. Enabled scanout, actual title/menu
assets and primitive rendering, launcher and human acceptance remain open.

Mounted native startup now completes the full sound initializer and returns
naturally to `800101FC`, then enters file loading at `8001971C`. All fourteen
remaining sound/TOC streams (575 words) are mapped and byte-checked against the
pinned EXE. Actual GetTN/GetTD commands read all four mounted disc tracks; source
code writes the complete track table and restores its caller stack. Xenogears'
MIT PsyCross ADSR helper supplies configurable envelopes in the shared SPU owner.
Actual mode4 reverb feedback RAM writes are observed at the final sound return.

The sound checkpoint passes707 pytest tests plus84 subtests,21 graphics and10
headless CTests. Its final trace checker passes108 focused tests, including
malformed logs, packet/order/deadline mutations and missing real reverb work.
See `artifacts/spu-startup-20260906/outer-verification.json`. The retained binary
below belongs to that checkpoint; the working build continues beyond it.

```sh
python3 tools/native_spu_boot_check.py --sound-outer \
    artifacts/spu-startup-20260906/native/sound-outer-native-boot \
    extracted/disc/files/SLUS_007.26 extracted/disc/disc.cue extracted/disc/disc.bin
```

The retained sound checkpoint is historical; the current draw continuation
stops at BIOS/GTE initialization `80053CF8` after DMA2 and display setup. Progressive VSync remains conditional on actual source/GPU
state, while the interlaced `XOR` path is unsupported. Hardware timing/DSP
rounding, audible game sound and the menu remain unproven.

**Menu: NOT_REACHED.** The four-input check below opens the pinned disc and real
SDL audio owner; the one-EXE diagnostic intentionally retains its unmounted
`80044DBC` boundary.

In the preceding CD phase, the game issues `01`, `0A`, `0C`, drains four source
CD interrupts, and publishes
main gains `7FFE`, CD gains `3FFF`, and control `C001`. CD, SPU, Timer1, Timer2,
SIO and real VBlank events share the existing device epoch; reads do not create
time. Full digital input callbacks now return through their source, with
256-byte BIOS scratch `[84D8,85D8)`, eight return records and a counted
16384-instruction limit. The cold fixture covers three rejected `43` attempts
and recovery; native polling is timing-dependent and is not that fixture trace.

Validation: 633 pytest tests plus 84 subtests, 21 graphics CTests, 10 headless
CTests and symbol checks pass. The final SPU checker additionally passes 32
focused tests after its hardening; the historical CD checkpoint retains its own
580-test evidence. Four new
compiled digital-IRQ mutants and a live SDL no-queue mutant are rejected.
Real PipeWire queue/drain proves output progress; audible game sound, complete
BIOS/hardware cycle parity, the menu and launcher acceptance remain unproven.
The shared SPU now performs two actual 16-byte uploads, clocks all 24 voices,
registers DMA callback `3AA18` once, and opens/enables one polling event. Final
main gains are `7FFE`, CD gains are zero, and control is `C000`; sound RAM at
`1000..100F` contains the source-uploaded `07` bytes. The bounded synthesis subset
supports filter 0 and active ADSR `0000/0000`, with explicit refusals outside it.
Manual writes commit actual RAM synchronously under the documented native
policy; physical FIFO/key subphase and measured Gaussian rounding remain unproven.
The next common-attribute call also returns naturally at `8002C974`: seven actual
register writes restore CD gains `3FFF` and control `C001`, retaining the event
and uploaded RAM. Its live checker and 47 focused tests pass; evidence is sealed
in `artifacts/spu-startup-20260906/common-verification.json`. Reverb processing
and CD track-table commands remain ahead in the outer sound initializer.

Historical common-attribute checkpoint command (requires its earlier binary):
```sh
python3 tools/native_spu_boot_check.py --common-attributes \
    build/menu-resume/musashi_native_boot extracted/disc/files/SLUS_007.26 \
    extracted/disc/disc.cue extracted/disc/disc.bin
```

See [menu continuation](MENU-BOOT-CONTINUATION.md) and
`artifacts/cd-native-owner-20260906/` for source authority, logs and reviews.

The native targets are **diagnostics and an asset viewer**, not a bootable game.
Matched fixed-point functions are linked, but `pc_port/main.c` does not execute
the retail startup loop. Native startup executes through CD initialization; later game startup and
scene dependencies remain incomplete.

The earlier Checkpoint P viewer pause (2026-09-03) is historical. The current
priority is genuine native menu boot: see `tasks/plan.md` and the freshly
byte-checked [entry contract](NATIVE-BOOT-ENTRY.md). The viewer/GTE checks do
not establish native boot, a visible retail menu, or launcher acceptance.

What runs, all from the repository root after `./tools/run_tests.sh`:

```sh
# link check, pure fixed-point maths, no display needed
build/musashi_pc_smoke

# pinned EXE entry-data setup only; explicit synthetic return address, no game boot
build/musashi_boot_data_probe extracted/disc/files/SLUS_007.26

# one quad, verified by reading the framebuffer back
xvfb-run -a build/musashi_render_quad --screenshot quad.bmp

# any TMD model from the disc, auto-fitted, spun for N frames
xvfb-run -a build/musashi_render_tmd extracted/models/sc01_a97000.tmd \
    --frames 12 --screenshot model.bmp
```

Drop `xvfb-run -a` to see the window on a desktop. To find more models:

```sh
python3 tools/extract_cd.py  extracted/disc/files/SC01.CD --output extracted/sc01
python3 tools/extract_pac.py extracted/disc/files/SC01.CD --output extracted/pac/sc01
python3 tools/find_tmd.py    extracted/pac/sc01/0037_000.bin
python3 tools/extract_tmd.py extracted/pac/sc01/0037_000.bin --offset 0x87F0 \
    --output extracted/models/some_model.tmd
```

What does not run:

- **Integrated game execution.** The native executable does not run the game
  main loop or its input, audio, and scene paths.
- **Textured or per-vertex-coloured models.** The viewer and `tools/tmd.py`
  refuse them rather than draw garbage. Viewer expansion is not the current
  menu-boot milestone.
- **Lighting.** Every polygon takes its authored colour, flat.
- **Windows.** The roadmap names a Windows port. It is unverified: no MinGW
  cross-compiler is installed on the development machine, so nothing here
  has ever been built for it. Do not read "Linux and Windows" elsewhere in the
  docs as a claim that the Windows path works.

The smoke target verifies the link with pure fixed-point maths, so it runs
headless — requiring a window, display and GPU would make the build check
unusable in CI:

```text
musashi_pc_smoke: psyq rcos(0)=4096 rsin(0)=0 rcos(1/4)=0 rsin(1/4)=4096
```

Those values were read off the built library rather than assumed. PS1 fixed
point uses 4096 for 1.0 and 4096 units for a full turn, so cosine falls to zero
exactly where sine reaches one.

## Shipping model: run off the disc (2026-09-28)

The distributed port contains only our code (decompiled game C, our own
runtime/HLE, the native-lane glue) and open-source dependencies. Everything
from the game (code words, overlays, tables, assets) is read from the
user's own SLUS-00726 disc image at run time, or extracted from it in a
local, gitignored step (`extracted/`). Nothing retail is committed or built
into a binary.

- **Instruction fetch.** `formatter_fetch`, `input_callback_word` and
  `cd_irq_word` admit a PC by address range and read the word from guest
  RAM: the EXE that `musashi_boot_map_exe` mapped (SHA-256 checked) and the
  overlays the guest loaded from the disc. The 3,649 `k*Words` arrays keep
  only their shape (`static const uint32_t kX[N];`, zero, never read for
  words); 489,496 retail words left the build.
- **Overlay identity.** SC01/SC02 member selection compares an FNV-1a 64
  digest of the loaded 178-word entry span with the carved member's digest.
- **CMake** no longer extracts anything at configure time (the 17k-line
  `extract_asm_words.py` block and the SC01.CD member extraction are gone,
  and the tool itself is deleted); no `*_words.inc` exists. `pc_port/800cf02c_overlay_words.inc` is deleted.
- **First-run disc check.** A wrong or missing EXE prints which release is
  needed (Brave Fencer Musashi (USA), SLUS-00726), the expected SHA-256
  (`provenance/exe_identity.json`) and what was found; a wrong disc image
  (cue SHA-256 and bin size, `pc_port/disc_media.c`) gets the matching
  message. The original `... rejected before startup` lines remain.
- **Tests.** Probes that seed synthetic RAM fetch main-exe words from the
  developer's own extract through `MUSASHI_CODE_IMAGE` (size and digest
  checked; set by `tools/run_tests.sh`, CTest and `tests/conftest.py`).
  The shipped port never sets it. The shared helper regenerates
  `<addr>_words.inc` from the local EXE using
  `tests/data/formatter_word_ranges.txt` (addresses and counts only), and
  mutation tests push edits through `_sync_code_image`
  (`MUSASHI_CODE_IMAGE_MUTANT=1`, test-only).

No retail words remain in the port sources:
- **Site admission** (merge/retire, GTE, DIV/SRAV and the other per-site
  tables, 1,685 rows; 73 pc-bound guards) keeps only PCs plus a site-group
  id. `code_site(pc, group, instruction)` admits an instruction only if it
  equals the word the verified image holds at that PC *and* the group's
  FNV-1a 64 digest (over the image words at the group's PCs) matches, so
  a different overlay member or a changed word refuses exactly as the old
  word comparison did. 203 groups (image set x 256-byte window) over
  1,470 PCs, verified on every lookup (at most 64 words).
- **Generic checks** (a call to a named target, `jr`/`jalr`, `syscall`,
  a few load/store forms) use MIPS field encoders (`MIPS_JAL`, `MIPS_I`...).
- The 16-entry **switch jump-table copy** (8003C5EC/8003C6B4) is case-body
  arithmetic; member **signatures** in native_boot and the SC01/SC02
  identity are digests.
- **bios_exception.c** carries no BIOS code: the handler/continuation come
  from the user's ROM when supplied, else zero stand-ins (exception entry is
  serviced natively and never executes them); the game's own PATCHGTE
  handler is recognised by digest.
- CTest `retail_content_guard` runs `tools/retail_guard.py` over the shipped
  outputs (native boot, port libraries, lane archive) against the local EXE
  and every MAIN.CD member; it passes with the lane off and on and skips
  without a disc.

## PsyQ exclusion (2026-09-28)

No Sony PsyQ code is compiled or linked into the port: neither SDK objects
nor decompiled PsyQ C. `config/psyq_ranges.txt` lists the 71 PsyQ code
ranges of the EXE (snd*, lib*, apicard*, gap segments of the vendor splat
layout; the `sgap_*` islands are game code), built by
`tools/psyq_manifest.py build`. CMake runs `tools/psyq_manifest.py check`
over every `src/main` source of every target and fails configuration on any
PsyQ-range source, except the bindings listed in
`config/psyq_pending_replacement.txt`. That list started with the 19
decompiled-PsyQ bindings the port used to compile (libetc callback/IRQ/DMA,
libgpu device ops, libc2 console/string, libmcrd VBlank) and is now
**empty**: every one is replaced by port-owned HLE (table below), and
`tests/test_psyq_exclusion.py` fails if one returns or the list grows.

Replacement status (each entry: what now provides it):

| PsyQ entry | service | provider |
| --- | --- | --- |
| 80042C64 | libetc word clear | port HLE `musashi_hle_clear_words` |
| 8005C29C | libc2 memset | port HLE `musashi_hle_fill_bytes` |
| 8005CCC8 | libc2 memchr | port HLE `musashi_hle_find_byte` |
| 80043060 | libetc word exchange | port HLE `musashi_hle_exchange_word` |
| 8005C388 / 8005CD48 | libc2 TTY putchar / puts | port HLE `musashi_hle_console_putc/puts` |
| 800425B0 / 800425E0 | libetc InterruptCallback / DMACallback veneers | port HLE `pc_port/libetc_irq_veneer.c`, `libetc_dma_veneer.c` |
| 800429DC | libetc InterruptCallback registration (I_MASK, BIOS ChangeClearPad/RCnt) | port HLE `pc_port/libetc_irq_registration.c` |
| 80042F8C | libetc DMACallback registration (DICR) | port HLE `pc_port/libetc_dma_registration.c` |
| 80042CE8 | libetc VSync callback service | port HLE `pc_port/libetc_vsync_handler.c` |
| 80042E08 | libetc DMA interrupt service (DICR drain) | port HLE `pc_port/libetc_dma_irq.c` |
| 800427F4 | libetc interrupt service (I_STAT dispatch, storm guard, RFE) | port HLE `pc_port/libetc_irq_dispatch.c` |
| 8005BD7C (+800426FC) | libgpu graphics reset (I_MASK exchange, GPU DMA/GP1 reset) | port HLE `pc_port/device_reset_bindings.c` |
| 8005C1C0 | libgpu GPU-type probe (GP1 10000007h) | port HLE `pc_port/gpu_bindings.c` |
| 800616D0 8006291C 80062988 | libmcrd memory-card VBlank service (request stack step, idle query, completion) | port HLE `pc_port/libmcrd_vblank.c` |

The HLE (`pc_port/include/musashi_psyq_hle.h`, header-only) is written from
the documented C-library/libetc behaviour, not from PsyQ code. The native lane never
admits a PsyQ-range function: **469 of 655 game functions** are admitted
(240 PsyQ candidates excluded); interpreted game code still runs the PsyQ
words the user's own disc provides, which ship with nothing.

## Third-party components and licenses

| component | use | license |
| --- | --- | --- |
| PsyCross (REDRIVER2 project), `tools/third_party/psycross` | PsyQ API layer, GPU/GTE/audio backends | MIT |
| SDL2 | window, input, audio | zlib |
| OpenAL Soft | audio (PsyCross) | LGPL-2.1 (dynamic) |
| OpenGL / Mesa | rendering | system library |
| OpenSSL libcrypto | SHA-256 identity checks | Apache-2.0 |
| mkpsxiso (dumpsxiso), `tools/third_party/mkpsxiso` | local disc extraction tool, not linked | GPL-2.0 |
| clang/LLVM | native-lane compiler (build tool) | Apache-2.0 with LLVM exception |
| decompals old-gcc, maspsx, m2c | matching toolchain (dev only, not linked) | GPL / MIT / GPL |

## Platform layer wiring (2026-09-28)

`bfm_plat` (pc_port/platform, from port/platform-layer; design in
docs/ARCHITECTURE-PORT.md) is a CMake target linked into
`musashi_native_boot` (`MUSASHI_WITH_BFM_PLAT`). At startup, after the EXE
identity check, native_boot brings it up with the `null` renderer, audio
and input backends (native_boot still owns PsyCross/SDL until the game-facing
calls are routed through `bfm_plat_*`), binds guest RAM and, during a hook,
the live guest registers, and registers the known hook sites (12 today).
The formatter reports every guest function entry (a `jal`/`jalr` target,
after its delay slot, before the native lane) through
`musashi_formatter_set_entry_hook`, which calls `bfm_plat_hooks_at` for
registered sites, and reports the selected overlay member (`SC02_031`,
`SC01_000/001`, `MAIN_0004/0007/0010`) through
`musashi_formatter_set_overlay_hook` → `bfm_plat_hooks_set_overlay`.
`MUSASHI_BFM_PLAT=0` turns the layer off.

Routed through `bfm_plat_*` (opt-in; the defaults keep native_boot's own paths):

| switch | effect |
|---|---|
| `MUSASHI_GPU=bfm_plat` | GP0/GP1 are teed into port-plat's GPU bridge and drawn by `MUSASHI_RENDERER` (default `psycross`); the controller keeps FIFO/DMA/GPUSTAT with its rasteriser off. The controller's backend is the bridge (VRAM/env/display applied there, reads via `bfm_gpu_bridge_read_vram`); present follows the bridge's GP1 03 state. The splash fades in correctly headless. |
| `MUSASHI_INPUT=bfm_plat` | the SIO digital pad samples `bfm_plat_input_pad(0)` from the backend `MUSASHI_INPUT_BACKEND` (default `sdl`, bindings from `[input]`; `null` is the scripted pad mods drive). Polled once per VBlank, where native_boot already pumps SDL events. `MUSASHI_HOLD_*`/scripted test overrides still apply on top. |
| `MUSASHI_AUDIO=bfm_plat` | the SPU core's 44.1 kHz stereo output goes to `bfm_plat_audio` (backend `MUSASHI_AUDIO_BACKEND`, default `openal`; bfm_plat falls back to `null` when the device cannot open, and says so). The null backend drains at the platform clock's audio rate. STOP prints `AUDIO_SINK frames= nonsilent= peak=` and `SPU_KEYON writes=`. Headless: OpenAL Soft's `wave` driver (`ALSOFT_CONF` with `drivers=wave`) records the device output. Default audio (`MUSASHI_AUDIO` unset: SDL; `null`) is unchanged. |
| `MUSASHI_TIMING=bfm_plat` | with `MUSASHI_GUEST_CLOCK=1` only: each guest VBlank calls `bfm_plat_timing_vsync(1)` (NTSC field pacing, fast-forward, `BFM_EVENT_VSYNC` for mods) in place of `guest_clock_wait`'s throttle; `MUSASHI_UNTHROTTLED` runs uncapped and still emits the events. Refused under host-paced time, which host time already paces. |

## Port Dev Menu (opt-in, off by default)

`BFM_DEV_MENU=1` (or `--dev-menu`) turns it on; without it nothing in
`pc_port/dev_menu.c` runs. F1, or L3+R3 (or Guide) on a controller, opens
it; Up/Down (d-pad, left stick, arrows) move, Cross/A or Enter/C select,
Circle/B or Esc/Backspace go back. It is drawn over the presented frame with
the built-in 5x7 font (`musashi_gpu_psycross_set_overlay`, scissored GL
clears; VRAM is not touched), and while it is open the game reads a neutral
pad. "DEV" stays in the corner while any cheat is on.

| page | entries |
|---|---|
| Cheats | Infinite HP (current 80078EB4 = max 80078EB2, the game's refill in func_8014BC44), Infinite BP (80078EB8 = 80078EB6), Max Drans (80078E8C = 99999, the game's cap). Applied once per VBlank, only after the first scene is reached. |
| Warps | Only warps tested working in the port are listed (`kWarps`). None yet: the port does not reach player control. |

`BFM_DEV_MENU_KEYS="frame:KEY,..."` (TOGGLE, UP, DOWN, OK, BACK) drives it
headless. The model test is `tests/dev_menu_probe.c` (ctest `dev_menu_model`).

## Debug forcing (divergences, off by default)

native_boot writes no guest RAM behind the game's back by default. The
following pokes run only with `MUSASHI_DEBUG_FORCE=1`, print a
`DIVERGENCE` line when applied, and all act only after `SCENE1_REACHED`:

| poke | what it bypasses |
|---|---|
| player `+0x4D` 2 -> 1 (with auto-dismiss, or after the fade) | the game's own control unlock after the opening dialogue |
| `0x800B99F0` = 9 | the game-mode selector reaching gameplay by itself |
| player `+0xA9` = 0x41 | the pad type the game copies every frame from `D_80078D98[port]` (`func_80148648` <- `func_80018C64` <- the pad buffer's ID byte, only when its status byte is 0) |

A headless no-force boot reaches the title (8 `TITLE_LOAD`s, no
refusals); gameplay, where the pokes would act, is not yet reached
headless, so what each one covered for is not yet observed.
`MUSASHI_GAMEPLAY_ACTION` scripted inputs are separate opt-in test drivers:
they write pad words (player `+0xAA/AC/AE`, `80078DCA/DD2`) and `800AE658`
straight into guest RAM in gameplay, and print a `DIVERGENCE` line when set.

What the `+0xA9` poke covered for is unproven until a gameplay run. The
game writes 0x41 itself when the port-1 pad record takes the buffer's ID
byte (status 0, PadGetState reporting a pad). The removed non-retail
`8005F704` PadInfo getter differed from the retail code only for port 2
(`a0 & 0xF0` nonzero), so it does not by itself explain a wrong port-1
type; the proof is a lane-on gameplay run without forcing where `+0xA9`
reads 0x41 from the game.

## Native lane: running matched decomp C on the host (2026-09-28)

Goal: the port executes byte-matched decomp C (`src/main/<vram>.c`, the
`#else` branch) instead of word-stepping the retail MIPS for those
functions, without losing the interpreter as the fallback.

### Executed-path tracer

`MUSASHI_TRACE_FUNCS=<path>` makes the formatter record every guest
function entry it takes: `jal`/`jalr` targets (`j`/`r`) and router entries
(`d`: run_startup_cpu, nested source execution, CD IRQ, BIOS event and
fixed-entry routers). `<path>` gets one line per first-seen entry as it
happens (`seq target kind caller`, flushed, so a killed run keeps it);
`<path>.counts` gets every entry with its call count, first kind/caller and
whether the lane ran it (`native`/`interp`), sorted by count, at exit. With
the variable unset the cost is one predictable branch per `jal`, and guest
state is never touched (the heap-continuation probe's byte-identical-CPU
assertion holds with tracing on).

`tools/native_lane_gen.py --only <path>.counts` restricts the lane to the
traced PCs, so integration can follow what a boot really runs. **Not yet
done on this box:** the live native boot needs an X display (PsyCross opens
a GL window; there is no xvfb here), so only the headless probes have been
traced (`entry_cpu`, `heap_continuation`, `callback_registration`).

### Generic native lane

`formatter_step` consults one guest-PC keyed table (`musashi_native_lane.h`)
before it fetches a word, so the startup loop, the IRQ routers and nested
source execution all take the same lane. An entry runs only at a clean call
boundary (no pending delayed load or LWL/LWR merge, not in a delay slot,
sequential npc) and only when guest RAM is host-mapped at `0x80000000`;
otherwise the interpreter steps the words exactly as before. The adapter is
the o32 register ABI: a0-a3, further arguments at guest `sp+0x10`, v0/v1,
return to ra. Each entry charges `2 x words` cycles to the execution clock
once per call (PCSX-Redux's BIAS=2 without the memory surcharge): an
estimate, since a native loop has no per-iteration cost.

`musashi_native_guest_call` is the interpreter thunk for unported callees:
it runs the callee on the *same* `FormatterCpu`, re-entering the enclosing
`run_startup_cpu` loop when there is one (so host-handled PCs, checkpoints
and IRQ delivery behave as for interpreted code, and the checkpoint sees
the same continuation identity), with the 16-byte o32 home area and stack
arguments written below the caller's guest sp. A refusal inside the callee,
or a host stop request, `longjmp`s back to the lane and refuses the step;
guest writes the native code already made are not undone, which is the same
fail-closed stop the interpreter gives.

The existing bespoke bindings (`MUSASHI_NATIVE_*` defines) are unchanged;
their files are excluded from the table.

**Host-sequenced PCs stay interpreted.** native_boot drives startup and
observes the game at specific guest PCs (`run_startup_cpu_loop`'s
observer list, the formatter's `cpu->pc ==` gates and named ranges such as
`STARTUP_ENTRY`). A function whose body holds one of those PCs is refused
by the generator (`hooks` rule, which resolves enum names too), and PsyQ
entries whose body holds one are listed in `musashi_native_lane_hooked_psyq`
and skipped. The IRQ dispatcher suspends the lane
(`musashi_native_lane_suspend`), so IRQ callbacks are always interpreted.

**PsyQ entries (port-plat's compat table).** `pc_port/native_lane_psyq.c`
installs the non-STUB rows of `pc_port/platform/psyq/bfm_psyq_compat.def`
next to the generated entries and binds guest RAM, the scratchpad and the
GTE bridge (`musashi_gte_owner_native_begin/_end`, which take the cop2 lease
for the call). `MUSASHI_PSYQ_LANE` selects the classes:

| value | installed |
|---|---|
| unset, `pure` | libc, packet helpers, OT clear, CD position maths, GTE-state libgte |
| `all` | pure + VSync, DrawSync, DrawOTag, Load/Store/MoveImage, Put{Draw,Disp}Env, SetDispMask, CdSearchFile, putchar; only with `MUSASHI_GPU=bfm_plat` (otherwise pure) |
| `0` | none |

Rows the differential probe found to differ from the retail code are held
back by PC (`kHeldBack`, with the reason; empty since port-plat 7935600a2). A wrapper that refuses leaves the
interpreter to run the retail code. ratan2 and RotMatrix* are exact HLE over
the game's own tables in guest RAM, not PsyCross's copies: those equal the
game's data and would put retail bytes in the binary (retail_content_guard).

Bisecting: `MUSASHI_NATIVE_LANE_RANGE=lo-hi` (hex, hi exclusive) keeps only
those guest PCs, and `MUSASHI_NATIVE_LANE_DENY=pc,pc,...` drops PCs. The
tracer (`MUSASHI_TRACE_FUNCS`) marks each entry `native` or `interp`.

### Memory model: guest RAM at host 0x80000000 (design (a))

Two designs were evaluated:

- **(a)** Map guest RAM at host virtual `0x80000000` on the 64-bit host and
  admit only functions an automatic checker proves pointer-width safe.
- **(b)** A 32-bit host build (`-m32`) where all decomp C has PS1 data
  layout. This needs i386 SDL2/OpenAL/GL (the owner would install them), a
  32-bit PsyCross build, and it still needs the guest RAM window, because
  host addresses of host objects are no more guest addresses at 32 bits.

**Decision: (a).** Measured on the current tree, (b) would only rescue the
`layout` rejects (25 functions using structs or arrays holding pointers).
Every other rejection below is about the host not being a PS1 (inline MIPS
register asm, MMIO through pointer cells, IRQ-dependent polling, host stack
locals whose address reaches guest code, trap-on-divide, port hooks) and
applies to a 32-bit build equally. (a) runs today with the libraries the
port already uses, and the checker makes the admitted set a proof rather
than a guess.

How (a) works:

- `musashi_native_boot` (built with `MUSASHI_NATIVE_LANE`) `mmap`s its boot
  owner with `MAP_FIXED_NOREPLACE` at `0x80000000`; guest RAM is its first
  member (static-asserted), so `memory.bytes == 0x80000000`. If the window is
  taken or `MUSASHI_NATIVE_LANE=0`, the static owner is used and the lane
  stays off.
- The generator compiles each candidate with clang for x86-64
  (`-fwrapv -fno-strict-aliasing -ftrivial-auto-var-init=zero`), and before
  optimisation rewrites every guest data symbol (`D_XXXXXXXX`, asm-label
  aliases such as `"D_80078E7C+1"`, named symbols from `config/*sym*.txt`)
  into its absolute guest address, every address-taken guest function into
  its guest code address, every pointer-typed load/store to non-local memory
  into a 32-bit access (guest pointer cells are 32-bit), and every
  int-to-pointer conversion into a zero extension of the 32-bit value.
- Candidates: registry entries in `main` with recovery `c` whose source has a
  native word export (so the interpreter can always run them) and no bespoke
  binding: 870 today.
- Rules, each rejection naming the first that failed (`report.json`):
  `source` (no `long`/`sizeof`), `compile`, `symbols` (resolve into
  `[0x80010000,0x80200000)`; no mutable TU data), `layout` (no pointer-bearing
  aggregate or pointer-element indexing, checked before optimisation, when
  struct GEPs are still typed), `memory` (no pointer load/store left to guest
  memory, no atomics, no volatile: decomp C marks device access volatile),
  `escape` (no host stack/data address stored, converted, returned or passed
  to guest code), `inttoptr`, `calls` (only direct calls; no inline asm, no
  indirect calls), `arith` (no variable or trapping divisor, variable shifts
  masked to 5 bits), `ub` (no undef/poison/unreachable), `irq` (no loop that
  neither stores nor calls), `abi` (8/16/32-bit and pointer parameters; a
  direct call may not pass fewer arguments than the callee reads, because a
  MIPS callee then sees whatever the caller left in a1-a3), `hooks` (no PC
  the port compares against — pc/ra/continuation checks — inside the
  function, since native execution would bypass the hook).
- Guest calls from native code go through per-signature stubs that convert
  each value to and from the 32-bit register the MIPS ABI carries (so
  mismatched prototypes behave as on the PS1): a direct call when the callee
  is admitted, otherwise the interpreter thunk.

Current tree: **603 of 870 admitted**; rejected hooks 84, compile 52 (MIPS
register asm and similar), escape 44, layout 25, calls 21, memory 13,
arith 9, irq 8, ub 4, abi 3, inttoptr 2, source 2. Stubs: 179 direct,
270 thunks.

Known model gaps (documented, not checked): native code cannot reach the
RAM mirrors (KUSEG `0x00000000`, KSEG1 `0xA0000000`, the 2-8 MB repeats)
or the scratchpad, and its address arithmetic does not wrap at 32 bits; a
wild or mirror pointer faults the host instead of aliasing. A native call
runs without IRQ delivery or per-instruction clock charging between its
own instructions (IRQs are delivered in thunked callees and after return).

### Verification

`native_lane_differential` (CTest, built with `MUSASHI_NATIVE_LANE=ON`)
maps guest RAM at `0x80000000`, loads the pinned EXE, and for every entry
and three argument patterns runs the function on the interpreter only and
then through the lane (forked child each), comparing v0 and all guest RAM
outside the callee's frame. Result on this tree: **1297 comparable cases
identical, 0 different, 0 native failures; 500 of 603 functions verified**.
381 cases were not comparable (the interpreter itself refused: a device,
BIOS call or unsupported pointer), 129 hit the mirror gap and 2 the
wraparound gap, both from the probe's deliberately absurd arguments. The
probe found the two real hazards the `memory`/volatile and `abi`/arity rules
now close (`func_8003B08C` writes DMA through the pointer cell `D_8006B560`;
`func_80046ABC` passes 1 of 3 arguments to `func_80046CFC`).
`tests/test_native_lane.py` pins those decisions.

Build it with `-DMUSASHI_NATIVE_LANE=ON` (needs clang). The option stays
**OFF** until the live boot (`native_startup_boundary`, needs xvfb) has
been run with it on.

Gate on this branch (2026-09-28): `tools/run_tests.sh` with the user-local
dependency prefix (`CMAKE_PREFIX_PATH=~/opt/host-port-deps/usr:...`):
**561 tests OK (2 skipped)**, 22/22 CTests (8 display-dependent skips),
port symbol check and smoke clean, exit 0. Without that prefix the only
error is the known `test_native_smoke` configure failure (no system
SDL2/OpenAL/GL). With `MUSASHI_NATIVE_LANE=ON`, 23/23 CTests including
`native_lane_differential`.

## Building

Native builds also require OpenSSL development headers and `libcrypto` for
the entry-data loader's pinned-image check. CMake uses `OpenSSL::Crypto`;
no TLS component is linked. When using Homebrew graphics dependencies, pass
`-DOPENSSL_ROOT_DIR=/home/linuxbrew/.linuxbrew` in a fresh build directory to
keep the dependency prefix consistent. Existing cached `OPENSSL_INCLUDE_DIR`
and `OPENSSL_CRYPTO_LIBRARY` selections must also point to that installation.

```sh
brew install sdl2 openal-soft     # or the system equivalents
./tools/fetch_toolchains.sh       # vendors PsyCross among the rest
./tools/run_tests.sh
```

`run_tests.sh` adds Homebrew to `CMAKE_PREFIX_PATH` when present; override with
`MUSASHI_CMAKE_PREFIX`. Configure with `-DMUSASHI_WITH_PSYCROSS=OFF` to build
the port without SDL2 or OpenAL at all — that path is verified and keeps the
decomp tooling usable on a machine with no graphics stack.

## What PsyCross actually provides

Measured from the built archive, not from its README: **399** C-linkage
functions, including `ResetGraph`, `DrawOTag`, `ClearOTagR`, `PutDrawEnv`,
`VSync`, `InitGeom`, `RotMatrix` and `RotTransPers`, plus 51 SPU, 20 CD and 19
pad entry points.

**There is no `libgs`.** Zero `Gs*` symbols are exported. This confirms
empirically what was previously only a research claim, and it matters: Brave
Fencer Musashi is a 3D game and will need TMD rendering. No permissively
licensed `libgs` exists anywhere. The options are to build on the
ordering-table level `libgpu` primitives that PsyCross does provide, to treat
the GPL-3.0 implementation in the Silent Hill decomp as a specification to study
rather than copy, or to accept GPL-3.0 for the port. **That is a licensing
decision, not a technical one, and it should be made before any TMD code is
written.**

## Upstream defects worked around

The build owns its integration rather than using PsyCross's own
`CMakeLists.txt`, which is only a source glob and two `find_package` calls. Four
problems had to be handled, all invisible to upstream because it is built with
MSVC and premake:

1. **The Psy-Q layer is nearly lost to a case-sensitive glob.** `src/psx` holds
   the implementations — `LIBGPU.C`, `LIBGTE.C`, `LIBCD.C` and eight more — in
   files named with an uppercase `.C`. A `*.c` glob silently skips all eleven on
   Linux, and the library still *builds and links*; it simply exports none of the
   Psy-Q API. Upstream's own `CMakeLists.txt` has this bug. A configure-time
   check now fails loudly if `LIBGPU.C` is not picked up, because the failure
   mode is silent and the symptom appears much later.
2. **GCC treats `.C` as C++.** The language is forced back to C so these symbols
   stay unmangled and callable from the decomp's C.
3. **`find_package(OPENAL)` in upper case** only resolves on case-insensitive
   filesystems, and upstream links a bare `OpenAL` target that modern config
   packages do not export. Handled by finding and linking the real targets.
4. **`include/psx/strings.h` shadows glibc's.** It is a Psy-Q compatibility stub
   pulling in `string.h` and `ctype.h`, and being on the `-I` path it wins over
   the system header, so `strcasecmp` is never declared on Linux even though
   `PsyX_main.cpp` includes `<strings.h>`. `pc_port/compat/psycross_compat.h`
   declares the prototype directly and is force-included into PsyCross sources
   only.

## Known hazard: 64-bit pointer truncation

`LIBETC.C` returns a callback pointer as an `int`:

```c
int old = (int)vsync_callback;
```

Harmless on the 32-bit PS1, lossy on x86-64. GCC 14+ makes it an error, which is
downgraded so the build completes, **but the truncation is real**: the handle
returned by `ResetCallback` and `VSyncCallback` must not be cast back to a
pointer on a 64-bit host. Anything relying on those return values needs fixing
before it can be trusted.

The VBlank scheduler is the only reviewed registration exception: it calls
`VSyncCallback` to install or remove its single callback, discards the returned
value, refuses to replace an existing callback, and never restores one. The
archive checker still rejects both APIs by default; its
`--allow-vsync-discard` switch is required explicitly by the native test
runner and leaves `ResetCallback` forbidden. That exception proves only that
the unsafe return is ignored, not that a host VBlank or game boot occurred.

`pc_port/irq_controller.c` supplies the native `I_STAT`/`I_MASK` storage used
by the display-backed scheduler probes. VBlank raises bit zero, the recovered
dispatcher acknowledges it with the retail mask write, and unknown addresses
are refused. This removes test-local IRQ register semantics; cold-start
ownership and general guest callback execution are still unfinished.

The native cleanup boundary is split across `pc_port/bios_events.c` and
`pc_port/bios_kernel.c`. They own a 22-record event pool and priority registry,
create the CD session through five class `F0000003` polling events plus IO/DMA
nodes, and remove only those owned resources at A0:72. The run closes five
handles, unlinks two nodes, preserves game RAM, and leaves
`bios_event_used=0` and `bios_cd_installed=0`. The actual CD handler is still
unsupported and refuses closed-loop execution if reached.

SYS(2) enables guest IRQ eligibility after cleanup. `pc_port/irq_scheduler.c`
keeps PsyCross's worker as a VBlank queue producer.
The owner pump consumes and coalesces pending edges at live FormatterCpu
instruction/call checkpoints, then runs the recovered dispatcher and consumes
the B0:17 continuation. The worker does not write guest RAM or hold a broad
host mutex across guest execution. The earlier cleanup gate reported guest IRQ
enabled with zero cold-prefix deliveries; the current StartCARD check reports
guest IRQ enabled with no fault. A separate live fixture covers scheduler
delivery. Production HBlank input and GPU_cw remain open. The optional serial
integration uses a dedicated 112-byte BIOS scratch frame `[8568,85D8)`, executes
five exact SIO configuration writes around a paced Timer2 sample before refusing
`F830` at `8005DB04` when only 16-bit providers are supplied. The current
byte-enabled metadata fixture instead reaches `8005DC04 -> 8005EAE8`;
`read16` does not advance Timer2. It is not a
natural controller transaction. The source ledger accounts for startup/input
instructions while Timer2 and bound SIO advance from actual host-time cuts;
CD and video deadlines remain disconnected. The source ledger reaches `950315`;
the earlier source-only Timer2 count `8039` is historical. Current Timer2 counts
vary with elapsed host time; retail phase parity is unproven. See
`artifacts/cd-init-device-20260905/contract.md` for the
source-authoritative three-boot CD contract.

The current native SIO owner is bound to digital port 1 and an SDL keyboard
provider on the same paced epoch. Startup reports `keyboard_polls=0` and no
DATA-pad packet, so provider binding is not natural input proof. Six metadata
edges are mapped, four new metadata arrays are integrated, and the legacy
16-bit path remains refused at `8005DB04`. The selected-device identity fix
and focused probe pass; the synthetic fixture reaches `DC04`/`EAE8` refusal
with metadata stores. See
`artifacts/controller-protocol-20260905/integration-contract.md` for the
bounded retail and native contract.

The graphics gate reports `GPUREAD=00000400` and query-7 leaves `GPUREAD=400`;
the physical GPU revision response is unproven. Blank-only presentation is
available while geometry, VRAM transfer, enabled scanout, and production HBlank
input remain unsupported. The latest validation is **557 passed plus 84
subtests** and **21/21 CTest tests**, with graphics/headless builds, archive
symbol verification, focused NTSC/live checks, and diff checks passing. The
Timer2 owner is integrated under a paced host-time device epoch with variable
phase; six additional Timer2 focused tests pass, including the large-
`UINT64_MAX` case. The native final check still stops at the CD byte boundary.
The independent source check covers 26 newer exports and 1,528 exact words;
24 of these exports remain source-only, with two used by the metadata stage. This source proof is separate
from native mapped/runtime proof.
Source exports include **121/66**
word-exact event-registration/helper exports and eight five-word callback exports; **40/28/37** remains
historical.

The native continuation CPU comparison covers all 32 GPRs plus HI/LO at five
interim boundaries. The historical StartCARD comparison reports 13/15/14 GPR differences at
ENTRY/RETURN/NEXT; the selected frame agrees except RETURN `a0` (native `1`,
retail `2`, following the SYS2 source value). Full CPU parity is unestablished;
the native initial GPR/HI/LO profile and incoming RA are zeroed, not
retail-seeded.

The cleanup validation is deliberately scoped to owned state: five CD records
(140 bytes) and all 22 status words match before and after cleanup. The full
616-byte initial table does not match because retail unused slots retain prior
BIOS payloads and stale stack words; native startup does not seed those values.
Separate allocation serials/leases detect replacement of owned slots while
preserving guest handles and the B0 low-16-bit alias. The full-table comparison
in `tests/test_bios_events.py` remains a same-input cleanup test; the kernel
test's negative control records why it is not initial-RAM parity.

The preceding event milestone prepared native input C0 kernel ownership. The eight
event records match retail's first five words and the registration `v0=1` is a
source comparison literal for a void path, not successful API proof. BU state
matches; CPU comparison has 15/12 GPR differences and HI/LO differences while
selected `v0/gp/sp/fp/ra` match. The eight callback flag stores pass focused executor tests; their delivery
was not observed during this short boot window. Menu, hardware, full BIOS cold-boot, cycle, low-RAM,
and whole-CPU parity remain unproven.

## Rendering: one quad, verified headless

P1 draws a red `POLY_F4` on a blue field through the Psy-Q layer and verifies
it in CI with no display. `tools/render_check.py` runs `musashi_render_quad`
under `xvfb-run` with Mesa's llvmpipe, reads the pixels it reports, and judges
them with `tools/vram_pixel.py`. Sampled: inside `rgb5=(30,0,0)`, outside
`rgb5=(0,0,31)`. Without `xvfb-run` the ctest is reported as **skipped**, never
as passed.

`--screenshot PATH` writes the frame as a BMP for looking at with human eyes.
It is off by default, so the automated check stays a pixel comparison rather
than something that quietly depends on file output.

Verified on real hardware as well as software: on Mesa Intel (ARL) the sampled
values are identical to llvmpipe's, so the CI result is not an artefact of the
software renderer.

Getting a single quad on screen took a long chain of measurements, and several
of my own intermediate conclusions were wrong. They are corrected here because
the wrong ones were plausible.

### Root cause: two consumer-side defines

**`USE_EXTENDED_PRIM_POINTERS` must be defined by the consumer.** The
primitive-tag macros (`setaddr`, `getaddr`, `nextPrim`, `isendprim`) are gated
on it — *not* on the 64-bit check that sizes the tag struct. Without it, on
x86-64, a 12-byte tag holding a `uintptr_t` is filled by macros that truncate
every pointer to 32 bits and test for a 24-bit terminator. PsyCross never
defines it; upstream sets it in premake. It is now PUBLIC on `psycross_static`
because the macros expand in our code too.

**`USE_PGXP` defaults to that same value, and must be held off for now.** With
PGXP on, `VERTTYPE` is a 16-bit `half` float under C++ and a `short` in C.
Same byte layout, different type: the shorts our C writes are reinterpreted as
float16 denormals, so `80` becomes ~5e-6 and every vertex normalises to
exactly `-0.5` — two degenerate triangles that rasterise nothing, with no error
anywhere. PsyCross's `_HF()` macro exists precisely to convert in C mode. PGXP
stays off until the port has real GTE transforms to feed it.

### Verification must read framebuffer 0, not VRAM

`GR_ReadVRAM` can never observe a rendered frame. Its backing array is uploaded
*to* the GPU as texture source; both places that copy GL output toward it pass
`update_vram=0`. And `GR_SaveVRAM`'s `bReadFromFrameBuffer` argument is unused —
it dumps that same array. An earlier note here called an all-black `GR_SaveVRAM`
capture "decisive" evidence that rendering failed. It was not evidence of
anything; it only showed the CPU array was empty, which it always is.

The render target therefore reads framebuffer 0 with `glReadPixels`, fetched
through `SDL_GL_GetProcAddress`, after `DrawSync` and before `PsyX_EndScene`
presents the frame, and packs the 8-bit sample the way VRAM stores it so the
harness judges one format.

### A measurement trap

`DrawOTag` calls `DrawAllSplits` internally, which ends in `ClearSplits()`.
Reading `g_splitIndex` after `DrawOTag` therefore always shows zero, and an
earlier note here concluded from that reading that the ordering-table walk was
broken — a second, separate fault. It was never broken. Measured correctly, by
calling `ParsePrimitivesLinkedList` directly before anything draws, the walk
produces one split and six vertices, identical to the single-primitive path.
There was one fault, not two.

### Colours round-trip lossily

8-bit colour becomes 5-bit VRAM and comes back through GL: red `248` sampled
as `241`. The judge asks for the expected channel to dominate and the others to
stay near zero, in 5-bit units, rather than for an exact value.

## A real model renders, without libgs

`musashi_render_tmd` loads a model extracted from `SC01.CD`, transforms it
through the GTE and draws it as ordinary `libgpu` primitives. ctest verifies it
headlessly alongside the quad. The target at `0xA97000` is a hexagonal column,
18 primitives, drawn in its authored beige `(240,197,152)`; sampled inside as
rgb5 `(29,24,18)` against an expected `(30,24,19)`.

This is the `libgs`-free path the project chose. No permissively licensed
`libgs` exists, so the model is walked here and emitted as `POLY_G3` and
`POLY_G4` into an ordering table, which is what `libgs` would have done
internally.

**Lighting is not set up.** Every vertex takes its primitive's own colour, so
the model draws flat rather than shaded. That is enough to prove geometry
reaches the screen, which is what this target exists for.

**`tools/tmd.py` is the tested reference for the format**, and refuses the
variants it does not read. The C reader in the port covers the same subset and
refuses the same cases, rather than drawing garbage from a packet shape it does
not understand.

### The 64-bit width hazard, a third time

`RotTransPers` takes an `int*` for the packed screen coordinate, not a `long*`.
Using a `long` on x86-64 writes eight bytes where four are expected and
corrupts the neighbouring entry. This is the same class of bug as the ordering
table being `OT_TAG[]` rather than `u_long[]`. Screen coordinates are now
unpacked explicitly into `setXY3`/`setXY4` instead of being punned through a
pointer.

### Judging arbitrary colours

The pixel judge originally asked for one dominant channel and near-zero others.
That suits a red quad on blue and is wrong for anything else: this model's
beige has three substantial channels, and the judge rejected a correct frame.
It is now a per-channel tolerance of 3 in 5-bit units, which covers both the
primaries and authored colours without special-casing either.

### Ordering tables must not be declared `u_long`

Found while getting this far, and it applies to all ported code.

PS1 source declares an ordering table as `u_long ot[N]`, because a tag was
exactly one 32-bit word. On x86-64 an `OT_TAG` is **12 bytes**: `DECLARE_P_ADDR`
carries a `uintptr_t` plus bitfields, and `P_LEN` becomes 3. `u_long` is 8.

So `u_long ot[8]` hands `ClearOTagR` a 64-byte buffer for 96 bytes of writes.
It corrupts memory and crashes. Every ordering table in ported code must be
declared `OT_TAG[]`, casting at the `ClearOTagR` and `DrawOTag` call sites,
which take `u_long *`. This is the "structs containing pointers change size on
64-bit" hazard in its most concrete form.

### Ordering table direction

`ClearOTagR` chains `ot[i]` to `ot[i-1]`, making `ot[0]` the terminator and
`ot[n-1]` the head. Drawing with `DrawOTag(ot)` walks nothing and fails
silently.

### Two inconsistent VRAM packings in PsyCross

The framebuffer path packs a pixel as `r | (g << 5) | (b << 10) | (a << 15)`
with 5-bit components, which is the PS1 format and what `tools/vram_pixel.py`
implements. `GR_ClearVRAM` instead writes `r | (g << 5) | (b << 11)` with
*unmasked 8-bit* components, so clearing to red 255 yields `0x00FF` rather than
`0x001F`. Read-back of rendered content follows the first; do not calibrate
against `GR_ClearVRAM`.

### GPU seam decisions: integer commit, depth policy, reference hierarchy

Decided 2026-09-18 from reading the vendored backend, not from experimenting.
These pin the seam between decomp-owned geometry and PsyCross so later
graphics work does not re-litigate them.

**Vertices commit as integers; subpixel is opt-in per vertex, never ambient.**
`MakeVertexTriangle/Quad` (`tools/third_party/psycross/src/gpu/PsyX_GPU.cpp`)
read integer packet shorts, add the float drawing offset, and store into
`GrVertex.x/y`, which are `short` while `USE_PGXP=0` (forced in `CMakeLists.txt`).
The only subpixel path is `ApplyVertexPGXP` overriding `x/y/z` from the PGXP
cache on a per-vertex hit, falling back to `z = 0, scr_h = 0` on a miss. So the
integer packet is the commit point today, and turning PGXP on later must go
through the `_HF()` conversion hazard already recorded above — nothing may
reinterpret C-written shorts as `half` implicitly.

**Depth is the OT order, not a Z-buffer; keep it that way until PGXP earns an
exception.** The PS1 has no depth buffer, and the backend agrees by
construction: without PGXP every vertex takes the constant-`z` ortho path
(`Projection * vec4(xy, 0.5, 1.0)` in `PsyX_render.cpp`), so `GL_LEQUAL` passes
in draw order and the painter's algorithm *is* the depth system. Two
consequences. First, the opaque/depth-on vs blended/depth-off split in
`GR_EnableDepth` is harmless while `z` is constant and must be re-audited the
moment real `z` flows, because it changes blended-vs-opaque interleave versus
retail. Second, `ParsePrimitivesLinkedList` deliberately does not capture the
OT bucket index — draw order is preserved, bucket numbers are dropped. If depth
synthesis from OT position is ever wanted, it needs a new hook there; do not
smuggle it in through `z` without deciding the fight policy for PGXP-`z` vs
OT order first.

**Reference hierarchy: retail framebuffer first, N64 art intent second,
synthetic pixels always.** The standing gate stays the synthetic pixel tests
(quad/TMD under `xvfb-run`, judged by `tools/vram_pixel.py` at tolerance 3 in
5-bit units). The next rung is retail framebuffer comparison once the boot
reaches a stable scene: same frame number, emulator VRAM dump as ground truth.
The N64 version of the game is art-intent reference only — different renderer,
different hardware, never pixel truth. PGXP stays off until (a) real GTE
transforms feed the cache on the path being tested, (b) the `_HF()` boundary
is converted, and (c) the `z`-vs-OT policy above is written down.

## Vendor matched intake (cleaned)

Raw Druthulu matched sources live under `vendor/bfm-decomp/` (see `VENDOR.md`).
The PC port does **not** compile those giant TUs directly.

Smoke-matched symbols (`func_80012AB0`, `func_80012E6C`, `func_8001311C`,
`func_80013154`) link from cleaned house-style files:

- `src/main/80012ab0.c`
- `src/main/80012e6c.c`
- `src/main/8001311c.c`
- `src/main/80013154.c`

Local cleaned forms win over vendor `__asm__("$0")` / dump style. Cleanup rules:
`docs/VENDOR-CLEANUP.md`.

```sh
# default: cleaned src/main leaves (same as before)
cmake -S . -B build
cmake --build build --target musashi_pc_smoke

# vendor-aware configure: asserts vendor/bfm-decomp exists; still links cleaned leaves
cmake -S . -B build -DMUSASHI_USE_VENDOR_BFM=ON
cmake --build build --target musashi_pc_smoke
ctest --test-dir build -R musashi_pc_smoke --output-on-failure
```

`musashi_native_boot` remains on the address-file / word-array interpreter path for
most of the EXE; only the smoke-matched leaves above are compiled as host C today.
Widening intake means cleaning more vendor functions into `src/main/800*.c` and
adding them to CMake — not flipping on all of `vendor/bfm-decomp/src/`.
