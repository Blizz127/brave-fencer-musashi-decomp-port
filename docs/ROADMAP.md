# Roadmap

The project is intentionally staged so that every later porting decision is
backed by an observed retail executable.

1. **Retail registration**
   Register the contributor's CHD and verify the pinned size and SHA-256.
2. **Disc extraction**
   Extract the CHD with an installed, explicitly selected `chdman` build (or
   another reviewed extractor). If no extractor is available, stop; do not
   infer files from the CHD container.
3. **Boot identity**
   Discover `SYSTEM.CNF`, resolve its `BOOT` target, locate the `PS-X EXE`,
   and record its header fields plus whole-file and header hashes.
4. **Toolchain identification**
   Determine the original compiler, version, assembler, linker, flags, and
   post-link transforms. See [COMPILER-ID.md](COMPILER-ID.md).
5. **Static layout and symbols**
   Partition the executable into code/data, establish sections and labels,
   and add only evidence-backed declarations under `asm/` and `src/`.
6. **Byte matching**
   Compile one bounded function at a time and compare the resulting bytes with
   the pinned executable. A successful build is not a match without a binary
   comparison.
7. **Runtime validation**
   Validate the matched executable under a deterministic PS1 emulator, then
   exercise the same decompilation-backed code through the Linux and Windows
   ports.

Steps 1 through 3 are complete and recorded in
[MILESTONE-1.md](MILESTONE-1.md). Step 5 has a first pass: splat covers 99.50%
of the executable as raw assembly.

Step 4 is under way and has its first result. `tools/identify_compiler.py`
records code-generation fingerprints in `provenance/compiler_evidence.json`;
the executable carries no toolchain strings whatsoever, so identification rests
entirely on structural evidence.

`tools/discriminate.py` compiles one function against every candidate and
eliminates those that cannot reproduce retail. The candidate set is cut from
six to three — PSY-Q 4.3, 4.4 and 4.5 are out, leaving 3.5, 4.0 and 4.1 — and
that result is **replicated** across two independent functions.

`func_80014128` carries the stronger evidence, because it matches retail
byte-for-byte: its C is known correct, so failure to reproduce it is decisive
rather than conditional on the decompilation. The difference is the return
delay slot — retail restores `$sp` before `jr $ra` and leaves the slot empty,
while the 2.8.x line fills it with the restore.

Step 4 is now **complete**. `func_800CF3B0`, whose C is verified correct by a
byte-exact match, is reproduced by exactly one candidate out of six:
**gcc-2.7.2-psx, PSY-Q 4.0, ASPSX 2.56**, at `-O2 -G0`. Its closest rivals
manage 19 of 22 words. The identification is recorded in
`provenance/compiler_identity.json`; see [MATCHING.md](MATCHING.md).

Step 6 has its oracle. `tools/match_function.py` extracts a word-aligned range
of retail text by runtime address and renders a verdict against rebuilt bytes,
exiting 0 on a match, 1 on a mismatch, and 2 on any refusal.

The local toolchain needed to produce candidates is present and verified
working: `cc1` from `gcc-2.7.2-psx` self-reports as
`GNU C 2.7.2 [AL 1.1, MM 40] Sony Playstation`, `maspsx` is available, and
binutils supplies `mips-linux-gnu-{as,ld,objcopy,objdump,nm}` driven with
`-EL` for little-endian output.

Step 6 has two matches. `func_80012AB0` rebuilds byte-identically from
`src/main/80012ab0.c`; it carries no toolchain evidence, since every locally
available compiler at every optimization level reproduces it.

`func_80014128` is the first match containing a call. `build_candidate.py` now
links before extracting, so functions referencing globals or calling other
functions are comparable at all — previously only relocation-free leaves were,
which excluded most of the 1334 functions. See [MATCHING.md](MATCHING.md).

Overlay code is now reachable and matchable. `MAIN.CD` unpacks into 49 members,
member 0007 splits at an observed load address of `0x800CEDF8`, and
`func_800CF3B0` inside it matches byte-exactly. See [OVERLAYS.md](OVERLAYS.md)
for how that address was derived from the member's own bytes rather than taken
from an external memory map.

No work toward step 7 has been attempted.

## Evidence tracks

- **Static matching:** rebuilt MIPS bytes equal the pinned retail bytes for a
  precisely named function, object, overlay, or executable range.
- **Emulator observation:** the rebuilt PS1 artifact is executed and observed
  in a pinned emulator. A static match alone does not establish this.
- **Native runtime:** Linux and Windows execute the same decomp-owned C through
  reviewed platform adapters. The current smoke target proves plumbing only.
- **Human parity:** a person completes a documented comparison against retail
  behavior. Automated tests and screenshots do not substitute for this gate.

## Extraction pipeline

The pipeline is `chdman extractcd` to produce a BIN/CUE pair, then `dumpsxiso`
to read the filesystem:

```text
chdman extractcd -i <verified.chd> -o <ignored/disc.cue> -ob <ignored/disc.bin>
dumpsxiso -x <ignored/files> -s <ignored/layout.xml> <ignored/disc.cue>
```

`dumpsxiso` replaced an earlier `bchunk` + `7z` stage that silently truncated
every CD-XA Mode2/Form2 file on the disc. Any future substitution must be
measured against the sizes recorded in [MILESTONE-1.md](MILESTONE-1.md), not
assumed equivalent because it produces files without erroring.

## Archive format

Most of the game is not in `SLUS_007.26`. It streams from `.CD` archives, which
`tools/extract_cd.py` unpacks.

A `0x800` header holds a `u32` entry count, a `u32` pad, then eight bytes per
entry — a `u32` start sector and a `u32` size. Offsets are sector-scaled
(`sector * 0x800`) and relative to the archive itself; members are padded out to
sector boundaries. At most 255 entries fit in the header.

Verified against `MAIN.CD`: 49 members totalling 5,985,664 bytes of a 5,988,352
byte archive, every entry landing inside the file.

The archive carries no filenames, so members are written out by index. Inventing
names would be fabrication, and the index is how the game addresses them.

Parsing fails closed. A misparsed archive does not error, it yields
plausible-looking garbage, so the declared count is checked against the header's
real capacity and every entry against the real file size.

Archives nest. `SC01.CD` is a `.CD` archive whose members are themselves PAC
containers, and `tools/extract_pac.py` walks those: 199 chunks across 78 of its
86 members, covering 100% of the PAC region with no gaps or overlaps. See
[ASSETS.md](ASSETS.md) for the chunk header and for the external claim it
corrects.

## Milestone 2 precondition

Milestone 2 is compiler identification. It must not conclude from a plausible
version string or a build that merely succeeds. It requires disassembly or
linker fingerprint evidence drawn from the pinned executable, per
[COMPILER-ID.md](COMPILER-ID.md), and the first byte comparison of a rebuilt
function against retail.
