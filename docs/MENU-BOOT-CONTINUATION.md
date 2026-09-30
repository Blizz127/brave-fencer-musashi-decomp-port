# Native menu continuation — 2026-09-08

## Start-screen visual result

Run68 naturally reaches the rendered start screen. The desktop capture
`artifacts/bfm-takeover-20260907/boot-68/window-13.png` shows the retail logo,
**PUSH START BUTTON**, and **© 1998 SQUARE**. The 1280x960 image is centered,
with equal top/bottom bars and no bottom crop. No guest PC, menu flag, or Start
input was forced to obtain that capture. This establishes visible start-screen
output, not gameplay, physical timing parity, or attract-mode completion.

The natural idle transition subsequently refuses at unexported800CF300. The
explicit `tools/run_native_menu_preview.sh` launcher is intended to stop host
execution after natural start-screen frames and retain that rendered window for
inspection. Its window title labels the paused preview; close the window to exit.
It uses the existing `build/menu-takeover` binary and local extracted retail media.
A normal launch without `MUSASHI_PAUSE_AT_START_SCREEN=1` remains unpaused.

The final focused suite passes57 tests. Run70 exposed a further host catch-up
race: the previous bound delivered a sector one cycle before its successor's
deadline. ReadS now cuts at the delivered sector's own deadline so its callback
has the full guest transfer period. The scheduler regression checks this explicitly.
Run72 verifies the persistent paused preview: the naturally rendered start
screen is held after34 menu iterations while the source's blinking prompt is
visible. The live desktop capture is
`artifacts/bfm-takeover-20260907/boot-72/start-screen-centered.png`.
The window is labeled "Brave Fencer Musashi - start screen preview (paused)".
It is left open for inspection; closing it ends the preview. No guest state was
changed to choose the visible blink phase. The final suite passes57 tests.

## Earlier continuation — 2026-09-08, runs 40–58

The start menu remains **NOT_REACHED**. The old desktop game process is closed.
The source-loaded member-4 route and centered scanout remain in use; no automatic
Start input or host movie-completion flag is enabled.

The CD stream callback and DMA3 completion path now execute their actual retail
instruction exports. DMA byte writes drive the shifted full source register,
DMA3 sync-mode-0 ignores BCR's upper count, and the observed chopped mode consumes
one word per native deadline with the configured CPU gaps. Completion follows
actual FIFO consumption. The source header and rectangle copies now support their
checked LWL/LWR/SWL/SWR sequences, including interleaved load-delay retirement.

Mode-C0 XA sectors are decoded to PCM and routed through the CD volume matrix into
the SPU queue, without publishing a data INT1. Four real sectors match FFmpeg's
37,800 Hz ADPCM output exactly. The documented resampler has an independent
convolution check; physical initial phase and exact hardware rounding remain
unverified. The combined XA/CD tests pass five cases.

Run49 reaches the movie VLC decompressor. Trapping ADD now checks signed overflow;
its SUB sites retain overflow refusal. The exact movie COP0 read/write sites are
admitted, and their SwC bit is retained by the status owner. PSX-SPX documents no
observable PSX cache effect for SwC. The CPU checks pass eight tests.

Run52 reaches MDEC command 3A001280. The new color decoder consumes actual RLE
parameters, uploaded quantization and scale tables, performs integer IDCT and
YUV conversion, and provides reordered color pixels to DMA1. The native model
buffers a complete input command, caps output at 1024 macroblocks, and uses
1+word-count transfer deadlines. It does not claim physical bus timing or exact
hardware numeric rounding. Monochrome and direct CPU output reads remain refused.
Tests verify DC quadrant placement, real deferred DMA output, and all 64 positive
and negative IDCT basis vectors against an independent cosine transform.

Run55 completes 86 MDEC transfers and captures real movie imagery in
`boot-55/frame.png` (converted losslessly from the native framebuffer PPM).
A live-window capture raced process exit, so this image is framebuffer evidence,
not a fresh desktop screenshot. Rectangle merge tests pass. The stream then stalls
because an acknowledged but ignored data announcement blocks all later INT1s.
ReadS now permits replacement of that announcement while preserving any separately
latched FIFO and outstanding IRQ/response. The regression uses real sector bytes;
three CD sector tests, including semantic mutants, pass. ReadN behavior is retained.

Run57 reaches the nested stream status-worker path after that fix. Run58 admits
its exact five-frame source ancestry; subsequent behavior is pending observation.
Run56's later member-4 helper CFFEC is still unexported and may be encountered
again after movie playback. Continue the natural route and verify the actual menu.

### Streaming timing and visible-output follow-up — runs 59–66

Live movie output is captured in `boot-64/window-3.png`: the 1280x960 window
has equal 40-pixel black bars above and below, with no bottom crop. This is a
fresh desktop capture, superseding the framebuffer-only observation above.

Host catch-up during the CD interrupt caused repeated ready callbacks. The movie
IRQ now limits sector production to instruction cost, while DMA retains its
absolute native clock. An active chopped transfer protects its FIFO until it
finishes. ReadS production also waits when fewer than 2352 pending PCM slots are
available. These are explicit native scheduling choices, not physical timing
parity. The extracted native scheduler test checks pending publication, ignored
ReadS announcements, instruction-cost pacing, active DMA, and PCM backpressure.

A transport underrun can leave silence already rendered by the SPU. Arriving XA
PCM is queued intact at the next available sample and subsequent blocks retain
that contiguous anchor. `XA_UNDERRUN` reports the gap; the queue test verifies no
samples are dropped, ordering is retained, and timestamp overflow is refused.
The audio queue size itself is unchanged. Audibility and audiovisual sync still
need human verification.

The exact nested stream ready-callback guard has positive and negative ancestry,
stack, argument, and live-slot tests. The movie-only stack bound is 0x200 bytes;
other CD callback bounds remain 0x178. Source exports and selectors pass40 tests.
Device checks pass14 tests, and the later PCM queue test passes separately.

Runs63/65 complete361 MDEC transfers, return from the movie, and resume opening
state setup. CFFEC–D0100, CF3E8–CFB3C, and CFC1C–CFE00 are now exported from the
same source-loaded member4 and retain instruction identity checks. Run66 is the
next observation. The start menu is still unverified; continue until it is visible.

## Corrected opening route — 2026-09-07, runs 25–29

The earlier SC01 route below was induced by an incorrect host overlay replacement
and automatic Start input. It is not evidence of reaching the menu. The retail
loader places MAIN.CD member 4 at 800CEDF8; the 9600-byte RAM capture in
`boot-25/overlay-before-host-load.bin` exactly matches member 4 at file offset
0x800. The old host loader overwrote it with member 7, then forced execution
addresses to compensate. Both the replacement and those PC redirects are removed.
Automatic Start is disabled by default. Member 4 now executes directly from its
source-selected instruction exports, checked against the actual loaded RAM.

Run 28 reaches 800D1E9C naturally and refuses the unadmitted routine. The next
exports cover 800D1E9C–800D24D0, including its initialization helpers. New archive
and selector tests verify member identity and every exported source word. The
actual menu remains **NOT_REACHED**. The image-centering fix below remains valid
and visually observed. No conflicting old game process remains running.

Run 31 confirms the next boundary: PC800D2D98 stores 80000000 to
1F801824 (v1), the MDEC reset/control register. The interpreter has no MDEC bus
binding. This is a device implementation gap, not another missing instruction
span. Do not bypass the write or fake movie completion. Source reference:
https://psx-spx.consoledev.net/macroblockdecodermdec/
Next work is source-backed MDEC reset/table/decode and DMA0/1 device support,
followed by natural boot. Added MAIN4 helper exports through D353C and EXE
helpers4657C,46B44,46C38,46D1C remain guarded/source-verified. Focused archive,
selector, source-export and GPU checks pass86 tests. Runs30/31 exit cleanly.

The shared Homebrew SDL2 header links disappeared during this continuation.
The installed sdl2-compat package is intact. The local build cache now points
SDL2_DIR directly to its Cellar/2.32.72/lib/cmake/SDL2; rebuild succeeds without
changing shared package links. Native boundary diagnostics now include v1.

### MDEC setup continuation — runs 32–35

The new MDEC controller accepts real reset, quantization/scale table commands,
and DMA0 request-mode table transfers. It owns the corresponding registers,
consumes actual guest RAM, and raises shared DMA completion only after transfer.
Absolute device time advances completion; polling reads do not advance it.
The 1+words timing is a native transport model, not physical cycle parity.
Decode commands and output DMA remain refused pending their implementation.

Run34 proves two table transfers completed naturally: command60000000,
remaining0, transfers2, control60000000, DMA0idle00000201. Execution then stops
at missing library helper8004689C. The next build includes that helper and its
source-backed callees. MAIN4's actual MDEC-output callback800D24D0 is admitted
on DMAchannel1 only while that overlay is selected. No menu/movie flags are set.
The reset/table/DMA test includes disabled-channel, pending-transfer, overrun,
reset-during-DMA, and unsupported-decode cases. Focused checks pass95 tests and
16 subtests before the latest additional library exports. Actual menu remains
NOT_REACHED. No game process is intentionally left running after diagnostics.

### Streaming CD mode — runs35–37

Run35 reaches Setmode(C0), refused by the A0-only drive. C0 requests double
speed, 2048-byte payloads, and XA audio. The drive now accepts that mode and
returns the actual Mode2 payload at raw offset24. Audio/realtime XA sectors
explicitly refuse until XA decoding is implemented; they never become data or
silently disappear. Source: https://psx-spx.consoledev.net/cdromdrive/

The new real-disc PVD test failed before this change and passes afterward,
including the existing zero-data and request-rewind negative controls. Five
CD/MDEC tests pass. A stale refusal test for Setmode while reading contradicted
the previously implemented LIST-owner behavior; it now checks unsupported C8
(filtered XA) instead. The LIST-owner acceptance test for active Setmode remains.
Run36 proceeds to movie-stream DMA3 callback registration800469CC. The next
build exports that actual handler and permits its registration while MAIN4 is
selected. Actual menu remains NOT_REACHED; MDEC pixel decoding and XA audio
are still implementation work, not external blockers.

### ReadS and paused-buffer restart — runs37–39

ReadS1B now uses the same successful-sector transport as ReadN06, retaining its
command identity for INT3/INT1. Error retry behavior remains unmodeled. The PVD
fixture covers both commands in modeC0. Run38 then proves a second root cause:
the prior paused read retains sector578 with only12 of2340 bytes consumed. A
new ReadS was rejected solely because this old requested view existed.

DuckStation BeginReading/BeginSeeking invokes ClearSectorBuffers. The native
owner now retires old views at new-read ACK, only with verified DMA idle, and
preserves actual consumed-byte counts. A real-disc pause/restart test fails
before and passes after; its busy-DMA negative control preserves the old view.
Source inspected: https://raw.githubusercontent.com/stenzek/duckstation/master/src/core/cdrom.cpp
Five CD/MDEC checks (including two semantic mutants) pass. Run39 now successfully starts ReadS and publishes its first movie data sector
(LBA4657). The next refusal is the real CD-ready callback: IRQ call80045300
target80046610, which calls the already-exported stream handler80046D1C.
Next work: admit the source-backed stream callback and its validated call path
to the guarded CD IRQ executor, then continue through MDEC/XA decoding. No host overlay replacement or automatic Start input.

## Historical induced SC01 route — runs 16–21 (superseded)

Title asset loading completes: run16 reaches `80128158` with no exhausted IRQ
budget. The source file is **SC01.CD**, whose offset `0x800` exactly matches the
PAC header at disc LBA6162. Its compressed first sector at6163 independently
decodes to `artifacts/bfm-takeover-20260907/title-first-decoded.bin` (4516 bytes).
The code resembles MAIN member0012 but is a different overlay: at `801282AC`,
MAIN's word is `8C22E618`, while the live/independently decoded SC01 word is
`8C22EEA0`. New MAIN exports at80128158/80128288 preserve their existing matched
C and are guarded against the loaded RAM words. They execute the common prefix
then correctly refuse at801282AC. These SC01 observations are retained as historical decoding evidence; they do
not identify the natural menu route.

The user-reported image clipping is repaired and visually verified. The intro
uses640x480, but the display descriptor was reading only240 VRAM rows. GP1 mode
bits2+5 now double the field-height range for the full interlaced image. Other
mode combinations retain240 rows; returning to320x240 is covered. Reference:
https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#gp108h-display-mode
This is full-frame host presentation, not proof of physical interlace fidelity.

Before: `boot-18/window.png` shows SQUARESOFT cut off at the bottom. After:
`boot-21/window-08.png` shows the entire centered logo; `window-05.png` shows the
complete publisher card. The logo's nonblack bounds are(304,448)-(978,510) in
1280x960, placing its center at(641,479). Captures were from the actual X11 game
window, not reconstructed VRAM. Window geometry was fully on-screen already.
The optional `MUSASHI_GPU_TRACE_PRIMITIVES` records drawing coordinates and
scanout dimensions; normal runs leave it disabled.

Validation:47 GPU-controller tests pass, including a regression that failed
before the height change.654 focused source/selector/IRQ-progress/GPU tests
pass (`latest-focused.log`). The real-disc LIST fixture, wrong-header negative
control, and nonterminating decoder control pass3 tests (`sector-irq-checks.log`).
Run21 exits at the guarded SC01 mismatch and removes all owned devices.
**Intro centering: OBSERVED. Actual game menu: NOT_REACHED.** No commit or push.

## Takeover checkpoint — 2026-09-07

The previous desktop writer was terminated and its processes verified gone.
The current continuation preserves its uncommitted work. Fresh evidence is in
`artifacts/bfm-takeover-20260907/`; runs 02–06 progressively execute the title
state dispatcher and its loader helpers. The former refused CD edge
`8001A6EC -> 80018714` now executes from pinned source words, followed by the
PAC decoder at `80018730`. A real-disc fixture checks the LBA 6162 PAC header
and all 4516 expanded bytes from LBA 6163 against an independent decoder.
The focused CD fixture passes; three negative controls reject missing destination
publication, an insufficient decoder budget, and a nonterminating copy loop.

Overlay 0007 and 0010 instruction selection refuses unmapped addresses instead
of falling through to overlay 0000. Compiled selector tests cover refusal and
every exported title word against retail bytes. The combined source-export and
selector checks pass **605 tests**. These results do not certify the whole suite.

Run 08 uses a separate RelWithDebInfo build in `build/menu-takeover` and still
retries at expected LBA 6165. Measured long callbacks take roughly 80 ms.
Retail code at `80019F04` retries after 300 incomplete polls; the cause of the
missing sector delivery remains under investigation. No device timing or sector
retirement semantics have been changed in response to this observation.
SIGTERM now requests an orderly diagnostic stop: run 08 reports `HOST_STOP`
and `OWNED_DEVICES removed=1`, with timeout exit 124. Run 09 adds optional
sector/FIFO diagnostics under `MUSASHI_NATIVE_CD_TIMING`.

Run 09 isolates a native scheduling defect: after the decoder claims LBA 6164,
LBA 6165 remains incoming with a pending publication deadline. The incoming
branch serviced only command deadlines, so publication waited until a retry
restored the default callback. The native scheduler now services an eligible
publication deadline through `advance_without_fetch`, preserving occupied-buffer
backpressure and the controller's acknowledgment rules. A compiled regression
using the production scheduling function fails before the change and passes
afterward; it also checks unclaimed announcements and unread IRQ/response state
remain blocked. The direct CD owner and log parser checks pass 19 tests.
Run 10 encounters an independent trailing-INT1 window: source `80044828`
records intended Pause before `8004482C` writes its command register. The INT1
guard now admits command 9 as well as ReadN/Setmode; the real-disc callback
fixture reproduces the former refusal and passes afterward.

Run 11 reaches LBA 6195 and exposes a decoder allowance incorrectly shared by
all sectors in one root IRQ. The 2,097,152 instruction decode limit now begins
at each guarded `80018730` call. Both insufficient-budget and infinite-copy
negative controls still reject. Run 12 then reaches the separate non-decoder
16,384-instruction allowance. The source root loops at `800452C4` to drain
additional sectors. Run 13 tests renewing that allowance only after a guarded
decoder return proves strictly advancing, contiguous output within 2 MiB RAM
and consecutive source sector identities. No-progress loops retain their
existing bound. Compiled progress-guard tests cover stationary/backward output,
repeated/skipped sectors, discontinuous destination and address overflow.

**Latest checkpoint: run 14.** The root reaches the image-upload phase around
LBA 6318, then reports `CD_IRQ_BUDGET exhausted=1` at `8005BBB0` after 9,280,239
instructions. Run 13 stopped at `800423B0` in the same phase; that PC alone did
not prove an unmapped instruction. The explicit budget diagnostic identifies
the remaining IRQ-work allowance, not the per-call PAC decoder limit. Extending
renewal to image uploads requires a validated bounded progress condition; this
checkpoint does not silently raise the general limit.

Latest focused validation: **626 passed** (source exports, overlay selection,
native publication, PAC progress guard, CD owner and log parser). The real-disc
LIST fixture plus insufficient-decoder-budget and infinite-copy controls also
pass **3 tests** in `progress-irq-checks.log`. No full-suite pass is claimed.
Continuation runs 15–16 address the remaining productive IRQ work. The
image allowance renews only after a completed mode-1 callback advances the
64-byte bitmap's one-hot cursor (at most 512 tiles) and consumes a consecutive
sector. Run 15 reaches LBA 6542 in SPU-data mode4, proving image progress beyond
the prior stop. Modes2/3/4 now renew only after completed LIST callbacks show
consecutive expected-sector advancement from the real DMA header comparison.
Stationary/backward/skipped/overflowing identities do not renew. Run16 and the
real-disc LIST/header-corruption/infinite-decode controls are the current checks.


The captured display still reads **NOW LOADING…**. **Menu/logo: NOT_PROVEN.**
The inherited diagnostic runner supplies held R1 and an automatic Start press
during overlay 0007, loads member 0007 through a host helper, and makes its
window nonfocusable. These are unresolved native-launch/input limitations,
not human menu acceptance or full retail boot parity. No commit or push was
made during takeover.

## Current continuation milestone — 2026-09-06

The latest retained native run completes projection, ClearImage, both framebuffer
environments, and the second InitGeom call. It stops at camera setup **800144D4**
(NPC800144D8, RA80014478, SP801FFF90). **Menu: NOT_REACHED; scanout disabled.**

Since the previous CPU/GTE checkpoint, 831 additional retail words were exported
and independently compared against the pinned EXE and assembly. These include
projection and its queue calls, ClearImage, framebuffer constructors, and the
second geometry caller. Existing matched C bodies remain under conditional
exports; no new C match or registry change is claimed. Signed DIV at52868 uses
proven MIPS results and retains the source zero-divisor BREAK refusal.

The actual source ClearImage builds a five-command DMA packet containing GP0(02)
for black RECT(0,0,640,480). The GPU owner consumes the complete three-word fill
before publishing one canonical PsyCross VRAM update. Real readback checks all
307200 pixels with zero mismatches. Controller/backend each record one completed
fill; the final run completes eight DMA transfers and47 payload words without
fault. Partial fills preserve their packet state across DMA boundaries; DMA
readiness uses data readiness, allowing the remaining words while command-ready
is low. FIFO reset, backend refusal and drawing-state independence are covered.

The backend uses the existing PsyCross canonical VRAM copy/read functions. Its
old clear helper mispacks nonblack colors, so the adapter derives PSX RGB555
explicitly and handles wrapping. No vendor renderer code or Xenogears checkout
was changed. Nonblack-to-black content checks belong to the graphics fixture;
the native source itself supplied black, with no host prepaint.

The retail framebuffer constructors complete naturally. AtAF668/AF6C4 the draw
rectangles are(0,10,320,220)/(0,250,320,220), with offsets(160,120)/(160,360).
Texture-window halfwords+10/+12 are256; tpage+14 remains000A. The display
rectangles atAF77C/AF790 are(0,0,320,240)/(0,240,320,240). Whole-RAM fixtures cover
padding, nested stack stores, register preservation, NTSC/PAL and height480.

The second InitGeom enters through14444 and reuses the same CPU Status, BIOS
image, kernel/card hook and GTE bank. Actual publication reaches generation3
and28 patch stores; the GTE owner records20 control transfers. Source Status is
40000401, all logged owners are healthy and all owned teardown succeeds.
The new14454 ancestry is admitted only at the two existing CP0 and seven
InitGeom CTC2 sites; screen-distance4923C accepts its actual14468 caller.
Optional ClearImage diagnostic aliases59844/5986C refuse before clock or effects.

Retained evidence is under `artifacts/spu-startup-20260906/native/`, including
`gpu-fill-integration-1`, `draw-env-integration-1` and `second-geom-integration-1`.
Earlier refused-packet and projection captures remain immutable. The latest
binary SHA256 is40bfde77c707be53a9038ae5b2492713a7dfbbeddc365a38ee621ca1480e2ab5;
log SHA256 is79f05997303a3a9d76bfda25345ea93519d5191f194fcd250cba8840e04e7b03.

Validation: 57 focused GPU/fill/ClearImage/export tests pass; all 22 graphics tests,
11 headless tests and the symbol check pass. The extended persistent second-InitGeom
fixture passes all six cases, including four compiled mutants and ASan/UBSan.
The full Python suite passes 1029 tests and 92 subtests in 786.53 seconds.
All 177 test-file pins and 82 production-file pins remained unchanged during
that run. Aggregate evidence is `artifacts/spu-startup-20260906/gpu-fill-verification.json`.
The earlier CPU/GTE full-run failure remains recorded in its historical
manifest; a focused replacement pass did not retroactively make that run green.

Next is the actual camera initializer144D4 and its source descendants. The
source audit in `second-geom-source-plan.md` identifies a future LZCS/LZCR data
transfer at47D3C; that is not yet executed or supported. The 11-function camera
stage contains 572 independently checked words, outside production.
`gte-data-transfer-plan.md` specifies the signed leading-bit correction and
one-instruction MFC2 load delay needed before admitting those operations. Native timing remains
a reference policy, not hardware parity. Enabled scanout, retail title/menu
assets and primitives, usable launcher and human menu acceptance remain open.

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

The retained sound checkpoint is historical. A later historical draw checkpoint
stopped at BIOS/GTE initialization `80053CF8` after DMA2 and display setup. Progressive VSync remains conditional on actual source/GPU
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

See `artifacts/cd-native-owner-20260906/` for source authority, logs and reviews.

The fresh mounted checker and its 18 focused tests also pass; the CD checkpoint
is sealed in `artifacts/cd-native-owner-20260906/verification.json`. Work on the
next SPU phase is in progress and must not be read as a new native return claim.
The normal cold interpreter capture in `artifacts/spu-startup-20260906/retail-run2/`
returned through both initial SPU calls to `8002C90C`: 727 writes, 82 resolved
reads, 62578 preserved prelude writes, and one polling event registration.
The observer changed no guest state. Its private profile and CPU trace establish
the boot configuration; the retry screenshot was black, so GUI labels were not
visually verified. Hardware transfer/key timing and reference binary/source
build equivalence remain unproven. The native implementation uses explicit
source-backed device policies with real sound RAM and voice evolution.


The preserved `integration/mounted-ready-query.log` records source ledger
`1705913`, epoch `10524729`, 12 actual source edges, 16 IRQ deliveries and seven
keyboard samples. It contains two complete `42` packets and one `43` command;
other transactions time out naturally. These are observations, not quotas or
retail phase equivalence. The CD IRQ uses a 411-word exact source export and live
HookEntryInt-derived game scratch `[8006CA94,8006CAEC)`, independently of the
BIOS input frame and suspended startup CPU. The source four-word `426D4` read
and actual GPUSTAT/Timer1 word reads close the final CD polling dependencies.

The media owner verifies the exact CUE/BIN sizes and SHA-256 before exposing
an immutable private snapshot. The separately owned controller maintains the
response FIFO, internal IF/enable and deadlines; only the CPU IRQ owner latches
mask `0004`. Reset's second phase is scheduled from its source event deadline.
The SPU owner starts from an explicit settled BIOS-main-mute policy and renders
through its owned 44100 Hz stereo SDL sink. It does not implement the full voice,
transfer, ADSR or reverb behavior needed by the next game initialization.

```sh
python3 tools/native_cd_boot_check.py build/menu-resume/musashi_native_boot \
    extracted/disc/files/SLUS_007.26 extracted/disc/disc.cue extracted/disc/disc.bin
```

The checker uses private X11 and the actual user PipeWire runtime. A missing
real audio device is NOT_RUN. This command checks the bounded CD return and
`8002C8F4` stop; it does not certify a menu. Next-source plans are
`artifacts/cd-native-owner-20260906/integration/post-cd-spu-plan.md` and
`artifacts/spu-startup-20260906/capture-plan.md`.

All snapshots below are historical. The current checkpoint above supersedes
their smaller scratch sizes, partial input packets and unmounted CD boundary.

## Prior continuation snapshot

The native entry now executes CRT, constructor, graphics, InitCARD, StartCARD,
backup-unit initialization, eight BIOS event registrations, and input setup on
one source CPU. Input C0:03/C0:02/C0:0A services and the two IRQ-register writes
match the pinned retail observation. The game input descriptor precedes the
actual BIOS SIO owner; its source verifier and handler use a separate bounded
BIOS scratch stack. Unknown child calls retain their effects and refuse.

Input setup returns naturally at `800101E4`. CD initialization then prints
`CD_init:addr=8006cf68`, installs its callback, and stops at the first unbound
CD byte write, `80044DBC -> 1F801800` (bounded status `0`, executable exit `2`).
I_MASK and game enabled bits are `000D`, guest IRQ is enabled, and the input
owner/frame are healthy. The observed five VBlank deliveries preceded input
registration: no natural input IRQ ran in this short native or retail window.
The menu remains **NOT_REACHED**. Full CPU, cycle, low-RAM, hardware, and BIOS
cold-boot parity remain unproven; the executable loads the pinned EXE via host
`fread` and never executes retail-file code bytes.

## Historical advancement

The event milestone added exact 121/66-word registration/helper execution and
eight five-word flag callbacks. The current input milestone adds exact 26/91-word
verifier/handler exports, checked kernel ownership and C0 guest results, and a
112-byte native BIOS scratch frame `[8568,85D8)`. It does not copy a
BIOS RAM snapshot or borrow the interrupted game stack. The matched console
string body is composed into the same CPU's subsequent CD initialization.
No match-registry promotion, commit, or push was performed.

The serial continuation adds eight exact source exports (434 words), with four
mapped into the native formatter. The current native SIO owner now binds the
digital port-1 profile to the paced Timer2/device epoch and an SDL keyboard
provider, but the startup trace has `keyboard_polls=0`; no keyboard sample or
DATA-pad packet was observed. Six metadata edges are mapped, with four new
metadata arrays integrated; remaining exports stay source-only. The legacy
16-bit path still refuses the call to `F830` at `8005DB04`. The newer batch
contains 26 exact exports (1,528 words); only its two metadata bodies
`FB70` and `E228` are newly mapped, alongside the earlier `F830` and `F384` exports. Native boot and the updated live input fixture select NTSC through
`SetVideoMode` before PsyCross creates its worker; its unset default had chosen
PAL cadence. This establishes the native source policy, not retail video-phase
parity. The owner, source-clock, Timer2, source-word and live tests are recorded
in `artifacts/clock-epoch-20260905/integration-contract.md`; four clock
mutations reject lost edges, early waits, lost sticky faults and sequence gaps.

Fresh native observations before shutdown:

| State | Value |
| --- | --- |
| Native stop | status `0`, `pc=80044DBC`, target `80044DBC` (first unbound CD byte write) |
| Callback guard / enabled bits | `0001` / `000D` |
| IRQ slots 0 / 3 | `80042CE8` / `80042E08` |
| I_STAT / I_MASK | `0000` / `000D` |
| DPCR / DICR | `33333B33` / `00000000` |
| DMA2_CHCR | `00000401` |
| Timer 1 stored mode | `0507` |
| Custom VBlank policy selected | yes, by both game BIOS calls |
| BIOS CD installed / allocated events | `0` / `8` at stop |
| Guest IRQ enabled / faulted | `1` / `0` |
| Card state | initialized `1`, started `1`, active `0`, pad started `0` |
| Card hook / maintenance | calls `14` / `5` |
| Card SIO | mode `000D`, baud `0088`, control `0000`, delay calls `3` |
| SIO owner | digital port 1, cards absent, bound `1`, transmitted bytes `2`, keyboard polls `0` |
| Backup-unit state | flags `1111`, action `0`, last port `1`, step `0` |
| Memory-card policy | explicitly disconnected; two `81/FF` no-ACK transactions/timeouts |
| BU directories / status | `960` zero, `40` broken `FF`, status `5` zero |
| CARD lease / cleanup | priority-1 CARD lease gone; card+BU cleanup `1 / 1` |
| Card VBlank / game delivery | `5` edges / `5` IRQ deliveries and ticks |
| Active event records | 8 handles `f1000000..f1000007`, flags zero |
| Input descriptor / next | `80078988` / `000074A8` |
| Input owner/frame faults | `0 / 0` |
| Scheduler removed | `1` |
| First eight GPU writes | exact ordered match to retail |
| Native GPU query state | `GPUREAD=00000400`, query-7 leaves `GPUREAD=400` |
| Historical A0:72 cold-prefix deliveries / tick | **0 / 0**, before this continuation |

SYS(2) enables guest IRQ eligibility after cleanup. The scheduler's PsyCross
worker only queues VBlank edges. The owner pump runs
at live FormatterCpu instruction/call checkpoints, coalesces pending edges,
and invokes the recovered dispatcher with its consumed B0:17 continuation.
Guest RAM worker writes and a broad host interrupt mutex are not used. This
run proves cleanup and the bounded handoff state, **not a delivered IRQ in the
cold prefix**; a separate live fixture proves the scheduler source path.

## Historical device contracts and limits

`pc_port/dma_controller.c` implements the two control registers and DICR's
write-one-to-clear flags and rising-edge IRQ3 assertion. Idle DMA channel-two
`CHCR=00000401` is supported as observed state; actual channel transfers and
other channel-register behavior remain rejected. Enabling DPCR does not
manufacture a transfer.
Contract: [PSX-SPX DMA Channels](https://psx-spx.consoledev.net/dmachannels/).

`pc_port/scanline_timer.c` implements only the observed timer 1 mode: wait for
the first VBlank, then count supplied HBlank edges, with interrupts disabled.
Mode writes reset the count; mode reads clear reached flags. Other modes are
rejected. Its edge tests are synthetic; a production HBlank source is still
missing, and the native owner does not expose this unscheduled counter to
guest reads. Contract: [PSX-SPX Timers](https://psx-spx.consoledev.net/timers/).

`pc_port/irq_policy.c` stores B0:5B/C0:0A acknowledgement choices. Native policy
starts unknown, not as an asserted retail BIOS state. The event provider
refuses custom delivery until both competing VBlank acknowledgements have
explicitly been disabled. This does not implement BIOS pad/timer IRQ handlers.
Contract: [PSX-SPX Kernel](https://psx-spx.consoledev.net/kernelbios/).

`pc_port/bios_events.c` and `pc_port/bios_kernel.c` own the native BIOS event
records and priority registry. The CD session opens five class
`F0000003` polling events, registers IO then DMA nodes, enables them, and A0:72
closes the five owned handles and unlinks the two owned nodes while preserving
game RAM and unrelated registrations. The CD executor is intentionally
unavailable and refuses if actual handler execution is requested; this is a
native service boundary, not whole-BIOS emulation.

## Reproduce earlier boundaries

```sh
cmake -S . -B build/menu-resume \
  -DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew \
  -DOPENSSL_ROOT_DIR=/home/linuxbrew/.linuxbrew
cmake --build build/menu-resume -j 8
python3 tools/native_boot_check.py build/menu-resume/musashi_native_boot \
  extracted/disc/files/SLUS_007.26
```

The check rejects a malformed EXE before display initialization, then runs the
pinned image under Xvfb and requires executable exit 2 with bounded status 0 plus the complete state above,
including `pc=80044dbc`, `target=80044dbc`, `bios_cd_installed=0`,
`bios_event_used=8`, `guest_irq_enabled=1`, `irq_faulted=0`, and the BU/card
cleanup flags. It also requires the per-gate `CPU_TRACE` record: the first entry
into every distinct (call gate, caller) pair, so the caller registers prove the
port reached each recovered gate on its own path. The summary must state
`menu=NOT_REACHED` (the CPU stopped at the refusal) and
`visual_check=REQUIRED` (a human still has to look at the presented frame).
CTest names this `native_startup_boundary`. Passing certifies the documented
refusal, not a bootable game. The executable can also run directly on desktop.

The earlier 457 Python tests and 16 CTest targets are historical verification
for the previous A0:72 boundary. The latest validation is **557 passed plus 84 subtests** and **21/21 CTest
tests**, with graphics/headless builds, archive symbol verification, focused
NTSC/live checks, and diff checks passing. The Timer2 owner is integrated under
a paced host-time device epoch with variable phase; the native final check still
stops at the CD byte boundary. Six additional Timer2 focused tests pass,
including the large-`UINT64_MAX` case.
Source exports include **121/66**
word-exact event-registration/helper exports and eight five-word callback exports; prior **40/28/37**
compiled matches remain historical. These checks certify
bounded startup progress, not a bootable game.

The controller integration contract is pinned in
`artifacts/controller-protocol-20260905/integration-contract.md`. Its retail
contract is digital port 1 (`01`, `42`, responses `41 5A buttons_lo buttons_hi`)
with separate absent-card `81` ownership. The selected-device correction and
focused identity probe pass, but the native run has not sampled a keyboard or
completed a DATA exchange. A synthetic fixture reaches the `DC04`/`EAE8`
refusal and records metadata stores; that is not natural controller or menu
evidence. The final controller suite passes 557 Python tests plus 84 subtests and all
21 CTest checks. Seven compiled callback/SIO mutations are rejected; the
earlier failed fixture run is preserved separately.

Validation is scoped to owned state. The fresh native session matches the five
owned CD records (140 bytes) and all 22 event status words before and after
cleanup. It does not match the full 616-byte initial event table: retail unused
slots retain prior BIOS payloads and stale stack words, which are deliberately
not seeded into the native runtime. `allocation_serial` metadata and leases
detect replacement of a CD-owned slot without changing guest handles or the
B0 low-16-bit alias. `tests/test_bios_events.py` still compares the full 616
bytes for cleanup from an identical captured input; `tests/test_bios_kernel.py`
checks the native owned-record/status result and the negative full-table
mismatch. The preserved `artifacts/bios-remove-20260905/kernel-full-table-mismatch.log`
is a failed validation attempt, not full BIOS initial-RAM parity.

The graphics continuation is recorded in
`artifacts/gpu-startup-20260905/retail-run1/manifest.json`, with native output
in `artifacts/gpu-startup-20260905/native-first.log`. The authoritative
GPU-state range `80072740..80074750` matches retail after at 8208 bytes;
CPU stack/kernel and unrelated coldboot state are excluded. Physical GPU
revision response is unproven. Blank-only presentation is available; geometry,
VRAM transfer, and enabled scanout remain unsupported. The separately tracked
reset-owned clears `80078874..80078974` (256 bytes) and
`800C5510..800C6D10` (6144 bytes), plus queue scalars `8007288C/90/9C`, also
match the native capture; the broad 8208-byte range is not the entire GPU
working-set authority.

The entry continuation CPU comparison is recorded in
`artifacts/entry-continuation-20260905/cpu-comparison.json`; it compares raw
32-register arrays at five interim boundaries. The new comparison reports
13/15/14 GPR differences at ENTRY/RETURN/NEXT; the selected frame agrees
except RETURN `a0` (native `1`, retail `2`, following the SYS2 source value).
Full CPU parity is unestablished. The native initial GPR/HI/LO profile and
incoming RA are zeroed, rather than retail-seeded. Differences outside the
selected call/frame comparison have no automatic ABI-clobber causality.

The preceding event check reached InitCARD, StartCARD, `_bu_init`, and the eight event
Open/Enable/Test operations, then stops at `8005D8D8 -> 8005E178`. The eight
active callback records and zero flags match retail's first five words; the
registration return `v0=1` is a source comparison literal for a void path, not
successful API proof. BU state remains matched, with two disconnected-card transactions completed
by VBlank timeouts and five deliveries/ticks in the observed run. The CPU comparison has 15/12 GPR
differences at ENTRY/RETURN and HI/LO differences; selected
`v0/gp/sp/fp/ra` match. Full CPU, cycle, low-RAM, menu, or hardware parity
remains unproven.
The preceding event log and raw retail comparison are
`artifacts/backup-events-20260905/native-check.log` and
`artifacts/backup-events-20260905/comparison.json`. All 24 operation arguments
and results, eight handles/flags, and five words of all eight active records
match; unused event records are excluded. The synthetic CPU probe also covers
24 refusal positions and a full pool. Three compiled negative mutations fail
those checks. These fixtures are separate from runtime observation.

The latest input evidence is `artifacts/input-kernel-20260905/native-check.log`
and `comparison.json`. All 14 logged input RAM words, three service arguments
and results, and both MMIO writes match raw retail evidence at `800101E4`.
The input CPU comparison reports 9/13 GPR differences at ENTRY/RETURN and
HI/LO differences; selected `v0/gp/sp/fp/ra` agree. Downstream CD execution is
outside that retail capture. Dedicated-frame tests verify masked/disabled
branches, normal guest-zero returns, partial child-call refusal, scratch guards,
reentry refusal and retained faults. Three compiled input-frame mutations are
rejected. The separate `input_irq_live` fixture uses real PsyCross edges: a disabled
input handler returns guest zero, then BIOS SIO/custom dispatch advances tick1;
the next enabled handler retains its writes. The integrated optional serial path
now reaches the exact SIO writes and the paced Timer2 owner, driven by checked
source advance and actual host-time device cuts, before the legacy 16-bit-only call refusal at `8005DB04 -> F830`.
With byte providers, metadata execution now reaches `8005DC04 -> 8005EAE8`;
`read16` does not advance Timer2. The source ledger reaches `950315`; the paced
host device epoch advances from actual host-time cuts with variable phase.
The earlier source-only Timer2 count `8039` is historical; current counts vary
with elapsed host time. This does not establish retail phase parity, a natural controller transaction or a
completed input IRQ. `8005FBC8` remains mapped only for isolated tests, and
`8005DBD8` first refuses at its state-table indirect call (`8005DC04`). The
source ledger accounts for startup/input instructions while the paced device
epoch advances Timer2 and the bound native SIO device from host-time cuts; CD and video deadlines remain
disconnected. The current CD controller remains an unbound,
synthetic-tested model with caller-supplied drive status/cycles.

## Earlier next-work snapshot

1. Implement the CD byte-register/device path at `80044DBC`, preserving command,
   interrupt and data semantics; the source-authoritative three-boot contract is
   `artifacts/cd-init-device-20260905/contract.md`.
2. Exercise the bound digital SIO owner through a natural keyboard/DATA-pad
   transaction, then continue the remaining CD command/data path. The current
   synthetic metadata fixture is not a natural controller transaction.
3. Supply production HBlank/GPU and SPU timing/ownership, then continue CD
   loading, menu overlays, and interactive presentation. None is menu,
   hardware, whole-BIOS, or whole-CPU parity proof.

### Retail A0:72 trace correction and bounded findings

This is a diagnostic metadata correction. The verified retail call sites are
`jal 800427CC -> 8005CE50` (A0:72, with `ra=800427D4`),
`jal 800427BC -> 80042DB8` (channel installer), and
`jal 800427D4 -> 8005CF18` (SYS(2), ExitCriticalSection). Earlier call-site
labels in continuation notes and ignored logs remain historical evidence; the
source-owned instruction streams were already retail-exact and are unchanged.

The read-only interpreter trace in
`artifacts/bios-remove-20260905/retail-run3/observer.log` (parent exit 0),
driven by [`tools/retail_bios_remove_observe.lua`](../tools/retail_bios_remove_observe.lua),
shows the selected BIOS path closing events `f1000000` through `f1000004` and
removing the priority-zero nodes `a00091d0` and `a00091e0`. Its live C0:03
target is `00001444`, read from slot `80000680`; this path avoids the
stack-load branches at `000014A4` and `000014B8`. SR is `40000000` before and
after A0:72, then `40000401` after SYS(2). The trace changed 63 RAM bytes and
performed zero MMIO writes. The next GPU command is `00072740`, with
`ra=800592DC` at the correctly identified call `800592D4 -> 8005CE38`.
These observations qualify this BIOS/path only; they do not establish generic
dequeue correctness or native BIOS parity.

Reproduce with the observer's required environment variable and a fresh
private PCSX-Redux profile:

```sh
MUSASHI_BIOS_OBSERVE_DIR=artifacts/bios-remove-20260905/retail-run3 \
  $PCSX_REDUX_APPRUN \
  --portable "$(mktemp -d)" --cli --testmode --interpreter --debugger \
  --bios $BIOS_DIR/scph5500.bin \
  --iso $REPO/extracted/disc/disc.cue \
  --dofile $REPO/tools/retail_bios_remove_observe.lua \
  --run
```

The runner still uses bounded diagnostic CPU frames and explicit native-zero
incoming RA. It is not whole-CPU cold boot or BIOS parity. Do not replay an
initializer after partial failure without reloading entry state.

Pre-edit hashes, source snapshots, and run logs are local-only under
`artifacts/menu-resume-20260905/`. No commit or push was made.

## Start-screen continuation export — 2026-09-08

The observed MAIN4 idle dispatch at 800CF300 now has a guarded native export
through its return delay slot at 800CF36C. Its 28 words are checked against
MAIN.CD member4. The function calls 800CF3E8, subtracts four from 800EC68C,
and, once negative, clears a 640x480 rectangle through 80059888 and advances
state through 800118AC. This is an instruction export, not a C MATCH claim.

800118AC now has a compiled implementation matching all 124 retail bytes with
GCC 2.7.2 / ASPSX 2.56, -O0 -G0. A global GCC register binding retains the
retail base-address evaluation without adding a local stack slot. The registry
conservatively classifies this as `mixed`; no emitted assembly instructions or
retail-word macros are used in its implementation branch. The installed source
was rebuilt and passed the matching oracle. Evidence candidate:
`artifacts/start-screen-continuation-20260908/800118ac.bin`.

The fresh native build initially failed because 43 existing required export
inputs were absent and three implementation files lacked their native export
branches. These inputs were restored from `asm/main.s`, with every restored
word compared against the extracted EXE. Existing C bodies were preserved.
No new C recovery credit is claimed for those restored exports. The complete
`build/menu-takeover` build then succeeded; focused source/identity/progress
checks passed 673 tests and four subtests.

Fresh natural execution used no automatic Start, no held R1, and no preview
pause. Display/audio setup failures were resolved using Xvfb and the user's
PulseAudio runtime. The actual guest run completed 338 MDEC transfers, then
refused at 80046968, before proving the new CF300 continuation. The source at
80046964 clears CD data request (1F801803=0) while chopped DMA3 is still busy;
the next advance reports DMA3 fault1. The diagnostic shows CHCR01400100,
BCR00000066, FIFO requested0/cursor0, and due597493005 versus cycle597492991.
Investigate source DMA cancellation/request gating before changing this device
behavior. Do not bypass the write, force a menu state, or call CF300 runtime
verified. Full runtime log: `artifacts/start-screen-continuation-20260908/runtime-stdout.log`.
The start-screen continuation goal remains active. No commit or push.

The broad `python3 -m pytest tests -q` run remains in progress (PID2545038,
unified exec session77193, log `/tmp/musashi-cf300-tests.log`). Its progress
already includes a failure; inspect the completed traceback before claiming
suite success. Re-poll this handle or verify the PID before restarting tests.
`git diff --check` also reports an unrelated existing blank line at EOF in
`src/overlays/main_0012/80128228.c`; this continuation did not edit that file.

### DREQ pause repair and post-fade continuation

The CD device now exposes checked request state separately from FIFO byte
availability. An in-flight chopped DMA3 pauses on deasserted BFRD/DREQ, retains
its remaining MADR/BCR and actual copied bytes, and raises no completion IRQ.
Reassertion resumes at the newly observed native cycle without spending the
paused interval as transfer credit. Invalid callback results and timestamp
overflow still refuse. A waiting transfer no longer blocks CD command progress
in the native scheduler. Atomic transfer behavior is unchanged.

Reference comparison: DuckStation `CDROM::UpdateStatusRegister` drives
`DMA::SetRequest` from BFRD; `DMA::CanTransferChannel` gates on request. This
supports explicit request gating, not a claim of physical chopping/bus timing.
https://raw.githubusercontent.com/stenzek/duckstation/master/src/core/cdrom.cpp
https://raw.githubusercontent.com/stenzek/duckstation/master/src/core/dma.cpp

The regression failed before the repair. Synthetic pause/resume, invalid
request, overflow, actual disc-backed FIFO rewind/resume, and native scheduler
checks pass. No fabricated bytes or completion flags are used.

Natural unpaused execution now passes 800CF300 and refuses at 800CF370:
`artifacts/start-screen-continuation-20260908/dreq/runtime.log`. After exporting
800CF370 through its complete return delay slot, a second natural run passes
that routine and refuses at **800CF3A4**:
`artifacts/start-screen-continuation-20260908/cf370/runtime.log`. DMA3 is
fault-free in both final snapshots. No Start/R1 was injected and no guest PC
or menu state was edited. These Xvfb runs establish continuation execution,
not a fresh human desktop acceptance or gameplay/attract completion.

The actual CF300 callee 80059888 (`ClearImage`) is now ordinary C, registered
as a complete 148-byte function. Unsigned-byte RGB arguments reproduce retail
scheduling. The installed source independently matches 37/37 words with
GCC2.7.2/ASPSX2.56 -O2 -G0; candidate is
`artifacts/start-screen-continuation-20260908/80059888.bin`. Its neighboring
function starts at 8005991C; the old assembly label coalesces it, but the new
registry extent ends at the inspected first return delay slot. Parallel C
recovery continues on actual CF370 callee 800146B0; it is currently only an
instruction export, not a registered C match.

Latest build succeeds; 598 focused export/device/scheduler tests pass.
Both new natural runs finish 361 MDEC transfers before the later overlay boundary.
The earlier broad test run was deliberately interrupted after source changes
and confirmed fixture failures (6 failed, 229 passed, 72 subtests). It is no
longer live. Failures included stale SDL include metadata, missing callback
source linkage in fixtures, and one generated-include race during a source edit.
The audio test now passes using the current CMake SDL SDK. CD fixtures now
link their real required callback implementations with the proper native
compile definitions. The corrected integration run is live as PID2662002,
unified exec session42499, log `/tmp/musashi-start-continuation-integration.log`.
Poll it before rerunning or editing formatter includes. Full-suite success is
not claimed. Next natural boundary remains 800CF3A4. No commit or push.

800146B0 has now been installed and independently rebuilt: MATCH49/49 words,
196 bytes, GCC2.7.2/ASPSX2.56 -O2 -G0 with its actual link address800146B0.
Its C uses two empty compiler barriers and a register-bound zero argument;
these constraints emit no instructions. The registry records the classifier's
actual recovery category. Candidate evidence is
`artifacts/start-screen-continuation-20260908/800146b0.bin`. It loads the selected
display-mode record, configures dimensions, resets graphics and invokes the
source clear/setup helpers. Parallel recovery moves to800183E0, called by the
observed pending CF3A4 continuation. The integration handle above is still live;
formatter include changes remain pending its completion to avoid a test snapshot
race. No whole-engine or gameplay recovery claim.

800183E0 is also installed and independently rebuilt: MATCH28/28 words,
112 bytes at its actual link address800183E0, GCC2.7.2/ASPSX2.56 -O2 -G0.
Empty constraints preserve the original base-relative access shape without
emitting instructions. It reserves the first inactive slot among32 entries,
stores the supplied word and clears one state byte; it has no callees.
Candidate: `artifacts/start-screen-continuation-20260908/800183e0.bin`.
The 17-word CF3A4 export is checked against MAIN4 and added to CMake; its
formatter selector is still pending the current integration run. Adding a
source export alone is not a runtime-continuation claim.

The corrected integration run has completed successfully: **15 passed** in
579.32s, including CD/PVD semantic mutants and ClearImage sanitizers. PID2662002
and session42499 are terminal; do not resume them. The CF3A4 formatter selector
is now wired with MAIN4 selection and source-RAM identity checks. Focused
archive/selector/export checks pass615 tests after that wiring. Its fresh native
run is still pending the rebuild; do not infer execution from these static checks.
80014774, called by146B0, is also installed and independently MATCH17/17 words
as ordinary C (640x480 black ClearImage wrapper). Candidate is
`artifacts/start-screen-continuation-20260908/80014774.bin`. The other called
setup helper147B8 remains an unregistered staging candidate, currently91/92.

### Natural CF3A4 completion and executable transition

The CF3A4 build ran naturally to the next executable dispatcher at **80011380**.
Evidence: `artifacts/start-screen-continuation-20260908/cf3a4/runtime.log`.
The final snapshot has382 MDEC transfers and fault-free CD/DMA state, with
DMA3transfers1840 and1619592 actual bytes. The invocation at11380 is missing
from the previous formatter; it is not a device refusal.

800147B8 is now installed and independently MATCH92/92 (368 bytes), same pinned
toolchain at link address800147B8. Two empty output constraints eliminate the
last scheduling mismatch without emitted instructions. Registry classification
is `mixed`; evidence candidate is
`artifacts/start-screen-continuation-20260908/800147b8.bin`.

The new11380 export includes the actual shared epilogue11664–1167C, even though
the disassembler labels that tail separately. All192 words in[11380,11680)
match the EXE and assembly; internal jumps remain within the complete range.
597 focused source tests pass. Its selector is wired and a fresh rebuild/run
is pending. Parallel C recovery is restricted to this actually selected routine.
No completion or runtime claim for11380 yet. No commit or push.

### Initialization helpers after 80011380

The `exe11380/runtime.log` run reached 800295D4 naturally (RA80011400).
Added checked native exports around the existing C for800295D4 and80029664;
independent pinned-toolchain rebuilds retain MATCH36/36 and11/11 respectively.
The resulting build completed. Its natural run, with automatic Start and R1
both disabled, is terminal (exit2):
`artifacts/start-screen-continuation-20260908/init295d4/runtime.log`.
It passed those helpers and80011DCC, then refused at **80011E24** (RA80011518).
DMA3 reports1825 transfers,1613140 bytes,fault0. This is execution evidence
under Xvfb, not fresh human desktop acceptance.

The full80011380 C candidate remains unregistered in staging:191/192 words.
Its missing register copy cannot be counted as a match; a scratch experiment
that restores the copy enlarges the stack frame. Native export remains192 words.
Parallel C work moved to its directly called80011E24 initialization helper.
The24-word[80011E24,80011E84) native export now matches both EXE and assembly,
and its formatter selector is wired.600 focused word/overlay tests pass.
Rebuild is pending in `/tmp/musashi-11e24-build.log`; no claim yet for execution
through80011E24. Earlier corrected15-test integration pass remains valid at
its recorded source snapshot; no full-suite pass is claimed. No commit or push.

80011E24 C recovery is now installed and independently rebuilt with pinned
GCC2.7.2/ASPSX2.56 -O0 -G0 at its actual link address: **MATCH24/24**,96 bytes,
SHA256 `f66af3fcb8ac2684fc47416c46c6354a13ac4f274356fb97339d85565b4f3c8d`.
Registry classification is `c`; no inline assembly. The documented unused local
preserves the observed stack allocation. Candidate:
`artifacts/start-screen-continuation-20260908/80011e24.bin`.
The build completed successfully;642 focused tests and4 subtests pass after
integration. Fresh run `init11e24/runtime.log` is pending (session91810).

The `init11e24` run is now terminal (exit2; session91810 closed). It executed
80011E24 and reached the next direct initialization callee **8005C4CC** with
A0=1,RA80011524. DMA3 reports1794 transfers,1556616 bytes,fault0. This is the
current observed refusal and next porting boundary; do not restart atCF300.
No live native runtime or test process is intentionally retained.80011380 C
remains unmatched191/192 and unregistered; its staging candidate is preserved.

### 8005C4CC initialization continuation

Added its4-word native export around the existing C body; all words agree with
EXE and assembly. Independent pinned-toolchain -O2 rebuild retainsMATCH4/4,
SHA256 `87bd1bfa5afbe8b47293da19db01aa2692dc70ef4001c9ee42db6d70346690e2`.
Candidate:`artifacts/start-screen-continuation-20260908/8005c4cc.bin`.
601 focused source/overlay tests pass. Native rebuild is pending in
`/tmp/musashi-5c4cc-build.log` (session1650). C recovery continues independently
on the actually selected11380 state machine; its191/192 candidate is not a match.

The5C4CC build and natural `init5c4cc/runtime.log` run are terminal. Execution
passed initialization and reached **801282AC** (RA80128168). This is specifically
an overlay identity refusal: `expected=8c22eea0 loaded=8c224f08`, not a missing
instruction export or DMA failure. DMA3 reports3077 transfers,2670396 bytes,
fault0. The formatter's broad SC01-member0[80128158,80128420) selector supplies
the expected word; even the existing MAIN0012 word at this PC is8c22e618, so
switching blindly to that member would also be wrong. Identify the newly loaded
PAC/overlay before changing selection; retain the source-RAM identity guard.
No fresh human desktop acceptance is claimed.

80011380 is now independently rebuilt and registered: **MATCH192/192**, full
768-byte range including shared epilogue. Pinned GCC2.7.2/ASPSX2.56 -O0 -G0,
SHA256 `fb39f11f5727441e3db4c0d1419194edbf967417ebe2866d71b486bbd7753b5b`.
Candidate:`artifacts/start-screen-continuation-20260908/80011380.bin`.
Removing hard register bindings from two live index locals preserved the missing
copy without growing the frame. One empty input constraint emits no instructions;
current registry classifier reports `c`. This supersedes prior191/192 status.
No commit/push. Runtime session44967 closed; no runtime intentionally left live.

### SC02 member31 identification

A bounded scan of decoded type4 PAC0 streams in SC archives found one candidate
for the observed801282AC word: **SC02.CD member31**. ArchiveSHA256
`8a04737374845155390268f7269c1599ecf6b6669357089059b20f1dcad11607`, member offset
15247360,size1050624, PAC length329157. The source-backed decoder consumes327109
bytes, emits588215 bytes, SHA256
`5bb5b6364206ccd0c47d6cf6a1dc2627e13bbef3cda40d618103b92ccfef4db6`.
The178-word entry span differs from SC01 at only80128218,8012821C,801282AC.

Added SC02's exact[80128158,80128420) export. At the natural80128158 entry,
selection requires all178 words to match guest RAM; each executed overlay word
still passes the existing identity guard. No RAM is replaced. Exhaustive tests
reject every single-word mutation and every unreadable word, and distinguish
SC01/SC02 selector outputs.666 focused tests and4 subtests pass. Updated the
shared test include resolver for this explicitly named PAC export. BIOS callback
integration is pending (`/tmp/musashi-sc02-bios-tests.log`,session67438).
Build session78255 completed; natural run `sc02entry/runtime.log` is pending.

80011B7C is independently rebuilt and registered as C: MATCH37/37,148 bytes,
pinned GCC2.7.2/ASPSX2.56-O0-G0 actual link address. SHA256
`e1fb6ee4d4a1b8cdc2fc08ce196f7e93fc5a648dcbeff2567d4e286632d0f391`.
Candidate:`artifacts/start-screen-continuation-20260908/80011b7c.bin`.
It writes the supplied halfword and clears seven related halfwords; no callees.
No commit/push or human desktop acceptance claim.

BIOS callback integration completed:1 passed in44.60s; session67438 closed.

The natural SC02-entry run passed the full178-word guest-RAM match, executed
through the former801282AC identity refusal, and reached **80128420** via the
retail dispatch table (RA801282BC,V0=80128420). Evidence:
`artifacts/start-screen-continuation-20260908/sc02entry/runtime.log`.
No overlay word mismatch is reported. DMA3:3028 transfers,2595600 bytes,fault0.
This establishes SC02 member31 for the executed entry span; it does not authorize
arbitrary later code without decoded-source validation. Next boundary is its
selected80128420 routine. No runtime/gameplay completion claim.

### SC02 initialization routine

Decoded80128420 is a38-word initialization routine ending801284B8. Its first
callee80129220 (10words) invokes80129258 (9words: clear128 halfwords at stride56)
and80129248 (4words: store the supplied halfword flag). Exported all four complete
spans from the same pinned SC02 member31 PAC0 decode. They require the previously
established SC02 entry identity, and retain per-instruction guest-RAM comparison.
625 source/selector tests pass, including all new export words and rejecting
these ranges when SC02 is not selected. Build pending:
`/tmp/musashi-sc02-init-build.log`,session22780. No execution claim yet for the
new ranges. Parallel C recovery is working on actually called main8002D4C8.

Build22780 completed.667 focused tests and4 subtests pass after source integration.
Natural run `sc02init/runtime.log` is pending (session29541); auto Start and R1
remain disabled.8002D4C8 is now independently rebuilt and registered as C:
MATCH53/53,212bytes,GCC2.7.2/ASPSX2.56-O2-G0 at8002D4C8. Candidate:
`artifacts/start-screen-continuation-20260908/8002d4c8.bin`, SHA256
`0ea10f4bb9f9bf2ec665b7a18522b0df13416e0374ba2863f3b3ebcb1cedc876`.
The C dispatches commands by range and processes pending work; no assembly.

Natural `sc02init` run is terminal (exit2,session29541 closed). It executed the
record-clear/flag chain and reached **801378F0**, RA80128438, the second direct
callee of80128420. DMA3:3024 transfers,2585232 bytes,fault0; no overlay word
mismatch. Current next boundary is801378F0 in the same verified SC02 member31.
No runtime/test process intentionally retained. No commit or push.

### SC02 record initialization at801378F0

Added complete58-word[801378F0,801379D8) and20-word[8013A860,8013A8B0)
exports from pinned member31. The first clears a0xBFC-byte block, initializes
three0x4C-byte records from four retail halfwords each, then invokes the second
to clear two16-byte records. Both call80016714, whose existing checked native
binding remains unchanged pending runtime evidence.620 source/selector tests pass.
Build pending:`/tmp/musashi-sc02-378f0-build.log`,session94528.

80011C10 is independently MATCH31/31,124bytes, registered C, pinned-O0 toolchain
at actual link address. It increments one halfword and clears four related fields.
Candidate:`artifacts/start-screen-continuation-20260908/80011c10.bin`, SHA256
`6fe4b706d44b7749b657c0b2391125d826d8d3e4fa0353677d208f237fa7794f`.
No instruction assembly. No commit or push.

Build94528 completed.667 focused tests and4 subtests pass after C integration.
Fresh natural run `sc02records/runtime.log` is pending (session9225), automatic
Start/R1 disabled. The80016714 binding's span guard only rejects BIOS overlap;
no new allowance or host-state patch was introduced for SC02.

Natural `sc02records` run is terminal (exit2,session9225 closed). It passed
801378F0,8013A860 and the subsequent80011E24 call, reaching **80128C14** with
RA80128448. DMA3 reports3020 transfers,2605024 bytes,fault0; no overlay identity
mismatch. This is the current SC02 initialization boundary. No runtime/test
process intentionally retained, no commit/push, no gameplay completion claim.

### SC02 parameter selection at80128C14

Added33-word[80128C14,80128C98) exact decoded export under the established SC02
identity selector. It chooses a parameter from the signed halfword at800B9A08
and calls existing matched80010AE0.620 source/selector tests pass. Build pending:
`/tmp/musashi-sc02-28c14-build.log`,session90492. Parallel C recovery is working
on direct initialization callee80015310, currently native-export-only.
No commit/push or execution claim for this new span yet.

Build90492 completed;667 focused tests and4 subtests pass after C integration.
80015310 is independentlyMATCH15/15,60bytes, registeredC, pinned-O2 toolchain at
actual link address. It clears64entry halfwords; a documented unused local
preserves the observed stack allocation. Candidate:
`artifacts/start-screen-continuation-20260908/80015310.bin`, SHA256
`37506dc34aabe5087f35ad09c256dcdbc27efb50b16320a01d49bf331166553f`.
Natural `sc02param/runtime.log` run pending (session65037), no forced input.

Natural `sc02param` run is terminal (exit2,session65037 closed). It passed
80128C14 and subsequent initialization calls through80015310, reaching
**800285AC** (RA80128468). DMA3:3063 transfers,2621952 bytes,fault0; no overlay
mismatch. Current next boundary is main800285AC, called by SC02's80128420.
No live runtime intentionally retained. No commit/push or completion claim.

### Main800285AC continuation

Added8-word native export around existing C. Independent pinned-O2 rebuild still
MATCH8/8,32bytes, SHA256
`91c25b90f565b57a4a0d7801d90e7195051ea3bf2561191ea77347b819102805`.
Candidate:`artifacts/start-screen-continuation-20260908/800285ac.bin`.
It calls800538EC(0), whose native export already exists. Parallel C work targets
that actual callee.621 source/selector tests pass; build pending:
`/tmp/musashi-285ac-build.log`,session51578. No commit/push.

Build51578 completed;668 focused tests and4 subtests pass.800538EC is now
independently MATCH35/35,140bytes, registeredC, pinned-O2 toolchain at actual
link address. Accepts modes0..3; invalid values call the retail diagnostic.
Candidate:`artifacts/start-screen-continuation-20260908/800538ec.bin`, SHA256
`33315f76b706f1c55d55946bc998506d38613fa73fe68f1e38056ff2dbab4765`.
Natural `sc02mode/runtime.log` pending (session17362), forced input disabled.

Natural `sc02mode` run is terminal (exit2,session17362 closed). It passed
800285AC/800538EC and reached **8013C98C** (RA80128480), the next SC02 initializer.
DMA3:3088 transfers,2628104 bytes,fault0; no overlay mismatch. Current boundary
is8013C98C in member31. No live runtime intentionally retained. No commit/push.

### SC02 clear at8013C98C

Exported full14-word[8013C98C,8013C9C4) from pinned member31. Clears8bytes via
80016714 and zeros two globals. Build34927 completed;668 focused tests and4
subtests pass. Natural `sc02clear/runtime.log` pending (session52796).

Installed ordinary C independently rebuilds to the same14 decoded retail words
with pinnedGCC2.7.2/ASPSX2.56-O2-G0 at8013C98C and explicit
`src/overlays/sc02_0031/symbols.txt`. Candidate:
`artifacts/start-screen-continuation-20260908/sc02_8013c98c.bin`, SHA256
`7487b2ebefaa6b848620fb5b3cd8a5b1f1d76d658b88f3df1c1325d2fa8c89b6`.
Comparison freshly decodes the hash-pinned SC02 archive, not main EXE. This C
remains **unregistered** because current main/overlay oracle coverage does not
include this decoded PAC region. No progress-credit or registry-support claim.
No commit/push.

Natural `sc02clear` run is terminal (exit2,session52796 closed). It passed
8013C98C and reached **80129C40** withA0=0,RA80128488. DMA3:3020 transfers,
2601236 bytes,fault0; no overlay mismatch. Next boundary is80129C40 in the same
SC02 initializer. No runtime intentionally retained; no completion claim.

### SC02 block initialization at80129C40

Exported complete46-word[80129C40,80129CF8) from pinned member31. Clears eight
0xA4-byte blocks and three byte flags, then calls existing exported800144D4.
24 focused decoded-source/selector tests pass. Build pending:
`/tmp/musashi-9c40-build.log`,session17808. Parallel C recovery targets this
actual routine against decoded PAC bytes; no main-oracle substitution.

Build17808 completed.668 focused tests and4 subtests pass. Installed ordinary C
independently rebuilds to all46 decoded retail words at80129C40, pinned-O2
compiler and explicit `src/overlays/sc02_0031/symbols.txt`. Candidate:
`artifacts/start-screen-continuation-20260908/sc02_80129c40.bin`, SHA256
`075122349868e987c2b826f0dbd136458e1dc4eed611c40c281c6373ba7db9f0`.
Comparison freshly decoded hash-pinned SC02; C remains unregistered pending
PAC-region oracle support. Natural `sc02blocks/runtime.log` pending,session53709.
No commit/push; no source-RAM replacement or forced input.

Natural `sc02blocks` run is terminal (exit2,session53709 closed). It executed
80129C40 and800144D4, reaching **800D0630** (RA80128490). DMA3:3044 transfers,
2591224 bytes,fault0; no overlay mismatch. This next call is in the lower overlay
address space: verify its loaded-member authority before exporting a same-address
routine. No live runtime intentionally retained, no completion claim.

### MAIN10 reset at800D0630

The logged lower-overlay eight-word prefix uniquely matches MAIN.CD member10,
at member offset20004 (base20000=>800CEDF8). Fresh archive member extraction
agrees exactly with `extracted/overlays/main/0010.bin`. Added11-word
[D0630,D065C) wrapper and14-word[D065C,D0694) reset helper under MAIN10 selection.
Both new ranges additionally require each instruction to match live guest RAM.
26 decoded-source/selector tests pass. Build pending:
`/tmp/musashi-main10-d0630-build.log`,session95916. Parallel C recovery targets
these actual called routines; no substitution of SC02 authority for lower code.

Build95916 completed. Installed MAIN10 C independently passes the existing
flat-blob oracle: D0630MATCH11/11 SHA256
`7df7e29c9666572c849b2678e8bbf0423673be15d880c4ddc965fae99a4f1aa5`;
D065CMATCH14/14 SHA256
`68235ef37a20fc285dbd64f6e22a72c7065734138fa98caa1f1e3fa955982614`.
Candidates:`artifacts/start-screen-continuation-20260908/main10_800d0630.bin`
and`main10_800d065c.bin`. Pinned-O2 builds use explicit
`src/overlays/main_0010/symbols.txt`. Oracle retail file0010.bin SHA256
`97933aae35fd27e3917866e4f9c7ef5716850c7efda3f96719496c9ba5b86246`,
flat-blob base800AEDF8 (member code begins at20000).
These remain unregistered: no MAIN10 registry target/symbol-map configuration.
Clarification for earlier SC02 notes: the generic oracle supports flat blobs;
the missing support is reproducible registry target/decoded-artifact and symbol
configuration, not an inability to compare decoded bytes. Natural run
`main10reset/runtime.log` pending (session92367). No commit/push.

670 focused tests and4 subtests pass. Natural `main10reset` run is terminal
(exit2,session92367 closed). It executed both MAIN10 reset routines with loaded
word checks and reached **80145CEC**, RA80128498, back in SC02 member31.
DMA3:3081 transfers,2624012 bytes,fault0; no overlay mismatch. This is the next
initializer boundary. No runtime intentionally retained; no completion claim.

### SC02 state initialization at80145CEC

Added complete127-word[80145CEC,80145EE8) decoded member31 export under SC02
selection and per-word loaded identity. This initializes a0x254-byte state block,
pointers and halfwords, then invokes multiple setup routines; no wholesale engine
or gameplay-C claim.26 decoded-source/selector tests pass. Build pending:
`/tmp/musashi-45cec-build.log`,session54335. Parallel C recovery targets its
first missing main-executable callee8001C320. No commit/push.

Build54335 completed;670 focused tests and4 subtests pass. Natural run
`sc02state/runtime.log` pending (session58049); input remains unforced.
8001C320 C work remains in progress, not a match claim.

Natural `sc02state` run is terminal (exit2,session58049 closed). It entered
80145CEC and reached **8001C320**, A0=800AFAE8,A1=800DE2A4,RA80145D3C.
DMA3:3017 transfers,2610960 bytes,fault0; no overlay mismatch. This is the
current main-executable continuation boundary. Parallel worker still owns only
staging/start-screen/8001c320.c and/tmp/musashi-1c320; preserve its active work.
No live runtime intentionally retained. No commit/push or completion claim.

### Main object/resource initializer8001C320

Installed full74-word[8001C320,8001C448) export and independently rebuilt C:
MATCH74/74,296bytes,pinned-O2 actual link address, SHA256
`e66fc99b964e8f9d2b69917b93db240a71c86e9908c49dea6bd18522ddd8df0d`.
Candidate:`artifacts/start-screen-continuation-20260908/8001c320.bin`.
Registry classification `mixed` reflects register bindings/empty constraints;
no instruction assembly. Initializes an object and walks its tagged resource
pointer list to build linked destination entries.624 focused source/selector
tests pass. Build pending:`/tmp/musashi-1c320-build.log`,session92878.
Parallel C work moved to actual first callee8001C9D0. No commit/push.

Build92878 completed;671 focused tests and4 subtests pass. Fresh natural run
`object1c320/runtime.log` pending (session86154), no forced input.1C9D0 C worker
owns only its staging/scratch files and remains independent of source exports.

Natural `object1c320` run is terminal (exit2,session86154 closed). It reached
**8001C9D0**,A0=800AFAE8,RA8001C348. DMA3:3049 transfers,2615852 bytes,fault0;
no overlay mismatch.8001C9D0 is now installed and independentlyMATCH19/19,
76bytes,pinned-O2 actualbase, registered ordinaryC. Candidate:
`artifacts/start-screen-continuation-20260908/8001c9d0.bin`, SHA256
`7a5b6c427d3c99397743249802323dd7c0d0906c2a5b470155b7872832447795`.
Its native word export/selector is still pending; C recovery alone does not
advance runtime. No live runtime or worker intentionally retained. No commit/push.

### Main8001C9D0 native continuation

Wrapped its independently matched C with19 checked EXE/assembly words and wired
the native selector. Rebuild retainsMATCH19/19;625 source/selector tests pass.
Build pending:`/tmp/musashi-1c9d0-build.log`,session73711. Parallel C recovery
moved to direct initializer callee80052D90, whose current export is native-only.
No commit/push.

Build73711 completed;672 focused tests and4 subtests pass.80052D90 C is installed
and independentlyMATCH31/31,124bytes,pinned-O2 actualbase, registeredC. Candidate:
`artifacts/start-screen-continuation-20260908/80052d90.bin`, SHA256
`8c63a80ed93023d11fcf361d65a244ed7c4a72ceb9563304bbcf61fc68fa65d1`.
Fresh natural run `object1c9d0/runtime.log` pending (session94500), input unforced.

IMPORTANT: `object1c9d0` natural run is terminal (exit2,session94500 closed) but
failed EARLIER in the movie/CD path at **80046C54**,RA800D27CC. This run does not
prove object continuation. DMA3 fault1=`MUSASHI_CD_DMA3_ACCESS`, active chopped
CHCR01400100,BCR000000BF,MADR000F47E4,cycle579547213,due579547217; CD IRQ frame
faulted. Latest BFRD sequence clears at579546945 then asserts at579547124.
CD FIFO remains valid,lba5498,size2048,cursor12,requested1. DPCR3B3BBBBB and
CDROM_DELAY21020843 are recorded. Diagnose the exact failing DMA access/ready
predicate before further export-only work; do not hide this run with a retry.
Evidence:`artifacts/start-screen-continuation-20260908/object1c9d0/runtime.log`.
Previous verified object boundary remains8001C9D0. No runtime intentionally live.

### DMA fault localization, behavior unchanged

The preserved CD IRQ frame stopped at retail80046EAC: store20943 to pointer
D8006D068, the recordedCDROM_DELAY register1F801018. Current DMA write32 rejects
all writes whileCHCR_BUSY before inspecting address. Added failure-only
`CD_WORD_REFUSED` diagnostic to native_boot, preserving rejection behavior.
Build39519 passed. A deterministic synthetic pause/resume probe reproduces this
exact valid delay-register rejection with active01400100 and faultACCESS;
source:`artifacts/start-screen-continuation-20260908/dma_refusal_diag/busy-delay-repro.c`.
It retains RAM/MADR/BCR/CHCR; it is not physical timing proof.

Source comparison inspected DuckStation Bus::HWHandlers::MemCtrlWrite:
https://raw.githubusercontent.com/stenzek/duckstation/master/src/core/bus.cpp
It updates delay registers separately, without DMA busy gating. DMA register
writes are separately implemented in core/dma.cpp. This supports reviewing our
blanket guard, not blindly permitting every write or claiming bus-cycle parity.

Fresh diagnostic run `dma_refusal_diag/runtime.log` is terminal (exit2,
session13932 closed). No CD_WORD_REFUSED occurred in this timing-sensitive run;
it reached **80053290**,A0=800D9290,RA8001C3D0, after object/matrix initialization.
DMA3:3042 transfers,2621400 bytes,fault0. This does NOT resolve the preserved
intermittent busy-delay fault. Correct/verify that guard before treating the
runtime path as stable. Parallel C work for53290 remains staging-only pending
worker result. No commit/push or completion claim.

DMA fixture suite completed:3 passed in1.30s. Worker53290 finished with reported
MATCH30/30 candidate at `/tmp/musashi-53290/candidate.bin` and
`staging/start-screen/80053290.c`; parent integration/independent oracle still
pending. Preserve staging; worker is terminal. Runtime and diagnostic tests closed.

### Busy CD memory-control delay correction

Added deterministic regression `chopped_delay_write`: paused/resumed chopped
DMA accepts retail80046EAC's20943 delay write without changing channel state,
deadline,RAM,FIFO progress or completion, then completes pending bytes normally.
It failed before the fix at that write. Negative controls retain refusal for
busy MADR/BCR/CHCR writes and invalid delay values. Changed only the busy guard:
CDROM_DELAY is an independent memory-control register and still uses existing
value validation. DMA channel writes remain guarded.4 DMA/publication tests pass
in1.34s; build91913 completed. Natural `dma_delay_fix/runtime.log` pending,
session47532. This targets the diagnosed predicate, not broader DMA timing parity.

80053290 C independentlyMATCH30/30,120bytes,pinned-O2 actualbase, registeredC;
empty constraint emits no instructions. Candidate:
`artifacts/start-screen-continuation-20260908/80053290.bin`, SHA256
`c97806e6bea52b2720daeb4c8ae0d9d83ff312cbb785b7f1cffa069c0361a256`.
Native export remains pending. Parallel check confirmed existing registered
800553D8 C MATCH7/7; corrected its stale contradictory unverified comment only.
Candidate:`artifacts/start-screen-continuation-20260908/800553d8.bin`.
No commit/push.

Natural `dma_delay_fix` run is terminal (exit2,session47532 closed), reaching
**80053290**,A0=800D9290,RA8001C3D0. DMA3:3015 transfers,2634744 bytes,fault0;
no rejected-write or overlay-mismatch diagnostic. The exact busy-delay predicate
is covered by deterministic regression; this one natural pass is not an
exhaustive host-timing guarantee. Resume native resource relocation at53290.
No live runtime or worker intentionally retained. No completion claim.

### Resource relocation native exports

Wrapped existing matched C80053290 (30words) and800553D8 (7words) with checked
EXE/assembly exports and wired their native selectors. Parallel C work targets
next directcallee800553F4. Build/tests pending:
`/tmp/musashi-resource-reloc-build.log`,session51080. No forced input or DMA
policy change beyond the previously verified delay fix. No commit/push.

Build51080 completed;630 DMA/source/selector checks pass in1.75s. Natural
`resource_reloc/runtime.log` pending (session14084). Parallel553F4 C recovery
identified full[800553F4,80055D40),595words with30-way primitive switch; only
external call is diagnostic5C604. Preserve full packet-building scope and worker
staging/scratch; no reduced primitive subset or match claim.

Natural `resource_reloc` run is terminal (exit2,session14084 closed), reaching
**800553F4**,A0=800A5BD0,A1=801152C8,RA8001C410 after resource relocation and
entry setup. DMA3:3160 transfers,2666020 bytes,fault0; no refused-write diagnostic.
Current next native boundary is the complete595-word packet builder553F4.
Parallel worker retains only staging/start-screen/800553f4.c and/tmp/musashi-553f4;
C recovery still in progress. No live runtime intentionally retained. No
commit/push or gameplay completion claim.

### Complete native primitive packet builder553F4

Exported all595 retail words[800553F4,80055D40), checked against EXE andassembly.
Control-flow checks cover all direct branches/jumps and all30retail jump-table
entries at80073C20, each inside the complete span. Only direct call5C604 is the
unsupported-type diagnostic. Native selector wired; no C match claim.
Build/tests pending:`/tmp/musashi-553f4-build.log`,session88637.
Worker recovered full switch draft and identified zero-count dependence on
incomingt0 (first assigned inside loop5547C, read at55CE8). Preserve this retail
edge case; do not initialize it away or reduce primitive/count coverage.

Build88637 completed;606 source/control-flow/DMA checks pass in1.64s. Natural
`packet553f4/runtime.log` pending (session96990), using unforced input and actual
loaded resource list. Static full-case coverage is distinct from which cases
this one runtime path executes. C worker remains active in staging/scratch only.

Worker553F4 is now terminal. Full staging C draft compiles541words versus595
retail; oracle LENGTH_MISMATCH. No integration or registration. It includes the
reconstructed30-way table and an attempted incomingt0 preservation constraint;
semantic review and exact matching remain, so this is not proof of C fidelity.
Preserve `/tmp/musashi-553f4/` including candidate.bin,with-table.s,raw.txt and
staging/start-screen/800553f4.c. Native execution still uses exact retail words.

Natural `packet553f4` run is terminal (exit2,session96990 closed). It passed
resource relocation/packet construction and returned to SC02 state setup,
reaching **801552F4**,A0=80126B58,RA80145DF0. DMA3:3109 transfers,2663144 bytes,
fault0; no rejected-write or overlay-mismatch diagnostic. Next native boundary
is801552F4 in verified SC02 member31. No runtime/worker intentionally live.
No commit/push or gameplay completion claim.

### SC02 resource-tag state at801552F4

Exported complete20-word[801552F4,80155344) from pinned member31. It masks the
loaded null-terminated resource pointer list at800DE2A4 with80FFFFFF and sets
state+44 bits30000000.26 decoded-source/selector checks pass. Build pending:
`/tmp/musashi-sc02-552f4-build.log`,session27789. Parallel C recovery targets
this actual routine; full unmatched553F4 packet draft remains preserved.

Build27789 completed;676 focused tests and4 subtests pass. Installed ordinary C
independently passes flat-blob oracleMATCH20/20 against pinned decoded31,
-O2 actualbase with explicit SC02symbols. Candidate:
`artifacts/start-screen-continuation-20260908/sc02_801552f4.bin`, SHA256
`9bb65ec4ea5456b472c8952dbd7efe4ec699b4fad7b74e8e8e1b4d1544be5130`.
Remains unregistered pending reproducible PAC target configuration. Natural
`sc02tags/runtime.log` pending (session83055). No commit/push.

Natural `sc02tags` run is terminal (exit2,session83055 closed). It passed
801552F4 and reached **80147084**,A0=80126B58,RA80145DF8. DMA3:3137 transfers,
2649172 bytes,fault0; no rejected-write diagnostic. Current next boundary is
80147084 in SC02 setup. No runtime/worker intentionally live; no completion claim.

### SC02 initialization flag helpers

Exported the three actually called two-word byte-clear helpers47084,47098,470AC
(full ends4708C,470A0,470B4). They clear object+221,+220,+222 in branch delay
slots. Each requires SC02 selection and loaded source-word identity. No C match
credit. Parallel worker resumed full595-word553F4 matching, preserving all cases
and incomingt0 edge semantics. Build/tests pending:
`/tmp/musashi-sc02-flags-build.log`,session99323. No commit/push.

Build99323 completed;29 decoded-source/selector/DMA tests pass in1.44s. Natural
`sc02flags/runtime.log` pending (session63057), input unforced. Worker553F4
continues in staging/scratch; no C integration or match claim for that builder.

Natural `sc02flags` run is terminal (exit2,session63057 closed). It passed all
three byte-clear calls and reached **80149210**,A0=80126B58,A1=8,RA80145E28.
DMA3:3049 transfers,2589220 bytes,fault0; no rejected-write diagnostic. Current
next SC02 native boundary is80149210. Packet-builder C worker remains separate
and active; no live runtime intentionally retained. No commit/push/completion.

### SC02 paired state values and bit clear

Exported complete6-word[80149210,80149228) and9-word[8014BE9C,8014BEC0), both
actually called by80145CEC initializer. First stores paired halfwords; second
calls existing80029124(12F,0). Member31 identity guards retained. Build/tests
pending:`/tmp/musashi-sc02-statebits-build.log`,session41761.
Parallel553F4 worker reports initial68synthetic Unicorn differential cases pass
output/return comparison; broader transition set pending. This is separate
from byte matching (still541/595) and natural game runtime.

Build41761 completed successfully;29 focused tests passed in1.50s. Natural
`sc02statebits/runtime.log` is running with unforced input (session34365).
The553F4 worker finished68single/empty and1024two-primitive synthetic
comparisons with zero mismatches; a corrupted-color negative control was
detected. Evidence remains in `/tmp/musashi-553f4/verification-summary.json`.
C remains541words versus595retail and is not promoted or registered.

Natural `sc02statebits` run completed at the next unsupported call,
**8016533C**, RA=80145E38, S0=80126B58 (exit2; session34365 closed).
Both new exports were passed on the natural path. DMA3 reports3079transfers,
2610020bytes,fault0; no CD_WORD_REFUSED or OVERLAY_WORD_MISMATCH diagnostic.
Next continuation boundary is8016533C. No gameplay acceptance or completion
claim; no commit or push.

### SC02 state-byte dispatch

Added complete guarded exports [8016533C,80165374),14words, and
[801659DC,80165A18),15words. The first copies80078EC1 to80126CF8 and
calls the second with80126B58; the second selects a function through the
retail80187A4C table using object+1A0. No selected target is assumed.
Build5480 passed;29 focused source/identity/DMA tests passed in1.56s.
Natural `sc02dispatch/runtime.log` pending, session36177, input unforced.

Parent independently rebuilt ordinary C from worker staging for80149210,
8014BE9C,8016533C with pinned GCC2.7.2/ASPSX2.56 -O2 -G0 at actual VRAM.
Flat-blob hash-pinned oracle matched6/6,9/9,14/14words respectively.
Integrated these C branches, retaining unchanged native word exports;
26 source/selector tests passed afterward. These SC02 matches remain outside
the registry until an explicit SC02 target is supported. Worker now recovering
801659DC;553F4 remains unmatched. No commit/push/completion.

Parent also independently rebuilt and matched801659DC15/15words (SHA256
5958ddf8500072435929dad78c18b03bb5aed1d65dd78ee7c2d4daf10cdeeca3),
integrated its ordinary C branch;26 source/selector tests passed in0.22s.
Natural `sc02dispatch` is terminal, exit2, session36177 closed: selected
**80165A18**, A0=80126B58, RA=80165A08. This is the next unsupported
boundary, observed through the real function table. DMA3:3061transfers,
2629664bytes,fault0; no rejected-write or overlay-identity diagnostic.

### SC02 state pool initialization

Added complete guarded native exports for observed empty return80165A18
(2words), and direct initializer callees801627C0,80162AF4,80165C78
(10words each; ends801627E8,80162B1C,80165CA0). They clear8011F750/58,
8011F9D0/820,8011D030/A50 through80016714. Parent independently rebuilt
worker C for all three at actual VRAM using pinned GCC2.7.2/ASPSX2.56
-O2 -G0; hash-pinned SC02 flat oracle matched10/10words each. C branches
integrated, no registry addition. Empty return is native-only.
Build36129 passed;29 source/identity/DMA tests passed in1.56s, and26
source/identity tests passed after C integration in0.20s. Natural
`sc02pools/runtime.log` pending(session54171), input unforced. Parallel C
recovery now covers next direct callee801468C8, full[801468C8,80146924).
No commit/push/completion.

Natural `sc02pools` terminal(exit2;session54171 closed) reached **801468C8**,
A0=80126B58,A1=1,RA=80145E5C. All three state clears passed naturally.
DMA3:3106transfers,2608520bytes,fault0; no rejected-write/identity diagnostic.
Broader export/overlay checks:634passed in0.59s. Worker produced ordinary C
for full801468C8; parent independently rebuilt and matched23/23words, SHA
 e81b7c982b17dc3454ab9093969b16705795ccb60967a06ef50e6b9c0c13c662.
This next function remains in staging/start-screen/sc02_801468c8.c awaiting
native export/integration. No game-completion claim or commit/push.

### SC02 slot registration and flag-dependent setup

Integrated guarded native/C801468C8 full23words and guarded native80145EE8
full75words, preserving both modes and all five flag checks/calls. Parent
rebuilt integrated801468C8 at actual VRAM and reverified23/23 exact; worker
then recovered full80145EE8 ordinary C, parent independently rebuilt and
matched75/75 (SHA4a7ba709d2c6295d7a2b200a0b3aa88c13a5dfc01a3707f510829884d810969f).
Its C branch is now integrated; both remain outside SC02 registry support.
Build98355 passed;29 focused tests passed in1.44s;26 source/selector tests
passed after C integration in0.20s. Natural `sc02register/runtime.log`
pending(session79748), input unforced. Parallel recovery inspecting actual
callee80146014. No commit/push/completion.

Natural `sc02register` completed(exit2;session79748 closed), reaching
**80146014**,A0=801866B0,RA=80145F74. The mode0 flag-dependent branch
selected the third resource list naturally. DMA3:3156transfers,2625392bytes,
fault0; no rejected-write or overlay-identity diagnostic. Next unsupported
function is full[80146014,80146074),24words:16byte-record loop ending at
zero word+C, calls80059A80(record,word+8,word+C). Worker C recovery remains
active in staging/scratch; no matched C or native export yet for this function.

### SC02 resource-list iteration

Added full guarded native[80146014,80146074),24words. Build98921 passed;
29 focused source/selector/DMA tests passed in1.72s. Natural
`sc02upload/runtime.log` pending(session79552), input unforced. Worker
continues full C matching for80146014; no C match claimed yet.
Direct executable callee80059A80 inspected through full[80059A80,80059B44),
49words: validates rectangle via59760, refuses zero dimensions, packs
arguments in retail command storage then invokes driver callback. It has
no native export yet. No commit/push/completion.

Parent independently rebuilt80146014 ordinary C and matched24/24words,
SHAe21c065a19863f6bede509752994c17c62329f0ffb25b22558dcf5fd7b74d15d;
C branch integrated,26 source/selector tests pass in0.23s. No SC02 registry
addition. Natural `sc02upload` terminal(exit2;session79552 closed) reached
**80059A80**,A0=801866B0,A1=224,RA=8014604C. DMA3:3065transfers,
2601956bytes,fault0; no rejected-write/identity diagnostic. Next native
boundary is executable80059A80. Worker recovering its full49words in
staging/scratch, no C match claimed yet. No commit/push/completion.

### Executable rectangle-move submission

Added complete49word native[80059A80,80059B44), checked word-for-word
against both asm/main.s and executable. Retains zero-dimension returns,
retail command packing and indirect driver dispatch.608 export/SC02/DMA
tests pass in1.69s. Build92990 pending at /tmp/musashi-moveimage-build.log.
Worker continues full C recovery in staging/scratch. No commit/push/completion.

Build92990 completed successfully. Natural `moveimage/runtime.log` running
with unforced input(session37257). C worker confirmed live; matching remains
pending. No execution success inferred from the build.

Natural `moveimage` terminal(exit2;session37257 closed) passed80059A80
and reached **8005B750**,RA=8005BBEC,A0=800C659C,A1=0. Next unsupported
native boundary is8005B750 inside the actual driver call chain. DMA3 reports
3103transfers,2608144bytes,fault0; no rejected-write/identity diagnostic.
80059A80 C worker remains active; native execution is not a C-match claim.

### Correction:8005B750 is a GPU backend refusal

Read-only diagnosis supersedes the preceding unsupported-native-boundary
label.8005B750 is already in complete exported[8005B710,8005B75C); it
writes linked-list DMA2 CHCR=01000401. The preserved moveimage log states:
`gpu_dma2: GP0 refused node=000c659c word=000c65a0 value=80000000 opcode=80 fill_words=0 store_phase=0 accepted=0`.
GPU_DMA2 fault3 is MUSASHI_GPU_DMA2_BACKEND; accepted_in_transfer=0,
payload=4. Thus the next work is GP0 VRAM-to-VRAM copy command support,
not CPU instruction export. gpu_controller.c accepts fill/store/primitives/
environment commands but has no opcode80 parser. Existing backend already
exposes read_vram/store_vram; gpu_psycross implements both through real VRAM.

Reference inspected: https://psx-spx.consoledev.net/graphicsprocessingunitgpu/
GPU Memory Transfer Commands: command high3bits100, source/dest coordinates,
width/height; mask applies, coordinates wrap independently, size fields
normalize modulo1024/512 with zero=max. Need exact overlap ordering authority
before implementation; no snapshot-copy assumption. Implement with meaningful
parser,mask,wrap,overlap and partial-command/negative controls then natural
runtime replay. No GPU fix yet; original fault log preserved. Parallel
80059A80 C currently49words,42/49 exact according to worker, unpromoted.
No commit/push/completion.

### GP0 copy implementation in progress

Parent added tests/gpu_copy_controller_probe.c and pytest driver. Baseline
strict build succeeds but test fails on first GP0(80) packet, matching natural
failure. Tests cover aliases80..9F, source wrap, mask, partial reset, missing
backend, horizontal overlap direction, vertical propagation and zero width.
Implementation worker /root/gpu_copy owns only gpu_controller.c/header;
parent owns tests/docs/runtime. Reference overlap ordering is emulator-derived,
not hardware parity. No fix/pass/runtime result claimed yet.
80059A80 worker terminal: preserved full49word C draft,42/49 matching,
unpromoted. Scheduling mismatch remains in staging/scratch.

GP0 copy implementation landed in controller/header:80..9F four-word parser,
normalizedsize/coords,mask/wrap/referenceoverlap,reset/status,checked callbacks,
completed-copy and successful-pixel audit. Parent regression failed beforefix
and passesafter; partial backendfailure/reentry cases stopfaulted with actual
pixel credit and no commandcompletion.59 GPU/controller/fill/DMA tests pass
in5.19s; strict copy probe and ASan/UBSan pass. Initial broader run crashed
because Python ctypes controller layout lacked addedfields; updated mirror,
then reran successfully. Build82762passed. Natural gpucopy/runtime.log
running unforced(session13343). No runtimepass claimed yet.
Reference https://raw.githubusercontent.com/stenzek/duckstation/master/src/core/gpu_sw_rasterizer.inl
CopyVRAMImpl1833-1963: horizontal direction,forwardrows,wrapped rectangle
split; wrappedcopy ordering explicitly unverified onconsole. No hardware
parity claim. Existing backend read/store routes actual canonical VRAM.

Natural gpucopy run terminal(exit2;session13343 closed) passed the real
VRAM-copy submission and reached **8014C968**,RA=80145E88. No GP0 refusal
was logged. This proves natural continuation beyond prior GPUfault, not
full game acceptance or console parity. Additional zeroheight,missingstore,
busy-finalword retry tests pass. Next nativecallee8014C968; C worker still
recovering full80059A80. No commit/push/completion.

### SC02 post-copy state clears

Added complete guarded native8014C968[ends8014C978] and8014C6D0[ends8014C6E0],
4words each. Parent independently rebuilt worker ordinary C at actualVRAM,
flatblob oracle matched4/4 each (SHA0c1e9e605220d52c69cff4f3380af011141a3a3646692fa798d55d2812ad32ca,
a344ba46147988f9a857694e68a8640f08293db7010416f13d9593cdce7a7691).
C branches integrated, no SC02registryaddition. Build59786passed;
27 source/selector/GPUcopy checks passed in0.30s;26 source/selectorchecks
passed afterC integration in0.22s. Natural sc02clearflags/runtime.log
running unforced(session68541). Worker also staged ordinaryC80165938
full[80165938,80165958),8words matchedworkeroracle, parentintegrationpending.
80059A80 C remains42/49 after bounded type/scheduling investigation;
fullscope preserved, unpromoted. No commit/push/completion.

Natural sc02clearflags terminal(exit2;session68541closed) passed both clears
and reached **80165938**,RA=80145EA0. DMA3:3012transfers,2572704bytes,
fault0; no GP0refusal. Parent independently rebuilt staged80165938 and
matched8/8words SHA38f10024106065c371dc542892738c8f8f9466fb2cc4ecd7ced197e5fa60e8e5.
Awaiting native/Cintegration. Worker additionally recovered following direct
callee80153C8C full[80153C8C,80153C9C),4words clears80115210; stagedsource
andworkerexactmatch await parentverification. Goal remainsactive.

### SC02 final player-initializer resets

Integrated complete guarded native/C80165938(8words) and80153C8C(4words).
Parent pinned builds at actualVRAM matched8/8 and4/4 against hash-pinnedSC02;
no registryaddition.27 source/selector/GPUcopychecks pass in0.29s.
Build23969pending at /tmp/musashi-sc02-finalresets-build.log. Parallel C
recovery now inspects80144B9C, nextdirectcallee in80128420 after80145CEC.
No commit/push/completion.

Build23969passed. Natural sc02finalresets/runtime.log running unforced,
session2177. Worker established full next80144B9C span[80144B9C,801457A4),
770words; full C recovery underway with allbranches/callees retained.
This substantial initializer remains unexported and unmatched, not reducible
to nearby reset/thunk credit.

Natural sc02finalresets terminal(exit2;session2177closed) reached
**80144B9C**,RA=801284A0. This observes80145CEC returning into80128420
and the next initializer call. DMA3:3074transfers,2609960bytes,fault0;
no GP0refusal or overlayidentity diagnostic. Next portscope full770word
80144B9C; parallel full C recovery remains active. No gamecompletionclaim.

### Full SC02 world initializer export

Added complete native-only[80144B9C,801457A4),770words, hash-pinned to
SC02member31. Verified finalreturn/delayslot and internal directbranch spans;
SC02selection and loaded-wordidentity guards remain required. No recoveredC
credit.27 source/selector/GPUcopy tests pass in0.33s. Build88386pending
/tmp/musashi-sc02-worldinit-build.log. Parallel full C recovery remainsactive.
No commit/push/completion.

Build88386passed; natural sc02worldinit/runtime.log running unforced
(session47412). Worker recovered full ordinary C80144B9C; parent independently
built pinned GCC2.7.2/ASPSX2.56 -O0 -G0 at actualVRAM and matched770/770,
SHA2c5643a101f044996bd8abddf7146b7d85dff8944c0cb1bd423c3daae1f9e186.
Integrated C branch and required SC02 symbol definitions;26source/selector
checks pass in0.19s. No SC02registry support/entry yet. Full branches retained;
byte match does not prove completed natural execution of initializer.

Integrated80144B9C source rebuilt using only repositorySC02 symbols and
reverified770/770. Natural sc02worldinit terminal(exit2;session47412closed)
entered initializer and reached firstcallee **80019018**,RA=80144BF4.
DMA3:2985transfers,2600116bytes,fault0; no GP0refusal. Nextnative dependency
is80019018. Complete770word C match established; natural initializer completion
not yet established. No commit/push/completion.

### World initializer flag calls

Existing ordinary C80019018 and80018E78 independently rebuilt/reverified
4/4 and9/9words, then wrapped with complete EXE/assembly-backed nativeexports.
No C replacement/registrycredit.607 export/GPUcopy checks pass in0.38s.
Build32041pending /tmp/musashi-worldflags-build.log. Parallel recovery now
inspects fullSC02801457A4, nextcallee after these flags. No commit/push.

Build32041passed; natural worldflags/runtime.log running unforced(session2021).
Worker confirmed next801457A4 full[801457A4,801458E0),79words, scene-ID
selection including defaulttableentry, no callees. FullC recovery underway.

Natural worldflags terminal(exit2;session2021closed) passed bothcalls and
reached **801457A4**,A0=301C,RA=80144C30. DMA3:3094transfers,2672584bytes,
fault0; no GP0refusal. Parent independently rebuilt worker full ordinaryC
scene selector and matched79/79words(-O0),SHAe141311f57690429fb9af6354492af0500671ac0eb90fb2878e4163a4e72541f.
Source staging/start-screen/sc02_801457a4.c awaiting native/Cintegration.
Nextportboundary801457A4; goalactive, no commit/push/completion.

### Scene parameter selector integration

Integrated complete guarded native/C801457A4[ends801458E0),79words; parent
rebuilt integratedsource with repositorysymbols and matched79/79 at-O0.
27source/selector/GPUcopychecks pass in0.32s. Build44296pending at
/tmp/musashi-sceneselect-build.log. Parallelworker recovered ordinaryC
8002AB64full[8002AB64,8002AC00),39words; workeroracleexact, parentverification
andnativeintegrationpending. No commit/push/completion.

Build44296passed; natural sceneselect/runtime.log running unforced(session79657).
Parent independently rebuilt staged8002AB64 and matched39/39words,
SHA12cb2d5521bf521f92ef5327704b9fd580d7d8cc134c414a30be3ccdcd52180f.
Still staged; no newmainregistryentry or nativeexport for thatcallee yet.

Natural sceneselect terminal(exit2;session79657closed) passedselector and
reached **8002AB64**,RA=80144C84. DMA3:3062transfers,2609876bytes,fault0;
no GP0refusal. Nextnativeboundary8002AB64 with full39word C independently
matched and staged. Goalactive, no commit/push/completion.

### Fresh-scene count and entry initialization

Integrated complete native/C8002AB64[8002AB64,8002AC00),39words and main
Cregistryentry(-O2). Parent rebuilt integratedsource and reverified39/39.
608export/GPUcopychecks pass in0.47s. Build53774pending
/tmp/musashi-flagcount-build.log. Worker recovered conditionalcallee8002AC00
full[8002AC00,8002AC98),38words; parent independently rebuilt/matched38/38,
SHA6670f9f74b24254a70a921960745c260550108af866af9680b2bcb65272f7264.
Callee remains staged awaitingnativeintegration. No commit/push/completion.

Build53774passed. Natural flagcount/runtime.log running unforced(session61847).
Both integratedcaller andstagedcallee independently exact; conditionalpath
execution remainspending until naturalrun evidence.

Natural flagcount terminal(exit2;session61847closed) took count==1 path and
reached **8002AC00**,A0=1,RA=8002ABA0. DMA3:3138transfers,2674092bytes,
fault0; no GP0refusal. Nextnativeboundary8002AC00, full38wordmatchedC staged.
Goalactive; no commit/push/completion.

### Fresh entry table setup

Integrated complete native/C8002AC00[8002AC00,8002AC98),38words with main
Cregistryentry(-O2). Parent rebuilt integratedsource and reverified38/38.
609export/GPUcopychecks pass in0.40s. Build50933pending at
/tmp/musashi-entryflags-build.log. Worker independently verified existing
80015978 C9/9 full[80015978,8001599C), nextdirectworldinitializercallee;
no nativeexport added yet for that function. No commit/push/completion.

Build50933passed; natural entryflags/runtime.log running unforced(session91675).
Full matched entry initializer now executable; natural loop completion remains
pending until terminal evidence.

Natural entryflags terminal(exit2;session91675closed) passed entryloop and
reached **80015978**,A0=80126B5C,A1=80126DB0,RA=80144FA8. DMA3:
3037transfers,2591280bytes,fault0; no GP0refusal. Nextnativeboundary80015978.
No gamecompletionclaim; goalactive, no commit/push.

### World position copy

Wrapped existing80015978 C with complete9wordnativeexport[80015978,8001599C),
verified EXE/assembly and rebuilt integratedC9/9 exact.610export/GPUcopychecks
pass in0.40s. Build8576pending /tmp/musashi-positioncopy-build.log.
Parallelworker recovering fullSC028012A1BC[8012A1BC,8012A2F4),78word state
snapshot including unalignedblocks; no narrower fragmentpromotion.
No commit/push/completion.

Build8576passed; natural positioncopy/runtime.log running unforced(session8369).
Next branch remains unobserved until terminalrun evidence. SnapshotC worker
stillactive, no exactmatch claim yet.

Natural positioncopy terminal(exit2;session8369closed) passed80015978 and
reached **8018129C**,RA=801450C0, via initializer dispatch. DMA3:
3066transfers,2628140bytes,fault0; no GP0refusal. Nextportboundary8018129C;
do not assume optional8012A1BC executed. Parent independently rebuilt staged
8012A1BC and matched78/78words, SHAe21b72491ca1c024b5e26bb630846f88b6183cfc6916c84abbfd847676b52b7c.
One empty compiler memory barrier preserves snapshot scheduling; worker labels
mixedC, fullscope preserved. Remains staged/unregistered/unexported. Goalactive,
no commit/push/completion.

### Selected scene camera callback

Confirmed801450B8 jalr through params+18 selected observed8018129C.
Added complete guarded SC02native[8018129C,8018133C),40words; initializes
camera fields and calls8012A018,8012A094,8018133C.27 source/selector/GPUcopy
checks pass in0.30s. Build53992pending /tmp/musashi-scenecamera-build.log.
Parallel fullcallback C recovery underway. No commit/push/completion.

Build53992passed. Parent independently rebuilt fullcallback ordinaryC and
matched40/40words SHA c8674d929b7ac5d0164e1bdd512c1398ae17f182c50871c6751362f39c33a945;
integrated C branch,26source/selectorchecks pass in0.21s. Natural scenecamera/
runtime.log running unforced(session20554). No SC02registryaddition or
executioncompletion claim.

Natural scenecamera terminal(exit2;session20554closed) reached **8012A018**,
A0=8018133C,A1=0,RA=80181310. DMA3:3084transfers,2644408bytes,fault0;
no GP0refusal. Nextnativeboundary8012A018 callbackregistration; goalactive,
no commit/push/completion.

### Camera registration and snapshot exports

Added full guarded SC02native8012A018[endsA048),12words;
8012A048[endsA094),19words;8012A094[endsA0E0),19words. Preserves callback
storage, phasebyte, stateclears, positioncopy and unalignedsnapshot ordering.
27source/selector/GPUcopychecks pass in0.32s. Build62297pending
/tmp/musashi-camerareg-build.log. Parallel full C recovery for allthree.
No commit/push/completion.

Build62297passed. Parent independently rebuilt allthree C functions and
matched12/12,19/19,19/19words; integrated C branches.8012A048 contains one
empty memorybarrier; no instruction-substitution.26source/selectorchecks
pass in0.20s. Natural camerareg/runtime.log running unforced(session40497).
No SC02registryentry, commit/push/completion.

Natural camerareg terminal(exit2;session40497closed) passedregistration and
stopped inside exported snapshot at **8012A0A4** LWL,A0=80126948,RA=80181320.
DMA3:3093transfers,2626360bytes,fault0; no GP0refusal. This is an instruction
merge-site gate, not missingfunctionexport: formatter preflights opcodes34/38/
42/46 against enumerated retail merge sites. Nextwork audit/add complete
snapshot LWL/LWR/SWL/SWR pairs with load-delay semantics and tests. Goalactive,
no commit/push/completion.

### Snapshot merge-site audit and regression

Added test_camera_snapshot_merge to existing production-interpreter probe:
all4alignments, exactbytecopy/surroundingpreservation, singlebit instruction
negativecontrol. Baseline failed at missingmergekind(48.00s). Added only8
exactSC02sites8012A0A4..8012A0C0 plus two audited LWR-delay transitions
(A0AC nextLWL andA0B4 firstSWL), samepattern as existing MAIN4 rectanglecopy.
Merge arithmetic/memoryguard unchanged. Full test_pvd_merge.py including
semanticmutants running session18834 /tmp/musashi-camera-merge-tests.log.
Separate nativebuild pending /tmp/musashi-camera-merge-build.log.
Parent independently rebuilt/matched nextcallback8018133C15/15words,
SHA9b1f3e8930a206a5443ede15c6d196e40fe753a6fa4559a20081af5ed9bef658;
staged only, nativeintegrationpending. No runtimepass/commit/push/completion.

Build91023passed. Fullmerge test18834 terminalpassed includingmutantcontrols.
Natural cameramerge terminal(exit2;session70256closed) crossed auditedsnapshot
and reached **8018133C**,A0=80126948,RA=80181328. DMA3:3069transfers,
2610040bytes,fault0; no GP0refusal. Nextnativeboundary8018133C full15wordC
already independently matched/staged. Goalactive, no commit/push/completion.

### Camera phase dispatch

Integrated full guarded native/C8018133C[8018133C,80181378),15words;
parent rebuilt integratedsource and reverified15/15.27source/selector/GPUcopy
checks pass in0.31s. Build2130pending /tmp/musashi-cameradispatch-build.log.
Parallel worker recovering table0target80181378 full[80181378,801814E4),
91words with coordinate-dependent camera adjustments/clamps. Actual target
still requires naturalrun evidence. No commit/push/completion.

Build2130passed; natural cameradispatch/runtime.log running unforced(session1708).
Parent independently rebuilt full80181378 ordinary C and matched91/91,
SHA8f0f3ab382fbe7c066a8c26b6dcd4cdd803bce98ca09e6c463171f664ed5a8ab.
Source staging/start-screen/sc02_80181378.c awaitsnative/Cintegration.

Natural cameradispatch terminal(exit2;session1708closed) selected **80181378**,
A0=80126948,RA=80181368. DMA3:3120transfers,2662776bytes,fault0;
no GP0refusal. Nextnativeboundary80181378, full91wordC independentlymatched
andstaged. Goalactive, no commit/push/completion.

### Selected camera handler integration

Integrated full guarded native/C80181378[80181378,801814E4),91words,
parent rebuilt integratedsource and reverified91/91.27source/selector/GPUcopy
checks pass in0.32s. Build10489pending /tmp/musashi-camerahandler-build.log.
Parallelworker recovering full801814E4[801814E4,801816C0),119words, with
smoothing/matrix/vector transform, no branch narrowing. No commit/push.

Build10489passed; natural camerahandler/runtime.log running unforced(session94316).
Parent independently rebuilt full801814E4 ordinaryC and matched119/119,
SHA7e4043b133710c3d41c9fdd5afac870bf1d28ac00a05e39091cb77e171cf5c4c.
Source staging/start-screen/sc02_801814e4.c stagedpendingintegration.

Natural camerahandler terminal(exit2;session94316closed) passed fullhandler
and reached **801814E4**,A0=80126948,A1=801FFE78,RA=801814D4. DMA3:
3099transfers,2624728bytes,fault0; no GP0refusal. Nextnativeboundary801814E4,
full119wordC independentlymatched/staged. Goalactive, no commit/push/completion.

### Camera calculation integration

Integrated complete guarded native/C801814E4[801814E4,801816C0),119words;
parent rebuilt integratedsource and reverified119/119.27source/selector/GPUcopy
checks pass in0.30s. Build60860pending /tmp/musashi-cameracalc-build.log.
Worker recoveredSC0280148800 full[80148800,8014880C),3words and independently
verified existingmain80012C6C19/19,80012ABC18/18. Callee nativeintegration
pending; no commit/push/completion.

Build60860passed; natural cameracalc/runtime.log running unforced(session55897).
Full integratedC byteproof retained separately from pendingnaturalexecution.

Natural cameracalc terminal(exit2;session55897closed) enteredfullcalculation
and reached **80148800**,A0=80126B58,RA=80181508. DMA3:3030transfers,
2595004bytes,fault0; no GP0refusal. Nextnativeboundary80148800. Goalactive,
no commit/push/completion.

### Camera accessor and smoothing exports

Integrated SC02native/C80148800(3words) and complete native wrappers for
existing main80012C6C(19words),80012ABC(18words). Parent rebuilt integrated
sources and matched3/3,19/19,18/18. Initial overlayharness failed because
mainselectors inserted in overlaysection; moved them to mainsection, reran
637checks successfully in0.63s. No tests weakened. Build95948pending
/tmp/musashi-camerasmooth-build.log. Matrixcallee80049CAC full[80049CAC,80049F38)
163words (paddingnextword) recovered semanticC draft byworker but147word
compileroutput remainsunmatched; stagingonly, no promotion. No commit/push.

Build95948passed; natural camerasmooth/runtime.log running unforced(session43541).
No smoothingexecution claim until natural terminalevidence.

Natural camerasmooth terminal(exit2;session43541closed) passedaccessor,
entered smoothing and reached its callee **80012CB8**,RA=80012C98,
A0=A1=1F4. DMA3:3043transfers,2607504bytes,fault0; no GP0refusal.
Nextnativeboundary80012CB8; smoothing completion not yetproven. Goalactive,
no commit/push/completion.

### Scalar smoothing step wrapper

Wrapped existing80012CB8 C with full21wordnativeexport[80012CB8,80012D0C),
parent rebuilt/reverified21/21.613export/GPUcopychecks pass in0.39s.
Build73636pending /tmp/musashi-smoothstep-build.log. Worker recovered
80012D0C full[80012D0C,80012DBC),44words and verified existing80012B04
full[80012B04,80012B58),21words; nextcallee80012B58. No productionchanges
fromworker; parentverification/integrationpending. No commit/push/completion.

Build73636passed; natural smoothstep/runtime.log running unforced(session2441).
Parent independently rebuilt staged80012D0C and matched44/44,
SHA15973c4d5d283696d04f3801f4c23608cc485faea7fae568df36fff0ae49b8ee.
Still staged; nextangle-step80012B58 recovery underway separately.

Natural smoothstep terminal(exit2;session2441closed) passedscalarwrapper and
reached **80012B04**,A0=A1=2AA,RA=80012AE8. DMA3:3060transfers,2625564bytes,
fault0; no GP0refusal. Do not assume recursive12D0C executed with equalinputs.
Worker additionally staged full80012B58[80012B58,80012C6C),69words matched
workeroracle; parentverificationpending. Nextnativeboundary80012B04.
Goalactive; no commit/push/completion.

### Complete smoothing dependencies

Integrated complete native/C80012B04(21words),80012B58(69words),80012D0C
(44words); parent rebuilt integratedsources and matchedallwords. Added main
Cregistryentries for new12B58/12D0C; existing12B04C retained. Recursive paths
remaincomplete. Tests/build session30370running, buildlog
/tmp/musashi-smoothing-build.log. Parallel fullmatrix49CAC semantic/exactness
investigation continues, unpromoted. No commit/push/completion.

Build30370passed;616export/GPUcopychecks pass in0.40s. Natural smoothing/
runtime.log running unforced(session44912). Worker49CAC draft remains147vs163
words but reports28440bounded differentialcases pass (axis/edge/random/overlap),
mutationdetected. Evidence /tmp/musashi-49cac/verification.json; no byteparity
or naturalruntimeacceptance claim, sourceunpromoted.

Natural smoothing terminal(exit2;session44912closed) passed camera smoothing
and reached **80049CAC**,A0=80126960,A1=801FFE28,RA=80181630. DMA3:
3099transfers,2612144bytes,fault0; no GP0refusal. Nextnativeboundary full
retailmatrix80049CAC[80049CAC,80049F38),163words. SemanticC draft stays
unmatched/unpromoted. Goalactive; no commit/push/completion.

### Retail matrix instruction export

Added complete native-only80049CAC[80049CAC,80049F38),163words, verified
againstEXE andassembly; excludesfollowingpadding. UnmatchedC remainsstaged,
no registrycredit.617export/GPUcopychecks pass in0.37s. Build83572pending
/tmp/musashi-matrix-build.log. Parent independently rebuilt nextSC02transform
wrapper8012F14C full[8012F14C,8012F1A4),22words and matched22/22,
SHA2e4a2d11c10793479488fdb5f8a611519cb226b68e53f2e7705eca393adc7a33.
Wrapperstagedonly, calls4914C/491AC/4945C. No commit/push/completion.

Build83572passed; natural matrix/runtime.log running unforced(session41035).
Matrixwordexecution pending; semanticdraft byteparity remainsunproven.

### Matrix MULTU boundary diagnosis

The matrix run terminated with exit 2 at 80049D78, instruction 01690019
(`multu t3,t1`), inside the complete exported matrix routine. DMA3 reported
3115 transfers, 2638888 bytes, fault 0. The interpreter lacked SPECIAL
function 25; this was not a missing function export.

Added unsigned 32-by-32 multiplication into HI/LO. The regression probe
uses the actual retail instruction and 49 operand pairs, including unsigned
high-bit and negative-looking matrix operands, and checks clock refusal
preserves CPU state. Before implementation the new execution assertion failed
(48.60s). Existing signed MULT and BCD checks remain in the same probe.
This implements arithmetic semantics within the existing execution clock;
it does not establish hardware multiply latency parity.

Parallel pinned builds also verified the existing assembly implementations
8004914C (12 words), 800491AC (8 words), and 8004945C (12 words), under
/tmp/musashi-gte-camera/. These use GTE instructions and are not ordinary C
recovery. No commit or push.

MULTU verification: the full arithmetic probe passed (48.62s), and the native
build passed (/tmp/musashi-multu-build.log). Natural `multu/runtime.log`
terminated with exit 2; see the current boundary below.

Current natural boundary: **8012F14C**, RA=80181684, A0=801FFE28,
A1=801FFE48. The matrix routine returned through its retail caller. DMA3:
3051 transfers, 2593572 bytes, fault 0; no GP0 refusal or overlay mismatch
reported. The exact 22-word C transform wrapper remains staged for integration.
This is instruction-path progress, not human-visible start-screen acceptance.
Goal remains active; no commit/push.

### Transform wrapper and following lighting C

Integrated full SC02 8012F14C [8012F14C,8012F1A4), 22 words. Parent rebuilt
production source with pinned GCC/ASPSX and independently matched 22/22
against the pinned decoded SC02 member (function SHA above). Native export
requires SC02 selection and existing per-word RAM identity. SC02 export and
overlay identity checks: 26 passed. Native build passed. SC02 recovery remains
outside the supported registry targets; no main-registry credit assigned.

Parallel decomp traced return80181684 through the camera callbacks to world
initializer801450C0, followed by8002850C and80028620. Integrated ordinary C
for full80028620 [80028620,80028694),29 words, independently rebuilt and
matched29/29, SHA da41ceeb178ee33d4825ef4e51a47dec4340b59fa90d5d50047e4a00b9df860b.
It copies a16-byte light record, handles a zero vector, and calls80053328.
Registered as complete C; existing native words remain identical. All616
main native-export checks passed. This does not claim the future call has
executed in the current natural run. No commit/push.

Natural transform run terminated exit2 at **8004914C**, RA=8012F170,
A0=801FFE28, SP=801FFDF0. DMA3:3092 transfers,2612260 bytes,fault0.
The next work is the complete GTE rotation-load, translation-load, and vector
transform path called by the recovered wrapper. Read-only audit confirms
shared doCOP2 supports MVMVA4A480012; interpreter admission/transfer handling
is missing. Required sites include LWC2 data0/1, SWC2 data11 (distinct from
legacy control-read site), and delayed CFC2 FLAG31. Preserve exact caller/site
and callback guards. No human-visible acceptance or goal completion claimed.

### SC02 GTE transform path under verification

Added complete retail instruction exports for8004914C (12words),800491AC
(8words),8004945C (12words), retaining their existing assembly branches.
Parent pinned rebuilds matched12/12,8/8,12/12 againstEXE. These are GTE
assembly, not ordinary C. Main export checks619passed.

Added bounded SC02caller/PC/word admission for rotationCTC2, translationCTC2,
LWC2V0, MVMVA4A480012, MFC2IR1/2, SWC2IR3, and delayed CFC2FLAG31.
Owner controls5..7 and read-only31 added with positive and rejected-selector,
context,CU2,lease and reentry tests (4tests,2subtests passed).

First gtetransform natural run exited2 at80129398,RA801452DC; DMA3
3173transfers,2619644bytes,fault0. This run is SUPERSEDED for transform
acceptance: review found the owner raw IR read and initial fixture expected
stale high-half data (7654014A). IR registers must sign-expand their16-bit
value; local PsyCross INLINE_C.C MFC2 and the GTE register specification
https://psx-spx.consoledev.net/geometrytransformationenginegte/ agree.
Owner now calls existing MFC2, with15high-sentinel positive/negative IR cases;
normal/sanitizer regression failed beforefix and owner suites pass afterfix.
Corrected source-path tests and natural rerun still pending.

Parallel recovered full SC0280129398[80129398,80129428),36words ordinary C.
Parent independently rebuilt staged source and matched36/36, SHA
558825bbb197af6fe6653c25bd3f61022a6fb04d484982aecdba712acba2689e.
Source remains staged until current instruction-path verification finishes.
No commit/push or goal completion.

The corrected transform fixture also reproduced a pending-load retry defect:
a refused data read at49470 discarded the preceding MFC2 result. Formatter
now snapshots the pending CPU before retirement and restores it on a GTE
transfer refusal. Direct production-probe rebuild passed after this fix,
including rejection at49470 and49474, positive/saturated/negativeIR3 output,
FLAG delayed acrossJR, and missing caller/selection/callback/memory gates.

UBSan independently reported negative translation left shift at upstream
PsyX_GTE.cpp599. tools/prepare_psycross_gte.py now generates a build copy,
replacing exactly six CV1/2/3 signed shifts with int64 multiplication4096.
All int32 translation inputs fit this operation. CMake and real-GTE fixtures
compile the same generated TU. Counts drift fails closed; focused generator
test passed. The ignored vendor checkout remains unchanged by this work
(existing main/pad edits preserved). No arithmetic implementation replaced.
646 generator/main-export/SC02/identity tests passed. Final sanitizer and
natural run remain pending; do not use the superseded raw-IR run as proof.

Corrected native build passed (/tmp/musashi-gte-final-build.log). Natural
`gtechecked/runtime.log` terminated exit2 at **80129398**,RA801452DC,
SP801FFED0. DMA3:3117transfers,2606708bytes,fault0; no GP0 refusal,
overlay mismatch, or CD word refusal reported. The corrected GTE path returned
through camera callbacks to world initialization and reached the staged
initializer. This replaces the earlier raw-IR run as current runtime evidence.
Existing camera source probe passed a fresh direct rebuild after its stale
IR upper-half expectations were corrected to architectural sign-extension;
assertions and mutation checks were preserved. Final transform baseline passed;
mutation and sanitizer variants still running. No human-visible acceptance.

Final transform suite passed all3variants in166.04s: baseline, wrong output
store mutation detected, and ASan/UBSan with halt_on_error enabled. Both pending
callback refusals preserve CPU/pending state. Positive, saturated, and negative
IR3 results, caller/overlay/device/clock/memory guards, and delayed FLAG store
are covered. No sanitizer suppression added.

Integrated80129398 ordinary C and36-word export after those checks finished.
Production-source pinned rebuild independently matched36/36 (SHA above).
SC02 export/identity checks26passed. Build /tmp/musashi-9398-build.log pending.
No SC02 main-registry credit, no commit/push, goal remains active.

Build9398passed. Natural recordinit run executing (session61238).
Parent independently verified staged next8012944C[8012944C,8012956C),
72words ordinary C, SHA2636077d8b15e0073d1a83e48dffd1ffe52e9892271b5e3907b941411809ec49.
It preserves the conditional retail write clearing instruction8005128C,
then processes both84-byte records with all selector branches. This source
remains staged. Static pinned default params80184C24 fields+36/+38 are0;
worldinit copies them after9398, implying944C's default branches would call
801298F4/801299C8. This is a static inference, not an execution observation.

Natural recordinit run terminated exit2 at **8012944C**,RA8014549C,
SP801FFED0. DMA3:3258transfers,2629380bytes,fault0; no GPU refusal,
overlay mismatch, or CD word refusal reported. The integrated36-word
initializer returned naturally. Next72-word routine is independently matched
and staged; its callees remain under parallel decomp. Goal active; no
commit/push and no human-visible start-screen acceptance claimed.

### Record dispatch integration and pointer-table correction

Integrated full8012944C[8012944C,8012956C),72words ordinary C plus retail
export. Production pinned rebuild matched72/72 (SHA above);26 SC02/identity
checks passed, build /tmp/musashi-944c-build.log passed. Natural recorddispatch
run executing session18107.

Correction to the preceding static inference:80184C24 is a POINTER TABLE,
not the params object. Its default [0] points to8018954C. Actual params
selectors+36/+38 are7FFF/7FFF, so this path should skip298F4/299C8.
The earlier inference reading table+36/+38 as object fields was incorrect.
Parallel decomp redirected to the following worldinit call; no runtime claim
for either skipped callee. Full298F4 nevertheless independently matched53/53
in staged mixed C (empty pointer constraints/memory barrier), SHA
85a655fbd2c89b7495210a290d4d76f0cd585027849366d7dce6aa80f1e7b4c8.
It remains staged, unexported, and unregistered.299C8 draft remains unmatched
157candidate vs158retail words. No commit/push/completion.

Natural recorddispatch exited2 at **801336E8**,RA8014556C,A0=A1=8019EC4C.
DMA3:3176transfers,2631804bytes,fault0. It passed944C and reached the next
unconditional worldinit call. Integrated SC02-specific336E8[801336E8,8013373C),
21words ordinary C; parent matched21/21, SHA
9701149347228b96b46500945c62930f7e101c4b310fb5dad5e3ae6906c5efa7.
The similarly named MAIN12 routine uses different globals and was not reused.

Observed non-null A0 causes336E8 to call36BC4. Integrated complete SC02
80136BC4[80136BC4,80136C1C),22words ordinary C; parent matched22/22,
SHA480288f84195d8170e11a9a7aa663f5f5962f32e49df56cee7e6a54ecc30ea1a.
It relocates six resource offsets only when headerword+8 is20; no callees.
26 SC02/identity checks passed. Resource build5036 pending.

Read-only code-write audit:944C conditionally patches8005128C inside full
mainfunction[80051044,80051804),496words. SC02caller80129870 reaches it;
no current native export covers that function. The write removes0200 from
an E1 command. When this consumer is reached, any native export must honor
this exact mutable word, accepting only original34420200 or patched0, and
refuse other values; immutable extraction would be wrong. No speculative
consumer implementation or general RAM execution was added.

Resource build5036passed. Natural resources run executing session58293.
Next initializer3B83C is272words; full C recovery is in progress. Correct
params8018954C supply (80189C64,0,ED4) from offsets94/98/9C and next3B568
argument801831B4 fromA8. These argument values are static pinned-data checks,
not a claim of execution. SC02 exports remain unregistered in mainregistry.

Natural resources run terminated exit2 at **8013B83C**,RA8014558C,
A0=80189C64,A1=0,SP801FFED0. DMA3:3141transfers,2626576bytes,fault0;
no GPU refusal, overlay mismatch, or CD word refusal reported. The resource
wrapper and relocation path returned naturally. Full272-word next initializer
remains under parallel decomp. Goal active; no commit/push or human-visible
start-screen acceptance claimed.

### Full resource initializer C

Integrated complete SC028013B83C[8013B83C,8013BC7C),272words ordinary C,
all13switchcases retained. Parent independently rebuilt production source at
pinned-O0, linkbase8013B83C, rodata-base801B53EC and matched272/272,
SHA441f5b24ea0089671ab13faea04acc57b92d665e472999d070e83e7159988cd3.
Integrated following unconditional setter8013B568[8013B568,8013B598),12words
ordinary C at-O0, parent matched12/12, SHA
b21f9d66b3984526d211f20b064a786bd95b26de14d3d60954baf407b23de370.
26 SC02export/identity checks passed. Build69611 pending.
Observed A0resource80189C64 has pinned firstbyte46, selectingC08C. Parallel
recovered completeC08C[8013C08C,8013C0F8),27words ordinary C at-O0; staged
pending parent integration. No commit/push or completion claimed.

B83C switch table separately verified: allocated ELF section bytes
[801B53EC,801B5420),13entries, exactly match pinned SC02. Targets
/tmp/candidate.j8bv5x38/unit.elf. This supplements the code-word match.
Build69611passed; natural resourceinit run executing64483. Parent stagedC08C
rebuild matched27/27,SHA212d4f9a5528ca4d9d1d8f2d0c53e06c54c4dd622f38cb983e8c88bec32bc17f.
Next relocation leafC0F8 full154words remains under decomp.

Natural resourceinit run terminated exit2 at **8013C08C**,RA8013BB10,
A0=801B7AA0,A1=80189C64,SP801FFEB0. The runtime took the actual46switch
case through the retail table. DMA3:3118transfers,2642672bytes,fault0;
no GPU refusal, overlay mismatch, or CD word refusal reported. C08C staged
27-word C is independently verified; followingC0F8 full154-word recovery
still in progress. No commit/push/completion or human-visible acceptance.

### Complete resource relocation C

Integrated SC028013C08C[8013C08C,8013C0F8),27words ordinary C and
8013C0F8[8013C0F8,8013C360),154words ordinary C. Parent independently
rebuilt production sources with pinned-O0 and matched27/27 and154/154,
function SHAs212d4f9a5528ca4d9d1d8f2d0c53e06c54c4dd622f38cb983e8c88bec32bc17f
and992327346bec95ac7c4d8895ba9f3f216ebb16b8265343d44ea13f3f9e6bae6f.
C0F8 requires rodata-base801B5494; independently compared all27 emitted table
entries with retail via /tmp/candidate.58r1ynrq/unit.elf. All8opcodecases,
default, nested groups/entries, byte+4 relocation guard and finalguard1 retained.
26 SC02export/identity checks pass. Build34483pending /tmp/musashi-c0f8-build.log.
SC02C remains outside supported mainregistry targets. No commit/push/completion.

Build34483passed. Natural relocation run executing59616. Parallel verified
following MAIN10 800D02C0[800D02C0,800D02D0),4words ordinary C, stores
argument to800D375C; staging/start-screen/main10_800d02c0.c. MAIN10 member
identity checked independently, no MAIN4 substitution. Existing main80011E84
[80011E84,80011EB4),12words C independently matches at-O0. Neither has been
newly exported this pass; artifacts /tmp/musashi-main10-d02c0/.

Natural relocation run terminated exit2 at **800D02C0**,RA801455B4,
A0=801832F4,SP801FFED0. DMA3:3140transfers,2527784bytes,fault0.
Integrated MAIN10D02C0 four-word export and ordinary C; parent matched4/4
against fullmember hash97933aae35fd27e3917866e4f9c7ef5716850c7efda3f96719496c9ba5b86246,
base800AEDF8,functionSHAb6b2f6088a3a57179c89bb35de8b034abf7a703b5197034a1dd0f99b33c73e8c.
An initial verification command mistyped that hash; the corrected exact pin
passed, with no payload changes. MAIN10selection and per-word loaded-RAM
identity now includeD02C0..D02D0, not general RAM instruction execution.

Wrapped existing main80011E84 C with complete12-word native export;
parent rebuilt-O0 and matched12/12,SHA
5a9d8abb1de2a68dd5f3eb629338e1e9f887cfe55363e37ed065499d99434c81.
647 main/SC02/overlayexport-and-identity checks passed. Build19179 pending
/tmp/musashi-worldsetters-build.log. No newregistrycredit forMAIN10.
No commit/push/completion; callback aftersetters remains under investigation.

Build19179passed; natural worldsetters run executing78791. Parallel traced
fresh-scene entry correctly: defaultparams8018954C+C8 ->80188CF0 pointertable,
entry[0] ->80188C60; entry+18=8018129C agrees with earlier observedcallback,
entry+20=800D1E20. MAIN10D1E20 is fulltwo-word emptyreturn; staged ordinary
empty C matches2/2. Static fresh-scene inference only until next runtime
boundary; this return does not constitute substantial gameplay C recovery.

Natural worldsetters run terminated exit2 at **800D1E20**,RA801456D8,
V0=80188C60,V1=800D1E20,SP801FFED0, confirming the entry+20 callback chain.
DMA3:3138transfers,2612632bytes,fault0; no GPU refusal, overlay mismatch,
or CD word refusal reported. Full relocation and both setters returned
naturally. Empty callback is staged, not yet exported. FollowingSC023D3D4
remains under parallel decomp. Goal active; no commit/push/human acceptance.

### Callback return and palette initializer

Integrated MAIN10D1E20[800D1E20,800D1E28), exacttwo-word emptyreturn.
Parent pinned rebuild matched2/2,SHA
6d64edf91449c1b17746c1ef18afa2eb25c70bdf1322ab3df5a2630993b7e2f1.
Both selection and per-word RAM identity cover this callback. This is only
an empty callback, not substantial gameplay recovery. Build3210passed.

Integrated complete SC028013D3D4[8013D3D4,8013D53C),90words ordinary C,
allflagbranches retained. Parent production rebuild matched90/90,SHA
4d49dcf2f462f5c163ad299acc27c1bf1b5b2c6ddf8c071e8554e3b9c6f64dc7.
Correct params8018954C+A0/A4=(801A7D80,3), selecting unconditionalD53C,
thenD8FC andCF68. This is pinned staticpath evidence until runobserved.
28 SC02/MAIN10identity-and-export checks passed; palettebuild88070pending.
Parallel fullD53C (240words) and D8FC/CF68 recoveries remain in progress.
No commit/push/completion or human-visible acceptance claimed.

Palettebuild88070passed; natural paletteinit run executing12934. Parent
independently rebuilt staged SC02D8FC[8013D8FC,8013D9B0),45words ordinary C,
matched45/45,SHA46ada39344314a44e71102cea8c6898255cca82759447730918b02543fdb5ce0.
It scans all16-byte records toFF sentinel and uploads type9 rectangles using
pixels+signedwidth*2. Source remains staged; no claim this upload executed yet.

Natural paletteinit terminated exit2 at **8013D53C**,RA8013D488,
A0=801A7D80,A1=S0=3. DMA3:3085transfers,2640152bytes,fault0;
no GPU refusal, overlay mismatch, or CD word refusal reported.

Added complete native-onlyD53C[8013D53C,8013D8FC),240words and
CF68[8013CF68,8013D064),63words so runtime does not wait for C matching.
Full staged drafts remain unmatched (latest D53C237vs240, CF6874vs63),
no C recovery/registry credit. D53C draft overflow-domain semantics have not
been differentially validated; only retail words execute in native path.
Integrated matched ordinary C D8FC[8013D8FC,8013D9B0),45words; parent
production rebuild matched45/45,SHA above. All28 SC02/overlay checks pass.
Palette-path build82262pending. Parallel matching continues without templates,
padding, narrowed branches, or premature completion claims. No commit/push.

Palette-path build82262passed. Natural palettepath terminated exit2 at
**8013D5FC**,RA8013D488,SP801FFE98,V0=24,V1=5,A0=4. DMA3:
3129transfers,2594668bytes,fault0; no GPU/overlay/CD-word refusal reported.
This is inside the full exported palette routine: LWLv1,55A3(at), word882355A3.
Next required interpreter work is its real nine-byte unaligned copies, not
another function export. First pair has LUI/ADDU between LWL(D5FC) and
LWR(D608), likewise LWL(D614)/LWR(D620). Their safe next instructions are
LUIat,8018 atD600/D60C/D618/D624. Two signed-byte sites follow atD62C
(word802555A8) andD674(word80A20008). Stores D630/D634/D638/D63C and second
copy D654..D670 require exact merge-site admission; preserve byte9 and
neighboring memory. No guard widening implemented yet.

D53C draft still237vs240, report /tmp/musashi-sc02-d53c/verification.json
records80 bounded differentialcases without mismatch and storemutationdetected;
not exact C or overflow-domain proof. CF68 improved to65vs63 with one empty
pointer constraint, still staged/unmatched. No new C recovery credit.
Goal active; no commit/push/human-visible acceptance.

### Palette nine-byte copy guard repair

Added exact16mergePC/word sites for both retail copies and an eight-entry
pending-successor table (kind/register/PC/word). Nonadjacent first pairs retire
across LUIat,8018; the adjacent second pairs preserve existing forwarding.
Added only the two audited SC02 LB sites D62C/D674 with checked RAM preflight.
No merge arithmetic changed; unsupported words, registers, kinds, MMIO,
overlay identity and clock failures still refuse.

New direct production probe reproduced missingD5FCsite before implementation,
then passed afterfix. Coverage: all4sourcealignments,80/FF signextension at
bothLBs, both9-byte outputs, neighboringbytes,16sites x32bitmutations, loaded
word corruption, selection, pending register/kind/clock and invalidmemory.
Full tests/test_pvd_merge.py running28576 (existing semantic mask/store mutants
included); build41994pending. Independent review confirms merge retirement
for this sequence. Limitation: generic LB still publishes immediately, as do
existing ordinaryloads. These successors do not consume the loaded register,
so this proves copy behavior, not IRQ/checkpoint-visible load timing parity.
CF68 draft now63words but only50/63match; still unpromoted mixeddraft.
No commit/push/completion.

Merge build41994passed. Full tests/test_pvd_merge.py passed2tests in150.05s,
including existing independent bad-mask/no-store mutants and newpalettecases.
Natural palettecopy run terminated exit2 at **800CF854**,RA8014576C,
SP801FFED0,V0=800AE6A8,V1=7F000000. DMA3:3157transfers,2609112bytes,
fault0; no GPU refusal, overlay mismatch, or CD word refusal reported.
The corrected copy path progressed to the worldinit cleanup check. Next
MAIN10 function[800CF854,800CF864) loads unsignedbyte800AE6BF and returns
whether nonzero; recovery in progress. No MAIN4 substitution.
Goal active; no commit/push or human-visible start-screen acceptance.

Parent independently rebuilt staged MAIN10CF854 ordinary C and matched4/4,
SHA1a586103d2d9b9c0afc632db119ba7ec242afc779debbf2dc2ab54873d183e2d.
Conditional cleanup after this getter depends on mutable scene+17; no claim
that8001C14C or8012A110 executes until the natural run observes that branch.

### MAIN10 cleanup predicate

Integrated completeMAIN10CF854[800CF854,800CF864),fourwords ordinary C.
Parent production rebuild matched4/4,SHA above. MAIN10selection and exact
loaded-RAM word verification now coverthisrange.29 SC02/MAIN10export and
identity checks pass. Build30907pending /tmp/musashi-cleanup-check-build.log.
The mutable scene+17 predicate will be evaluated naturally, with no planted
flag or inferred cleanup execution. Parallel fullD53C matching continues.
No commit/push/completion.

Build30907passed. First cleanupcheck natural run exited2 EARLIER in movie
at800D34BC (valid SHt2,0(a1),A1=80100EEE),RA800D2764,cycle423491000.
This run did NOT reach the new cleanup check. SOURCE_CLOCK, DEVICE_EPOCH,
and SPU_OWNER faulted1; CD owner/DMA3/IRQ/video/input faults0. SPU snapshot
samples551419,submitted551314,queued7721. Specific SPU refusal cause remains
unresolved; no CPU/store guard changes justified. Original log preserved.
An identical-build retry will distinguish recurrence; no dependency bypass,
audio driver substitution, or queue-limit change. Cleanup branch unobserved.

Identical-build cleanupretry run terminated exit2 at **801284B8**,
RA801282BC,V0=801284B8,SP801FFF30. Sourceclock/SPU faults0; DMA3:
3152transfers,2648552bytes,fault0. World initialization returned to the scene
entry dispatcher and reached its next loop routine. This establishes forward
progress, not deterministic immunity to the earlier audio failure.

Read-only audio audit found a concrete hypothesis: queue-capacity skipping
advances sample_index without retiring pending XA frames; consume_pending then
refuses a head sample older than sample_index. The failed log's105sample gap
supports but does not prove this cause. A public-API regression is being built;
no queue policy/limit changes or fault suppression made. Earlier failure stays
unresolved. Scene loop801284B8 full decomp now in parallel. No commit/push.

### Scene-loop continuation and pending XA retirement

Parent independently rebuilt SC02 `801284B8` (14/14 words, SHA256
72df937302ffb4d520a067b7c6e3c044105e646158dd63af6559af34a146e90f)
and its unconditional MAIN10 `800D0588` callee (24/24 words, SHA256
a6b11be1271f28b710329f57e700aadd746c0c2611154e73480fc4e62ef45d69).
Both complete ordinary-C functions are integrated with exact native exports,
selected-overlay guards, and live instruction checks. No registry credit added.
D0588 calls D05E8; that callee is staged by parallel decomp, not yet integrated.

The public SPU API regression reproduced stale pending XA after the existing
sample-skip policy advanced the cursor. Production now retires only pending
frames strictly before the new cursor at both existing skip sites. Equal and
future samples remain queued; ordinary stale-input refusal and queue limits
remain unchanged. Tests cover no-drop control, limited/full queue, large skip,
resumed PCM order, and past/equal/future timestamps. Agent reports full probe
ASan/UBSan PASS. Parent focused SC02, overlay identity, and SPU run: 31 passed.
This proves the isolated regression, not attribution of cleanupcheck's earlier
native audio fault. Natural verification of the rebuilt binary remains pending.
No commit, push, or human-visible acceptance.

Build 21363 passed. Natural `sceneloop/runtime.log` run (session 38878)
terminated exit 2 at **800D05E8**, RA=800D05A0, A0=0000301C,
SP=801FFEF8. Both newly integrated functions were traversed naturally.
SOURCE_CLOCK faulted=0; CD DMA3 transfers=3177, bytes=2594000, fault=0;
SPU sample_index=2245796, submitted=2191102, queued=7727, faulted=0.
This run observed no recurrence of the earlier SPU fault; it does not prove
that earlier failure's cause. No human-visible or audible-game acceptance.

Parent independently rebuilt staged MAIN10 D05E8: 18/18 words over
[800D05E8,800D0630), SHA256
64d2fdb466d8342b3a9987e7e1fdf39fc69e203025c2e4b18bd511d32a958bca.
It uses one empty register constraint (mixed C), calls 80029504 and optionally
D_800D375C. This is the current observed refusal and next integration step;
its staging match alone does not advance the executable. Goal remains active.

### MAIN10 scene callback dispatch

Integrated D05E8's complete 18-word span with MAIN10 selection and exact live
instruction checking. Rebuilt against the pinned member: 18/18 MATCH, SHA256
64d2fdb466d8342b3a9987e7e1fdf39fc69e203025c2e4b18bd511d32a958bca.
SC02 and overlay identity checks: 31 passed. Build 91441 passed.
Natural run `scenecallback/runtime.log`, session 20991, started unforced.

Parallel recovery traced params 8018954C+D4 -> 801832F4, passed through
D02C0 into D_800D375C. Callback returns numeric 60 when byte80078EAF==4,
otherwise 10. Corrected D0588's provisional pointer return/local/argument
annotation to int; parent rebuild remains 24/24 exact, unchanged native words.
Callback 801832F4 is staged, not yet exported. No registry credit, commit,
push, or completion claim.

Natural session 20991 terminated exit 2 at **801832F4**, RA=800D0614,
V1=801832F4, A0=0, SP=801FFEE0. This directly observes the callback target
predicted from the parameter table. SOURCE_CLOCK faulted=0; CD DMA3
3197 transfers, 2579252 bytes, fault=0; SPU samples=submitted=2105852,
queued=7687, faulted=0. No live run remains.
Parent independent callback rebuild matches 8/8 words, SHA256
3607ac82fecc9dd7436f47162a69bff95fdae07afc4e165d5d1c972e9cf9b74b.
Current refusal is 801832F4; callback integration is next. Parallel recovery
continues on its caller's subsequent main8001ABBC path. No human acceptance.

### SC02 numeric scene callback

Integrated the complete ordinary-C callback [801832F4,80183314), with all
8 retail words, selected SC02 gating, and existing live instruction validation.
Parent pinned rebuild: 8/8 MATCH, SHA256
3607ac82fecc9dd7436f47162a69bff95fdae07afc4e165d5d1c972e9cf9b74b.
SC02/overlay tests: 31 passed; every SC02 exported word is included in the
selector and retail checks. Build session 15505 pending. No registry credit.

Build 15505 passed. Natural callbackreturn run (session23580) terminated
exit2 at **800424E4**, RA80042434, A0=10CB, A1=1, SP801FFFA0.
SOURCE_CLOCK faulted=0; CD DMA3 3170 transfers/2610128 bytes/fault0;
SPU samples=submitted=2051139, queued1275, faulted0. Callback was passed.
However CPU_OWNER reports faulted=1, sequence8990, SR40020404,
kernel_entered1, host_depth0. BIOS_IMAGE and GTE owners report no fault.
800424E4 already has its full39-word export and selector. Therefore this
is NOT established as missing code; CPU runtime/checkpoint diagnosis is the
next gate. No guard widening or replacement export is justified. Original
log preserved; read-only parallel diagnosis dispatched. No human acceptance,
commit, push, or goal completion. No native run remains live.

### CD IRQ callback refusal identified; main C recovery

The callbackreturn log identifies the actual fault: CD_IRQ frame_faulted=1,
instructions=248, call=80045300, target=800377D8, callback slot=800377D8,
kind=01, budget_exhausted=0. The foreground 800424E4 is interrupted here;
it is not a missing export. cd_irq_call rejects this callback target, and the
scheduler propagates that refusal into CPU_OWNER faulted=1. No CPU-status
repair or additional instrumentation is justified. Complete callback recovery
[800377D8,80037CC8) (316 words) is underway alongside contract inspection.

Recovered and integrated complete main8001ABBC C [8001ABBC,8001ACF0).
Parent staged and production rebuilds both match77/77, SHA256
b88da7f15152ace1a493f28edfb569bb9cee02517e714b4d14f29111abf7e79f.
One empty compiler memory barrier is retained; registry deliberately records
mixed recovery, although the automatic classifier treats empty asm as C.
All branches retained. Native word export unchanged. 620 main export tests
pass. This improves source recovery without claiming executable progress past
the IRQ refusal. No commit/push/human acceptance; goal remains active.

### Full sector-transfer IRQ body and contract work

Added exact native-only export [800377D8,80037CC8), 316 words, from pinned
EXE SHA66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a.
Function SHA256672095aa6fccee963c3af141383ae789c421272709588c8b94639a4ce07e21d1.
Both main and CD IRQ word selectors include the full body. Main export tests:
621 passed. No registry entry or C credit: complete staged C is303 words,
not316, with merged branches/global-load scheduling still unmatched.

The actual callback root is distinct from LIST: return80045308, direct
calls at depth1/SPsaved-70; header43994(800762A0,3), header conversion43B1C,
state1/2/3 sector43994(live80076228,200), then real3C4F0/3C498/3C438 calls.
All descendant bodies already have CD IRQ exports, but contracts remain to be
extended under this exact root. Worker owns cd_irq_call only; separate test
worker owns real CD/DMA fixture and state/negative controls. No native rerun
has occurred; prior callback refusal remains authoritative until verification.

### Bank callback contracts implemented, verification in progress

Added a separate 377D8 callback root in cd_irq_call with live slot/kind,
return/depth/SP, header/payload and real descendant argument checks. Parent
reviewed 3C498's UINT_MAX forwarding and 3C438's unsigned size clamp against
retail words. Continuous CD data-pointer validation now covers this root.
Added existing 26-word 3B0E4 export to CD IRQ word map for four exact SPU
poll-delay call sites. No generic callback admission or CPU fault suppression.

Two genuine polling-timeout diagnostic edges (3A934 and3A9C8 to5C604)
remain refused because no bounded IRQ formatter service exists. Normal-path
contract additions do not prove SPU MMIO or timeout completion. New real
CD/DMA fixture is being debugged; state3 and negative coverage remain pending.
Existing CD IRQ regression session65610 remains live. Build63120 passed before
last CD-word addition; updated build24477 remains live. No new native run.

Bounded C recovery377D8 ended at304 vs316 words (82 positional matches),
with two empty state-register constraints. Full states retained; oracle length
mismatch persists. No promotion or C credit. Prior callbackreturn runtime log
remains the authoritative natural refusal until the next verified run.

Updated build24477 passed. Existing CD IRQ tests session65610 completed:
5 passed in160.88s. New stream fixture independently exercises real CD/DMA
state1 and state2 normal progress/threshold transitions and wrong-header path;
fullstate3 and negative controls remain pending, not implied by those passes.

Natural bankcallback run session32649 terminated exit2 at **801284F0**,
RA801282BC,V0=801284F0,V1=3,SP801FFF30. CPU_OWNER faulted0,active0;
CD_IRQ frame_faulted0,instructions300,call=target=FFFFFFFF,budget_exhausted0;
SOURCE_CLOCK faulted0; DMA3 3360 transfers,2820864 bytes,fault0; SPU
samples=submitted=2110302,queued1009,faulted0. The previous CD callback
refusal was passed naturally. No claim that every transfer state or SPU
poll-timeout branch executed, nor human-visible/audible-game acceptance.
Current natural boundary is SC02 801284F0; parallel decomp notified.
Contract-negative and state3 fixture work continue. No commit/push/completion.

State3 fixture's first refusal3A8BC is SHv0,1A8(a0) toSPU FIFO1F801DA8.
Production routing already admits it; real SPU owner requires valid transfer
address and transfer_control4. Fixture omitted source-init3A6D8's control4
write. Test worker corrected that source-backed setup and is rerunning the
same fixture; no production guard change made for this refusal.

### Scene dispatch and bounded PIO execution

Parent independently matched/integrated SC02 801284F0 19/19 words, SHA256
ce466496c7ef738b7c5a59ac80616605ca2654fd536e77776f3ef87b37470e27.
Calls75308,E8F0, conditional75494 then main11C10. No SC02 registry credit.
Recovered main3C438(24 words) and3C498(22 words) independently matched and
integrated ordinary C, existing native exports unchanged, registry records C.
Main/SC02/identity checks:652 passed. Callback contract negative test passed
separately (1 test51.27s); it covers contracts, not device execution.

Real PIO state3 fixture subsequently exhausted the original16384 instruction
budget at3B12C after160 FIFOhalfwords, with valid SPU address/control and no
hardware fault. Added a dedicated one-time allowance after the accepted
bank3AE78->3A84C call: (ceil(bytes/64)+1)*70000+2048, bounded to bytes<=7EFF0.
Source uses three fixed delay calls (<1100instructions each) perbatch and
<=F01 polls (<16instructions each), plus stores/control; extra batch covers
initial delay/final poll. Independent review agrees bound fits unsigned
(max569032048). It is selected only within the accepted PIO frame; ordinary
IRQbudget unchanged. PIO exhaustion reports3, no loop replenishment. Fixture
rerun and budget-negative control pending; no success claim yet. The two
poll-timeout formatterdiagnostics remain refused. No new build/naturalrun
since these edits; prior natural801284F0 remains authoritative.

### SC02 state initializer

Parent rebuilt and integrated complete75308[80175308,80175414),67/67 words,
SHAfe4680409f3f90f92997216a185f7f9a2f0940703e5b69d066fe61a9957dcf29.
Mixed C uses empty register constraints; no SC02registrycredit.31 focused
SC02/identity tests pass. Build26122 running. Next directcallee757A0 is
staged ordinaryC16/16, not yet integrated or naturally observed.

DedicatedPIO allowance let the realfixture transfer1024halfwords/full2048bytes,
SPUcursor1810,109984instructions. It then refused at3C538(TestEvent), with
no budget exhaustion; fixture has not yet bound its realbios_events adapter.
Thus fullPIO transfer is observed in the fixture, wholecallback success still
pending. No fake event result or production eventguard change made.

Build26122 passed. Natural scenestate session44913 exited2 at **801757A0**,
RA801753E0,A0=0,SP801FFF00. CPU_OWNER/sourceclock/CDIRQ/DMA3/SPU faults0;
DMA3 3359 transfers,2859908 bytes. SPU samples=submitted2078547,queued882.
Initializer was traversed naturally; no live run remains. Parent nextcallee
757A0 independently matches16/16, SHA256
1156e2a1c09e2269293b4774b03f965c9caf07e753a2450242b2dfe915ddfc68;
it remains staged. Current natural refusal is757A0. Fullstate3 fixture and
budget-negative controls still pending; natural progress does not replace them.
No human/audible acceptance, commit, push, or goal completion.

### Buffer setup wrapper and consecutive-sector PIO

Integrated ordinaryC757A0[801757A0,801757E0), production rebuild16/16 MATCH,
SHA1156e2a1c09e2269293b4774b03f965c9caf07e753a2450242b2dfe915ddfc68.
All31 focused SC02/identity tests pass. Build73579 passed; updated build62267
includes subsequent PIO renewal fix and remains live.

The real double-speed fixture completed1024PIOhalfwords, then anotherINT1
reached3AE78 with110085instructions. One grant perIRQ was too restrictive:
multiple source sectors can arrive during a PIO call. Renewal now requires
prioracceptedPIOframe return at expectedSP and exactlyconsecutiveD8007623C
(noUINT_MAX wrap), resetscompletiononnewgrant, and leaves ordinaryIRQbudget
unchanged. No replenishment inside a stalled loop. Independentreview and
finite-twochunkfixture/negative tests pending. Fixturemetadata was previously
A5sentinel rather than a finite bank; worker is making that explicit synthetic
input finite, not changing native data. No new naturalrun yet.

Build62267 passed. Natural buffersetup session15853 exited2 at **80175DA8**,
RA801757BC,A0=0,SP801FFEE8. CPU/sourceclock/CDIRQ/DMA3/SPU faults0;
DMA3 3336 transfers,2853460 bytes; SPU samples=submitted2059174,queued1022.
No live native run remains. Parent independently matched fullstaged5DA8:
231/231 words [80175DA8,80176144), SHA256
b8508b67d07b9873edfb88990c58a04015fd1e820049cd847bb44ca1d580d2e7.
Mixed C, allfivepackets/branches retained; not yet integrated.

Testworker directfinite-state3 probe now completes twoactualsectors:
2048bytes sector16 +64bytes sector17 ->SPURAM1010..1850,1056halfwords,
finalstate4/recordcomplete. Exact media/neighbor checks pass; zeroPIOallowance
mutant detects exhaustion3. Freshbaseline+mutantpytest94387 stillrunning.
Independentrenewal-negative fragmenttest passed1test: completion,consecutive/
skipped/backward/wrappedLBA,missingstate,maxsize,framebudget and preservation
of exhausted ordinarybudget. Extractedproductionfragments assume outercall/
returngates validated; fullIRQexecution is the separate streamfixture.
No humanacceptance/commit/push/completion. Currentnaturalrefusal5DA8.

### Packet-builder integration and stream-test closure

Fresh stream suite94387 completed exit0:2 passed107.02s. Real fixture covers
state1/2 transitions, headerfailure, invalidtransferbounds/deviceguards, and
finite two-sector state3PIO+realBIOS completion (2112 exactSPURAMbytes,
finalstate4). ZeroPIOallowance mutant detected. Parent renewalnegative rerun:
1 passed. This closes the tested PIO profile, not DMA4/physicaltiming/audio
or human acceptance. Existing unsupportedpoll-timeoutdiagnostics remain so.

Integrated fullmixedC5DA8[80175DA8,80176144),231/231 parentproductionmatch,
SHAb8508b67d07b9873edfb88990c58a04015fd1e820049cd847bb44ca1d580d2e7.
Allbranches/five-packetloop retained; selectedSC02+liveinstructionguards.
31 focusedchecks pass. Build26419 pending. No registrycredit forSC02;
nextdirectcallee7742C recovery inparallel. No newnaturalrun yet.

Build26419 passed. Natural packetbuilder session1169 exited2 at **8017742C**,
RA80175F44,A0=800A4FAC,A1=FFFFFF31,SP801FFEB8. CPU/sourceclock/CDIRQ/SPU
faults0. The packet builder progressed to its firstcallee naturally. Current
7742Cfullspan is109words; stagedfullC currently110words and unmatched,
so no promotion. Boundedmatching continues, no native run remains live.
No human acceptance, commit, push, or completion; current naturalrefusal7742C.

### Meter-packet routine

Integrated complete109-word7742C[8017742C,801775E0) under selectedSC02 and
liveinstructionguards. Source SHA256
adcdfbf9ef32e734be83965c5dbe3404b85faef43ffe4e644bafa07a92b4bc68.
Initially native-only, then parallelmatching resolved fullmixedC109/109;
parent independently rebuilt and matched before inserting C. Empty register
constraints preserve scheduling; alltwo-packet/colorcycle branches retained,
no instructiontemplates/padding. Nativewords unchanged, noSC02registrycredit.
31 focusedchecks passed. Build48785 pending. Next757A0secondcallee75AB8
recovery dispatched. No newnaturalrun yet; currentobservedrefusal7742C.

Build48785 passed. Natural meterpackets session5005 exited2 at **8005A600**,
RA80175F60,A0=800A5614,A1=0,SP801FFEB8. CPU/sourceclock/CDIRQ/SPU faults0.
742C was passed naturally. Parallel verification found existingordinaryC
src/main/8005a600.c matches22/22 over[8005A600,8005A658),SHA256
f8cbddb0fb649f9aa71eab543660fb6394cb2393a1ab268a3cb0c75aa28df77f.
It calls5AB00/5AD34 and still lacks nativeexport; that is nextintegration,
notnewCrecovery. No live naturalrun remains. Goalactive/nohumanacceptance,
no commit/push. Currentobservedrefusal5A600.

### Draw-mode packet helper

Parent independently rebuilt existingC5A600[8005A600,8005A658),22/22 MATCH,
SHAf8cbddb0fb649f9aa71eab543660fb6394cb2393a1ab268a3cb0c75aa28df77f.
Added complete nativewordexport/CMake/mainselector. ExistingC retained, no
newrecoverycredit.622 mainexport tests pass. Directcallees5AB00/5AD34 already
have nativeexports; parallelC/provenanceverification dispatched. Build60246
pending; no newnaturalrun yet. Currentobservedrefusal remains5A600.

Build60246 passed. Natural drawmode session52984 exited2 at **80175AB8**,
RA801757C4,A0=0,SP801FFEE8. CPU/sourceclock/CDIRQ/SPU faults0.5A600 and
its coveredcallees were passed;5DA8 returned. Currentnextfullspan
[80175AB8,80175DA8) is188words. ParallelfullC draft190words remainsunmatched
(80 positionalmatches only, oraclelengthrejection); noCpromotion. Source
allcalls/branches retained. No native run live; goalactive/currentrefusal75AB8.
No humanacceptance/commit/push.

### Remaining UI packet builder

Exported complete retail75AB8[80175AB8,80175DA8),188words frompinnedSC02,
SHA9fb936d11257e19fb427a5318a62e58c31fa3921ece74d01234ebba8a0ac7193.
Selectedoverlay and livewordguards retained;31 focusedchecks pass. This is
native-only coverage; full190wordC draft staysstaged/unmatched, noCcredit.
Build87948 pending; parallel decomp followsfirstcallee76D94. No newnaturalrun.

Build87948 passed. Natural uipackets session22464 running. Independently
matched/integrated fullmain5AB00 mixedC22/22, SHA256
34d4b432819cbc5696b552f0c5e93397fd7d3e0f89af405cf4258b007dbf7de3.
Registerbinding/emptyconstraint retained, no emittedtemplates. Registrymixed,
nativewords unchanged.622 mainexportchecks pass.5AD34 fullordinarydraft
remains28/33 matching, notpromoted. No natural outcome claimed yet.

Natural uipackets22464 terminated exit2 at **80176D94**,RA80175B50,
A0=800A5628,A1=FFFFFF58,SP801FFEA0. CPU/sourceclock/CDIRQ/SPU faults0.
Full75AB8 execution reacheditsfirstcallee. No live native run remains;
76D94 decomp already inparallel. Currentnaturalrefusal76D94; nohumanacceptance,
commit,push,orcompletion.

### Status packet builder

Exported full76D94[80176D94,80176FF4),152retailwords, SHA256
2d96f3093eb3d30c8ee0286b72aa4b1f06dfa2984685be9dabe6896141de50e8.
All31 focusedSC02/identity checks pass. Cdraft isfull152words butonly120
match; noCcredit/promotion. Nativecode remainscomplete/source-backed with
selectedoverlay/livewordchecks. Build4833 pending. Parallelfirstcallee77784
nowstagedordinaryC14/14, notintegrated. No newnaturalrun yet.

Build4833 passed. Natural statuspackets51908 exited2 at **80177784**,
RA80176DD8,A0=800A5628,A1=0088FF58,SP801FFE60. CPU/sourceclock/CDIRQ/SPU
faults0.76D94 reacheditsfirstcallee naturally. Parent independent77784
rebuildmatches14/14,SHAe28f1647fdc59471d42580082d2df201360bf33b7b3a6fdedce5ac1c9e0a6cb5,
stillstaged, nextintegration. Numerichelper783D0 recovery inparallel.
No native run remains live; currentnaturalrefusal77784. No humanacceptance,
commit,push,orcompletion.

### Glyph helper

Integrated ordinaryC77784[80177784,801777BC),14words previouslyparentmatched,
SHAe28f1647fdc59471d42580082d2df201360bf33b7b3a6fdedce5ac1c9e0a6cb5.
31 focusedchecks pass; build63319 pending. Numerichelper783D0 independently
rebuilt26/26 [801783D0,80178438),SHA256
1c7a8836a1d1776db62d639b25a70f67604568b7602f4c6690850324fd8b6bbd,
remainsstaged. Current16-bit/clamped callers/shift0,4,8 fit C shift domain;
arbitrary oversizedinputs notclaimedportableC-equivalent (MIPS masks counts).
No newnaturalrun yet; noSC02registrycredit/humanacceptance/commit/push.

Build63319 passed. Natural glyphs10129 exited2 at **801783D0**,RA80176E40,
A0=FA(250),A1=4,SP801FFE60. CPU/sourceclock/CDIRQ/SPU faults0. Glyphcalls
completed naturally; observednumericinputfitsdocumentedCdomain. No live
nativerun remains. Currentrefusal783D0; staged26/26source readyforintegration,
777BC recovery inparallel. No humanacceptance/commit/push/completion.

### Numeric packing helper

Integrated783D0[801783D0,80178438),26words/ordinaryC parentverifiedpreviously,
SHA1c7a8836a1d1776db62d639b25a70f67604568b7602f4c6690850324fd8b6bbd.
Productioncomment records boundedCshift domain; nativeMIPS retains masked
shift behavior.31 focusedchecks pass, build47790 pending. No newSC02credit,
no runtimeclaim beyond previous783D0 refusal.777BCdecomp remainsinparallel.

Build47790 passed. Natural numeric73307 exited2 at **801777BC**,RA80176E78,
A0=800A5678,A1=2500,SP801FFE60. CPU/sourceclock/CDIRQ/SPU faults0. Numeric
conversion returnednaturally. Currentdigithelperfullspan[801777BC,801778A8)
59words; stagedmixedCcorrectsizebut17/59match, notpromotable. Allbranches
retained; noCcredit. No live native run remains; nextfullretailcoverage777BC.
No humanacceptance/commit/push/completion.

### Digit packet loop

Exported full59-word777BC[801777BC,801778A8),SHA256
0fa2e36925478fcd70f5a30475877df57f8ce80e1ec1511e0fccc63a80380e4e.
SelectedSC02/liveinstructionguards retained;31 focusedchecks pass. FullC
staysunmatched17/59, noCcredit. Build67826 pending; followingactual75AB8
callee770E0 decomp inparallel. No newnaturalrun yet.

Build67826 passed. Natural digitloop17658 exited2 at **801770E0**,
RA80175B80,A0=800A50EC,A1=FFFFFF58,SP801FFEA0. CPU/sourceclock/CDIRQ/SPU
faults0. Digitloop and76D94 returnednaturally. Currentnextspan
[801770E0,80177340)152words; fullC120/152matched, remainsstaged/unpromoted.
Allcalls/three trailingrectangles retained; tail scheduling unresolved.
No live native run remains; currentrefusal770E0. No humanacceptance,
commit,push,orcompletion.

### Second status packet builder

Exported full70E0[801770E0,80177340),152retailwords,SHA256
35097fec875b4f2bce8bc48a96f009f0a89e39389e25ec9ee6014705f8f81300.
SelectedSC02/liveinstructionguards retained;31 focusedchecks pass. FullC
120/152 draft remainsstaged/unmatched/noCcredit. Build92302 pending;
parallelrecovery followsactual75AB8nextcallee77EA4. No newnaturalrun yet.

Build92302 passed. Natural secondstatus13399 exited2 at **80177EA4**,
RA80175BC8,A0=800A51F0,A1=100,SP801FFEA0. CPU/sourceclock/CDIRQ/SPU faults0.
70E0 andnumericconversion returnednaturally.77EA4 andfollowing77B5C recovery
inparallel. No live native run remains; currentrefusal77EA4. No humanacceptance,
commit,push,orcompletion.

### Seven-glyph number strip

Exported full77EA4[80177EA4,80177F84),56retailwords,SHA256
d7fb449042e542cb0ac033500bef4b588ad7bbf0fc460385ea10330ca863d134.
Allsevenpackets/leadingzero branches retained; selectedSC02/livewordchecks.
31 focusedchecks pass. FullC27/56 draft remainsstaged/unmatched/noCcredit.
Build73027 pending;77B5C and77940 recovery inparallel onactual75AB8callpath.
No newnaturalrun yet.

Build73027 passed. Natural numberstrip47559 exited2 at **80177B5C**,
RA80175C2C,A0=800A527C,A1=10,SP801FFEA0. CPU/sourceclock/CDIRQ/SPU faults0.
77EA4 returnednaturally. Current77B5Cfullspan[80177B5C,80177DA8),147words,
Cmatchingstillunresolved inparallel. Following77940full101worddraft14/101
matches remainsstaged/unpromoted. No live native run remains; currentrefusal
77B5C. No humanacceptance/commit/push/completion.

### Palette-selected digit packets

Exported full77B5C[80177B5C,80177DA8),147retailwords,SHA256
a6ab5b8c5bec96f3dcd3520e04838b36b0aa2a4ac94d2a89f7e417163fff889f.
SelectedSC02/livewordguards retained;31 focusedchecks pass. FullordinaryC
143worddraft remainsstaged/unmatched; registerallocation/signextension/UV
folding unresolved, noCcredit. Build54985 pending;78298recovery inparallel
alongobserved75AB8callpath. No newnaturalrun yet.

Build54985 passed. Natural palettedigits76962 exited2 at **80177940**,
RA80175CC0,A0=800A5948,A1=0,SP801FFEA0. CPU/sourceclock/CDIRQ/SPU faults0.
77B5C returnednaturally.77940fullspan101words remainsnextretailintegration;
its fullC14/101unmatched. Later78298stagedordinaryC78/78 suppliedbyworker,
notyetparentverified/integrated. No live native run remains; currentrefusal
77940. No humanacceptance/commit/push/completion.

### Clock digit packets

Exported full77940[80177940,80177AD4),101retailwords,SHA256
85c239ad8ee3c7b442a1021b3619d2f4daa8777fa167f7b9108a0360516ef170.
31 focusedchecks pass; selectedSC02/livewordguards retained. C14/101draft
remainsunmatched/noCcredit. Build86007 pending. Parent independent78298
rebuild78/78,SHA877e1f8f1fc80ad2f24d89d606edc8d64e55129f8ba2fcd01675a091464bd149,
remainsstaged. Parallel actual-call work: main24054 and SC02 **80176144**, the third
757A0 callee. No newnaturalrun yet.

Build86007 passed. Natural clockdigits93857 exited2 at **80024054**,
RA80175CF0,A0=801B5F90,A1=801FFEB8,SP801FFEA0. CPU/sourceclock/CDIRQ/SPU
faults0.77940 returnednaturally. Fullmain24054span[80024054,800241C0),91words
verifiedbyworkeragainstEXE; ordinaryC88worddraft remainsunmatched. No native
run remainslive. LaterSC026144 independentlyparentmatched53/53,SHA256
dca61a9505bbde362796fc44ada9c5364319b34d8cc644480e6675c2416f99a3,
stillstaged. Currentrefusal24054; no humanacceptance/commit/push/completion.

### Text-code conversion

Exported fullmain24054[80024054,800241C0),91retailwords,SHA256
ef505b6590a3b64217812a570f2062b407e5faae70ef372f712f28bcf0750562.
PinnedEXEidentity checked; allinternalbranches/tables retained.623 mainexport
checks pass. FullordinaryC88worddraft staysstaged/unmatched/noCcredit.
Build72048 pending. ParallelSC028004recovery followsverified6144callpath;
78298/6144 remainverifiedstaged forupcomingintegration. No newnaturalrun yet.

Build72048 passed. Natural textconvert4846 exited2 at **80178298**,
RA80175D1C,A0=800A59AC,A1=801FFEB8,SP801FFEA0. CPU/sourceclock/CDIRQ/SPU
faults0. Textconversion returnednaturally. Parallel24054matching resolved
91/91; parent staged+productionrebuilds independentlyMATCH. IntegratedmixedC
with emptyoffsetconstraints/fixedinputregister; no emittedtemplates/padding.
Registrymixed, nativewordsunchanged. No extra symbolmapneeded forproduction
build (addressnamedglobals auto-resolve). Later8004fullC163vs165remainsstaged.
No live native run remains; currentrefusal78298 verified78/78source ready.
No humanacceptance/commit/push/completion.

### Glyph parser continuation

Integrated the complete SC02 `80178298` parser (78 words). The production
ordinary C independently matches the pinned member 31 bytes, SHA256
`877e1f8f1fc80ad2f24d89d606edc8d64e55129f8ba2fcd01675a091464bd149`.
The build completed and 31 focused SC02/overlay checks passed. Natural run
`glyphparser` (session 98985) exited 2 at `80176144`, RA `801757CC`,
with CPU, source-clock, CD IRQ, and SPU fault flags clear. No human acceptance.

Rebuilt and verified the complete staged `80176144` routine: 53/53 words,
SHA256 `dca61a9505bbde362796fc44ada9c5364319b34d8cc644480e6675c2416f99a3`.
Integrated its ordinary C and guarded native word export. Validation and
natural continuation are pending. Parallel `80178004` C recovery continues.
No registry credit for unsupported SC02 targets; no commit or push.

The production `80176144` C rebuild also matches 53/53 words. All 31 focused
checks passed, and build session 58559 exited 0. Natural run `scenebuffer`
(session 37821) is now testing the continuation; its result remains pending.

Natural `scenebuffer` session 37821 exited 2 at **80178004**, RA `801761C8`,
A0 `800A53F8`, A1 `00000020`, SP `801FFEC8`. CPU, source-clock, CD IRQ,
and SPU fault flags are all zero. This confirms the next actual callee.
No native run remains live; parallel C recovery of this full routine remains
in progress. Start-screen acceptance is not yet proven.

### Fixed glyph and percentage packets

Integrated the complete retail `80178004` span `[80178004,80178298)`,
165 words, SHA256 `05abbc84e40dd593100f2b2b37c71c8ee71243624095298673e6ad697ec146fd`.
Pinned decoded SC02 member identity was checked before extraction. The full
word export retains all fixed glyphs and numeric-loop branches; C recovery
remains unmatched and continues in parallel. No C or registry credit claimed.
All 31 focused SC02/overlay checks pass. Build session 87893 is pending.

Build 87893 passed. Natural `fixedglyph` session 47641 exited 2 with a
CD IRQ failure: ordinary budget exhausted (`1`) after 16384 instructions,
stop call/target `8003AD08`, callback slot `800377D8`, kind 1. Foreground
CPU boundary is `80034BFC`; CPU and CD IRQ fault flags are set, while
source-clock and SPU flags remain zero. This run does not prove return from
`80178004`. The next action is diagnosis of the exact `8003AB84` polling
path and SPU status behavior, not a foreground export or blanket budget raise.
No native process remains live. Parallel `80178004` bounded matching still
produces 163 versus 165 words; `8016E8F0` recovery is active.

### IRQ diagnosis and parallel recovery

The `fixedglyph` trace shows consecutive SPU transfer-address writes from
`8900` through `9C00` near exhaustion. Current dispatch renews ordinary
allowance for completed LIST callbacks but has no corresponding bank-root
completion renewal. This suggests cumulative productive callbacks, pending
a regression that distinguishes that cause from a stalled poll. No budget
or device semantics changed on this evidence alone.

Independently rebuilt staged SC02 `8016E8F0`: ordinary C matches the full
10-word span `[8016E8F0,8016E918)`, SHA256
`62909794114d0bdb5cf44e589b0854287c52239910b55367f5b2ccdd98940cef`.
The verified scene routine calls it unconditionally after `80175308`; it
calls `80016714(80115218,80)`. It remains staged, without registry credit.
Parallel decompilation now also covers the actual IRQ callee `8003AB84`.

Read-only diagnosis confirms `8003AD08` precedes the first poll read at
`8003AD14`. The last SPUCNT value `C0A1` already has the expected DMA-write
mask `20`; transfer address `9C00` reads back correctly. Owner fault flags
are zero. Thus this stop is cumulative ordinary-budget exhaustion before
the poll, not a demonstrated SPU polling failure. Regression work is assigned
to the stream-fixture owner. Proposed renewal requires a validated bank-root
return, consecutive LBA, starting state 1..3 and final state 1..4; unchanged,
skipped, wrapped, no-op and error paths must not receive renewed allowance.

### Guarded bank completion renewal

Added ordinary-budget renewal only after the accepted bank root returns at
`80045308` with matching stack/depth, initial state 1..3, final state 1..4,
and exactly one consecutive nonwrapping source sector. No renewal occurs
inside a poll or on nested, inactive, failed, repeated or skipped progress.
The dedicated PIO allowance remains separate. New exhaustive state/sector
helper test failed before implementation; it and the existing PIO-renewal
test now pass (2 tests). The first build caught a wrong byte-reader helper
name; corrected to existing `read8`, then build 30092 passed. Natural run
`bankrenewal` session 56196 is live; cumulative stream regression is pending.
No runtime-success claim yet.

Natural `bankrenewal` session 56196 exited 2 at **8016E8F0**, RA `80128508`,
SP `801FFF18`. CPU, CD IRQ, source-clock and SPU fault flags are zero. This
proves natural return through `80178004` and scene-buffer setup for this run.
The next routine is already staged and independently matched 10/10. The
40-sector regression and disabled-renewal control remain pending; do not
call IRQ validation complete from one natural run. No native process live.

### Scene reset and transfer C recovery

Integrated complete SC02 `8016E8F0` (10 words, previously independently
matched) and its guarded word selector. Independently rebuilt both staged
and production main `8003AE14` as ordinary C: 34/34 words, SHA256
`6bff5c136a2f93e8270d73268a1edf786f123746dd2c56e2cc315a492ed4def6`.
Registered the main function as ordinary C; native words are unchanged.
All 33 focused SC02/overlay/renewal checks and 623 main word-export checks
pass. Build 65053 exited 0. Natural `scenereset` session 66449 is live.
The long stream regression remains pending final accounting/control checks.

Natural `scenereset` session 66449 exited 2 at **80175494**, RA `80128524`,
SP `801FFF18`; CPU, CD IRQ, source-clock and SPU fault flags are clear.
`8016E8F0` returned naturally. Recovery of the new actual callee is active.

Direct long-stream baseline passed all 40 immutable sector payload checks
(79,936 SPU bytes) in 4,270,175 instructions. Accounting includes 40 payload
DMAs and 45 header DMAs, five of which are trailing state-4 notifications.
Disabling bank renewal reproduces budget 1; zero PIO allowance triggers
budget 3. Fresh three-variant pytest session 50878 remains pending. This is
a fixture proof using real CD/DMA3/PIO owners and native-style incoming
backpressure, not DMA4 hardware parity or human-visible acceptance.

Fresh stream pytest 50878 exited 0: **3 passed in 167.23s**, including
the 40-sector baseline, disabled bank renewal (ordinary budget 1), and zero
PIO allowance (budget 3). This closes the pending cumulative regression.
Existing IRQ and bank-call contract checks are running as session 62771
(`/tmp/musashi-bank-renewal-irq-regression.log`); no inventory changes
while their generated includes are being prepared. Current natural refusal
remains `80175494`, with complete-span recovery in progress.

Existing IRQ and bank-call contract pytest 62771 exited 0: **6 passed in
216.22s**. Together with the fresh stream controls and progress tests, the
guarded renewal validation is complete for this scope. No general budget
raise or device behavior change was made.

Independently rebuilt staged and production `8003B08C`: ordinary C matches
11/11 words, SHA256 `242f17b75a07920c5870cb5ea38639db348dffb6dd50f72a5f759beb3854f1ac`.
Integrated and registered this actual DMA-write callee; native export bytes
are unchanged. The companion DMA-read routine remains staged only.

### Scene-state setter

Integrated full SC02 `80175494` span `[80175494,801754A8)`: ordinary C,
5/5 words independently rebuilt from staged and production sources, SHA256
`6db536ab23d86792a9456edacbb221c1168e598f5c9d0f01287c38e29423560b`.
The routine sets byte `8011F7A8` to 2 and returns; it has no callees.
All 31 focused SC02/overlay checks pass and build 39174 exited 0.
Natural `statesetter` session 51872 is live. No SC02 registry credit.

Natural `statesetter` session 51872 exited 2 at **8012853C**, RA `801282BC`,
SP `801FFF30`; CPU, CD IRQ, source-clock and SPU fault flags are clear.
Scene setup returned naturally. No native process remains live.

Parallel `801777BC` digit-loop recovery resolved as ordinary C, 59/59 words
with a signed-short digit variable and no assembly constraints. Staged and
production rebuilds independently match SHA256
`0fa2e36925478fcd70f5a30475877df57f8ce80e1ec1511e0fccc63a80380e4e`.
Integrated C; native words unchanged, 31 focused checks pass, no SC02 registry
credit. Recovery continues at actual `8012853C` and earlier called `80177B5C`.

### Scene callback continuation

Integrated full `8012853C` ordinary C, `[8012853C,80128564)`, 10/10
words independently matched from staged and production sources, SHA256
`f22122bad2ecafb086270cf5a033089abe244fb5965b2e4ea77a64b8b7ede42e`.
Calls `80146074` then `80011C10`. The 31 focused checks and build 66710
passed. Natural `scenecallback2` session 85544 is live.

Its unconditional callee `80146074` is independently matched as an empty
return, 2/2 words `[80146074,8014607C)`, SHA256
`6d64edf91449c1b17746c1ef18afa2eb25c70bdf1322ab3df5a2630993b7e2f1`.
It remains staged. These small callbacks do not establish game-engine C
completeness or start-screen acceptance.

Natural `scenecallback2` 85544 exited 2 at `80146074`, RA `8012854C`,
with CPU/CD IRQ/source-clock/SPU fault flags clear. Integrated that complete
empty return; production C also independently matches 2/2 words. The 31
focused checks pass and build 84713 exited 0. Natural `emptycallback`
session 1901 is live. Parallel full `80177B5C` draft improved to 130/147
matching words but remains staged and unmatched, without C credit.

Natural `emptycallback` session 1901 exited 2 at **80128564**, RA `801282BC`,
SP `801FFF30`; CPU/CD IRQ/source-clock/SPU fault flags are clear. Recovery
of the new actual callback is active. No native process remains live.

Full `80177EA4` ordinary C now independently matches 56/56 words from staged
and production sources, SHA256
`d7fb449042e542cb0ac033500bef4b588ad7bbf0fc460385ea10330ca863d134`.
Integrated its C recovery with native words unchanged; 31 focused checks
pass. No SC02 registry credit or start-screen acceptance is claimed.

### Scratchpad-stack callback

Integrated the full retail `80128564` span `[80128564,801285D4)`, 28 words,
SHA256 `b71c659e394e2d7bda30c660ae793eda52600aca1d52f44f03d99d147a91e2ae`,
after checking the pinned decoded SC02 identity. It saves/restores SP via
`1F8003FC`, calls `80178608` on stack `1F8003F8`, conditionally calls
`8002D4C8(6,0)` for scene `300A`, then calls `80011A3C` and stores short 1
at `800B99F2`. The explicit stack-switch routine receives no C credit.
Existing interpreter scratchpad support covers the 1KB address range.
All 31 focused checks and build 76001 passed. Natural `scratchcallback`
session 98471 is live; actual stack-switch execution remains pending.

Natural `scratchcallback` session 98471 exited 2 at **80178608**,
SP `1F8003F8`, RA `8012858C`. CPU/CD IRQ/source-clock/SPU fault flags are
clear. The retail scratchpad stack switch executed naturally; return and
stack restoration remain unobserved until the callee can continue.
No native process remains live. Full callee recovery continues in parallel.

Parallel bounded `80177940` recovery improved its full 101-word candidate
to 40 matching words (previously 14). It remains unmatched and staged,
without production changes or C credit. Its remaining differences are
register allocation and scheduling. Actual `80178608` recovery is active;
no new native run has been started while that full span is established.

### Scratchpad initialization

Integrated full `80178608` ordinary C, `[80178608,8017869C)`, independently
matched from staged and production sources: 37/37 words, SHA256
`db05b9d36e7345e0bd365eb66e36cc8425f983477588d9681428244dbd8a594e`.
It clears two buffers, initializes state, and calls `8013E4B4`. The 31
focused checks and build 7176 pass. Natural `scratchinit` session 13296 is
live. Next callee `8013E4B4` is independently matched 41/41 words, SHA256
`2093ec3ff40c8c16819b72dab3f861b82dd4656b6405cddd7ee1c01ef0b8332d`;
it remains staged with both nullable terminated-array loops preserved.
Parallel `80178004` ordinary draft improved to 164 versus 165 retail words
but remains unmatched and staged. No SC02 registry credit.

Natural `scratchinit` session 13296 exited 2 at **8013E4B4**,
SP `1F8003E0`, RA `8017868C`. CPU/CD IRQ/source-clock/SPU fault flags are
clear. Initialization reached its next callee naturally on the scratchpad
stack. That full callee is verified and staged for integration. No native
process remains live; stack restoration and start-screen acceptance remain
unobserved.

### Entry-array reset

Integrated full `8013E4B4` ordinary C and guarded export, `[8013E4B4,8013E558)`,
41 words. Production independently matches SHA256
`2093ec3ff40c8c16819b72dab3f861b82dd4656b6405cddd7ee1c01ef0b8332d`.
Both nullable 20-byte entry-array loops are retained. All 31 focused checks
and build 30518 pass. Natural `entryreset` session 7810 is live.
Parallel decompilation continues on the earlier called `80176D94`.

Natural `entryreset` session 7810 exited 2 at **8012C750**, entry pointer
A0/S0 `801899A8`, SP `1F8003C8`, RA `8013E4E8`. CPU/CD IRQ/source-clock/SPU
fault flags are clear. The first entry-array loop is active in this run.
No native process remains live; callee recovery is active.

Full `80176D94` mixed C now independently matches 152/152 words from staged
and production sources, SHA256
`2d96f3093eb3d30c8ee0286b72aa4b1f06dfa2984685be9dabe6896141de50e8`.
Integrated recovery retains existing empty compiler constraints, with no
emitted assembly. Native words are unchanged; all 31 focused checks pass.
No SC02 registry credit or human acceptance.

### Entry slot search

Integrated full retail `8012C750` span `[8012C750,8012C820)`, 52 words,
SHA256 `aaba1aa9d53e7fa7446ec324df0da75cd4d454f8764899643559619e551f6757`.
Pinned SC02 identity checked before extraction; both 96-slot search
directions and exhaustion path retained. Ordinary draft was 49 words;
latest mixed draft has 52 words but remains unmatched. No C credit.
Build 28671 and all 31 focused checks pass. Natural `slotsearch` session
83369 is live.

Full `801770E0` mixed C independently matches 152/152 words from staged
and production sources, SHA256
`35097fec875b4f2bce8bc48a96f009f0a89e39389e25ec9ee6014705f8f81300`.
Integrated recovery retains existing constraints, native words unchanged.
No SC02 registry credit or human acceptance.

Natural `slotsearch` session 83369 exited 2 at **8012C890**,
A0 `801899A8`, A1 `801202A0`, SP `1F8003B0`, RA `8012C810`. CPU/CD IRQ/
source-clock/SPU fault flags are clear. The real search selected the first
free slot and reached initialization. Full callee recovery is active; no
native process remains live. Start-screen acceptance is still unproven.

### Allocated-slot initialization

Integrated full retail `8012C890` span `[8012C890,8012CAE4)`, 149 words,
SHA256 `e03de9f7423b47fc7b7c180225c9d58ed74a2d962572b0f18e0ee19714acb9bb`,
after checking pinned decoded SC02 identity. Retained initialization,
indirect callback, failure cleanup, and position-override branches. C
recovery is pending; no credit claimed. All 31 focused checks pass.
Build 79801 is live; the indirect callback target has not yet been observed.

Build 79801 exited 0. Natural `slotinit` session 95703 exited 2 at
**80131FDC**, the real table-selected callback. A0/A1 `801202A0`,
SP `1F800388`, RA `8012C9C4`, V1 `801A8538`. CPU/CD IRQ/source-clock/SPU
fault flags are clear. No callback target or scene state was substituted.
Full recovery continues on initialization and this observed callback.
No native process remains live; start-screen acceptance remains unproven.

### Entry-state dispatch

Integrated full `80131FDC` ordinary C, `[80131FDC,80132018)`, independently
matched from staged and production sources: 15/15 words, SHA256
`d05965cf5bbbde84a1190acd3013c85d2eee9e9142df7f8e9dbd3e9a59193de2`.
Dispatch uses the SC02 table at `801851F8`, not the different MAIN12 table.
All 31 focused checks and build 14912 pass. Natural `entrydispatch`
session 84715 is live; selected target remains to be observed.

Parallel full `8012C890` recovery improved to 147/149 matching words. Two
branch encodings in attached-object cleanup remain different. Full mixed C
stays staged, without exact-match credit or production changes.

Natural `entrydispatch` session 84715 exited 2 at **8013221C**,
A0 `801202A0`, SP `1F800370`, RA `80132008`. CPU/CD IRQ/source-clock/SPU
fault flags are clear. The real state table selected this target; recovery
is active on it. No native process remains live. Start-screen acceptance
and the full scratchpad-stack return remain unproven.

### Starting-level objective and object initialization

The user now explicitly requests reaching the starting level and decompiling
where necessary. Continue the natural retail-backed path; level runtime
acceptance remains unproven. Integrated `8013221C` ordinary C, full 27/27
words independently matched from staged and production sources, SHA256
`c79862b737d8b80531d3248227a537843c741c6eafd843bd77ce53440fc8c28d`.
All 31 focused checks and build 5940 pass. Natural `objectinit` session
80540 is live. Next wrapper `8012C1B8` independently matches 9/9 words,
SHA256 `8a1fae9542cd684d27ba4d6b01f1bc9af2a81930b512d939ed3ec4b7df1631a0`,
and returns `8001CFDC(26,85)` unchanged. It remains staged; recovery of
that main allocator has been assigned in parallel.

Natural `objectinit` session 80540 exited 2 at **8012C1B8**,
A0 `801202A0`, SP `1F800358`, RA `80132230`. CPU/CD IRQ/source-clock/SPU
fault flags are clear. No native process remains live. Both this wrapper
and its unconditional main allocator `8001CFDC` are staged as exact C.
The allocator spans `[8001CFDC,8001D050)`, 29 words, independently matched
against pinned EXE, SHA256
`28c19451b7b52bb603572aaae70206db21ed017ac7b14bf07acf4e1d91f8ba2f`.
It preserves bounds, scan and NULL-result paths. Next action is integration
of these called routines; starting-level runtime acceptance is unproven.

### Record allocation continuation

Integrated verified ordinary C and guarded exports for SC02 `8012C1B8`
(9 words) and main `8001CFDC` (29 words). Both production rebuilds match
the complete pinned spans; main allocator registered as ordinary C, while
SC02 has no registry credit. All 655 focused word/SC02/identity checks pass.
Build 75736 exited 0. Natural `recordallocation` session 46694 is live.
Existing main `8001C214` source is under independent read-only verification
for the conditional successful-allocation path.

Natural `recordallocation` session 46694 exited 2 at **8001C214**,
A0/returned record `800B0C70`, A1 `0`, SP `1F800358`, RA `80132254`.
CPU/CD IRQ/source-clock/SPU fault flags are clear. Real allocation succeeded.
Existing `8001C214` C independently matches full `[8001C214,8001C2C4)`,
44 words, SHA256 `28e1abf81471be2d717d8c27f38b87e06d2bd4dbdb39e4e4cb0474a48d0908af`.
It needs an export; all four callees already have native coverage. No
native process remains live. Starting-level runtime remains unproven.

### Record initialization

Added guarded native coverage for existing ordinary-C `8001C214`, full
44-word `[8001C214,8001C2C4)` span. Production rebuild independently
matches the pinned EXE; no new C recovery credit for this existing source.
All 656 focused checks and build 55976 pass. Natural `recordinit` session
38686 is live. Parallel recovery now covers full `80054514`, the called
matrix routine, including its ancestor traversal rather than only the
initial parent-free case.

Natural `recordinit` session 38686 exited 2 at **80181F20**,
A0/A1 `801203AC`, SP `1F800388`, RA `8012C9C4`. CPU/CD IRQ/source-clock/SPU
fault flags are clear. The first record initialized and returned naturally;
the entry loop advanced to the next allocated slot and selected this new
callback. No native process remains live; callback recovery is active.

Full `80054514` ordinary C independently matches 177/177 words from staged
and production sources, SHA256
`a83a8a3dcb32eddf41744586c3ec59eee790a3bb468af8387527e3b2bfd5a443`.
Integrated and registered the complete matrix traversal/copy implementation;
no constraints or assembly templates. Native words remain unchanged.

### Second record dispatch

Integrated full `80181F20` ordinary C, `[80181F20,80181F5C)`, independently
matched from staged and production sources: 15/15 words, SHA256
`1400756a4d8c61c79d82ce3af7c266ca0841cbf29d766a84c3056ecb64f4b060`.
Dispatch uses table `80188E50` and preserves the record pointer. All 31
focused checks and build 79185 pass. Natural `seconddispatch` session
66201 is live. Its state-0 target `80181FA4` is independently matched
51/51 ordinary-C words and remains staged, including all metric thresholds.

Natural `seconddispatch` session 66201 exited 2 at **80181FA4**,
A0 `801203AC`, SP `1F800370`, RA `80181F4C`. CPU/CD IRQ/source-clock/SPU
fault flags are clear. The state-0 target was selected naturally. Its full
51-word C is independently verified, SHA256
`82aa2411ab6164758753220c3223c6136bb35f0d2626044963bc17c3297b6cb2`,
and awaits integration. No native process remains live; starting-level
runtime acceptance remains unproven.

### Second record initialization

Integrated full `80181FA4` ordinary C, `[80181FA4,80182070)`, 51 words.
Production independently matches SHA256
`82aa2411ab6164758753220c3223c6136bb35f0d2626044963bc17c3297b6cb2`.
All metric thresholds, allocation failure and success branches remain.
All 31 focused checks and build 50008 pass. Natural `secondinit` session
12649 is live; conditional helper `80181F5C` is being recovered in parallel.
Natural `secondinit` session 12649 was interrupted before STOP; its retained log has no final PC.

Integrated full `80181F5C` ordinary C, `[80181F5C,80181FA4)`, independently
matched from staged and production sources: 18/18 words, SHA256
`63dfdbe58dded8bd2e2310d0bec0cfff1797f53fe5d7a2d861a13dc0b4bb8fa3`.
Table scan preserves the terminator-return of the lookup key and the matching
value. Focused overlay/export checks pass 703 tests plus 4 subtests. Native
rebuild of `musashi_native_boot` succeeded. Natural `tablelookup` run is next.
No SC02 registry credit or starting-level runtime acceptance.

Natural `tablelookup` run exited 2 at **801819E8**, A0/A1 `801205C4`,
SP `1F800388`, RA `8012C9C4`. CPU/CD IRQ/source-clock/SPU fault flags are
clear. `80181FA4`/`80181F5C` returned naturally; the entry loop selected
another slot callback. No native process remains live.

### Third record dispatch

Integrated full `801819E8` ordinary C, `[801819E8,80181A24)`, independently
matched from staged and production sources: 15/15 words, SHA256
`381f681bbc2a1c7a1acd341cf16de1a03484baf7051cc0a6d4d83a7ae29c2070`.
Dispatch uses table `80188E00` and preserves the record pointer. Focused
overlay/export checks pass 703 tests plus 4 subtests.

Natural `thirddispatch` run exited 2 at **80181A24**, A0/A1 `801205C4`,
SP `1F800370`, RA `80181A14`. CPU/CD IRQ/source-clock/SPU fault flags are
clear. Table `80188E00` selected this state-0 wrapper naturally.

### Third record state-0

Integrated full `80181A24` ordinary C, `[80181A24,80181A44)`, 8/8 words,
SHA256 `24bea16848e714ddfee15b71068ed77c6b862616619f5e596e872eff7c91c38e`,
and its unconditional callee `8012AD50`, `[8012AD50,8012AD64)`, 5/5 words,
SHA256 `c2e1014d8cbc7de8164373398688d67bab641df23646526ba51bab04f6777900`.
The helper clears offset `0x34` and increments the record state.

Natural `thirdinit` run exited 2 at **80181B78**, A0/A1 `801206D0`,
SP `1F800388`, RA `8012C9C4`. CPU/CD IRQ/source-clock/SPU fault flags are
clear. `80181A24`/`8012AD50` returned naturally; the entry loop selected
another slot callback.

### Fourth record dispatch

Integrated full `80181B78` ordinary C, `[80181B78,80181BB4)`, independently
matched from staged and production sources: 15/15 words, SHA256
`08133cbb26f53f5ae76e32179beaa1a8200ebbff6feab368ca923d1f35f865e9`.
Dispatch uses table `80188E08` and preserves the record pointer.

Natural `fourthdispatch` run exited 2 at **80181BB4**, A0/A1 `801206D0`,
SP `1F800370`, RA `80181BA4`. CPU/CD IRQ/source-clock/SPU fault flags are
clear. Table `80188E08` selected this state-0 body naturally.

### Fourth record state-0

Integrated full `80181BB4` ordinary C, `[80181BB4,80181C2C)`, independently
matched from staged and production sources: 30/30 words, SHA256
`cd9a0c6e263ae608e9143d92fc36b23e6f863ab18308a7ee6ea87b2098b71b7c`.
It gates a 10-entry table walk on `80181EF0`, then calls `8002D4C8` and
`8012AD50`. Next callee `80181EF0` remains unmatched.

Natural `fourthinit` run exited 2 at **80181EF0**, A0/A1 `801206D0`,
SP `1F800350`, RA `80181BD0`. CPU/CD IRQ/source-clock/SPU fault flags are
clear. This is the first callee of `80181BB4`.

Integrated full retail `80181EF0` span `[80181EF0,80181F20)`, 12 words,
SHA256 `9cf422449d513fa228d4f41ecbe16c05060cbcf5d437331a78042b95bdb013a6`.
Ordinary C is staged but unmatched (register allocation). No C credit.

Natural `gatecheck` run exited 2 at **80181868**, A0/A1 `801207DC`,
SP `1F800388`, RA `8012C9C4`. CPU/CD IRQ/source-clock/SPU fault flags are
clear. `80181EF0` returned zero, so the previous record skipped its table
walk; the entry loop selected another slot callback.

### Fifth record dispatch

Integrated full `80181868` ordinary C, `[80181868,801818A4)`, independently
matched from staged and production sources: 15/15 words, SHA256
`b396bf70419e029eee473ae5e0a03cfb40e035e7648136d841be3eac32a4a0f2`.
Dispatch uses table `80188DE8` and preserves the record pointer.
