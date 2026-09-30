> **PROJECT GOAL:** a native PC port in the mould of Ship of Harkinian /
> the Silent Hill decomp ports, ultimately re-rendered (HD-2D).
> **Read `docs/PORT_GOAL_AND_PLAN.md` before planning any work.**
> The port is the deliverable; the decompilation is how you get there.
> Recovered *assembly* is worth nothing to the port -- you cannot run PS1 MIPS
> on x86. Report C-only executed-path coverage, not just total coverage.

# Definition of Done — byte-for-byte retail decompilation + native PC port

Status date: 2026-09-11. Owner: this repository. This file is the authority for
what "complete" means. It exists because "60%" and "port to PC" are otherwise
ambiguous, and the ambiguity is worth roughly 835 functions of work.

## The two deliverables

1. **Decompilation.** Every function of the North American retail release is
   recovered as source that the pinned toolchain rebuilds byte-for-byte
   identical to retail.
2. **Native port.** A Linux/Windows build runs that same recovered game code —
   not a reimplementation, emulator, asset viewer, or stubbed success — far
   enough to show and drive the real retail menu.

The decompilation is the authority. The port consumes it.

## How progress is measured

The single honest meter is `python3 tools/progress.py`:

> **qualifying unique coverage** = the overlap-union of *complete* functions
> whose `extent` is `function` and whose reviewed `recovery` is `c` or
> `assembly`, counted once per region, over the identified split-code
> denominator.

The denominator is fixed at **806,272 bytes**: `SLUS_007.26` `.text` 411,648 +
`MAIN.CD` member 0007 9,600 + member 0012 code chunk `[0x28000,0x86000)`
385,024. It is *code-segment* bytes, not function bytes, so it includes
data-in-text, jump/pointer tables, string/const pools, and alignment padding.

Two rules that make the meter honest:

- **Function count is not the meter.** Counting matched functions reads
  `2,411 / 3,963 = 60.8%` today, but that counts a 12-byte stub the same as a
  4,716-byte routine. The docs and `tools/progress.py` already reject this.
- **The meter is a claim, not proof.** Only `tools/verify_registry.py`
  re-earns the byte matches by rebuilding every entry and comparing through
  `tools/match_function.py`.

## Full Definition of Done

Decompilation is complete when:

- [ ] Every splat-enumerated function in the three regions is recovered as
      complete `c` or (only where ordinary C cannot emit the instruction, e.g.
      GTE/cop2, or the function is genuinely handwritten) `assembly`.
- [ ] `python3 tools/progress.py` reports qualifying unique coverage equal to
      the reachable ceiling: **697,632 / 806,272 bytes = 86.53%**, from all
      3,962 functions.
- [ ] The residual **108,640 bytes** are documented, audited non-function
      bytes (BIOS A0-call stub chains, jump tables, globals, const data, zero
      padding, the 0007 data tail, and the 0012 tail hole
      `[8017E354,80186158)`). The denominator is never redefined to reach a
      number.
- [ ] `python3 tools/verify_registry.py` exits 0 — every entry re-verified.
- [ ] `python3 tools/run_tests.sh` (or the CTest + pytest suites) passes.

The port is complete when:

- [ ] The native build executes decomp-owned game code from the real entry
      point `80010000` through retail display setup, the genuine
      loader/dispatcher, and the **real retail menu**.
- [ ] The menu is **input-responsive**.
- [ ] The same executable launches from a desktop/start-menu entry and is
      re-verified there.
- [ ] No emulator launch, asset viewer, replacement menu, injected menu state,
      or success-returning stub satisfies any of the above.

## The 60% checkpoint (the agreed interim target)

The 60% target means: **byte coverage ≥ 60.00%**, decompilation-led, port
following. Concretely, all of the following must hold:

- [ ] `python3 tools/progress.py` reports **≥ 483,764 / 806,272 bytes
      (≥ 60.00%)** qualifying unique coverage.
- [ ] `python3 tools/verify_registry.py` exits 0.
- [ ] The full test suite passes.
- [ ] No registered range was produced by redefining the denominator, by
      bulk-generating assembly from the disassembly, or by registering pure
      `.word` dumps. `classify_recovery` must still exclude dumps.

Reaching 60% does **not** require the native port; it is a decompilation
milestone. The port resumes once decompilation is far enough along.

## Honest current status (2026-09-18)

| Measure | Value |
| --- | --- |
| Qualifying coverage | **322,368 / 806,272 = 39.98%** |
| Portable C-only subset | **240,824 / 806,272 = 29.87%** |
| 30% threshold (241,882) | **met** |
| To the 60% checkpoint (483,764) | **+161,396 bytes** |
| Reachable ceiling | 697,632 / 806,272 = 86.53% |
| Unmatched to the ceiling | ~375,264 bytes |
| Registry | 3,010 / 3,010 re-verified through the oracle |
| Native menu | **NOT_REACHED** (display disabled; port stops at `80053CF8`) |

The C-only subset is now reported because it, not the total, predicts a
playable native build. The 2026-09-18 batch moved 152 bytes of that gap
back: `func_80043300`, the startup retry dispatcher, is no longer an
instruction `__asm__` overlay but source C that rebuilds 38/38 words, and
the boot-path leaf `func_80046564` was recovered as C (6/6) as well.

The same day's second batch is much larger: the vendored Druthulu/BFM-decomp
tree is a matching decompilation of the same disc, and
`docs/VENDOR-CLEANUP.md` already authorized intake. Sweeping it against the
oracle and promoting every exact body added 280 functions / 64,500 bytes,
which is what moved portable C-only coverage from 21.87% to 29.87% and
completed `main_0007`. See `tasks/plan-2026-09-18-vendor-promotion.md` for the
pipeline and the two traps (prototypes must not join the preamble; the
defining file varies per function).

Two corrections to the 2026-09-11 status below, both load-bearing:

- The 32-bit Psy-Q toolchain is **not** blocked on this host. It fails only
  inside the command sandbox; the same `cc1` runs normally outside it, and
  the registry re-verifies from there. See `docs/MATCHING.md`.
- The claim that a full sweep yields nothing still holds for m2c's *default*
  output, and was re-confirmed on a working toolchain: zero new matches
  across all 1,526 unregistered sources. But 1,220 of those sources did not
  compile at all, for one fixable reason, so the oracle had never actually
  judged them. Running m2c under `--valid-syntax` and re-carving only the
  failures produced **60 matches** from the same sweep that had produced
  none. The remaining work is still manual; the automated floor was just
  lower than it needed to be.

Where the remaining bytes live (largest buckets of unmatched functions):
513–1024 B: 170 fns / 118,440 B; >1024 B: 64 fns / 110,620 B; 257–512 B:
323 fns / 113,852 B; 129–256 B: 436 fns / 81,884 B; smaller: ~315 fns / ~7.5 KB.

To reach 60% requires recovering **~54% of all remaining function bytes** —
roughly 835 average functions, or about 73% of the 1,024–512 B and larger
tiers. There is no automated shortcut: a full re-sweep of every unregistered
source with the current sanitizer and all optimization candidates yielded
**0 new matches** beyond 14 latent ones, and a 30-function sample of fresh
sanitized `m2c` output yielded **0 matches**. The toolchain (GCC 2.7.2 /
PSY-Q 4.0 / ASPSX 2.56) rarely reproduces the original C from `m2c`, so each
function is manual work against the oracle.

## Non-goals and integrity rules

- Do not move the denominator, count function stubs, or count
  `unclassified`/`partial`/`mixed` rows or non-function extents.
- Do not mass-generate assembly or `.word` dumps to inflate coverage. Assembly
  recovery is reserved for compiler-inexpressible code and is reviewed.
- Do not claim the port from a build, a static audit, or a stub. The oracle
  says match; the game says the port works.
- Retail-derived bytes, disassembly, extracted assets, and models stay
  untracked.

## Verification commands

```sh
python3 tools/progress.py                 # the meter
python3 tools/verify_registry.py          # re-earn every byte match (exit 0)
python3 -m pytest tests -q                # Python suites
./tools/run_tests.sh                      # C/CTest suites
```
