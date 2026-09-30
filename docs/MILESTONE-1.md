# Milestone 1 evidence

Status: **retail extracted; boot identity observed; first disassembly produced**.

Every value below was read from the contributor's own verified disc. Nothing in
this document is inferred from filenames, box text, or online references.

## Tooling pinned

| Tool | Version / pin | Source |
| --- | --- | --- |
| chdman | MAME 0.289 | Homebrew `rom-tools` (userspace, no sudo) |
| dumpsxiso | mkpsxiso @ `a6b11ea86e67c189137ac50a4066044b3fdb0525` | built under `tools/third_party/` (gitignored) |
| splat | 0.41.1 (spimdisasm 1.42.2) | pre-existing user install |

## Media identity

Verified before any extraction ran:

- size `210618433`
- SHA-256 `511e4bcae71bb09b5b7e4818aa303a6fa06c189a29dfdef55224a68d4e4150cc`

Disc structure is one data track plus three Red Book audio tracks:

```text
TRACK 01 MODE2/2352
TRACK 02 AUDIO   TRACK 03 AUDIO   TRACK 04 AUDIO
```

ISO9660 volume metadata: volume `MUSASHI`, publisher
`Square Electronic Arts L.L.C.`, copyright `SQUARE`, created `1998-09-22`.
28 files were extracted.

## Extractor correction (the substantive finding)

The Milestone 0 pipeline was `chdman` → `bchunk` → `7z`. That chain is **lossy on
this disc** and fails in the dangerous direction: quietly.

`bchunk` renders the Mode2 track as a 2048-byte-per-sector Form1 ISO. Brave
Fencer Musashi stores its MDEC video and XA audio in **Mode2/Form2** sectors,
which carry 2324 user bytes. Everything Form2 is therefore truncated to
2048/2324 of its true length. Measured ratio across all `.STR` files: `0.8767`.

`7z` reported hard errors on only three files (`DUMMY_DA.DA`, `ST01_13A.DA`,
`ST01_13B.DA`, each landing as 0 bytes). It reported **no error at all** for the
`.STR` files it silently shortened:

| File | via bchunk+7z | via dumpsxiso | verdict |
| --- | --- | --- | --- |
| `SLUS_007.26` | 413,696 | 413,696 | identical |
| `SYSTEM.CNF` | 68 | 68 | identical |
| `MAIN.CD`, `SC01..07.CD`, `LIST.CD` | equal | equal | identical |
| `ST01.STR` | 12,795,904 | 14,595,328 | truncated |
| `OPEN.STR` | 22,724,608 | 25,920,256 | truncated |
| `END.STR` | 5,341,184 | 6,092,288 | truncated |
| `ST01_13A.DA` | 0 | 10,325,324 (WAV) | lost entirely |

The pipeline now uses `dumpsxiso`, which decodes both sector forms and also
emits the MKPSXISO layout XML a rebuilt disc will be assembled from. `bchunk`
and `7z` are no longer dependencies.

**The executable was never affected.** `SLUS_007.26` is Form1 and hashed
identically under both pipelines, so the boot identity below is independent of
this correction.

## Boot identity

`SYSTEM.CNF` (68 bytes, SHA-256 `cf7feab3…c4c28557`):

```text
BOOT = cdrom:\SLUS_007.26;1
TCB = 4
EVENT = 16
STACK = 801ffff0
```

Boot executable `SLUS_007.26`:

- size `413696` (`0x65000`)
- SHA-256 `66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a`
- SHA-1 `143dbb89f34491258bbc27810d0a12ec8b43a8dd`
- header SHA-256 `dd59bf27b7c9ca361501ebcfa77f307b7016b98ccf4bcb861d6d5ab305df5f9e`

PS-X EXE header:

| Field | Value |
| --- | --- |
| `pc0` | `0x80010000` |
| `t_addr` | `0x80010000` |
| `t_size` | `0x00064800` (411648) |
| `d_addr` / `d_size` | `0` / `0` |
| `b_addr` / `b_size` | `0` / `0` |
| `sp_addr` | `0x801FFFF0` |
| region | the standard North America region marker string (not reproduced here) |

Consistency check: `0x800` header + `0x64800` text `== 0x65000 ==` observed file
size, so the image is one flat text segment with no separate data or bss
segment of its own. `sp_addr` agrees with `SYSTEM.CNF`'s `STACK`.

## First disassembly

`config/slus_007.26.yaml` splits the executable with splat:

- **411 KB, 99.50%** of the image placed in a defined segment
- **1334** function labels in `asm/main.s`
- splat flagged trailing-nop padding at vram `0x80052430`, suggesting original
  translation-unit seams at file offsets `0x42C60` and `0x5E4F0`

Generated disassembly is a derivative of retail bytes and is gitignored; each
contributor regenerates it from their own copy.

Known and expected at this stage: `config/undefined_funcs.auto.txt` contains
entries up to `0x8FF02A54`, far outside PS1 RAM. These are false positives from
data regions being scanned as code on a first pass with no symbol information.
They are not yet an error to fix.

## Not established

- **Compiler identity remains `unknown`.** No disassembly or linker fingerprint
  evidence has been gathered, and `docs/COMPILER-ID.md` forbids naming a
  toolchain without it.
- **Nothing is matched.** A split is not a match; no byte comparison of rebuilt
  output against retail has been attempted.
- **No emulator or human parity run** has taken place.

## Reproduction

```sh
python3 tools/register_retail.py
python3 tools/extract_retail.py
python3 tools/identify_retail.py
python3 -m splat split config/slus_007.26.yaml
./tools/run_tests.sh
```

## Next gate

Identify the code-generation pipeline against the observed bytes, then rebuild
one bounded function and compare it byte-for-byte with retail. Until a
comparison report exists, no matching percentage may be claimed.
