# Excluded from this public snapshot

This snapshot was assembled from the project's private working tree:
the decompilation/tooling branch `main` at `f59d74798` and the native port
branch `port/native-lane` at `fe4a59820` (merge base `f0299da37`), both taken
with `git archive` (committed content only). The two
branches changed disjoint files since their merge base, so the snapshot takes
the port branch's tree and applies `main`'s later changes to its tools,
tests, docs, config and registry-facing files on top (a conflict-free union).
Only tracked files were considered; nothing untracked or ignored in either
working tree was a candidate.

Everything below was left out. When in doubt, a file was excluded.

## 1. Decompiled game code derived from the upstream decompilation (AGPL-3.0)

| Path | Files | Reason |
|---|---:|---|
| `src/` (registry origin `vendor-derived (Druthulu/BFM-decomp)` or `vendor-referenced`) | 269,279 | Derived from [Druthulu/BFM-decomp](https://github.com/Druthulu/BFM-decomp), whose project files are AGPL-3.0 and whose game sources carry no license. This repository is MIT, so none of it is redistributed. Credited in CREDITS.md. |
| `src/` with no origin recorded in the registry | 2,732 | House-style address-named files whose headers say they were "cross-checked against vendor/bfm-decomp". Their independence from the upstream code cannot be established from the record, so they are excluded as unclear origin. They are also reimplementations of Square's game code. |
| `src/` files not in the registry (drafts, shared runtime shims) | 136 | Unregistered, origin unreviewed. |

The 264 owner-authored functions listed in `config/lane_own_code_allow.txt`
(the only C that a release build compiles natively) also live under `src/`.
This snapshot leaves out all of `src/`, so their sources are not included
yet. The allow-list itself holds only addresses and is included.
| `include/labels.inc` | 1 | A symlink into the upstream tree (`vendor/bfm-decomp/include/labels.inc`). |
| `include/macro.inc`, `include/gte_macros.inc` | 2 | Byte-identical to the upstream decompilation's `include/` files. |
| `vendor/bfm-decomp/` | (untracked) | The upstream tree itself; never tracked, never considered. |
| `VENDOR.md` | 1 | Instructions for the local upstream intake; superseded by CREDITS.md and README.md. |

## 2. Sony / Psy-Q material

| Path | Files | Reason |
|---|---:|---|
| `src/` files whose origin is a Sony library (`psyq-lib from parasite-eve-decomp`, `psyq-lib from xenogears-decomp-…`, `hand-decompiled (Sony libgs/libgte/libgpu/libspu/libsnd/libpad/libmcrd/libcd …)`) | 109 | Decompiled Sony Psy-Q SDK code. |
| `src/` files registered as `licensed retail handwritten library; mnemonic assembly` | 42 | Sony libgte/libgs handwritten assembly transcribed from the retail executable. |
| `src/` own-C reconstructions inside Psy-Q library ranges (`Codex own-C reconstruction`, `own C from retail instructions …`) | 4 | Reimplementations of Sony library functions (libmcrd) from retail instructions; SDK-derived. |
| `tools/psyq/`, SDK headers, `psyq/` include trees | (untracked) | Compiler and SDK material, fetched locally, never tracked. |

`config/psyq_ranges.txt` (addresses and segment names of the Sony library
code, no code) and `config/psyq_pending_replacement.txt` (a list of source
paths) are included.

## 3. Retail game content and BIOS

| Path | Files | Reason |
|---|---:|---|
| `asm/` (`README.md` and `.gitkeep` placeholders; the splat output `asm/main.s`, `asm/functions`, `asm/data` is ignored) | 4 | The disassembly directory of the retail executable; left out as a whole. |
| `include/retail_scratch_call.h` | 1 | A transcription of a retail instruction sequence (the startup scratch-stack switch). |
| `tests/test_bios_heap.py` | 1 | Contains 40 bytes of BIOS (SCPH-5500) code as a hex string. |
| `tests/spu_reverb_probe.c`, `tests/spu_reverb_source_probe.c` | 2 | Contain the 32-halfword SPU reverb preset table found verbatim in the retail executable (64-byte run; see SCANS.md, found by the snapshot scan). |
| `tests/test_spu_reverb.py`, `tests/test_spu_reverb_source.py` | 2 | Drivers for the two probes above; they cannot run without them. |
| `extracted/`, disc images, `disc/`, `artifacts/`, RAM dumps, `*.png`/`*.ppm` captures, `*.log` runs | (untracked) | Game content from the owner's disc and runtime captures; ignored, never considered. |

One text scrub removed a retail string rather than a whole file: the PS-X
EXE region marker string (55 bytes) quoted in `docs/MILESTONE-1.md` was
replaced by a description.

## 4. Provenance records

| Path | Files | Reason |
|---|---:|---|
| `provenance/matches.json` | 1 | 83 MB, over GitHub's 50 MB warning size. It holds no retail bytes (names, addresses, sizes, source paths, origins and target SHA-256s). But 99% of its 272,166 entries describe the upstream-derived and Sony sources excluded above, and its origin strings name private local archives. `tools/native_lane_gen.py`, `tools/progress.py`, `tools/verify_registry.py` and `tools/register_match.py` need it and report it missing. |
| `provenance/exe_identity.json`, `provenance/compiler_identity.json`, `provenance/compiler_evidence.json` | 3 | Kept out to keep `provenance/` minimal. They hold no retail bytes. `tools/identify_retail.py` and `tools/identify_compiler.py` regenerate the first and last from your own disc. |

Included: `provenance/manifest.json` only, because `tools/register_retail.py`,
`tools/extract_retail.py` and `tools/identify_retail.py` need it to exist. It
holds the expected CHD size and SHA-256 and repo-relative paths. The local CHD
path and the `chdman` path were replaced by placeholders.

## 5. Third-party binaries and trees

| Path | Reason |
|---|---|
| `tools/third_party/psycross/`, `tools/third_party/mkpsxiso/`, `tools/third_party/lua/`, `tools/maspsx/`, `tools/m2c/`, `tools/psyq/` (old-gcc) | Untracked fetched checkouts and binaries. Fetch instructions are in README.md (`tools/fetch_toolchains.sh`, `tools/fetch_lua.sh`). |
| `include/m2c_macros.h` | 1 file: derived from m2c's `m2c_macros.h` (GPL-3.0); fetch m2c instead. |

No prebuilt binary (`.so`, `.a`, `.o`, `.exe`, AppImage, `.bin`) is in the
snapshot.

## 6. Personal infrastructure and agent notes

| Path | Files | Reason |
|---|---:|---|
| `tasks/` (`plan*.md`, `todo.md`) | 6 | Agent task plans and to-do lists. |
| `docs/HANDOFF-codex.md` | 1 | Agent handoff naming local worktrees and scratch directories. |
| `docs/MILESTONE-0.md` | 1 | Agent acceptance report naming the local repository path and operator review. |
| `docs/OPENING-CONTROL-2026-09-29.md` | 1 | Dated agent run report pointing at local scratch-archive logs. Its result (opening reached, controls not unlocked) is summarised in README.md. |
| `docs/ASSEMBLY-2026-09-29.md` | 1 | Dated agent accounting report on the registry of excluded sources. |
| `README.md`, `.gitignore` (originals) | 2 | Replaced: the old README described the pre-match state and a history rewrite; the old `.gitignore` named local agent files. |
| Untracked local files (`ORIGINAL_REQUEST.md`, `codex-session-*.md`, `.agents/`, `.claude/`, `staging/`, logs) | (untracked) | Local agent state; never considered. |

Scrubbed in place (content kept, local specifics replaced):

- absolute local paths in `docs/RETAIL-RUNTIME-BASELINE.md` and
  `docs/MENU-BOOT-CONTINUATION.md` became `$PCSX_REDUX_APPRUN`, `$BIOS_DIR/`
  and `$REPO`;
- the local path to a sibling project in `docs/ARCHITECTURE-PORT.md`;
- the owner's local launcher-library disc path in `tools/package_legion.sh`
  (the release packager, which writes `launch.sh`);
- hard-coded local paths in `tests/conftest.py`, `tests/test_bfm_plat.py`,
  `tests/test_bios_exception.py`, `tests/test_gte_init_source_words.py` and
  `tools/register_retail.py` became the environment variables
  `BFM_HOST_PORT_DEPS`, `BFM_DECOMP_ROOT`, `BFM_BIOS`, `BFM_TEST_TMPDIR` and
  `BFM_CHD` (or the repository root).

## What the snapshot cannot do without these

The port's CMake targets and many tests compile files under `src/` and use
`include/*.inc`; they are not buildable from this repository alone (see
README.md, "Building"). Tests that need the decompiled sources, your disc
files, a BIOS or PsyCross skip or fail when those are absent.
