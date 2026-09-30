# Vendor → house-style cleanup

Druthulu’s matched sources are the intake oracle. They are **not** the style we
keep under `src/main/` for the live PC port.

## House style (authoritative)

Samples: `src/main/80012ab0.c`, `src/main/80012e6c.c`, `src/main/80013154.c`.

- **One function** (or a tiny related leaf group) per file under `src/main/`.
- **Filename** = lowercase VRAM address: `80012ab0.c`.
- `#include "psx_types.h"` — do not pull `stdint` via `src/shared/types.h` in
  matching TUs.
- **File header**: provisional address-named file; rename when a real TU
  boundary is known; only verified matches belong in matched files.
- **Per-function comment**: plain-English behavior, VRAM address, verified word
  count when known, caveat that types/signatures are the loosest that reproduce
  bytes.
- Prefer meaningful locals (`delta`, `plain`, `scaled`, `step`, `current`) when
  they still match; keep split variables when collapsing breaks the match.
- `arg0`/`arg1` params are fine when the original names are unknown.
- Staging / hand models: padding structs, no guessed layout, honest
  “NOT verified” banners.

## What Druthulu looks like (contrast)

- Giant TUs (`src/800.c` ~22k lines) mixing many functions.
- MIPS-only pins such as `register s32 zr __asm__("$0")`.
- `goto` for cross-jump shapes; Phase-style shared headers; sparse comments of
  a different flavor.

## Cleanup rules for this repo

1. **Raw vendor tree stays** under `vendor/bfm-decomp/` as reference.
2. **Never replace** `src/main/` wholesale with vendor dumps.
3. When promoting a vendor function into the live tree, **split** it into an
   address-named file and rewrite comments/locals to house style.
4. If a local file is already byte-matched and cleaner than vendor’s form,
   **local wins** (example: `func_80012E6C` keeps the house-style body; do not
   reintroduce `__asm__("$0")` for the PC port).
5. Host / PC-port builds compile cleaned C with modern gcc/clang. They do **not**
   run the gcc-2.7.2-psx matching build inside `pc_port`.
6. Full-tree cleanup is **incremental**. First milestone: smoke symbols
   (`80012AB0`, `80012E6C`, `8001311C`, `80013154`) and the early-main neighbors
   already present as address-named files.

## Next intake slices

- Widen `musashi_shared` / probes to more cleaned early-main leaves already
  under `src/main/80012*.c` / `80013*.c` once smoke stays green.
- For brand-new vendor-only matches: extract → clean → add one file → wire
  CMake → rebuild smoke/native consumers.
- Overlay / `md_*` / `resident` intake follows the same split-and-clean rule;
  do not compile entire `ov_*` directories into the port in one shot.

## 2026-09-18 intake result

278 functions promoted in one pass (23 `main`, 250 `main_0012`, all 5 remaining
`main_0007`), worth 64,500 bytes once two more `main` leaves landed: portable
C-only coverage went 180,900 -> 240,824 / 806,272 (21.87% -> 29.87%) and
`main_0007` is complete. The working algorithm and its three traps are recorded
in `tasks/plan-2026-09-18-vendor-promotion.md`:

- The preamble must be the vendor TU's `extern`/`#define` lines only. Adding
  the TU's function prototypes changes codegen and loses matches.
- Which vendor file defines a function is not fixed: `main` matches from the
  big TUs (`boot.c`, `800.c`), while `main_0012` matches from the exact-stem
  shared body. A candidate may need every mentioning file tried before it
  scores, so a fixed first-match index under-reports.
- Keep the existing `MUSASHI_NATIVE_MIPS_WORD_EXPORT` block verbatim; several
  files retain a member span much larger than the function they start with
  (up to 2,259 words), so regenerating it from the function's own
  `glabel`/`endlabel` truncates the native carve.

`tools/verify_registry.py` gates every batch (3,010 / 3,010 after this intake).

