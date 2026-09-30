# Native boot entry contract

## Current mounted continuation — 2026-09-06

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

See [menu continuation](MENU-BOOT-CONTINUATION.md) and
`artifacts/cd-native-owner-20260906/` for source authority, logs and reviews.

The sections below retain earlier boundary contracts and evidence. Current
scratch, mounted CD execution and next-stop authority are summarized above.

## Historical boundary records

### Native SIO owner boundary, 2026-09-05

The digital port-1 SIO owner is now bound to the paced Timer2/device epoch and
an SDL keyboard provider. The current native startup trace reports
`keyboard_polls=0`, `transmitted=2`, and no DATA-pad packet; provider binding is
therefore not a keyboard-poll or input-delivery proof. Six metadata edges are
mapped, four new metadata arrays are integrated, and the `8568..85D8` scratch
frame checks all widths with opt-in byte access. The legacy 16-bit path remains
fail-closed at `8005DB04` after `F830`.

The source-independent check now covers 26 exports and 1,528 words (23/1,189
previous plus 3/339), with C, compiled candidate, and retail slices equal.
That is source exactness separate from native runtime evidence. The selected
device correction tracks PAD, absent-card, and ignore identity separately;
focused identity tests pass. The synthetic fixture reaches the `DC04`/`EAE8`
refusal and metadata stores, but natural keyboard/DATA exchange remains
**NOT_OBSERVED**. See
`artifacts/controller-protocol-20260905/integration-contract.md`.

### Diagnostic address metadata correction, 2026-09-05

The verified retail call sites are `jal 800427CC -> 8005CE50` (A0:72,
`ra=800427D4`), `jal 800427BC -> 80042DB8` (channel installer), and
`jal 800427D4 -> 8005CF18` (SYS(2), ExitCriticalSection). Earlier call-site
labels in ignored logs and continuation notes remain historical metadata. This
correction does not change source or instruction recovery: the source-owned
streams were already retail-exact.

### Native cleanup gate, 2026-09-05

The shared `pc_port/bios_events.c` and `pc_port/bios_kernel.c` modules create
the native BIOS CD session through the 22-record event pool and priority
registry. A0:72 closes five owned polling events and unlinks the two owned
priority-zero nodes while preserving game RAM and unrelated registrations.
Actual BIOS CD-handler execution remains unsupported and fail-closed if
requested; this lifecycle result does not claim whole-BIOS parity.

SYS(2) enables guest IRQ eligibility after cleanup. The PsyCross scheduler
worker only queues VBlank edges. The owner pump runs at
live FormatterCpu instruction/call checkpoints, coalesces pending edges, and
executes the recovered dispatcher with a consumed B0:17 continuation. It does
not write guest RAM from the worker or hold a broad host mutex across guest
execution. The earlier cleanup gate observed `event_used=0`, `cd_installed=0`,
`guest_irq_enabled=1`, `irq_faulted=0`, and zero deliveries in the cold prefix;
that is historical cleanup evidence. The preceding event/BU check reported
`guest_irq_enabled=0`, `irq_faulted=0`, `card_initialized=1`,
`card_started=1`, `card_active=0`, `card_pad_started=0`, 13 hook calls,
five maintenance calls, and five game deliveries/ticks. Five actual PsyCross
VBlank edges drive the disconnected-card transactions. The eight event
operations are observed, but the `v0=1` return is a source comparison literal
for a void path, not successful API proof.
A separate live
fixture is the scheduler delivery evidence. The checker also
requires `scheduler_removed=1` before accepting the bounded gate.

Validation covers the five owned CD records (140 bytes) and all 22 event status
words before and after cleanup. The full 616-byte initial table intentionally
does not match: retail unused slots contain prior BIOS payloads and stale stack
words, and those values are not seeded into the native runtime. Separate
`allocation_serial` metadata/leases detect replacement of a CD-owned slot;
guest handles and the B0 low-16-bit alias remain unchanged.

The fresh graphics evidence is
`artifacts/gpu-startup-20260905/retail-run1/manifest.json`, with native output
in `artifacts/gpu-startup-20260905/native-first.log`. The authoritative
GPU-state range `80072740..80074750` matches retail after at 8208 bytes;
CPU stack/kernel and unrelated coldboot state are excluded. Query-7 leaves
`GPUREAD=400`, but physical GPU revision response is unproven. Blank-only
presentation is available; geometry, VRAM transfer, enabled scanout, and
production HBlank input remain unsupported. Reset-owned clears
`80078874..80078974` (256 bytes) and `800C5510..800C6D10` (6144 bytes), plus
queue scalars `8007288C/90/9C`, also match; the broad 8208-byte range is not
the entire GPU working-set authority.

The final entry continuation is recorded in
`artifacts/entry-continuation-20260905/native-check.log` and the raw CPU
comparison in `artifacts/entry-continuation-20260905/cpu-comparison.json`.
It compares all 32 GPRs plus HI/LO at `80010178`, `800141F0`, `800101C8`,
`8005FC68`, and `8005CF48`. The historical StartCARD comparison reports 13/15/14 GPR
differences at ENTRY/RETURN/NEXT; the selected frame agrees except RETURN
`a0` (native `1`, retail `2`, following the SYS2 source value). Full CPU parity
is unestablished. Differences outside the selected call/frame comparison have
no automatic ABI-clobber causality. The native initial GPR/HI/LO profile and
incoming RA are zeroed rather than retail-seeded.

The preceding event stop was `8005D8D8 -> 8005E178` after eight event Open/Enable/Test
operations. The eight active records and zero flags match retail's first five
record words; BU state remains matched. The CPU comparison has 15/12 GPR
differences at ENTRY/RETURN and HI/LO differences; selected `v0/gp/sp/fp/ra`
match. BU flags are `1111`, action `0`, last port `1`, step `0`; directories
report 960 zero bytes, 40 broken `FF` entries, and status `5` zero, with the
priority-1 CARD lease gone and card/BU cleanup complete. This does not establish
full CPU, cycle, low-RAM, menu, hardware, or event-service parity. The 121/66
PsyQ exports and eight callback exports are exact; the eight callback flag stores pass focused executor tests; their delivery
was not observed during this short boot window.

The latest validation is **557 passed plus 84 subtests** and **21/21 CTest
tests**, with graphics/headless builds, archive symbol verification, focused
NTSC/live checks, and diff checks passing. The fresh Timer2 owner is integrated
under a paced host-time device epoch with variable phase; six additional Timer2
focused tests pass, including the large-`UINT64_MAX` case. The native final
check still stops at the CD byte boundary. Of the 26 newer exact exports, two metadata bodies are mapped and 24
remain source-only. The earlier F830 and F384 exports are also newly mapped.
The four-of-four SYS1/four-of-four SYS2
syscall exports remain verified. Five StartCARD negative controls compile and
reject as recorded under `artifacts/startcard-20260905`.

## Evidence and limits

Static audit of the current checkout, 2026-09-04. The file registered by
`provenance/exe_identity.json`, `extracted/disc/files/SLUS_007.26`, was freshly
hashed as `66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a`.
The header enters at `80010000`; text loads at that address for `64800` hex
bytes. File offsets below use `address - 80010000 + 800` (hex).

All instruction words in the following ranges were compared between the
generated listing and that executable, not inferred from symbol names:

| Range (end exclusive) | Words | SHA-256 |
| --- | ---: | --- |
| `80010000..800100A0` | 40 | `1443ac15db37bc83c0c96a6995334b57dfbd79b25f23dea2fda58e999fa3a985` |
| `800100A0..80010110` | 28 | `4afe3c3c96965f945ec429adb9bfd19977f1d22da75a4e0cd3df5100c4dcd5c1` |
| `80010178..80010218` | 40 | `9a00d100d5f061f3c3987d9d2502575fe3158d4a532f91a81f217352e7813430` |
| `80043060..80043078` | 6 | `11d327edbd4d62f21e0b6f7e2d12a834d424337883732011b7fedece44fc0237` |

These checks establish static entry behavior only. Retail execution,
native startup, visible menu, and launcher acceptance are **NOT_RUN** in this
audit. Registry membership below was inspected, not freshly recompiled here.

## Before the C startup call

`80010000` clears words in `[80074750,800C7F08)`, length `537B8` hex.
The EXE header's zero-length BSS descriptor does **not** describe this clear:
it is explicit entry code. A loader that relies only on that descriptor will
miss a required startup operation. The clear also overlaps the final `B0` hex
bytes of the loaded text/data image, whose end is `80074800`.

The initial word at `800629BC` is `00200000`; at `800629C0` it is `00000C00`.
The entry computes:

- guest stack address `801FFFF8` (subtract eight, then set the cached-segment bit);
- heap start `800C7F08`, written to `8006299C`;
- heap length `001374F0`, written to `800629A0`, ending at `801FF3F8`.

It stores incoming `ra` at `80074750` after clearing, sets `gp` to `80074750`
and `fp` to the stack, then calls `80010178` at `80010094`. A return reaches a
`break` instruction. These are guest-machine effects, not instructions to set
the host stack or cast guest addresses to native pointers. Incoming `ra` is
not established by the static EXE audit and must not be invented as evidence.

## First startup calls

Direct JAL targets decoded from the executable, in order before the outer loop:

| Call instruction | Target | Current registry |
| --- | --- | --- |
| `80010190` | `800100A0` | registered, 28/28 words |
| `800101AC` | `80043060` | matched source |
| `800101C0` | `800141F0` | matched source |
| `800101CC` | `8005FC68` | registered, 12/12 words |
| `800101D4` | `8005FCB8` | registered, 24/24 words |
| `800101DC` | `80018918` | matched source |
| `800101E4` | `80043300` | registered, 38/38 words |
| `800101F4` | `8002C8F4` | registered, 214/214 words |
| `800101FC` | `8001971C` | registered, 133/133 words |

`800100A0` reads a once guard at `80062998` (initially zero) and sets it to one
when clear. Its callback loop has a literal zero count in this executable;
it does not execute a callback on this path. The routine is now registered as
the exact source-backed assembly overlay `src/main/800100a0.c`; this preserves
the linker-resolved loop and does not make it native startup execution.

`80043060` returns the old word at `8006CBE8` and stores its argument there.
That word is initially zero; startup calls with zero. Its existing matched
source is `src/main/80043060.c` and should be reused, not copied into a second
native implementation.

`800141F0` is the first substantial integration boundary: its matched source
calls `80059234(0)`, `800594CC(0)`, `80059658(0)`, clears `98` hex bytes at
`800BA118` through `80016714`, and calls `800142C8`. These source dependencies
still require closure and native bindings; matching the caller alone does not
make it executable. `800142C8` itself invokes eleven reset helpers.

The previously unresolved direct callees of `800142C8` are now registered:
`8001903C`, `80029044`, and `8002906C` each match all 10 retail words at their
40-byte boundaries under `gcc-2.7.2-psx -O2`. The two `80029xxx` helpers clear
their respective byte tables; `8001903C` clears the eight-byte state record at
`800747C0` through the exact `80016714` body. These are static parity closures;
their broader startup callers remain outside the native port.

The native boundary now exposes `musashi_boot_call_8001903c`, which composes
the checked `80016714` binding at the observed `800747C0` address for its
eight-byte record. Boundary tests verify exact clearing and null rejection.
This is an isolated composition; it does not execute the full `800142C8`
callback sequence.

The native boundary also exposes checked bindings for the exact table clears
`80029044` and `8002906C`, targeting `[800AE648,800AE688)` and
`[800BA1B8,800BA2B8)`. Tests cover both writes and null rejection. These
bindings deliberately stop short of the surrounding indirect graphics calls.

## Next integration requirement

### Implemented entry-data boundary

`pc_port/boot_memory.c` and `pc_port/include/musashi_boot_memory.h` now supply
caller-owned shared byte storage, bounded cached-RAM views, aligned
little-endian word access, and pinned-EXE entry-data loading. The loader hashes
the exact buffer it copies, before any mutation, and rejects any size or digest
mismatch. It reproduces the documented CRT memory effects only; it does not
execute `80010178` or register a new byte-matched game function.

The caller must initialize the backing storage and serialize access. Bytes
outside the observed load/clear/writes are preserved, not treated as an
observed BIOS RAM snapshot. The incoming return address remains an explicit
parameter. The current probe supplies a clearly labeled synthetic value.
Only `[80000000,80200000)` is supported: other RAM aliases remain unsupported
until needed and verified. Returned byte views must not be cast to host
records; native symbol bindings are still the next task.

Hashing uses the installed OpenSSL `Crypto` component and its documented
[EVP_Digest interface](https://docs.openssl.org/3.5/man3/EVP_DigestInit/), including
the failure return and output-length check. No TLS/network operations or
hand-written hash implementation are added. Match the OpenSSL installation to
the graphics-library prefix when configuring a combined build; mixing system
and Homebrew libraries produced an actual CMake runtime-path collision warning.

Verification commands, from the repository root:

```sh
MUSASHI_REQUIRE_RETAIL_BOOT_DATA=1 python3 -m pytest tests/test_boot_memory.py -q
ctest --test-dir build --output-on-failure
build/musashi_boot_data_probe extracted/disc/files/SLUS_007.26
```

The Python tests compile and execute the C boundary, compare the entire 2 MiB
result against the pinned image plus the independently specified entry writes,
and check invalid spans, alignment, shared record views, overlapping source/
destination storage, modified headers/code/data/padding, and unchanged state on
rejection. Ordinary payload-free runs explicitly skip retail-dependent tests;
the required-retail mode above fails when the payload is absent. Independent
review identified this required-mode distinction as necessary: negative-only
tests would otherwise accept a loader that always refuses input.

The CTest `boot_data_boundary` test uses synthetic input; the explicit EXE
command additionally verifies a real pinned load and the bounded prefix below.
Its success is a diagnostic test result, not boot success. Neither is emulator
execution, complete game startup, or menu acceptance.

Verification on 2026-09-04: eight focused tests plus twenty subtests passed
with required-retail mode enabled; the combined native CTest suite passed all
nine checks, and the explicit pinned-EXE probe passed. A separate Clang 22.1.8
build with AddressSanitizer/UndefinedBehaviorSanitizer passed both no-graphics
CTest checks and the pinned-EXE probe. The initial GCC sanitizer attempt could
not link because the system's `libasan`/`libubsan` runtime files were absent;
the Clang run is the sanitizer evidence, not that failed attempt. No retail
payloads, library installations, commits, or publication were changed.

### Remaining symbol integration

The first matched scalar binding is implemented:
`musashi_boot_call_80043060` in `pc_port/startup_bindings.c` invokes the body
compiled from `src/main/80043060.c`. A native-only compile definition replaces
its one external word with an explicit scalar pointer and gives the function
a separate native symbol. The exchange body is shared, not copied. With the
definition absent, the retail ABI and bytes are unchanged; fresh verification
still matches all six words (24 bytes).

The adapter marshals the word at `8006CBE8` in and out through the byte-storage
API, preserving signed bit patterns with `memcpy`. It never casts the RAM
array to an `int *` or maintains a second persistent global. This is valid for
this specific call-free function with one nonvolatile RAM word and serialized
access. It must not be generalized to callbacks, MMIO, volatile observations,
or mutually aliased symbols whose intermediate accesses are observable.

Tests compare the whole memory image after the native call, cover signed
extremes and rejected null arguments, and keep two RAM instances independent.

### Bounded prefix and next stop

`musashi_boot_run_prefix` now performs the pinned CRT once-guard effects,
invokes that decomp-owned exchange with zero, then writes `3E0` hex at
`80074778`, in retail order. It returns `MUSASHI_PREFIX_UNSUPPORTED_CALL` with
call-site `800101C0` and target `800141F0`. There is no stub for that target
and no successful-boot status. The handwritten CRT guard adapter is platform
code, not a new pure-C registry match; the small prefix driver is a bounded
diagnostic, not a replacement for the complete game main routine.

The guard is set to one only when originally zero; arbitrary nonzero values
remain unchanged. Tests cover repeated invocation, null inputs, and the entire
RAM image before/after the prefix, including a run immediately after pinned
EXE loading. Repeating the diagnostic reruns the prefix rather than resuming
at its stop. The probe reports `incoming_ra=SYNTHETIC; startup_prefix=PARTIAL;
stop=800101C0->800141F0; game_boot=NOT_RUN`.

The next required dependency is the first call inside matched `800141F0`:
`80059234(0)`. A fresh byte audit isolates `[80059234,800593B8)`, 388 bytes,
97 words, SHA-256
`be0ca8a324c0887750f0fcb1f5c238e8ee0bf620ab5e78992f8f86ae2aacfda2`.
The generated `80059234` block incorrectly includes the separate function
beginning at `800593B8`; do not treat its generated `298` hex size as this
routine's boundary. Embedded strings at `80074084` and `800740A4` identify
`ResetGraph`. On mode zero it calls `8005C604`, `8005C29C`, `80042580`,
`8005CE38`, and `8005BD7C`, updates the shared environment at `80072788`,
and fills its draw/display blocks. These effects require recovery and native
bindings before the stopped initialization call can run.

The later `800141F0` call `80059658` is now recovered and registered at
`[80059658,800596F4)`, 156 bytes, with all 39 words matching retail under
`gcc-2.7.2-psx -O2`. It preserves the retail state-pointer and dispatch-table
layout, including the mode-dependent reset selector. Its indirect BIOS and
graphics callbacks remain unbound in the native port.

The preceding `800141F0` call `800594CC` is now recovered and registered at
`[800594CC,80059530)`, 100 bytes, with all 25 words matching retail under
`gcc-2.7.2-psx -O2`. The range boundary is significant: `80059530` begins the
next unlabeled helper in the same disassembly span. The setter's indirect BIOS
formatter callback remains unbound in the native port.

That adjacent helper, `80059530`, is now separately recovered and registered
at `[80059530,800595DC)`, 172 bytes, with all 43 words matching retail under
`gcc-2.7.2-psx -O2`. Its state transition and callback ordering are preserved;
the store-before-call source order reproduces the retail MIPS delay slot. Its
indirect graphics and callback-table calls remain unbound natively.

The two terminal leaf readers in this contiguous block are also registered:
`800595DC` and `800595EC` each match all 4 words at their 16-byte boundaries.
They read the shared environment bytes at `80072788` and `8007278A`; they are
parity recoveries only and are not yet native startup calls.

The adjacent `800595FC` state setter is registered at `[800595FC,80059658)`,
92 bytes, with all 23 words matching retail under `gcc-2.7.2-psx -O2`. Its
formatter callback and shared global state are still outside the native port.

The later environment-clear call `80016714` is now also recovered and
registered at `[80016714,800167B8)`, 164 bytes, with all 41 words matching
retail under the observed `gcc-2.7.2-psx -O1` build. Its separate word-loop
counter preserves retail's `v1` allocation while the original byte count
remains live for the tail clear. This closes a parity dependency but does not
provide the native graphics or callback boundary.

The same decomp-owned body is now compiled into the native port under a
host-only symbol and exposed through `musashi_boot_call_80016714`. The adapter
validates the complete cached-RAM span before calling it, including the
zero-count boundary, and the native branch uses a host-width address only for
alignment while retaining a signed loop counter. This is an isolated
environment-clear binding; it does not claim that `ResetGraph(0)` or the full
startup graph can run.

### Console dependency before the first environment clear

The first mode-zero call is not side-effect-free logging. The pinned chain is
`8005C604 -> 8005C640 -> 8005C388 -> 8005CF38`. Fresh EXE hashing still
matches the entry identity above. The wrapper `[8005C604,8005C640)` is 60
bytes, SHA-256
`7a197aa72056032fca382694dbbdbfea1294fc6990bee69769df0b37f7ca75bf`.
It spills the argument registers into contiguous PS1 stack slots and calls
`8005C640(1, format, argument_words)`. A host `va_list` is not this layout.
The exact retail sequence reserves a 24-byte frame, saves `a1`, `a2`, and
`a3` at offsets `+1C`, `+20`, and `+24`, saves the format word at `+18`, and
passes `sp+1C` as the formatter's argument-word pointer. The saved format
word is evidence that this is a compiler-generated varargs home area, not an
ordinary three-element local array. A fixed-argument draft reproduced 14/15
words and omitted the format save; a variadic draft using the host compiler's
`va_start` emitted the register-home stores before stack adjustment. Neither
is the variadic draft registered. The matching caller-area idiom is now
registered as `src/main/8005c604.c`. It is executed by the native MIPS seam,
not compiled as a host variadic function: the seam supplies raw `a1`/`a2`/`a3`
guest words, executes all 15 wrapper instructions, then enters the formatter.
This closes the host-ABI mismatch for the bounded console path only; it does
not bind the BIOS vector, ResetGraph, or startup.

The formatter `[8005C640,8005CCC8)` is 1672 bytes, SHA-256
`d6250b4b1fa6632adac403c1ceb607ac30477b35d303779e91c31d97f3f5fbd3`.
Its argument cursor advances by four bytes, including guest pointers for
string and count-store conversions. It uses the retail classification table
at `800728A9`, digit strings at `80074284`/`800742A0`, and null-string text
at `80074298`. String conversions also call `8005CCC8`/`8005CD18`.
The complete formatter body is now registered as `src/main/8005c640.c` and
matches **418/418 retail words**. The source-backed instruction overlay was
necessary because the pinned assembler adds padding after symbolic inline
branches and m2c still cannot construct this jump-table graph. Its literal
words are audited directly from the pinned retail body, not patched candidate
output. The native seam reads this exact audited word stream at configure time
and executes its required R3000 subset over checked guest RAM. Its known calls
are the matched character emitter plus guest bounded-search/string-length
helpers; everything else fails closed. Focused probes cover literal output,
`%d`, `%s`, and the `8005C604` home-area wrapper. This is a bounded console
binding, not a native ResetGraph or startup acceptance result.

The ignored recovery inputs are `staging/startup/8005c640-formatter.s`,
`8005c640-context.c`, and `8005c640-formatter.c`. Running m2c directly on
`asm/main.s` fails with `jtbl list must not be empty`: the generated table
contains numeric words rather than label references. The bounded analysis
assembly retains the function instructions and labels all 121 retail table
entries. The table `[800742B4,80074498)` is 484 bytes with 25 distinct
destinations, SHA-256
`0d9657a78366137a7e2abcee171525bcb616ded60a91861343f4f8abb310fdcd`.
Every destination is aligned and inside the audited formatter range. Neither
the production disassembly nor m2c was changed.

The raw decompiler output is not usable source: it omits the unused first
argument without context, collapses the numeric accumulator to s16, leaves
unresolved argument-word accesses and stack-relative expressions, and loses
the table's zero-character return case. The draft corrects those from the
instructions, uses a 40-byte digit buffer, and explicitly returns the count
for a trailing percent. It compiles to **1616/1672 bytes, unmatched**. The
candidate now uses retail's 136-byte frame but still has different register
allocation. Its generated jump table can now be placed at retail `800742B4`
using `tools/build_candidate.py --rodata-base 0x800742B4`. This option links
`.rdata`/`.rodata*` with word alignment and checks the actual section address.
It does not verify or register data parity. The full 484-byte table currently
has 121 entries and 25 distinct destinations. All pairs of entries agree with
retail about whether they share a destination, and all targets are aligned
inside the candidate body. Only 4/121 target addresses match retail: the
instruction layout is still different. Future exact matching must verify
the complete table as well as the function bytes.

`staging/startup/8005c640-probe.c` is an ignored, draft-only host diagnostic.
It reads the classification table from the pinned EXE and executes the draft
with the actual recovered character body and a recording byte sink. Seven cases
pass with Clang ASan/UBSan: the startup `%08x` address pair, negative
decimal, minimum signed integer, zero precision/zero value, literal text, trailing percent, and escaped
percent. Literal characters do not increment this formatter's returned count;
the startup diagnostic returns 16 for its two eight-digit conversions. The
first probe failure was stale output storage on the empty-output case, fixed
by clearing the harness's output terminator between cases.

A fresh minimum-integer test first failed UBSan on signed negation of
`-2147483648`. Using an unsigned magnitude and unsigned subtraction fixes
that host undefined behavior while producing identical candidate MIPS bytes.
The character temporary is now widened and the end-of-string test is explicit,
removing a redundant mask and reproducing retail's early termination branch
shape. This improves the draft but does not establish full parity. The current
GCC sanitizer link fails because its referenced `libubsan.so.1.0.0` is absent;
the fresh sanitizer result above is from Clang.

The probe additionally exhausts all 65,536 short bit patterns for `%hd`,
`%hu`, `%ho`, and `%hx`, with nonzero upper argument bits to check halfword
selection. Retail uses signed `lh` at `8005C878`, `8005C8FC`, `8005C9A4`,
and `8005C9E4`: even the unsigned conversions sign-extend the halfword
before unsigned division. Thus `%hu` of low halfword `FFFF` produces
`4294967295`, not `65535`. Expected values are computed from that observed
sign extension and then formatted as full-width host integers. All 262,151
scalar cases (including the seven earlier cases) pass Clang ASan/UBSan.
This is exhaustive over short values for these four conversions, not over
flags, widths, precision, or pointer conversions.

The draft's explicit `asm("s1")` register binding has been removed. It now
compiles in the host probe without preprocessing away a MIPS register name.
The retail compiler now emits 1616 bytes with a 136-byte frame, but chooses
`s2` for the argument cursor; retail uses `s1`. Register allocation and stack
layout remain recovery work, and no exact-match claim follows from this probe.

Removing redundant flag/sign snapshots in the output loop eliminated an
extra spill slot and brought the frame down from 144 to 136 bytes. The split
decompiler loop counters now share one local. All 262,151 scalar cases still
pass Clang ASan/UBSan. Matching frame size does not establish matching local
offsets or register allocation; those and the remaining instruction layout
are still unresolved.

The shared numeric conversion and output blocks now follow the conversion
cases, matching retail's physical block order. All 262,151 scalar checks
still pass. Table target offsets now expose the first missing instruction
at the `*` width case: retail stores width at `sp+50` in the branch delay
slot and again after negation; the candidate retains width in `fp`, uses a
NOP in that delay slot, and lacks the second store. Subsequent case offsets
start four bytes early. Correct local allocation remains necessary; no
padding or instruction replacement has been used to conceal the difference.

`staging/startup/8005c640-width-allocation.c` preserves a separate allocation
experiment: marking only width volatile produces exactly 1672 bytes and a
136-byte frame, but only 198/418 instruction words match. Width occupies
`sp+38` instead of retail `sp+50`, displacing other locals. Marking all four
scalar locals volatile produces 1676 bytes and a 120-byte frame, disproving
that broader layout hypothesis. The primary draft was restored and rebuilt
byte-identically to its preceding 1616-byte candidate. These qualifiers are
experimental compiler inputs, not evidence of original source declarations;
neither equal size nor these partial matches authorize registry promotion.

Reproduce the scalar diagnostic from the repository root:

```sh
clang -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -Iinclude staging/startup/8005c640-probe.c -o /tmp/musashi-formatter-probe
/tmp/musashi-formatter-probe extracted/disc/files/SLUS_007.26
```

This is not exhaustive formatter verification or a native binding. The host
compiler correctly warns about three guest-integer-to-host-pointer casts in
string/count conversions; those paths are not exercised or made safe by the
scalar tests. String helper calls abort in this test harness. No draft was
registered, no warning was suppressed, and native startup is unchanged.

The matched character body is now compiled into `musashi_pc_port` under a
native-only definition. `musashi_boot_console_char` validates the classification
table span at `800728A9`, marshals the little-endian column word at `80078978`,
and passes emitted bytes to a caller-owned sink. Recursive CR/LF and tab calls
stay inside the decomp-owned body; they are not reimplemented by the adapter.
The sink context is stack-local and valid only during the synchronous call.
The adapter rejects null memory or sink and does not expose guest pointers as
host pointers. It does not bind the formatter, BIOS vector, MMIO, or native
startup. `boot_data_probe` exercises an ordinary byte followed by newline
against the actual native binding, while its printed result remains
`startup_prefix=PARTIAL; game_boot=NOT_RUN`.

The character routine is recovered in `src/main/8005c388.c`, registered with
all 45 words matching `[8005C388,8005C43C)`, SHA-256
`ce91658c305cd799908710cec7ccafa0ffffd1cdb93a29bd10094d12323e3c69`.
The generated block extends to `8005C49C` and includes two separate case
conversion helpers, so its `114` hex length is not the character routine.

- Inputs are truncated to one byte. Ordinary characters increment the shared
  word at `80078978` when `classification[byte] & 97` hex is nonzero, then
  call `8005CF38(1, &byte, 1)`.
- Tabs recursively emit spaces until that column word is divisible by eight;
  even an aligned column emits eight spaces with the normal space class.
- Newline recursively emits CR, clears the column word, then emits LF.

The final thunk is `[8005CF38,8005CF44)`, 12 bytes, SHA-256
`47b66a3af6776c605fd2d505596f37a217dc08fd35788f7884bafaf8e4434dbd`:
BIOS vector `B0`, selector `35` hex. It is now recovered and registered in
`src/main/8005cf38.c`, with all 3/3 words matching through the retail oracle.
The local PsyCross `LIBAPI.C` `write` body is unimplemented and returns zero;
it cannot supply this boundary as-is. The native sink must consume actual
bytes and retain the decomp-owned column and newline/tab behavior. Registering
the PS1 vector thunk does not yet bind that BIOS service on the host.

The formatter's `8005CD18` string-length helper is now recovered and registered
at `[8005CD18,8005CD48)`, 48 bytes, with all 12 words matching; its retail
SHA-256 is `0ab7dc81ec6aefe3f8b53992eb473d1dffd6ace9708b76efaa4404b5d74010d5`.
The neighboring bounded byte-search helper `8005CCC8` is now registered at
`[8005CCC8,8005CD18)`: all 20 words (80 bytes) match with gcc-2.7.2-psx -O2.
Its retail SHA-256 is
`a9a0061e0455c0115a817527e2fd4218b24a7a3ba5726868cfb7b7c839a168cc`.
Combining the null/nonpositive-count guards and using `while (--count >= 0)`
reproduces the retail initial jump and decrement. The byte load increments
`source`, so returning `source - 1` returns the matching byte's address.
Null input, nonpositive counts, and exhausted searches return null; the target
is narrowed to an unsigned byte. This closes source parity for the helper;
the formatter and its native guest-pointer integration remain open.
The host regression in `tests/test_bounded_search.py` exercises this same
source with 7,680 target/count combinations and six null-input cases. It
checks byte truncation, first-match addresses, exhausted searches, and
nonpositive counts. The full registry re-verifies at 404/404 after recovery.

The native port now links this unchanged source. `musashi_boot_search_bytes`
validates the complete positive-count cached-RAM span, calls `8005CCC8`, and
converts the returned pointer to a 32-bit guest address. Null guest addresses
and nonpositive counts complete without accessing guest bytes. Unsupported
spans fail without changing the output; the adapter never treats an MMIO
address as a host pointer. Native boundary tests cover high-byte targets,
first matches, count exhaustion, the final RAM byte, oversized ranges, and
unchanged memory/output on rejection. The formatter is still unbound and
the startup stop remains `800101C0 -> 800141F0`.

The neighboring console-string emitter `8005CD48` is also now recovered and
registered at `[8005CD48,8005CD98)`, 80 bytes, with all 20 words matching;
retail SHA-256 is
`0261ba603feeea412a8eab55d299d5775adcb8b090299647cbe6f6895c0800a0`.
Its exact source idiom is `while ((character = *source++) != 0)`, which keeps
the pointer increment in the branch delay slot after the character load. Null
input selects the retail fallback string at `80074498`. It remains outside
the native build until that guest string is resolved safely.

`tests/console_probe.c` runs the actual recovered character body against a
test-only recording sink. It checks all 256 classification values for every
ordinary byte, byte truncation, sixteen tab starting columns, and CR/LF
output-time column ordering. These are synthetic isolated executions, not
BIOS execution, shared-RAM integration, or startup advancement. Native boot
still stops at `800101C0 -> 800141F0`.

### Recovered ResetGraph caller and device dependencies

The 388-byte `ResetGraph` caller is now recovered in `src/main/80059234.c`
and registered after a 97/97-word match. This closes source recovery of that
caller, not its dependencies or native integration. Initial scalar declarations
matched 85/97 words; expressing the three output aliases as arrays reproduced
the observed store/load ordering. Adding volatile qualifiers did not solve the
match (all-volatile input/output views expanded the candidate to 400 bytes).
The production source uses no such volatile qualifiers.

The native MIPS seam now also consumes the 97 source-owned words for
`80059234`. Its bounded mode-5 diagnostic executes the retail branch through
the exact `8005C29C(environment, 0, 0x80)` call, executes the 12-word
`80042580` guest-table wrapper, then stops at its selected initializer edge,
`80042598 -> 80042718`. A focused probe verifies the entire environment window
is zeroed and that the callback-table `+0C` cell selects the reported target.
It does not invoke, replace, or claim callback initialization, BIOS vector
registration, a device reset, ResetGraph completion, or startup.

The exact 18-word `800141F0` entry range is now a native diagnostic entry as
well. Its mode-zero probe supplies the observed formatter conversion cells for
the `ResetGraph:jtb=%08x,env=%08x` diagnostic, executes that actual startup
call through ResetGraph and the callback-table wrapper, and reaches the same
`80042598 -> 80042718` boundary. The remaining `800141F0` calls are not
skipped or replaced; they are simply not reached because the initializer
boundary remains unresolved. This is bounded native startup execution, not a
successful startup or menu boot.

The shared environment is 128 bytes at `80072788`. Mode classes are selected
by `mode & 7`, while the original full mode is passed to the low-level reset:

- Classes 0 and 3 print the diagnostic, then follow class 5's initialization.
- Initialization clears all 128 bytes, invokes callback/BIOS/device helpers,
  stores the low byte of the reset result at offset 0 and one at offset 1,
  stores table-selected halfwords at offsets 4 and 6, fills offsets
  `[10,6C)` and `[6C,80)` with `FF` (all offsets hex), then reloads byte 0.
- Other classes optionally print when debug byte `8007278A >= 2`, then call
  dispatch slot `+34` with argument 1 and return its result.

The initial dispatch base is `80072740`; its `+34` slot is `8005BD7C`.
The first five observed words at `80072808` are all `400` hex; those at
`8007281C` are `200,400,400,200,400`. This observation does not prove the
low-level return's range or authorize clamping an unexpected index.

Remaining boundary work includes `80042580`'s indirect call through slot
`+0C` of the table addressed by `8006CB84` and the three-instruction BIOS-vector
thunk at `8005CE38` (vector `A0`, selector `49`). The low-level reset's native
interrupt, register, queue, and mode-zero query path is now exposed through
explicit callbacks below; the actual PsyCross/device backend is still open.
A matching caller alone does not prove any of these platform operations.

`8005BD7C` is now registered from `src/main/8005bd7c.c`: 348 bytes, all 87
words match, SHA-256
`fad24fce39d9d32322c6436d1282cdd6067196c3711afea0e6bd1ae1f5e104d9`.
It disables the interrupt mask through matched `800426FC(0)`, clears
`80072890`, saves the previous mask at `8007289C`, reloads `80072890`, and
copies it to `8007288C`. The volatile queue observation reproduces that reload.

The native boundary `musashi_boot_call_8005bd7c` now compiles the same reset
body as retail, adapting RAM and MMIO operations at the access sites.
Both interrupt exchanges execute the shared body of `800426FC`, including
the discarded mask read before restoration. Review found that the previous
separate native body omitted that read and replaced shared-RAM reloads with
locals/constants; its earlier passing probe did not establish this ordering.
The strengthened probe first failed on that omission. It now verifies all
16 mode combinations, the two exchanges, and the entire 2 MiB RAM image
starting with nonzero sentinels. Retail matches remain 87/87 and 7/7 words
for reset and mask exchange. These are synthetic boundary checks; the
PsyCross backend, callback initializer, BIOS vector, and native startup
execution remain open.

- Classes 0/5: write `401` hex through `80072868`, OR `800` hex through
  `80072878`, write zero through `8007285C`, then clear `100` hex bytes at
  `80078874` and `1800` hex bytes at `800C5510` (the 64-entry queue).
- Classes 1/3: the same first two register operations, then ordered writes
  `02000000` and `01000000` hex through `8007285C`; no buffer clear calls.
- Other classes: no device writes in the switch. Every class restores the
  saved interrupt mask. Only class zero then calls `8005C1C0` with the full
  mode; the others return zero.

Fresh pointer-cell reads identify `80072858 -> 1F801810`,
`8007285C -> 1F801814`, `80072868 -> 1F8010A8`, and
`80072878 -> 1F8010F0`. These are device addresses, outside the RAM adapter.

The query `[8005C1C0,8005C29C)` is 220 bytes, SHA-256
`91f0896179fb6e990e4627fd58f183e3dfdfb979fbc1019b72b404dee4eced1c`.
It is now registered as `src/main/8005c1c0.c` with all 55/55 words matching.
The source retains volatile MMIO pointers; this static recovery does not
authorize replacing device reads or writes with cached RAM.

The observed query writes `10000007` hex through `8007285C`, then checks the
low 24 bits read through `80072858`. If those equal 2, mode bit 8 selects
either a `09000001` hex write and return 4, or return 3. Otherwise it writes
`(status & 3FFF) | E1001000` hex through `80072858`, performs a discarded
volatile read there, and reads status through `8007285C` again. A clear
status bit `1000` hex returns 0; when set, mode bit 8 selects a `20000504`
hex write and return 2, or return 1 without that write. Thus the actual
startup argument zero still has three device-dependent outcomes: 0, 1, or 3.
None was observed in an emulator or chosen as a native default by this audit.

The native port now exposes this exact body through an explicit
`MusashiGpuDevice` read/write callback seam. `pc_port/gpu_bindings.c` keeps the
wrapper separate from the memory-only bindings, while
`musashi_boot_call_8005c1c0` rejects incomplete adapters and invokes the
decomp-owned native symbol. `gpu_device_boundary` verifies the observed
address/order/value contract for the zero-status, status-two, and mode-eight
paths. This is a platform contract test with a fake device; the PsyCross GPU
backend, interrupt/device-reset closure, and native startup remain
**NOT_RUN**.

The byte-fill dependency `8005C29C` is now registered and natively linked from
the same `src/main/8005c29c.c` body: 44 bytes, 11/11 words, SHA-256
`bc4c08f514cc2afda0a05a2a41045b0d2c220a0f419f99d69759e72118c6450b`.
`musashi_boot_call_8005c29c` validates a cached-RAM byte span, then passes that
host byte view directly to the decomp-owned function. This avoids a duplicate
native fill implementation and preserves overlapping guest storage. Unlike
word accesses, fills allow unaligned addresses. Zero count is a no-op only
when its address lies inside the supported window; invalid or overflowing
spans fail without writes. Values are truncated to their low byte by the
matched body.

Whole-memory tests cover the reset environment, both FF-filled blocks,
the `100`/`1800` hex-byte clear ranges, zero length, unaligned starts, the last
RAM byte, rejected MMIO, and oversized counts. The CMake diagnostic also
executes the matched helper on synthetic RAM before the pinned load. It is
not yet called by natively integrated `ResetGraph`; the startup stop remains
`800101C0 -> 800141F0`.

The two recovered reset callers now declare the fill helper consistently with
its definition (`u8 *`, signed value, unsigned count, no return value). Their
retail match gates remain required after that prototype correction. A further
explicit-goto experiment on the device query still produced 216 rather than
220 bytes and was reverted; the unregistered draft is otherwise unchanged.

### Callback initialization boundary

`src/main/80042580.c` now recovers the 48-byte dispatch wrapper exactly
(12/12 words; SHA-256
`0f1406120d96daefa80dc1995ef85a2ff1e89dfeaf642574118ad3516b29b1fa`).
It calls slot `+0C` through the guest table pointer at `8006CB84` and returns
the result. Fresh executable reads give table base `8006CB64` and initial
slot target `80042718`. Keep the indirection: this is not permission to embed
a host pointer in the 32-bit guest table or bypass initialization.

The initializer `[80042718,800427F4)` is 220 bytes, SHA-256
`9720c2e99bc65ff2deb0283b241f3c4e8eb0231d1d72b294839cd9fe5ad8b79f`, and is
now registered as `src/main/80042718.c` with all 55 words matching. The
source-backed assembly preserves the guest pointer choreography; it is not a
native callback/MMIO implementation. The guard receives literal one before
`80042C90`; table slots `+14` and `+04` receive the results of `80042C90` and
`80042DB8`, respectively. The result of `8005CE50` is unused. Its observed
incoming `a0` holds the table pointer, without asserting BIOS argument
semantics.

The initializer's first guard is the halfword at `8006BAFC`, initially zero.
A nonzero guard returns zero immediately. On first initialization it:

1. Writes zero to the halfword at `1F801074`, reads it back, writes that value
   to `1F801070`, and writes `33333B33` hex to `1F8010F0`. Pointer cells
   `8006CB8C`, `8006CB88`, and `8006CB90` resolve to those device addresses.
2. Calls `80042C64(base,41A)` (hex count), whose loop clears words, not bytes:
   the clear is `[8006BAFC,8006CB64)`, ending immediately before the table.
3. Calls `8005CD98` on `base+38` hex. Its instructions save `ra`, `gp`, `sp`,
   `fp`, and `s0..s7` and return zero; the separate routine at `8005CDD4`
   restores context. A nonzero return branches to `800427F4`. This saved-context
   relationship is not an ordinary successful callback with a dummy return.
4. Writes `8006CB14` to the saved-stack word at `8006BB38`, passes
   `8006BB34` to BIOS-vector thunk `8005CEF8` (`B0`, selector `19` hex),
   and sets the guard to one.
5. Installs the result of `80042C90` in table slot `+14` and matched
   `80042DB8`'s result in slot `+04`, invokes `8005CE50` (`A0`, selector `72`
   hex), then `8005CF18` (`syscall` with `a0=2`), and returns `8006BAFC`.

The CPU context, alternate guest stack, callback slots, and device writes are
source-backed integration requirements, not evidence that the native runtime
already implements them. Callback-init recovery is registered but its native
platform binding remains open; the device-query/reset contracts now have
explicit callback seams, and the bounded native startup still stops before
`800141F0`.

### Scheduled IRQ bridge boundary, 2026-09-05

`pc_port/irq_scheduler.c` provides a single-owner bridge from PsyCross's
VBlank callback registration to the shared `800427F4` dispatcher. Its B0:19
adapter records the observed guest context address as opaque data, then
refuses installation unless the scheduler has checked RAM, IRQ device, and
hardware-event providers and PsyCross has no callback already installed. The
host VBlank provider must raise the virtual hardware event before the shared
dispatcher samples it. A refused raise does not increment the scheduler's
delivery count or invoke guest callback code.

The adapter supplies B0:17 only while the host VBlank callback is live. It
therefore delegates the recovered dispatcher return without treating the
48-byte guest register record as a host exception frame. The scheduler owns
one registration and removes only that registration; it neither reads nor
restores PsyCross's pointer-through-`int` return value. The archive callback
guard retains its default refusal of both callback APIs. The test runner must
name `--allow-vsync-discard` explicitly for this reviewed VBlank-registration
exception, while `ResetCallback` remains forbidden.

`MusashiCallbackDevice.hook_entry_int_userdata` separates B0:19's platform
scheduler state from the callback initializer's checked MMIO and BIOS provider
state. A null override preserves the existing `userdata` behavior. The
formatter boundary test demonstrates that the recovered initializer sends the
observed `8006BB34` guest context to the separate hook state, while the
scheduler boundary test uses a test-owned VBlank source to verify one raised
IRQ, actual `80042CE8` execution, tick increment, delegated B0:17 result,
refusal handling, and safe removal.

The post-entry startup integration test now supplies this scheduler at the
same B0:19 call reached by the recovered `800101E4 -> 80043300` path. It
observes the scheduler installed with context `8006BB34`, no delivery, and
safe removal after the bounded startup driver returns. The no-delivery result
is expected: that driver has not yet reached the retail code that establishes
the parent IRQ guard, and the test does not write it simply to force a frame.
The same test continues to pass with PsyCross disabled, retaining the ordinary
callback hook route in that configuration.

This is a scheduler contract, not a native cold boot: no production startup
object constructs the scheduler yet, and no guest request/completion or
title/menu path has executed. The bounded display-backed probes below observe
PsyCross's real event loop, but they do not begin at the executable cold entry.

The event-source portion is now observed independently: the display-backed
`musashi_irq_scheduler_live_probe` calls `PsyX_Initialise`, installs the
scheduler, and waits under the exported PsyCross interrupt mutex. Under
`xvfb-run` it observed `PsyCross VBlank -> 800427F4 -> 80042CE8 -> B0:17`,
including a raised virtual IRQ, child execution, tick increment, and delegated
return. Its RAM, IRQ device, and guard state are explicitly synthetic, so this
does not prove that the game's startup has reached a deliverable interrupt or
that a game frame, menu, or input path exists. The CTest is skipped when no
display is available; the observed Xvfb run is the runtime evidence.

The stronger combined probe loads the pinned executable, runs the recovered
callback initializer, and lets that initializer set `8006BAFC` itself after
the B0:19 scheduler installation. Its subsequent PsyCross VBlank observation
therefore reaches the shared chain without directly writing the parent guard:
`80042718 -> B0:19 -> PsyCross VBlank -> 800427F4 -> 80042CE8 -> B0:17`.
The callback/MMIO/BIOS and virtual IRQ devices remain test fixtures, and the
probe starts at the callback initializer rather than the full cold-entry path.
It proves this bounded startup-to-event handoff, not an on-screen game frame,
disc progression, input, or title/menu boot.

The live probes now use `pc_port/irq_controller.c` for the two hardware
registers consumed by the recovered dispatcher. PsyCross VBlank raises
`I_STAT` bit zero through this production adapter; guest writes acknowledge
status with `I_STAT &= value`, and `I_MASK` controls which pending bits the
dispatcher sees. Callback execution and BIOS return ownership remain separate
interfaces, so the controller cannot turn an unknown guest target into success.

The bounded native diagnostic now executes this exact 55-word stream through
the first unresolved service boundary. `MusashiCallbackDevice` owns only the
three observed IRQ/DMA registers; `80042C64` uses the existing checked
decomp-owned clear binding; and the exact 30-word `8005CD98..8005CE10`
save/restore range supplies the register record. The probe verifies the
mask-zero/readback/status sequence, DMA `33333B33`, clear, and saved `ra`
before it stops at `800427A0 -> 8005CEF8` (`B0:19`). It does not invoke or
emulate that BIOS thunk, set the guard, install callback slots, or claim that
callback initialization completed.

When the same platform adapter explicitly accepts `HookEntryInt`, the
diagnostic follows the return path into exact `80042C90`. That stream writes
`00000107` to the live pointer from `8006CBBC` (the pinned entry data supplies
`1F801114`), clears the eight words at `8006CB98`, registers guest handler
`80042CE8` at index zero through table slot `+08` (whose loaded target must be
`800429DC`), and returns `80042D60` for table slot `+14`. The adapter sees the
guest handler address—not a host callback—and may refuse registration. The
bounded runner then stops at the verified `jal 800427BC -> 80042DB8`;
`80042DB8`, channel registration, and delivery remain unimplemented.

The same registration adapter can advance through exact `80042DB8`: it clears
the eight words at `8006CBC4`, writes zero to the live pointer at `8006CBC0`
(the pinned entry data supplies `1F8010F4`), registers guest `80042E08` at
index three, and returns `80042F8C` for table slot `+04`. The runner stops
after that table-store delay slot at the verified `jal 800427CC -> 8005CE50`
(`A0:72`, `ra=800427D4`). This
proves the ordered guest writes and registration requests only; it does not
implement the `A0:72` BIOS service, queue/channel callback delivery, or a
completed callback initializer.

### Read-only retail A0:72 trace, 2026-09-05

The parent process for `artifacts/bios-remove-20260905/retail-run3/observer.log`
exited 0 after [`tools/retail_bios_remove_observe.lua`](../tools/retail_bios_remove_observe.lua)
observed the selected BIOS path. A0:72 closed events `f1000000` through
`f1000004` and removed priority-zero nodes `a00091d0` and `a00091e0`.
The live C0:03 target was `00001444`, read from slot `80000680`; the selected
path avoided stack-load branches `000014A4` and `000014B8`. SR was
`40000000` before and after A0:72, then `40000401` after SYS(2). The run
reported 63 changed RAM bytes and zero MMIO writes. The next GPU command was
`00072740`, with `ra=800592DC` at `800592D4 -> 8005CE38`.

This is evidence for this BIOS/path only. It does not prove generic dequeue
correctness, native BIOS parity, or removal of the native BIOS service.

`A0:72` is the BIOS `_96_remove` service; its behavior is BIOS-version
specific and the standard PS1 reference documents a kernel bug in this path.
The native diagnostic therefore passes the observed table address only to an
explicit provider, which may reject it. If that provider and a separate
`SYS(2)` ExitCriticalSection provider both accept their calls, the exact path
reaches the final `800427F0` return-transfer boundary. It deliberately stops
there in the standalone diagnostic, before executing the return's final nop.
This is not yet integration with the returning `80042580`/ResetGraph caller
and does not establish a completed initialization or native boot.

The following paragraph records the prior provider-boundary milestone; the
fresh graphics gate supersedes its GPU stop with the status-2 sentinel and the
retail-write comparison in `artifacts/gpu-startup-20260905/retail-run1/manifest.json`.
The integrated `800141F0` diagnostic now does execute that final nop and
returns through exact `80042580` and ResetGraph. Its first later unresolved
target is `800592D4 -> 8005CE38`, BIOS `A0:49` / `GPU_cw`, with the live
command word from `80072780` masked to 24 bits in the call delay slot. The
runner records that exact stop instead of treating it as an interpreter fault.
No host GPU synchronization or GP0 command behavior is supplied by this
boundary; the existing GPU device callbacks do not by themselves prove BIOS
`GPU_cw` semantics.

An explicit `GPU_cw` provider may now accept that exact command and supply its
returned guest word. This advances the integrated path to ResetGraph's next
call, `800592DC -> 8005BD7C`; the low-level reset is still a separate native
binding with its own live GPU-device pointer contract. The runner does not
silently reuse the `GPU_cw` adapter as a reset implementation.

Providing that separate adapter now invokes the checked decomp-owned
`musashi_boot_call_8005bd7c` binding with the exact mode and live pointer
cells. The integrated trace returns through the remaining matched ResetGraph
stores/clears and stops at `80014200 -> 800594CC`, the next startup call. The
synthetic integration fixture separately supplies `80072858/68/78/5C` and
records the reset-device contract; it is not GPU timing, display, or menu
evidence.

The next startup call, `800594CC(0)`, is now executed from its exact 25-word
stream. The zero argument stores zero to `8007278A`, bypasses its nonzero
diagnostic callback, and returns the old byte. The integrated runner reaches
`80014208 -> 80059658`; it has no general host binding for nonzero
`800594CC` calls or the stored callback pointer.

The initializer's word-clear helper is now registered in `src/main/80042c64.c`:
44 bytes, 11/11 words, SHA-256
`10a6fc24d646f22be8f184959bea92a50dba690615f6db08e6e1513d70c47e79`.
Its unsigned countdown writes zero words, advancing the destination by four
bytes; zero count performs no store. The initializer's `41A` hex count is
1,050 words / 4,200 bytes, so its final written word begins at `8006CB60` and
the dispatch table at `8006CB64` is outside the clear. Its native binding uses
a byte-backed view and preserves the word-count contract without casting that
storage to a host word pointer. `word_clear_boundary` verifies the full
`0x41A` clear and the surrounding table sentinel; the bounded callback
diagnostic invokes it only before stopping at the first BIOS boundary.

The callback initializer is now registered as `src/main/80042718.c` with all
55/55 words matching. Its guest pointer choreography remains separate from
native callback, interrupt, and BIOS bindings; no native initialization claim
follows from the static match.

The pinned PsyCross `ResetGraph(0)` in `src/psx/LIBGPU.C` clears its own GPU
state/image/splits and ends a scene; it does not populate Musashi's guest
environment records. Simply forwarding the name would not establish the
retail function's required memory effects. No such shortcut is installed.

Preserve one retail address space for overlapping globals before hosting these
functions. In particular, defining independent host globals for aliases would
silently separate state that retail shares. Keep guest data/function words
32-bit; resolve them at explicit boundaries rather than widening packed
records or truncating host pointers. MMIO requires device/platform semantics,
not a RAM pointer obtained by masking every address.

The next slice is closing the stopped `800141F0` graphics-initialization
dependency while retaining the decomp-owned caller. Stop at unsupported calls
until its actual dependency is implemented. Such a diagnostic is partial
startup evidence, never a successful menu boot. The full mixed-assembly draft
in `staging/startup/80010178-integer-add.c` remains unsuitable for native builds.

### Input-startup continuation, 2026-09-05

The exact `8005D8B4..8005D980` controller-startup body is now executed as a
51-word guest stream after the existing `80018918 -> 8005D0F8` path. Its four
platform crossings are explicit adapter operations named only by their retail
vector selectors: `C0:3(2,80078988)`, `C0:2(2,80078988)`, `C0:0A(3,0)`, and
the critical-section exit. Both calls through the callback word installed by
`8005F0C8` execute the exact 26-word `8005F228` record callback stream.

`musashi_boot_run_80018918_input_prefix` composes these routines and returns
only when the guest path returns to its sentinel. Its test starts from the
existing callback/event initialization fixture and checks the selector order,
record control-word writes, and final ready flag. This proves a bounded guest
return under supplied adapters, not controller delivery, BIOS semantics,
native boot, or menu output.

The next direct retail call after `80018918` is `80043300`. Its first call,
`80043450(1)`, is now executed as an exact guest slice. A nonzero injected
`80044D38` result follows retail's return-delay clear and retries; a zero
result executes the exact, call-free `80044BF4` CD command leaf before
returning ready. The dispatcher then reaches its first real unbound transfer,
the `8004359C` B0:7 callback-registration wrapper supplied with callback
`80043398`. `80044D38` itself, B0:7 semantics, and later callbacks remain
explicit boundaries. A normal interpreter retail boot did not reach
`8005D8B4` within the observer window, so no retail controller trace or menu
evidence has been claimed.

### Post-entry dispatcher composition, 2026-09-05

`musashi_boot_run_post_entry_dispatch_prefix` now crosses the genuine
`800101E4 -> 80043300` call boundary only after the existing bounded
post-entry driver reports that exact stop. It then executes the existing
`80043300` guest dispatcher under the explicit retry adapter. Its regression
probe supplies the guest CD-register pointer cells used by the successful
`80044BF4` leaf and verifies the three installed callback words. `COMPLETE`
means this bounded dispatcher returned; it is neither callback delivery nor a
native boot/menu result.

The dispatcher now executes its nested `80043450` instructions in the same
CPU state. Previously the separate runner reset SP to `801FFF80`, causing
the two saved frames to overlap. A regression reproduced the missing nested
return address before the fix and now verifies `80043318` at `801FFF64`
and the dispatcher sentinel at `801FFF7C`. Only the explicit `80044D38`
probe crosses to the host; this adapter still requires implementation for
real CD initialization.

The `80044D38` callback runner likewise keeps `80044670` command submission
in its current CPU instead of starting a second runner at the same stack
address. A failing-before-fix regression checks the command frame's saved
`80044E9C` return at `801FFF60`. The runner still stops at the unresolved
`80044734 -> 80044124` poll; no command completion is inferred.

The tick-enabled command runner now enters the shared executor at `80044670`
and calls `80044124` in the same CPU. Its regression first reproduced the
lost nested return and now verifies `8004473C` at `801FFF34` before the
timeout diagnostic. Direct poll entry continues to use `80044124`. This
preserves command arguments and frames without supplying command completion.

Tick calls in both poll runners now execute their instruction stream in the
current CPU, with the scheduler updating only the tick word at the existing
query boundary. A regression reproduced the missing nested return before
the fix and verifies `80044158` at `801FFF40` after the first tick call.
The shared poll executor also executes timeout state-reset instructions in
the current frame rather than launching a separate reset runner. The
controller command-sequence and timeout probes pass with these changes.

### Corrected timeout comparison, 2026-09-05

Follow-up export audit: `tests/test_native_word_exports.py` now compares all
54 macro-export streams with tracked disassembly, including the split tick
entry/epilogue layout. It reproduced five further incorrect words, corrected
after checking the retail EXE: `800596C0` (`12000002`), `8005F1B4`
(`AC90FFD0`), `8005F1D8` (`0461FFFD`), `800423F4` (`02201021`), and
`800423F8` (`18800007`). The errors affected branch register selection,
an allocator store base, and a signed branch opcode. Count-only extraction
did not catch them. This gate checks exported instruction values, not whole
game behavior or full C decompilation parity.

The exported instruction at `800441CC` was incorrectly transcribed as
`0062102A`. The retail EXE at offset `349CC` and tracked disassembly both
contain `0043102A`: `slt v0,v0,v1`, comparing the threshold against the old
counter. Normal counters through `003C0000` take `8004424C`; counters above
that value or an expired deadline enter the timeout diagnostic. The former
export inverted that decision. A new full-slice disassembly comparison test
reproduced the mismatch before correction. The executor also now maps the
normal-path `addu v0,zero,zero` at `8004424C`.

A fresh unexpired poll with supplied ready state now reaches `80043B9C` on
its first iteration. Earlier reasoning that worker integration required a
retry-exhausted snapshot was based on the incorrect export and is superseded.
This is fixture coverage; device completion and native menu output remain
unverified.

### Status-worker integration, 2026-09-05

The poll executor now executes the assembly-backed `80043B9C` worker in
its existing CPU frame. A regression supplies ready state and worker input
zero, follows the normal poll to its return, and verifies the nested saved
return `80044288` at `801FFF40`. The old explicit worker stop failed this
probe before integration. Formatter crossings still stop explicitly when
no console sink is supplied. All four exported poll/timeout instruction
ranges were compared with tracked disassembly before this change.

This fixture establishes execution and frame preservation under supplied
guest state; it does not prove hardware events or native menu boot.

### Command continuation to the device write, 2026-09-05

The executor now maps `8004473C..80044A8C` from the tracked 263-word
`80044670` disassembly. This is an assembly-backed bridge, not C recovery.
The command/poll fixture returns from the worker, executes command setup,
and stops at `8004482C` when the command pointer is `1F801801`. The test
reproduced the previous missing continuation before this change and verifies
the command mirror at `8006CCA5`. The hardware byte write remains rejected
by the RAM interface; implementing a CD device adapter is still required.

### Callback-registration recovery, 2026-09-04

Freshly rebuilt `staging/startup/800429dc-register.c` initially emitted 356
bytes for the complete retail `[800429DC,80042B30)` range of 340 bytes.
Keeping the callback-array base in `a1` across the guard and enable path,
while using the separately addressed `8006BB2C` on the disable path, recovers
the retail addressing asymmetry. Empty tied-operand assembly constraints
retain the relevant register values without inserting instructions.

The intermediate candidate emitted 340 bytes and the matching oracle reported
**MISMATCH, 79/85 words** against retail range SHA-256
`c4f2aaefb7fdbbaf4b2a1f16ec122758ce2f73d085a9dcb13314c000a6b77468`.
The six then-remaining differing words were the commuted `addu` operands at
`800429FC`, two early-return branches and their delay slots at `80042A18`
and `80042A28`, and the zero callback store at `80042A78` (candidate uses
the known-zero `s2`, retail uses `zero`). The BIOS call sequence and final
interrupt-mask restoration now match at their retail offsets.

Moving the return constraint into individual early-return branches expanded
the candidate; that experiment was not retained. The successful recovery
instead constrains the final IRQ pointer to `v0`, leaves callback allocation
to the compiler, and expresses the slot address as a PS1 integer address.
This preserves the retail load delay, early-return values, literal zero store,
and operand ordering without inserting instructions or changing the algorithm.

The source is now promoted to `src/main/800429dc.c` and registered for the
complete 340-byte range: **MATCH, 85/85 words**. The routine preserves the
guard/current-callback short circuits, interrupt masking, live callback slot
and cached-bit updates, special BIOS calls for indices 0/4/5/6, and mask
restoration. Its pointer views remain PS1-width only; it is not yet compiled
into the native port. Native BIOS dispatch and callback integration remain
open. Native boot and start-menu launch remain NOT_RUN.

Post-promotion verification: all **418/418 registry entries** rebuilt and
matched, **339 Python tests and 42 subtests** passed, and the CMake build
succeeded. These checks establish static recovery and regression status,
not runtime execution of the recovered callback-registration routine.

### DMA callback dependencies, 2026-09-05

The word-clear call from `80042DB8` now has registered source at
`src/main/80043034.c`: **MATCH, 11/11 words**, complete range
`[80043034,80043060)`, retail SHA-256
`10a6fc24d646f22be8f184959bea92a50dba690615f6db08e6e1513d70c47e79`.
It clears the supplied number of 32-bit words, including the zero-count
early exit. This is retail recovery, not a new native binding.

The returned DMA callback-registration target `80042F8C` remains unregistered.
Its draft is `staging/startup/80042f8c-channel.c`; the latest candidate emits
176 bytes after the semantic review below. Before that review it emitted
168 bytes but matched only 17/42 instruction words (first difference at
`80042FA4`). Equal length is not acceptance. The retail routine skips unchanged callback
words; otherwise it updates the slot and reads/writes the control register
through `D_8006CBC0`. The enable path combines the low 24 read bits with
`0x00800000` and the channel bit at `index + 16`; disable clears that channel
bit after applying the low-24-bit mask and `0x00800000`. These are observed
instruction operations, not proof of a native DMA device implementation.
The remaining draft differences include expression scheduling and register
allocation; no generated candidate bytes are patched or registered.

Follow-up compiler experiments did not close this match. Forcing `a0` for
both callback and masked flags, keeping `a2` live through the return, and
volatile register temporaries emitted 172–184 bytes. An explicit OR assembly
experiment also failed and was removed. The retained draft uses signed
32-bit callback words, a volatile control-register access, and only the
empty channel/base allocation constraints. Its main remaining differences
are the callback move, mask scheduling, OR association, and merging the
enable-path store with the disable-path tail. A separately existing draft
`staging/startup/80042f8c-channel-callback.c` is preserved untouched; the older
automatic draft in `staging/candidates` failed because its input range ended
at a jump without the delay slot. Any further automatic recovery must use
the full `[80042F8C,80043034)` range.

The range scanner has now been corrected for this failure: a `j`/`jr` only
completes after a contiguous delay-slot instruction, and PC-relative forward
branch destinations must be covered before an intervening `glabel` can end
the range. Three failing regression cases established the prior defect;
additional cases reject missing forward targets and noncontiguous delay
slots. The actual scanner now reports `func_80042F8C` as 168 bytes. Recovery
from `staging/startup/80042f8c-full.s` succeeds instead of reporting a missing
delay slot. The resulting plain C draft (`80042f8c-full.c`) emits 164 bytes,
so correcting the recovery input does not itself close the compiler match.
No batch promotion or registry change was performed for this scanner fix.

A subsequent registry-boundary cross-check caught an alignment regression
in that change: padding after a completed `endlabel` cleared the terminal
state and joined some BIOS thunks to their neighbors. A failing padded-boundary
test now guards the repair. A completed `endlabel` excludes padding only when
the delay slot and known forward paths have been covered; an interior
`endlabel` before a jump delay slot remains ignored. The DMA range remains
168 bytes and the registered 12-byte BIOS thunks retain their own boundaries.
The scanner is still not a function-boundary authority: the generated listing
has no entry label at some separately recovered functions (for example,
`8005BED8` and `8005FC98`), and those remain coalesced in automatic enumeration.
No registry acceptance was changed to fit the scanner.

A compiler-baseline sweep also failed to close DMA registration. All three
preserved drafts (`80042f8c-full.c`, `80042f8c-channel.c`, and
`80042f8c-channel-callback.c`) were built with the five other pinned toolchains
(`2.6.0`, `2.7.2-cdk`, `2.8.0`, `2.8.1`, `2.91.66`) at both `-O2` and `-O1`.
None of these 30 builds matched. Only the constrained channel draft reached
168 bytes with the first two toolchains, scoring 8–17 of 42 words; the other
builds had different lengths. Swapping the OR subexpression operands in the
plain draft under the default compiler still emitted 164 bytes and was not
retained. This rules out those combinations for these drafts, not the
toolchains for every possible recovery. The pinned default and registry
remain unchanged; repeating this matrix on unchanged drafts adds no evidence.

The complete body has since been promoted to `src/main/80042f8c.c` and
registered: **MATCH, 42/42 words**, `[80042F8C,80043034)`, SHA-256
`f5aead0f2a6fb145132d5490d40554064fe6a3af98885b75e9743b8f1682097d`.
The body includes the shared `8004302C` return tail and its delay slot; the
separate word-clear function begins at `80043034`. Symbolic inline branches
made the pinned assembler insert extra padding, so the promoted overlay uses
the audited retail instruction words directly. This is source-backed assembly,
not generated candidate output and not a native DMA implementation. The
previous C drafts remain staging evidence only.

### Callback-registration BIOS thunks, 2026-09-05

The two BIOS call sites used by the matched `800429DC` registration routine
now have byte-exact source overlays. Each body is 12 bytes; the following
alignment nop is excluded, as with the existing `8005CF38` write thunk.

| Entry | Vector / selector | Retail body SHA-256 |
| --- | --- | --- |
| `8005CF48` | `B0 / 5B` | `00edef32236c975680bc303e138c5c4f0af7b8f4cc322f5bb4225de45f51726a` |
| `8005CF58` | `C0 / 0A` | `5679de6d872f74c1cf6e01a3593fcbf41172e1f626c0c21341d455839067f641` |

Both preserve argument registers and the caller's return address, loading
the selector into `t1` in the `jr t2` delay slot. The oracle verified **3/3
words for each**. These overlays express retail dispatch instructions only:
no native BIOS return value, interrupt policy, or event behavior is fabricated.
The corresponding native services and the DMA registration match remain open.

### DMA draft semantic review, 2026-09-05

A fresh-context, read-only reviewer identified three constraints independent
of code-generation matching. Signed address shifts and signed channel-bit
shifts did not express MIPS wrapping/count masking; these were actionable.
The channel draft now computes unsigned address arithmetic and masks the
variable shift count to five bits. Callback publication must also precede
MMIO reads/writes; the callback-slot view is now volatile. The earlier emitted
candidate already placed those stores before MMIO, but textual ordering of
ordinary versus volatile accesses is not a portable native guarantee.

The pointer-alias finding remains valid and unresolved: channel `-1` addresses
the `8006CBC0` pointer cell itself. Retail snapshots its bits before overwriting
that slot, then accesses the saved target. A typed host integer array plus a
separate pointer global is not an adequate shared guest-memory model. No
native binding is claimed, and no bounds restriction was added to hide this
case. The revised PS1-only draft emits 176 bytes and fails the oracle length
check against 168; it remains unregistered. Cross-model review was skipped
for the automatic continuation. No new exact match or native boot resulted.

### Saved-context instruction recovery, 2026-09-05

`8005CD98` and `8005CDD4` now have registered source-backed assembly overlays,
each matching all 15 words of its 60-byte body. Capture writes `ra` at offset
0, `gp` at 44, `sp` at 4, `fp` at 8, and `s0` through `s7` at offsets 12–40,
then returns zero. Restore loads those same registers and jumps to the loaded
`ra` with `v0` equal to its incoming second argument. In particular, it does
not replace a zero argument with one. Alignment nops after `8005CE0C` are not
part of the restore body.

Retail body hashes are
`278fbe40af5ab27be7a1302914ac62558cb96e3323353cc94c5b916af6ca7aa4`
(capture) and
`462aefcefbdfb2be0b9b3f695de3a11dc6af6f48163c792ceea2d1ec9334800c`
(restore). These overlays close the static instruction recovery, not the
native control-transfer requirement. No host context is fabricated, no guest
stack pointer is used as a host stack, and no native setjmp/longjmp substitute
has been installed. The callback initialization context binding remains open.

### Remaining startup service entry recovery, 2026-09-05

Four more source overlays now match their complete retail bodies:

| Entry | Dispatch | Matched body |
| --- | --- | --- |
| `8005CE38` | vector `A0`, selector `49` | 12 bytes, 3/3 words |
| `8005CE50` | vector `A0`, selector `72` | 12 bytes, 3/3 words |
| `8005CEF8` | vector `B0`, selector `19` | 12 bytes, 3/3 words |
| `8005CF18` | syscall with `a0 = 2`, then `jr ra` | 16 bytes, 4/4 words |

The first three install `t1` in the vector jump's delay slot and exclude
the subsequent alignment nop. The syscall entry includes its return delay
slot; it is not a BIOS-vector tail jump. These entries are used by the
graphics/callback startup path and are now registered source, but are not
compiled into the native port. Their services, callback scheduling, and
context-transfer semantics remain unimplemented at the native boundary.
Static entry recovery does not establish native boot or launch readiness.

### Native halfword RAM access, 2026-09-05

The shared RAM boundary now exposes `musashi_boot_read16` and
`musashi_boot_write16`, needed for the callback guard at `8006BAFC` and
cached interrupt-state fields. These helpers operate on the same byte array
as the existing 32-bit accessors, with explicit little-endian encoding and
no host `uint16_t *` alias cast. They accept only aligned two-byte spans in
cached RAM. Invalid spans, null pointers, MMIO, and unsupported guest aliases
fail without changing RAM or read output.

Tests first failed on the missing functions, then passed with all 65,536
halfword values, overlapping byte/word views, first/last valid RAM addresses,
and rejected requests preserving state. This adds native memory primitives;
the callback initialization routine has not yet been bound to them. Device
halfwords still go through the separate device boundary, not these helpers.

The callback-state clear binding has also been tightened: a nonzero count
requires word alignment, and counts larger than RAM capacity are rejected
before multiplication, preventing size wrap on a 32-bit host. Zero count
retains the existing valid-RAM no-access behavior, including unaligned byte
addresses. An unaligned nonzero regression failed before this repair. The
probe now compares the whole RAM image for rejected requests and for the
`8006BAFC` / `0x41A`-word initialization clear, not only a trailing sentinel.
Retail and native builds now use one loop body in `src/main/80042c64.c`, with
only the per-word store operation adapted to host byte backing. The changed
source was rebuilt and still matches **11/11 retail words**. This repairs
the existing native dependency; callback initialization itself remains unbound.

### Live interrupt-mask pointer in native reset, 2026-09-05

Native reset no longer substitutes `1F801074` for the guest pointer cell
`8006CB8C`. Each call of the shared `800426FC` exchange receives a fresh
snapshot of that cell, then uses the same address for its read and write,
matching the retail pointer-load lifetime. A regression changes the cell
inside the synthetic device read: the first write must still use the old
address, while the restoring exchange must use the changed address. It
failed before this repair and passes afterward. Normal mode tests now seed
the actual pointer cell rather than relying on the former hardcoded target.

This is a bounded IRQ-pointer correction. The remaining reset DMA/GPU
addresses and query binding still need their guest-pointer lifetimes handled;
the synthetic alternate address is not evidence of hardware-register behavior.
Unsupported device semantics and native startup remain unresolved.

The reset body's own DMA/GPU accesses now resolve their live pointer cells
as well (`80072868`, `80072878`, `8007285C`). Retail snapshots `80072878`
for the read/OR/write operation, but reloads `8007285C` between the two GPU
commands in reset classes 1/3. The native boundary preserves this distinction
with a per-operation pointer snapshot for the OR and a fresh cell read for
each command write. One shared reset algorithm remains; only the memory and
device operations are adapted.

A synthetic device regression changes both pointer cells during callbacks.
It failed before the repair; afterward the RMW write retains the read's
address and the second GPU command follows the newly loaded address. All
normal reset-mode cases still compare complete RAM images. This does not
bind the query routine's pointer cells or supply real GPU/DMA semantics.

### Live GPU query pointers, 2026-09-05

The subsequent query binding repair removes that query-pointer limitation.
`musashi_boot_call_8005c1c0` now requires `MusashiBootMemory *` as its first
argument; all in-repository callers were updated. There is no implicit
fixed-address fallback. The shared routine snapshots the data pointer after
the initial command, retains it for the later data write, then reloads both
pointer cells before the discarded data read. It retains the resulting
control pointer for the control read and optional `20000504` command. The
`09000001` branch instead freshly loads the control cell, matching retail.

A synthetic seven-operation probe mutates pointer cells after the initial
command, after the first data/control reads, and during the discarded read.
The full trace asserts which mutations become visible and which snapshots
must remain unchanged. Original direct-query and reset probes also pass.
The changed query source still matches **55/55 retail words**. Device
callbacks continue to provide synthetic behavior in these probes; this is
pointer/order fidelity, not GPU emulation, timing parity, or menu boot.

### Callback initializer promoted to C, 2026-09-05

`src/main/80042718.c` now contains byte-exact C rather than the earlier
assembly-only overlay. The initial C draft freshly rebuilt to 228 bytes;
retaining the `8006BB38` base with an empty tied operand removed the separate
absolute guard address. Keeping that base in `s0` and constraining its final
adjusted value recovered the returned state-pointer adjustment in the syscall call's
delay slot. The final promoted source matches **55/55 words**, the same
220-byte range and retail digest as the previous overlay. No candidate bytes
were patched, and the existing registry entry retains its address and size.

The recovered control flow includes the already-initialized return, mask
readback, word clear, saved-context call, nonzero-resume call to `800427F4`,
stack-record setup, callback-table writes, and BIOS/syscall calls. This is
now suitable for developing a shared native control-flow body with explicit
operations; its current PS1 pointer views and CPU-context operations are
still not host-safe and it is not compiled into the native port.

### Nonzero-context dispatch recovery, 2026-09-05

The initializer's `800427F4` call is the interrupt dispatch loop, not a
no-op resume notification. Its complete range is `[800427F4,800429DC)`,
488 bytes. `src/main/800427f4.c` now matches **122/122 retail words** and is
registered. Initial drafts emitted 492 and 480 bytes; a later 488-byte draft
matched only 99 words. Empty tied operands recovered register allocation,
pending-mask load order, and the loop-invariant shift constant. A compiler
memory clobber preserves the active-flag store before the mask-pointer load.
The promoted source was rebuilt and compared independently of staging:
SHA-256 `e1f304577ce848c1c73e8aaec2233fa0a27d69a69339585ab9778f1c468ff9fe`.

The observed loop sets `8006BAFE`, intersects IRQ status, IRQ mask, and cached
enabled bits, then visits up to eleven callback slots starting at `8006BB00`.
It acknowledges a selected IRQ before loading that callback slot and calling
it, and rescans live interrupt state after a pass. The tail tracks remaining
masked interrupts with the signed counter at `8006CB94`; after the observed
threshold it prints diagnostics, resets the counter, and clears status. It
clears `8006BAFE` and calls `8005CED8`. Neither native dispatch nor that BIOS
service is implemented by this recovery.

Read-only adversarial review identified portability constraints: the tail's
binary expression and diagnostic arguments do not define portable C evaluation
order, and the ordinary callback load does not provide a portable ordering or
aliasing guarantee against MMIO. The pinned PS1 build's exact comparison proves
the required instruction order for this artifact, not for another compiler or
host memory model. These are accepted PS1-only limitations; native integration
must explicitly sequence those accesses through shared guest RAM/device views.
Cross-model review was skipped in the automatic continuation.

A native binding
must not replace the rescan loop with one pass, pre-copy callback slots, or
silently convert an unresolved interrupt loop into successful initialization.

### Shared native callback registration, 2026-09-05

`src/main/800429dc.c` now compiles one registration control-flow body for both
the pinned PS1 compiler and the native port. The PS1 branch retains its typed
pointer views and register constraints; a fresh build still matches all
**85/85 words**, SHA-256
`c4f2aaefb7fdbbaf4b2a1f16ec122758ce2f73d085a9dcb13314c000a6b77468`.

The native branch uses checked, little-endian guest RAM and explicit device
operations defined in `pc_port/include/musashi_callback_registration.h`.
The decomp-owned code reads the previous callback, honors the same-value and
uninitialized-guard exits, disables the IRQ mask, publishes the callback slot
and cached enabled bits, performs the applicable B0:5B/C0:0A calls, and restores
the mask. It snapshots the IRQ pointer for the initial read/clear pair and
reloads that pointer after BIOS calls for the final write. Indices use guest
32-bit address arithmetic and masked shift counts; unsupported RAM or device
accesses are refused.

`musashi_boot_call_800429dc` returns success and publishes the previous callback
word only when the routine returns. A missing/refused operation leaves earlier
RAM and device effects visible and does not publish a return value. Such a
partially executed call cannot safely be retried as a fresh successful
registration. Same-value and uninitialized-guard returns need no device.

`tests/test_callback_registration.py` checks enable and disable operations on
all eleven slots, complete surrounding RAM, BIOS routes and operation order,
live pointer changes, guest index wrapping, and refusal after partial effects.
The `callback_registration_startup` CTest then uses the hash-verified retail
EXE's initial RAM with the existing exact-instruction initializer diagnostic.
Its registration path calls this shared native C: slots 0 and 3 receive
`80042CE8` and `80042E08`, and the cached enabled bits and IRQ mask become
`0009`. Removing B0:5B prevents the diagnostic from reaching its final return
boundary. Assertions remain active when this probe is built with `NDEBUG`.

The integration probe still supplies synthetic device and BIOS services and
starts at the initializer's bounded entry. Startup-runner integration through
the actual dispatch wrapper is recorded below.
This establishes native execution of the recovered registration body and its
ordered initialization effects. Interrupt delivery, BIOS semantics, saved CPU
context restoration, native cold boot, and the interactive game menu remain
unproven.

### DMA registration promoted to shared C, 2026-09-05

`src/main/80042f8c.c` now contains a shared C body in place of the earlier
literal-word assembly overlay. The PS1 branch independently rebuilds to
**42/42 retail words**, with the unchanged range and SHA-256
`f5aead0f2a6fb145132d5490d40554064fe6a3af98885b75e9743b8f1682097d`.
No candidate output was edited. Empty tied operands retain the array and
control registers, order the masked control value before channel-bit
calculation, and recover the zero-register store on the disable path.
The registry records both this routine and `800429DC` as reviewed C/function
entries; its total range count does not increase.

`musashi_boot_call_80042f8c` binds that body to the shared guest RAM and the
checked `MusashiDmaCallbackRegistrationDevice` operations. When the callback
changes, it snapshots the pointer at `8006CBC0`, publishes the word in the
array at `8006CBC4`, reads the control register, and writes the updated flags
through that same pointer. The old pointer remains authoritative even when
the indexed slot aliases its pointer cell, or a device read changes that
cell. The native arithmetic preserves guest address wrapping and masked
variable shifts. Device refusal preserves earlier effects and leaves the
caller's previous-callback output untouched.

Tests exercise all eight slots in both directions, unchanged callbacks with
no device, full surrounding RAM, pointer-cell aliasing, read/write refusals,
misaligned device access, and wrapped indices. Disabling index seven clears
bit 23 even though the expression also ORs in that bit; this observed edge
case is preserved. The startup registration probe additionally exercises the
newly installed DMA target against the initialized RAM with a synthetic
channel-two register/clear call, checking control values `00840000` and
`00800000` and the returned previous callback word.

The channel-two diagnostic does not advance the game caller or deliver DMA.
Actual DMA transfers, IRQ delivery, native saved-context handling, and the
genuine game startup/menu sequence remain open.

Final verification for these registration bindings: `python3 -m pytest tests
-q` passed 420 tests and 84 subtests; all 11 CTest targets passed, including
both Xvfb renderer checks and the integrated registration diagnostic. Both
native archives passed the callback-symbol guard. The complete retail registry
rebuild passed **432/432 ranges**; its summed byte count remains 26,628.

### Startup dispatch uses recovered registration, 2026-09-05

The shared `800425B0` and `800425E0` wrappers now read the live guest table
pointer at `8006CB84` and dispatch through slots `+08` and `+04` to the
recovered IRQ and DMA registration bodies. Each PS1 build still matches
**12/12 retail words**. The native bindings refuse unsupported table reads
or unknown targets before writing state, and return the actual previous
guest callback only after the registration body completes. Tests relocate
the table, exercise successful state changes, and verify that unknown targets
preserve RAM, device state, and the caller's output word.

`MusashiCallbackDevice.registration_device` supplies only the checked IRQ
register and B0:5B/C0:0A operations. The startup runner no longer accepts a
game-level `register_callback` hook: its `800425B0` calls execute the shared
decomp-owned wrapper and registration body. The initialization and CD probes
check actual guest slots 0/3/2 (`80042CE8`, `80042E08`, `80045290`) and the
corresponding enabled bits and IRQ mask (`0009`, then `000D`). The dedicated
startup probe's synthetic DMA register/clear calls now use `800425E0` through
the table installed by the initializer.

The broader fixture now models IRQ-mask readback: retail writes zero to
I_MASK before reading it and copying the result to I_STAT. Its previous fixed
read response could not represent this sequence. The corrected assertion
checks that observed zero readback as well as subsequent registration effects.

These remain bounded diagnostics with synthetic device/BIOS providers.
Interrupt delivery, DMA transfers, saved-context restoration, genuine native
cold boot, and an interactive menu remain unimplemented or unproven. The
registry still contains 432 ranges; wrapper classifications change the
reviewed recovery totals, not the registered range or summed-byte counts.

Verification after dispatch integration: 424 Python tests and 84 subtests
passed; all 11 CTest targets passed, including both renderers. Both native
archives passed the callback-symbol guard, all **432/432 registry ranges**
rebuilt and matched retail, and `git diff --check` passed. These checks do
not establish native menu boot.

### Shared guest-RAM VBlank handler, 2026-09-05

`80042CE8` now shares its C loop between the pinned PS1 compiler and the native
port. Its 120-byte PS1 output still matches **30/30 words**, SHA-256
`d3914404d3789d86214634224413c1cd38ef8ab774c079928e1f9690d5835be5`.
The native body increments the guest counter at `8006CBB8`, then reads each
of the eight words at `8006CB98` immediately before invoking a nonzero target.
It neither copies the table ahead of time nor converts guest words to host
pointers. Callback effects on later slots and the counter remain visible.

The caller supplies a checked guest execution router. It may report success
only after the actual callback returns; no default success implementation is
provided. Missing/refused execution stops the body with preceding effects
intact. This API does not retain a resumable frame, so a partial execution
cannot be retried as though it had never started.

Four new native tests cover whole-RAM preservation and counter wrap on empty
slots, live callback mutation, refusal before later callbacks, and missing
execution. Their callback implementations are synthetic fixtures. The existing
startup probe also directly invokes the handler after initialization has
cleared its slots, confirming the real guest tick increments without an
executor. That direct diagnostic does not deliver an interrupt or establish a
VBlank clock. IRQ scheduling, the parent interrupt dispatcher, and execution
of installed game callback targets remain separate integration work.

Fresh verification: 428 Python tests and 84 subtests passed, all 11 CTest
targets passed, and both native archives passed the callback-symbol guard.
**432/432** registered ranges rebuilt and matched retail. Range and summed-byte totals
remain unchanged; the handler is now explicitly classified as a C function.

### Observed VBlank target recovered, 2026-09-05

The qualified retail run in `RETAIL-RUNTIME-BASELINE.md` observed slot seven
calling `800616D0`. Its complete `[800616D0,80061770)` body is now recovered
in C and matches 40/40 retail words. Its two direct helpers also match:
`8006291C` is 27/27 words and `80062988` is 4/4 words. All three are registered
as complete C functions; these PS1 pointer views are not yet native bindings.

`80062988` returns the sign bit of `80072A34`. For an initially nonnegative
value, `800616D0` invokes `8006291C` and queries again. The worker selects a
16-byte record at `80078D38 + index*16` and a function word at
`80078D78 + index*4`. After a nonzero callback result, it re-reads the current
index word and decrements it. A native binding must retain that live re-read
and guest arithmetic rather than decrementing the original index snapshot.

If the second query is negative, `800616D0` sets `80078C88`, copies the pending
words `80078C80/84` into `80078CC4/C8`, clears the pending words, and invokes
nonzero `80078CC0` with the two saved values. Explicit argument-read order and
volatile shared-state views reproduce the retail instructions. The ordinary
draft emitted 144 bytes; the first volatile draft matched 37/40 words until
argument reads were sequenced. Failed drafts remain in the private scratch
directory `/tmp/musashi-vblank-recovery.S67uBx`; no candidate bytes were edited.

The capture establishes arrival and return for `800616D0`, not the worker's
internal request target or completion callback. Those live dependencies and
native IRQ dispatch remain open. No game callback has been replaced by a
successful synthetic implementation.

The complete registry re-verifies **435/435 ranges**, totaling 26,912 summed
bytes. Focused progress/registry tests pass (27 tests, four subtests), and
`git diff --check` passes. These three recoveries are not linked into the native
target yet, so no new native-boot acceptance is claimed.

### Shared native VBlank callback chain, 2026-09-05

`800616D0`, `8006291C`, and `80062988` now compile their shared C control flow
into the native library. Fresh PS1 builds retain all 40/40, 27/27, and 4/4
retail words respectively. Native operations use checked guest RAM, unsigned
word arithmetic, and explicit one-/two-word guest execution interfaces in
`pc_port/include/musashi_vblank.h`. Unknown outer slot targets are refused;
the router binds only the retail-observed `800616D0` target.

The worker retains its index snapshot for selecting a function word and
16-byte record address, then re-reads the live index before a successful
request's decrement. Native decrement wraps as a guest word. The outer body
queries that updated state, copies the live pending words, snapshots the
completion target, clears pending state, and invokes a nonzero completion
with the saved words. Request/completion execution can fail, retaining earlier
effects; neither missing code nor a partial return is promoted to success.

Seven native tests cover the full handler/router/three-body chain: idle
whole-RAM preservation, busy and successful requests, callback mutation of
pending state and the index, word wrapping, request/completion refusal,
unavailable execution, invalid table reads, and unknown outer targets.
The request/completion implementations in these tests are synthetic. The
existing startup probe separately executes `8005FCB8`'s real initialization,
checks its slot-seven target and negative index, and directly invokes the
shared VBlank chain without an external executor. It increments the actual
tick through the handler and returns through the callback's genuine idle path.
No slot or idle index is injected into that integration probe.

The shared source initially exposed mixed declarations unsupported by the
PS1 compiler. After correcting C89 scopes, taking addresses for all globals
changed its instruction count; retaining direct global operations in the PS1
view restored the exact output. Native accesses remain explicit checked words.
This connects recovered startup state to the observed callback's native body.
Scheduled IRQ delivery, nonempty request/completion code bindings, and menu
boot are still unfinished.

Verification after this integration: 435 Python tests and 84 subtests passed,
all 11 CTest targets passed, both native archives passed the callback-symbol
guard, and all 435/435 registered ranges rebuilt byte-exactly. The diff check
also passed. Registered coverage did not increase; this change connects the
existing recovered bodies to native execution.

### Shared native parent IRQ dispatch, 2026-09-05

`src/main/800427f4.c` now compiles into the native library with
`MUSASHI_NATIVE_IRQ_DISPATCH`. Its shared dispatch loop keeps the retail
acknowledge-before-slot-load ordering and rescans live enabled/status/mask
state after each pass. Guest callback words stay in shared RAM. Device reads,
writes, callback execution, and the diagnostic call have checked providers;
missing or refused operations stop with result zero and preserve prior effects.

The native tail explicitly snapshots both register pointers before device
reads and retains those snapshots for diagnostic argument reads, matching
`80042920..80042980`. It reloads the status pointer after the diagnostic,
clears the counter, then writes status. Counter increment uses word wrapping;
the threshold comparison remains signed. This sequencing must not be replaced
with C expressions whose argument evaluation order is unspecified.

Result `MUSASHI_IRQ_CONTEXT_RETURN_PENDING` (2) stops before `8005CED8`.
It does **not** mean the BIOS restored CPU context or that an interrupt returned.
Both retail exits — including the guard-zero diagnostic path — now offer the
same explicit platform return provider after their observed preceding effects.
Without that provider, no default success implementation exists for
diagnostics, callbacks, or BIOS return.
An uncleared enabled interrupt continues the retail rescan loop; a device
provider can refuse further reads, but no timeout is reported as completion.

Eleven focused tests in `tests/test_native_irq_dispatch.py` exercise a synthetic
IRQ device, acknowledgement mutation of the live callback slot, delivery of a
newly raised IRQ on rescan, refusal effects, negative counter wrapping, and
pointer snapshot/reload ordering. The normal IRQ test executes the actual
shared `80042CE8` child and observes its tick increment before the parent
reaches the BIOS boundary. Synthetic diagnostic callbacks in tests are not
production bindings. This is a directly invoked dispatcher test, not scheduled
hardware delivery or startup/menu proof.

The PS1 variant remains **122/122 words**, 488 bytes, retail digest
`e1f304577ce848c1c73e8aaec2233fa0a27d69a69339585ab9778f1c468ff9fe`.
Input C0 kernel ownership has since been bound; the current stop is the first
CD byte write at `80044DBC`. The optional serial path uses the dedicated
112-byte scratch `[8568,85D8)`, executes five exact SIO configuration writes around a real Timer2 sample,
then refuses its unknown child. This is not a natural
controller transaction. The eight callback targets have exact source exports
and actual stores verified by focused executor tests. Unknown targets refuse;
delivery of these eight callbacks during boot remains unobserved.

### B0:17 exception-return boundary, 2026-09-05

`func_8005CED8` is now recovered as the three-word B0:17 thunk and matches
all **3/3** retail words (digest
`6403f02e44574f9166b7e8b8d3cb89a7a05b124c57f6427b05647750b30f0d1f`).
A fresh normal interpreter boot qualified the parent dispatch call by its
return address `800429B8`. The first observation entered the thunk with
dispatcher registers (`sp=8006CAEC`, `s0=0`, `s1=1`) and inactive flag zero;
the retained 48-byte game record contained `ra=80042780`, `sp=8006CB14`,
`fp=801FFFC0`, and `gp=80074750`. The record return PC is precisely the
initializer's nonlocal branch after `8005CD98`; it is not the IRQ exception
frame used by B0:17. Observer SHA-256 is
`9ecbb71b134c00353a5d1aaaedc4ac719f15b93c1f9db977ab6679f1d0678cfb`;
the private normal-boot log SHA-256 is
`4f2e72c6ffd1f839a69a3a51f14b1a9635585d5ef3d897415d1e6dc2c0ba8470`.

`MusashiIrqDispatchDevice.return_from_exception` is therefore an explicit
platform-owned boundary. The dispatcher returns
`MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED` only after the provider accepts the
return; without a provider it retains the `PENDING` boundary. A refusal after
the active flag clear preserves the prior acknowledge and callback effects and
returns zero. This interface does not infer host context from `8006BB34`, does
not resume a guest PC by itself, and does not prove scheduled IRQ delivery,
startup continuation, or menu boot. Focused tests cover provider ordering and
refusal effects. The guard-zero diagnostic route now has the same tested
provider behavior: it diagnoses first, then delegates B0:17 without touching
the active flag. A real scheduler must still own the actual exception frame.

### Startup-chain recovery ranking, 2026-09-18

`80010178` call order with registry status, ranked by boot-path reachability.
`C` = portable native-executable; `asm` = word-exact only; `part` =
C/unclassified (extent unproven); `--` = unmatched. This is the work order
for making the executed path native: top first.

| order | function | status | note |
| --- | --- | --- | --- |
| 1 | `800100A0` | asm | principled-assembly boundary, stays |
| 2 | `80043060` | C | done |
| 3 | `800141F0` | C | done |
| 4 | `8005FC68` | part | promote extent next: 48 B leaf |
| 5 | `8005FCB8` | part | promote extent next: 96 B leaf |
| 6 | `80018918` | C | done |
| 7 | `80043300` | C | matched 38/38 this turn (goto-vs-LICM idiom) |
| 7a | `80043450` | C | matched 27/27 this turn (taken-block delay-sink idiom) |
| 7b | `8004359C` | C | done |
| 7c | `800435B4` | C | done |
| 7d | `8004654C` | C | done |
| 7e | `80046564` | -- | second unmatched callee of 7; draft only |
| 7f | `8005C604` | C | done (varargs wrapper) |
| 8 | `8002C8F4` | -- | next unmatched after 7 returns; m2c draft 7 words long (221 vs 214), scattered $a-reg allocation diffs = temp-to-register mapping puzzle, plus a possible store-target misalignment near word 48; fix allocator mapping first, likely fixes many sites at once |
| 9 | `8001971C` | -- | follows 8 |
| 10 | `80010214` | -- | 0x724 tail; the large one |

Callbacks `80043398/C0/E8` are C. Net: the path is native-executable
through step 7 except the two unmatched leaves 7a/7e, which currently run
as word exports; steps 8-10 are the next matching targets in order.
