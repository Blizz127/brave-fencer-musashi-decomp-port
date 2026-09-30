# Matching log and method

## Status

The compiler is **resolved: GCC 2.7.2 / PSY-Q 4.0 / ASPSX 2.56**.

25 functions are byte-verified against retail — but only 3 are substantive;
the other 22 are two-instruction stubs. See "Counting honestly" below.
The 6→3 elimination is **replicated** across two independent functions, one of
them a verified-correct source. The compiler is still
**unresolved**, but no longer unconstrained: half the candidate set has been
eliminated by a complex function — see "First elimination" below. That result
is **unreplicated**; a second function was attempted and came out inconclusive.

| vram | words | source | verdict |
| --- | --- | --- | --- |
| `0x80012AB0` | 3/3 | `src/main/80012ab0.c` | MATCH |
| `0x80014128` | 8/8 | `src/main/80014128.c` | MATCH (first with a `jal`) |
| `0x800CF3B0` | 22/22 | `src/overlays/main_0007/800cf3b0.c` | MATCH (first in an overlay) |
| `0x8012BF4C` | 2/2 | `src/overlays/main_0012/8012bf4c.c` | MATCH (first in member 0012) |

21 further stubs in member 0012 are recorded in `provenance/matches.json`
rather than listed here.

## The compiler is resolved: gcc-2.7.2-psx

`func_800CF3B0` settles it. Its C is verified correct — it matches retail
byte-for-byte — so a candidate that cannot reproduce it is eliminated outright,
with no dependence on the decompilation being right.

Of six candidates bracketing PSY-Q 3.5 through 4.5, exactly one reproduces it:

| toolchain | PSY-Q | `func_800CF3B0` |
| --- | --- | --- |
| **`gcc-2.7.2-psx`** | **4.0** | **22/22** |
| `gcc-2.7.2-cdk-psx` | 4.1 | 19/22 |
| `gcc-2.6.0-psx` | 3.5 | 19/22 |
| `gcc-2.8.0-psx` | 4.3 | 21 words, wrong length |
| `gcc-2.8.1-psx` | 4.4 | 21 words, wrong length |
| `gcc-2.91.66-psx` | 4.5 | 21 words, wrong length |

The margin is three of twenty-two words against the closest rivals, not noise.

**Conclusion: GCC 2.7.2 / PSY-Q 4.0 / ASPSX 2.56**, with `-O2 -G0` and maspsx
division expansion. Recorded in `provenance/compiler_identity.json`.

This is consistent with everything measured earlier rather than replacing it.
`func_80014128` left 2.6.0, 2.7.2 and 2.7.2-cdk standing; `func_80012E6C`
eliminated 4.3, 4.4 and 4.5. `gcc-2.7.2-psx` is the only candidate surviving
every verified-correct source, and the intersection is a single toolchain.

It also confirms the 1998 Square cluster around GCC 2.7.2 that the research
suggested — but derived from these bytes rather than assumed from it. The
candidate set deliberately included later releases so a 2.7.2 answer could have
been falsified; they failed on their own.

**Scope.** One decisive function plus two corroborating results. Worth
re-checking as further substantive matches land, which `discriminate.py` now
does on overlay code as well as the executable.

## Counting honestly

`tools/progress.py` reports functions rebuilt from C and verified equal to
retail. It never reports a diff percentage: a project counting `objdiff` match
rate can show 100% while the "C" is verbatim `__asm__` transcription, and one
PSX project publicly retracted exactly such a figure as "true but meaningless".

That guard is not sufficient on its own. Reaching 25 matches took 22
two-instruction stubs — empty functions, single stores, plain returns. They are
genuine matches, and they are not meaningful progress. The tool therefore splits
the total, so the headline number cannot flatter:

```text
MATCHED 25 functions, 308 bytes
  of which trivial (<= 8 bytes): 22    substantive: 3
```

308 bytes against roughly 800KB of code is the honest picture.

## Regenerating the disassembly

`asm/` is retail-derived and untracked, and it has been found empty once
already. It comes back with:

```sh
python3 -m splat split config/slus_007.26.yaml
python3 -m splat split config/overlay_main_0007.yaml
python3 -m splat split config/overlay_main_0012.yaml
python3 tools/verify_registry.py
```

The last line rebuilds every registry entry and puts it through the oracle
again. The registry is a claim, not a record, and after anything touches the
tree it is re-earned rather than remembered.

## A destructive automation bug, caught only by re-verification

Batch-matching wrote each candidate's source, then deleted it when the build or
comparison failed. `func_8012BF4C` already had a hand-written, committed,
verified source; the batch overwrote it with m2c output, failed, and deleted the
file — destroying work already in git.

Nothing in the matching flow noticed. It surfaced only because every registry
entry is re-verified through the oracle after a batch, which is why that step
exists. Any future batch must refuse to overwrite an existing source rather than
assume it owns the path.

## Linking: why bytes from the object are not comparable

Until now only relocation-free leaf functions could be matched at all. A
function that calls another, or reads a global, compiles to an object whose
bytes still contain unresolved relocations, so comparing them against retail —
which is fully linked — is meaningless. That excluded the large majority of the
1334 functions.

`build_candidate.py` now links before extracting. The generated script defines
external symbols and places `.text`, then the function's final bytes are cut
from the linked image.

Symbol addresses come from two sources:

1. splat's `config/undefined_syms.auto.txt` and `undefined_funcs.auto.txt`,
   which are already in `name = 0xADDR;` assignment syntax.
2. The symbol name itself, for anything still undefined. splat names
   unidentified symbols after their address, so `func_80042610` resolves to
   `0x80042610`. This is needed because a callee *inside* the split is
   undefined in a single compiled object yet absent from the auto files.

A name carrying no address is reported as an unresolved external rather than
guessed at.

Two details that cost time and are worth keeping:

- **`ld` needs `-EL`.** The toolchain's linker defaults to big-endian MIPS and
  rejects a little-endian object outright.
- **Placement only needs the right 256MB region.** Branches are PC-relative and
  `%hi`/`%lo` carry absolute targets, so neither depends on where a function
  sits. `jal` takes its top four address bits from the delay-slot PC, which is
  the only reason a base address is needed at all.

`build_candidate.py` also clears its output file before building. A failed
build that left the previous run's bytes in place caused `match_function` to
report a confident MATCH on stale data during this work — the exact false pass
the oracle exists to prevent.

Reproduce with:

After `./tools/fetch_toolchains.sh`, every path resolves from the repository:

```sh
python3 tools/build_candidate.py src/main/80012ab0.c \
  --symbol func_80012AB0 --toolchain gcc-2.7.2-psx --output /tmp/candidate.bin
python3 tools/match_function.py --vram 0x80012AB0 --size 0xC --candidate /tmp/candidate.bin
```

## First elimination: `func_80012E6C`

The approach that worked is the one the negative results pointed at: take a
real function with room for compilers to disagree, compile it against the whole
candidate set, and see who cannot produce it.

**Target.** `func_80012E6C`, vram `0x80012E6C`, `0x108` bytes (66 words). A
relocation-free leaf — no `jal`, no `%hi`/`%lo` — so its compiled `.text` is
final and needs no linking. 9 distinct registers with a mix of multiply,
divide, shift, load, compare and branch. Decompiled with m2c and cleaned into
`src/main/80012e6c.c`.

Selection note: branch count is a poor proxy for discrimination.
`func_8001923C` has 25 branches but only 4 distinct registers and no loads,
shifts or multiplies — a repetitive compare chain every compiler encodes
identically. Instruction *variety* and register count matter more.

**Result**, via `tools/discriminate.py` over `-O0..-O3` x `-G0/-G4/-G8`
(12 configurations per toolchain):

| toolchain | PSY-Q | best word count | verdict |
| --- | --- | --- | --- |
| `gcc-2.7.2-cdk-psx` | 4.1 | **66** | survives, 36.36% |
| `gcc-2.7.2-psx` | 4.0 | **66** | survives, 34.85% |
| `gcc-2.6.0-psx` | 3.5 | **66** | survives, 34.85% |
| `gcc-2.8.0-psx` | 4.3 | 67 | **eliminated** |
| `gcc-2.8.1-psx` | 4.4 | 67 | **eliminated** |
| `gcc-2.91.66-psx` | 4.5 | 63 | **eliminated** |

Retail is 66 words. Three candidates hit it exactly; three never reach it at
any tested setting. The 2.8.x pair bottom out at 67 words and 2.91.66 at 63.

**Candidate set: 6 -> 3.** The surviving set is PSY-Q 3.5, 4.0 and 4.1. This
corroborates the 1998 Square cluster around GCC 2.7.2 / PSY-Q 4.0 without
having assumed it: the registry deliberately included the later releases so a
2.7.2 answer could have been falsified, and they failed on their own.

### What this does and does not establish

**Instruction count is the load-bearing signal, not the ratio.** It is
structural: it asks only whether a compiler can reach the right shape from this
source, which survives an imperfect decompilation. The ratios (34-36%) show the
C is plainly not the original source yet.

**The three survivors are not separated.** Their spread is 1.52 points, well
inside the noise of an imperfect decompilation. Nothing here distinguishes
2.6.0 from 2.7.2 from 2.7.2-cdk.

**The elimination is conditional on the decompiled C being approximately
correct.** If the C is wrong in a way that systematically suits the older
backend, the result would be an artifact. Two things argue against that: the
count match is exact rather than close, and it holds across all 12 flag
configurations rather than at one lucky setting. It should still be re-checked
against a second complex function before being treated as settled.

Per `docs/COMPILER-ID.md` the compiler therefore remains **unresolved**. This
is recorded as candidates eliminated, not as an identification.

## Replication achieved on a verified source

The 6→3 elimination is now **replicated**, on stronger evidence than the
original.

`func_80014128` matches retail byte-for-byte, so unlike a work-in-progress
decompilation its C is *known correct*. That changes what a failure means: when
some candidate reproduces retail exactly, any candidate that cannot is
eliminated outright, with no "conditional on the decompilation" caveat.
`discriminate.py` now applies that rule and reports which criterion it used.

| toolchain | PSY-Q | `func_80014128` | verdict |
| --- | --- | --- | --- |
| `gcc-2.6.0-psx` | 3.5 | **100%** | survives |
| `gcc-2.7.2-psx` | 4.0 | **100%** | survives |
| `gcc-2.7.2-cdk-psx` | 4.1 | **100%** | survives |
| `gcc-2.8.0-psx` | 4.3 | 75% | **eliminated** |
| `gcc-2.8.1-psx` | 4.4 | 75% | **eliminated** |
| `gcc-2.91.66-psx` | 4.5 | 75% | **eliminated** |

The elimination set is **identical** to the one `func_80012E6C` produced, from
an independent function using a different criterion. Two functions now agree.

### The concrete difference: the return delay slot

The two words 2.8.x gets wrong are the epilogue, and they are the
return-delay-slot behaviour this document previously could not pin down:

| | word 5 | word 6 | word 7 |
| --- | --- | --- | --- |
| retail, and 2.6.0 / 2.7.2 / 2.7.2-cdk | `addiu $sp, $sp, 0x18` | `jr $ra` | `nop` |
| 2.8.0 / 2.8.1 / 2.91.66 | `nop` | `jr $ra` | `addiu $sp, $sp, 0x18` |

**Retail restores the stack pointer before returning and leaves the return
delay slot empty. The 2.8.x line fills that slot with the restore.** This is the
documented discriminator — 2.8.1 fills the slot after `jr $ra`, 2.7.2 does not —
finally observed in a real function where the scheduling choice is constrained,
rather than in a trivial leaf where it is not.

This also settles the caveat recorded further below. The heuristic is sound; it
simply carries no signal in a function whose delay slot is trivially fillable.

### Still not resolved

`gcc-2.6.0-psx`, `gcc-2.7.2-psx` and `gcc-2.7.2-cdk-psx` all reproduce both
verified functions **exactly**. Nothing separates PSY-Q 3.5 from 4.0 from 4.1
so far, and `func_80012AB0` is non-discriminating for the familiar reason: at
three instructions, all six candidates reproduce it.

Separating the three survivors needs a verified match on a function with enough
register pressure to force allocation choices. The high-pressure candidates are
now reachable thanks to the link step — `func_800346D0` (8 saved registers, 15
distinct) and `func_800435CC` (8 saved, 18 distinct) are the obvious next
targets, but both index arrays of structures and need type modelling before
they will compile.

Per `docs/COMPILER-ID.md` the compiler remains **unresolved**: three candidates
survive, and this is elimination rather than identification.

## Earlier replication attempt: inconclusive

The elimination above rests on a single function, so it was re-tested against a
second one. **The replication did not succeed**, and the first result therefore
stands unreplicated.

**Target choice.** `func_80013028` was rejected despite being the documented
fallback: it has the same signature as `func_80012E6C` and the same +/-1 nudge
logic, so a decompilation wrong in the same way would confirm itself.
`func_80016714` was also rejected — it is a `memset`-style zero-fill with
alignment handling, and if it is linked Psy-Q library code rather than Square's
own it was built by Sony with different flags, which would contaminate the
result.

`func_80013154` was chosen instead: vram `0x80013154`, `0xAC` (43 words),
relocation-free, and structurally independent — a different signature
(`s32 f(s16, s16, s32)`), nested conditionals, and clearly game logic rather
than a library routine.

**Result.** No candidate reached 43 words from `src/main/80013154.c`:

| toolchain | best word count |
| --- | --- |
| `gcc-2.6.0-psx` | 44 / 41 |
| `gcc-2.7.2-psx` | 45 / 40 |
| `gcc-2.7.2-cdk-psx` | 38 |
| `gcc-2.8.0-psx` | 38 |
| `gcc-2.8.1-psx` | 38 |
| `gcc-2.91.66-psx` | 38 / 32 |

This says nothing about any compiler. It says the decompilation is not yet
close enough — the C is committed as work in progress rather than as evidence.

**A tool defect this exposed.** `discriminate.py` initially reported this as
"ELIMINATED (6)", which is badly wrong: eliminating every candidate is not a
discrimination result. Elimination is only meaningful relative to a candidate
that *did* reproduce the retail shape, so with no survivor the source is at
fault. The tool now reports `INCONCLUSIVE` and says so explicitly.

**Worth noting for the retry.** The word counts here spread from 32 to 45
across candidates, far wider than on `func_80012E6C`. Once the C is accurate
this function should discriminate *more* sharply, not less.

## A match is not a compiler identification

The first matched function was compiled against every locally available
toolchain at three optimization levels and two `-G` values. **All 18
configurations produced byte-identical output:**

| toolchain | `-O1` | `-O2` | `-O3` |
| --- | --- | --- | --- |
| `gcc-2.6.0-psx` | MATCH | MATCH | MATCH |
| `gcc-2.7.2-psx` | MATCH | MATCH | MATCH |
| `gcc-2.7.2-cdk-psx` | MATCH | MATCH | MATCH |

(`-G0` and `-G8` agree throughout.)

A three-instruction arithmetic leaf has exactly one sensible encoding, so its
codegen is invariant across compiler versions and optimization levels. It
proves the build and comparison pipeline works. It is worth **zero** evidence
about which toolchain built the game.

The practical consequence: **discriminating functions must be selected
deliberately, not stumbled into.** A function only carries version evidence if
candidate toolchains actually disagree on it. Before treating any future match
as toolchain evidence, compile it against the full candidate set and confirm
the alternatives *fail*. A match that every candidate reproduces is a pipeline
test, not a fingerprint.

Properties that make a function likely to discriminate:

- enough live values to force register allocation choices
- a non-trivial return path, so return-delay-slot behaviour is exercised
- division or modulo, whose expansion varies by ASPSX version
- `-G`-sensitive access to small globals, which changes `$gp` addressing
- floating point, which becomes libgcc calls under `-msoft-float`

## Division-idiom probe: one flag established, version still open

`tools/idiom_probe.py` reads the division expansion retail uses, emits the same
construct with each candidate toolchain, and compares mnemonic shape with
operands ignored.

Retail is unambiguous. All **69** division sites use the same expansion:

```text
div|divu  ->  bnez  nop  break        (100% of sites)
```

That is the divide-by-zero guard. Probing the candidates isolates one build
setting cleanly:

| maspsx division expansion | candidate expansion | verdict |
| --- | --- | --- |
| off (`idiom_probe --no-expand-div`) | `div + mflo jr nop` | differs from retail |
| on (default) | `div + bnez nop break` | reproduces retail |

**Established: the build requires maspsx division expansion.** Without it no
candidate emits the guard at all, so this is a configuration fact read off
retail bytes rather than a preference. `build_candidate.maspsx_command`
therefore passes `--expand-div` by default, and `idiom_probe` keeps
`--no-expand-div` only to reproduce the contrast above.

**Not established: the compiler version.** The full candidate set is now
installed locally by `tools/fetch_toolchains.sh`, spanning PSY-Q 3.5 to 4.5.
Every one of them reproduces the expansion identically:

| toolchain | PSY-Q | ASPSX | division guard | `a / 10` |
| --- | --- | --- | --- | --- |
| `gcc-2.6.0-psx` | 3.5 | 2.34 | reproduces | identical |
| `gcc-2.7.2-psx` | 4.0 | 2.56 | reproduces | identical |
| `gcc-2.7.2-cdk-psx` | 4.1 | 2.67 | reproduces | identical |
| `gcc-2.8.0-psx` | 4.3 | 2.77 | reproduces | identical |
| `gcc-2.8.1-psx` | 4.4 | 2.79 | reproduces | identical |
| `gcc-2.91.66-psx` | 4.5 | 2.81 | reproduces | identical |

Both idioms are **definitively non-discriminating** across the entire PSY-Q
range. This closes the line of investigation rather than leaving it open.

## Why synthetic idiom probes fail here

The two results have different causes and together they generalise:

- The divide-by-zero guard is an **assembler** macro. ASPSX expands it, so it
  cannot separate compiler versions no matter which one emitted the `div`. The
  probe was testing the wrong layer.
- Constant division *is* a compiler transform, but magic-number strength
  reduction for `a / 10` produces the same sequence from 2.6.0 through
  2.91.66. These releases share a backend for straightforward code.

The generalisation: **simple single-expression probes cannot discriminate these
compilers.** Anything with one obvious encoding will be emitted identically by
all of them, which is the same reason the first matched function proved
nothing.

Discrimination requires constructs where the compilers have room to disagree —
register allocation under pressure with many simultaneously live values,
instruction scheduling through non-trivial control flow, and delay-slot filling
where the choice is genuinely constrained. Those are properties of *real*
functions, not of synthetic one-liners.

**Revised approach: stop building synthetic probes.** Decompile a genuinely
complex function from the executable and compile it against all six
candidates. The toolchains that fail to reproduce it are eliminated. This is
what established PSX projects actually rely on, and the harness needed for it
already exists.

## Caveat on the return-delay-slot heuristic

Commit `915c6fe` recorded a 17.81% return-slot fill rate and cited the rule
that GCC 2.8.1 fills the slot after `jr $ra` while 2.7.2 does not. A trivial
leaf compiled here with `gcc-2.7.2-psx` **did** fill its return slot, and the
matched `0x80012AB0` has its `andi` scheduled into the delay slot in retail
too.

So the rule is not a flat property of the compiler. When the slot is trivially
fillable, 2.7.2 fills it. The heuristic only carries signal where scheduling is
genuinely constrained, which means the 17.81% aggregate cannot be read as
"mostly 2.7.2" without segregating functions by scheduling difficulty first.

**Resolved.** `func_80014128` supplies exactly that constrained case, and the
rule holds there: retail leaves the slot empty and restores `$sp` before
returning, while the 2.8.x line fills the slot with the restore. The heuristic
is sound; the aggregate was simply the wrong way to apply it. See "Replication
achieved on a verified source" above.

## Operational note

`argparse` rejects an option value that looks like a flag, so the optimization
and `-G` arguments must use the `=` form:

```sh
--optimization=-O2 --gp=-G0      # correct
--optimization -O2               # error: expected one argument
```


## D4: not every function is -O2

`func_80010A98`, on the boot chain just after crt0's constructor-table loop,
registers two callbacks. m2c's decompilation needed real types before it would
compile at all -- its placeholders for the callback parameters left `?` where
a function pointer belonged -- but even cleaned up, the natural C did not
match at -O2: GCC drops the frame pointer this function never uses, producing
56 bytes against retail's 72.

It matches byte-exactly at -O0. `provenance/compiler_identity.json` identifies
-O2 for the executable, established from two functions, neither of which is
this one. That identification was never claimed to hold per-function, and this
is the first direct evidence it does not: short, unconditional init code is
exactly what a build might reasonably compile at a lower level than the hot
path. `provenance/matches.json` entries now carry an optional `optimization`
field for exactly this case; `tools/verify_registry.py` passes it through
when present and otherwise assumes the identified default.

Two automation gaps surfaced while landing this one function, both fixed
alongside it:

* argparse reads a bare `-O0` value as another flag, because it starts with
  `-`. The fix is the `--optimization=-O0` single-token form.
* That argparse failure calls `sys.exit()` rather than returning, and
  `tools/verify_registry.py` was running every build inside a stdout/stderr
  capture that did not catch `SystemExit` -- one malformed entry silently
  killed the entire sweep with no output at all, not even a stack trace.
  `_quiet` now catches it and reports a normal failure for that one entry.

## Boot-chain functions set aside

### Startup function boundary evidence

The generated `asm/main.s` labels split the startup routine into ranges that
cannot be independently treated as ordinary C functions. At `0x80010178`,
the prologue allocates a `0x38`-byte frame and saves `$ra`, `$fp`, `$s1`, and
`$s0`. It initializes `$s0` and `$s1`, then jumps from `0x80010204` to
`0x80010214` without a call or frame teardown. That block uses the existing
saved-register state. Its loop jumps from `0x80010930` to `0x80010234`.
The block at `0x80010938` calls a helper and jumps back to `0x80010204`.
The epilogue at `0x8001094C` restores exactly the frame established at
`0x80010178`; the next prologue starts at `0x8001096C`.

This supports recovering the bounded range `[0x80010178, 0x8001096C)` as one
startup routine with internal labels, subject to checking references into
the range. The banked `func_80010178.c` instead ends in a C call to
`func_80010214`, so its mismatch does not establish that the compiler cannot
reproduce the complete routine. The batch enumerator's current heuristic
also splits at these jumps. Recovery must preserve the internal control flow
and verify the entire range before claiming startup source parity.
No full-range C match or native execution is established by this analysis.

A scratch reconstruction using `m2c_assembly_text` exposed a newline bug:
the whole-line label regex consumed the newline and joined the next
instruction to the replacement label. The helper now replaces only the
directive, with LF and CRLF regression coverage. After this fix, requesting
the complete startup range yields a 137-line draft instead of the old
33-line wrapper. Both `-O2` and `-O0` builds currently fail on unresolved
`sp` state and missing structure fields (including `unkA3D4` and `unkA3D2`).
The draft has not been promoted. Stack-variable recovery and retail-backed
structure declarations are the next prerequisites for comparing its bytes.

`config/context/startup.c` now provides a partial state view for m2c: eight
fields with retail-observed offsets/widths, plus two 12-byte descriptors at
offset zero. The descriptor's address word is `u32`, preserving the PS1
layout on a 64-bit host. Unknown bytes remain padding and the view's end is
not a claim about the complete object's size. Preprocess this file with
`cc -E -P -I include` and pass the result to m2c with `--context` and
`--no-cache`, together with the coalesced startup assembly. The resulting
draft resolves the eight fields and descriptor stores; it still contains
unresolved stack expressions and other buffer accesses. The context is
recovery metadata, not a matched source or native runtime object.

The stack expressions correspond to explicit scratchpad-stack switches:

| Retail instruction range (inclusive) | Helper called on scratchpad stack |
| --- | --- |
| `0x800105EC`–`0x80010610` | `func_80015498` |
| `0x80010614`–`0x80010638` | `func_8001C00C` |
| `0x8001071C`–`0x80010740` | `func_800D25FC` |

Each sequence stores the original `$sp` at `0x1F8003FC`, sets `$sp` to
`0x1F8003F8`, calls the helper, then restores `$sp` through that saved word.
Consequently, adding a C local named `sp` would not recover its behavior.
The exact PS1 reconstruction needs to preserve these assembly sequences;
the native boundary needs a separate account of helper stack/scratchpad
dependencies before choosing its implementation. Neither has yet been
verified. `tests/test_startup_context.py` checks field offsets, widths and
descriptor stride, including rejection of a narrowed address word.

The context also describes startup's four 20-byte-stride address-table views
and byte-addressed buffers. These views overlap: `A6528 = A651C+12`,
`AE7C8 = AE7BC+12`, `AA60C = A6610+0x3FFC`, and `BA0E4 = BA0D8+12`.
They must not become separately allocated objects in a native implementation.
Opaque two-element blocks at state offsets `0x38` (stride `0x5C`) and
`0x14C` (stride `0x14`) preserve the observed layout without assigning
unverified library types.

m2c with this context produces typed address-table indexing and byte-scaled
buffer arithmetic. To prepare a scratch compile, pass the preprocessed
context **together with** m2c output through `_sanitize`; sanitizing output
alone inserts guessed declarations for context-owned globals. The sanitizer
now recognizes typedef/tagged-struct extern declarations instead of
contradicting them with `extern s32`. The resulting `-O0` compile reaches
only the unresolved `sp`, `subroutine_arg0`, and void-expression stack
placeholders. This is not source parity: the state-relative opaque-block
arguments still contain m2c byte-offset pseudocode that needs explicit C
addressing before behavioral or byte-level verification.

### Mixed startup compile probe

`include/retail_scratch_call.h` preserves the three scratchpad sequences as
explicit MIPS assembly with caller-clobbered registers and memory declared.
It rejects native compilation. The original stack pointer is restored after
each call; the fixed save cell makes this operation non-reentrant. The
pinned maspsx parser requires tab-separated `.set` directives: space-separated
ones in the first probe left extra delay-slot instructions in the output.

The local, ignored `staging/startup/80010178-mixed.c` combines that fragment
with recovered C and explicit byte-pointer arithmetic for the two opaque
state-block arguments. Build the probe with:

```sh
python3 tools/build_candidate.py staging/startup/80010178-mixed.c \
  --symbol func_80010178 --link-base 0x80010178 --optimization=-O0 \
  --output /tmp/musashi-startup-probe.bin
python3 tools/match_function.py --vram 0x80010178 --size 0x7F4 \
  --candidate /tmp/musashi-startup-probe.bin
```

The initial full candidate was **not a match**: `-O0` emitted 1,936 bytes versus retail's
2,036, and `-O2` emitted 1,424 bytes. In each compiled candidate, all three
40-byte stack-switch sequences occur exactly once and equal the respective
retail sequences, including their helper call targets. This proves those
assembly fragments only. Their relocated positions, the differing prologue
and surrounding C still require matching work; runtime behavior remains
unverified. No new entry was added to the pure-C match registry.

The subsequent ignored `staging/startup/80010178-registers.c` probe uses
ordinary C `register` locals for the addresses initialized into `$s0` and
`$s1` at retail `0x80010198..0x800101A4`. At `-O0`, GCC emits those same
initializations. No hard-coded register bindings or artificial stack padding
were introduced. The frame still differs (candidate `0x28`, retail `0x38`).

Inspection of `func_800189A8` at `0x800189A8..0x80018A1C` found no direct
use of incoming argument registers; it initializes its loop state and supplies
its own `$a0` for each helper call. The type context now declares it with
`(void)`. With that context, m2c stops passing the two incidental live values
that it had inferred at startup's `0x800103BC` call. This eliminates 13
unnecessary setup instructions in the candidate. Explicit 16-bit assignments
and multiplication-form byte offsets also reproduce more of the unoptimized
retail code generation. The revised register-local probe builds to 2,016
bytes; the complete 2,036-byte retail comparison still refuses the differing
length. Loop branching, frame layout, and expression ordering remain
unresolved; the size difference is not a parity percentage.

Two other functions examined for D4 turned out to be poor fits and were left
alone rather than forced:

* `func_80010000`, the crt0 entry itself, is flagged `/* Handwritten function
  */` by splat and directly manipulates `$sp`/`$fp`/`$gp`/`$ra` in ways no C
  compiler emits from ordinary source. It belongs in hand-written assembly,
  not decompiled C.
* `func_800100A0`, immediately after it, loads a count of `0` with a bare
  immediate (`lui/addiu`, no relocation) and then still emits the full
  runtime loop guard instead of folding the always-false branch away. The
  likely explanation is a classic `__CTOR_LIST__`-style idiom: the true source
  computes the count from a pair of extern symbols the compiler cannot fold,
  and the *linker* resolved them to an identical address only in this
  particular build, leaving no relocation for the disassembler to show.
  Reproducing that requires placing two symbols at the same address through a
  custom symbol file, not just cleaning up C, and was set aside rather than
  guessed at.

## Batch triage: the 65-128 bucket

With the oracle unavailable, 861 pending functions in the 65-128 bucket
(267 main, 594 main_0012) were m2c-decompiled and sanitized in parallel
by subagents into scratch (never `src/`, never the registry), then gated
through `gcc -fsyntax-only -std=c89` — C89 specifically, because modern
GCC reads `f()` as `(void)` while gcc-2.7.2 reads it as unchecked args,
so the default dialect false-rejects the harness's own convention.

Result: 313 parse-OK, banked for promotion the moment the toolchain runs
again (9 need no linking at all). The sanitizer rules this forced are in
`tools/batch_match.py::_sanitize`, each RED-tested: `? (*name)()`
locals, `extern ? D`, bare and named `?` params, callback
declarators, NULL to 0, missing-symbol declarations, and extern-pointer
refinement. A corpus OK-set diff proved zero regressions.

The remaining 541 fail almost entirely on one pattern, which is the hand-work
playbook for this bucket:

```c
*((D_800AF630.unkA3D2 * 4) + &D_800AE810) = func_8004239C(1);   /* func_8001099C */
```

m2c models a table at an unknown global as a struct and names members by
byte offset (`unkA3D2` = +0xA3D2). The `extern s32` default cannot take
member access, and no mechanical declaration can: member sizes are unknown,
so synthesising a padded struct would be guessing at layout. These need
per-function struct modeling — read the indexed offsets off the
disassembly, write the minimal struct that places them, match through the
oracle. Smaller mechanical classes also remain: `spNN` stack-slot
temporaries (stack-passed varargs need real signatures),
`saved_reg_*`, `unaligned` accesses, and `M2C_ERROR` markers, all hand
work by the same reasoning.

## Batch triage, continued: the <=64 buckets and the durable bank

The same pipeline later swept both <=64 buckets: main <=64 yielded ~70%
C89-clean, main_0012 <=64 ~46-59% (later slices cleaner than earlier
ones). One new sanitizer rule came out of it, RED-tested in
`SanitizeTests`: m2c prints `Warning: ...` diagnostics (e.g. `missing "jr
$ra" in last block`) to stdout, so the harness captures them as candidate
body — `_sanitize` now drops those lines, recovering ~13% of the 0012
small bucket alone.

Two operational lessons, both now structural:

- The durable bank is `staging/candidates/` (gitignored, local-only),
  plus `staging/pend_*.txt`. An earlier `/tmp`-only bank of ~1500 files
  partly evaporated mid-session — `/tmp` here is shared with at least one
  other active agent and files vanish. Gate passes into `staging/`
  promptly; treat `/tmp` as scratch. `docs/RECOVERY.md` holds the regen
  one-liners and the oracle probe.
- Coverage honesty: "attempted" means a gated pass exists in
  `staging/candidates/` under the function's name. Raw m2c outputs in
  `/tmp` are not coverage — they evaporate and were never gated.
- As of 2026-09-04 the full pending set (3765: main 1334, main_0012
  2431) is triaged to zero fresh: 1848 gated passes banked (49%),
  the rest in `staging/fails_*.txt` by bucket. The fails are dominated
  by struct-table member access (`unkXXX` on `extern s32`), then
  `M2C_ERROR`, `spNN`/`saved_reg_*` temporaries, and arity-mismatched
  static prototypes. Nothing mechanical is left; the remaining work is
  per-function struct modeling plus the oracle sweep.

## Exact placement and structured startup probe

The candidate linker previously rounded word-aligned retail bases up to the
input `.text` alignment. Correct extraction offsets did not correct internal
absolute jump relocations: a startup build requested at `0x80010178` actually
linked at `0x80010180`. The script now fixes the output address explicitly and
uses `SUBALIGN(4)`. A real assembler/linker regression checks internal jump
targets at two non-16-byte-aligned bases and one aligned control. Both
unaligned cases failed before the fix and all three pass afterward. All
362 existing registry entries re-verified with the corrected linker; the full
suite passed 317 tests plus 5 subtests on 2026-09-04.

The local, ignored `staging/startup/80010178-postincrement.c` probe replaces
the recovered gotos with nested `while` loops, uses a global counter ternary
without a spill temporary, and restores postfix halfword increments.
Compiled at `-O0` with base `0x80010178`, it is exactly 2036 bytes, with
496 of 509 instruction words equal at the same addresses. The remaining
13 words are ten frame-allocation/save/restore differences (candidate frame
`0x20`, retail `0x38`) and three reversed addition operand orders at
`0x800104C4`, `0x8001059C`, and `0x800106D4`. No padding locals have been
invented to force the frame. Original local declarations remain unresolved.
Casting the address bases before addition in the separate `addresswords`
probe did not change these differences.

A subsequent `staging/startup/80010178-integer-add.c` probe uses
`offset + (u32)address_base` for those three additions. Unlike pointer
addition or base-first integer addition, it reproduces their operand order.
It matches 499/509 words at identical addresses; only the ten frame words
above remain different. This does not resolve the original local declarations
or qualify the mixed-assembly probe for the pure-C registry.

These are static recovery observations only. The probe still includes three
retail scratch-stack assembly fragments and is neither a pure-C registry
match nor native boot evidence. Reproduce the compile with:

```sh
python3 tools/build_candidate.py staging/startup/80010178-postincrement.c \
  --symbol func_80010178 --link-base 0x80010178 --optimization=-O0 \
  --output /tmp/musashi-startup-postincrement.bin
```

## Native queue, 2026-09-11: every function drafted, oracle pending

The function inventory is closed (see `docs/PROGRESS.md`, ceiling section):
1531 main + 19 main_0007 + 2412 main_0012 = 3962 functions, 697632 bytes.
All splat disassemblies were regenerated with hole-evidenced symbols
(`config/symbol_addrs.main.auto.txt`, `config/symbol_addrs.main_0007.auto.txt`;
old instruction bytes verified identical, zero lost).

Every unregistered function has (or is getting) a dual-shape draft in
`src/`: pinned SHA256 word export plus an UNVERIFIED m2c body, via
`/tmp/carve_all.py` (generalizes `/tmp/carve_batch.py` per region; asserts
words against the blob, never rewrites seam words, skips verified bodies).
As of the disk-quota interruption: 652/1519 spans carved. Resume with:

```sh
python3 /tmp/carve_all.py full /tmp/carve_full.log > /tmp/carve_resume.log 2>&1
```

Then, on a shell where the 32-bit toolchain executes, drain the queue.
`--max-size` must be raised: 75% of the pending bytes are functions over
the 256-byte default cap:

```sh
python3 tools/batch_match.py --asm asm/main.s --region main --register-existing --max-size 100000
python3 tools/batch_match.py --asm asm/overlays/main_0007/main_0007.s --region main_0007 --register-existing --max-size 100000
python3 tools/batch_match.py --asm asm/overlays/main_0012/main_0012.s --region main_0012 --register-existing --max-size 100000
python3 tools/verify_registry.py
python3 tools/progress.py
```

Caveats: `verify_existing` never stamps, so matched files keep their
UNVERIFIED header notes after registration — the registry is the
authority, not the file header. m2c-failed spans stay word-only and need
hand-written bodies (struct-table member access dominates, per the triage
above). The 31 pre-existing `enddlabel func_` data labels (80072xxx run
et al.) are proven data (no prologue, no call/jump target, table/ASCII
heads) — do not "recover" them.

## Environment watch: 32-bit toolchain execution

The vendored Psy-Q compilers are 32-bit statically linked i386 binaries.
They die with SIGSYS (exit 159, empty output, not even `--version`) in a
sandbox whose seccomp profile rejects a legacy startup syscall; `cc1`
fails identically while `m2c`, `maspsx`, and binutils (all Python or
64-bit) are unaffected. When this holds, `tools/verify_registry.py`
correctly fails closed at 0/252 rather than claiming anything. `personality()`
itself works (`setarch`/`linux32` exit 0), so the block is syscall-specific,
not a missing compat layer. If a session shows this, do not work around it
by weakening the oracle: bank verified-ready candidates and wait for an
environment where the toolchain executes.

**Resolved, 2026-09-14.** The block is the sandbox, not the host. The same
`cc1` that exits 159 under the sandboxed shell runs normally when the
sandbox is off, and the full `tools/verify_registry.py` re-verified every
entry from there. So the rule above stands with one correction: the failure
signature is a *sandbox* signature, and the first thing to try is the same
command outside it. Only treat the toolchain as unavailable once it fails
in an unsandboxed shell too.

## The sweep is exhausted; the near-miss band is not

Re-running the full `--register-existing` sweep over all 1,526 unregistered
sources, on a shell where the toolchain executes, produced **zero** new
matches. That confirms the plan's premise: m2c output does not become
retail bytes on its own, and no wider sweep will change it.

The useful measurement is a different one. Build each failing draft and
count how many instruction words differ from retail:

| words differing | functions | bytes |
| --- | --- | --- |
| 0 (matched but classified `unclassified`) | 20 | 2,332 |
| ≤ 4 | 39 | 5,160 |
| ≤ 10 | 89 | 8,172 |

A draft four words from retail is not a failure, it is a codegen-drift bug
with a mechanical cause. The catalogue below is what those causes turned
out to be; each entry was confirmed by fixing it and getting a MATCH.

### Codegen drift idioms

* **Dropped leading parameters.** Retail `li $a2,12` where the draft emits
  `li $a0,12` means m2c guessed too few parameters: the real function takes
  leading arguments it passes straight through. Add them.
  (`func_80045640`, `func_8003BE74`, `func_801717A0`, `func_8014BC80`.)
* **Pointer scale.** m2c types a global `s32 *` and then writes
  `&D_X + 0x1A0`, which scales to 0x680 bytes. Retail's `addiu $a1,$s0,416`
  says the offset was bytes: declare `extern u8 D_X[];` and index it.
  The same bug appears as `var += 4` on an `s32 *` where retail adds 4
  bytes, and as `sll` by the wrong amount. (`func_80029690`,
  `func_80014444`, `func_8013ED6C`.)
* **Address versus load.** Retail `addiu $a0,$a0,%lo(D_X)` passes the
  *address*; the draft's `lw` passes the contents. Pass `&D_X`.
  (`func_8003A53C`, `func_800CEEFC`, `func_80060404`, `func_80060614`.)
* **Volatile stores stay out of delay slots.** Where retail ends
  `sw ...; jr $ra; nop` and the draft ends `jr $ra; sw ...`, the store is
  through a volatile pointer: GCC 2.7.2 will not schedule one into a delay
  slot. Declaring the pointee `volatile` reproduces it, and the reverse
  case — draft one word longer — means the volatile does not belong.
  (`func_8003B0B8`, `func_8005B710`.)
* **Commuted operands.** `v1 = v1 + v0` and `v1 = v0 + v1` are the same
  value and different instructions. Write the operand order retail shows.
* **Shared-base CSE.** Two uses `&D[0x1A0]` and `&D[0x1F0]` fold to a base
  of `&D[0x1A0]` with a small second offset (`a1=s0; a1=s0+80`), while
  retail keeps the base at `&D` (`s0=D; a1=s0+0x1A0; a1=s0+0x1F0`). An
  explicit `u8 *base = &D[0];` with `base + 0x1A0` / `base + 0x1F0`
  reproduces the retail shape. (`func_80014444`.)
* **Pointer-form increment.** `D += 1` on a `u16` scalar compiles to a
  direct `lhu`-offset/`sh`-offset pair, while retail holds the address in
  a register (`lui+addiu; lhu 0(v1); sh 0(v1)`). A `{ u16 *p = &D;
  *p = *p + 1; }` block reproduces it. (`func_800304C8`.)
* **Negative inner test with shared tail.** `if (x) goto shared` emits
  `bnez`-to-call plus `j`, while retail has `bne`-to-skip plus `j`-to-call
  with a `nop` slot. Flipping to `if (x != V) goto skip;` with the call
  hoisted to a single shared tail reproduces it. (`func_8012C218`;
  `func_8012F68C` was the pass-through-argument variant of the same
  family.)   `func_801301E8` resists this shape: GCC cross-jumps the two
  `==0 -> end` tests into one `beqz`, going 4 words short, and is still
  open.
* **Address held across the block.** Retail `lui $a0,%hi(D); addiu
  $a0,$a0,%lo(D); lw $v1,0($a0)` followed later by `sw $s2,0($a0)`
  (same `$a0`) where the draft emits direct `lui $v1` + `lw $v1,off`
  pairs means the source keeps `&D` in a local across the block:
  `{ void **pp = &D; if (*pp != 0) {...} *pp = arg; }`.
  (`func_801377B4`.)
* **Volatile local pointer over a store/load pair on one global.**
  Retail `lui $v1,%hi(D); addiu $v1,$v1,%lo(D); lw $v0,0($v1); sw $a0,0($v1)`
  (one base register for both accesses, store before `jr`, delay slot `nop`)
  where the plain `s32 old = D; D = arg; return old;` draft gives two
  separate `lui` bases, and a plain local pointer gives one base but sinks
  the store into the `jr` delay slot (one word short). Marking the local
  pointer `volatile` (`volatile s32 *p = &D; s32 old = *p; *p = arg;
  return old;`) stops the sink *and* keeps the single base, matching
  6/6. The neighbouring `func_8004654C` is the same two-base shape and
  stays the plain form, so do not sweep the volatile in — it is per-site.
  (`func_80046564`.)
* **Hoisted mask constant seeks a branch delay slot**
  (`func_80131A34`, open, best 33/37). The else branch computes
  `field & ~4` while retail holds the mask in `$v1` from an `li` sitting
  in the *last* comparison's delay slot (`bne` + `li $v1,-5`). A named
  `s32 mask = ~4` temp fixes the register mirror (`and $v0,$v0,$v1`)
  and gains 2 words, but where the temp is assigned decides which delay
  slot the `li` sinks into: before the `if`-chain it lands in the
  *second* comparison's slot (words 12/18 swapped, 33/37); inside the
  `else` it stays at the use site (31/37). Still open: the `sw $s0` /
  `sw $ra` prologue save order (words 3-4) never flips under any
  declaration order, signature, or toolchain tried — likely a deeper
  allocation-order effect of the true source.
* **-O0 frame size.** At `-O0` the frame immediate is the only difference
  (e.g. retail `addiu $sp,$sp,-0x20` vs draft `-0x18`, with the six
  prologue/epilogue offset words following): the body already matches.
  Any 4-byte unused stack slot (`s32 pad;` plus a `(void)` cast to keep
  `cc1` quiet — verified to emit no code) grows the frame to retail's
  without touching the body. This is a codegen constraint, not source
  evidence; the original may have had an unused parameter or local.
  (`func_800110CC`, `func_80011144`.)
* **Assignment sinks below its use.** `var = call() - 1` before the `if`
  that indexes with `call()` makes GCC reuse the decremented register for
  the index (`addiu $a0,$v0,-1` then `sll $v0,$a0,3`); retail keeps the
  raw result for the index (`addu $a0,$v0,$zero`, `sll $v0,$a0,3`) and
  computes the decrement in the branch delay slot (`addiu $v0,$a0,-1`).
  Moving the assignment below the `if` reproduces it.
  (`func_8012A988`.)
* **Positive test into the body.** `if (call(...) == 0) return 0; body;
  return 1;` emits `beqz`-to-exit, while retail has `bnez`-into-body
  with the next call's first constant hoisted into the delay slot
  (`bnez $v0,label` + `addiu $a0,$zero,1`). Writing it as
  `if (call(...) != 0) { body; return 1; } return 0;` reproduces it.
  (`func_8012DF34`, together with a hoisted `s32 *p = D;` base so the
  two `p[8]`/`p[14]` loads share one `lui+addiu` instead of folding
  each offset into its own `lui`.)
* **One variable, sequential ranges, shared register.**
  (`func_80132288`, 388 bytes.) A temp pointer computed early
  (`sz = **arg1`, used for a length) and a size base needed late live
  in the SAME register (`$a2`) in retail, while the draft's two temps
  split across `$a1`/`$a0`. Merging them into one `void *sz` variable —
  reassigned (`sz = arg2`) unconditionally just before each use block —
  reproduces it. The unconditional staging assignment is hoisted by the
  scheduler into the preceding branch's delay slot (`bne` + `move
  $a2,$s0`), which is the fingerprint that the assignment dominates
  both paths. A conditional staging (`sz = arg2` only on one path)
  instead emits the move after the branch and costs words. Corollary:
  when the size always comes from one pointer on every path, the
  path-dependent-pointer reading is wrong — recheck the delay slots
  before believing it.
* **Unused middle parameters.** Retail `move $s1,$a3` + `lw $s2,0x30($sp)`
  where the draft has `move $s1,$a1` + `move $s2,$a2`: the live values
  arrive in `$a3` and on the stack, so the signature must declare (and
  ignore) the `$a1`/`$a2` slots. m2c already names them positionally
  (`arg0, arg3, arg4`); adding `s32 unused1, s32 unused2` between them
  reproduces the prologue. (`func_80046ABC`.)
* **Saved copy for late uses.** Retail `move $s0,$v0` right after a call,
  then `sw $v0,...` + `bnez $v0,...` (immediate uses stay in `$v0`),
  while the draft uses the saved `$s0`/`$s1` everywhere: introduce
  `save = temp;` and keep immediate store/test on the raw return,
  switching later uses to the copy. Companion: the call arg may be the
  *address* cast to integer (`(s32) &D_...`, `lui+addiu`) where the
  draft loads the value — check the callee, which may already store it
  as a word. (`func_80142DC4`, `func_80142E38`, `func_80142EC0`.)
* **Mask-into-$v0 cascade** (`func_8005CF68`, open, best 33/39).
  Three identical `arg2 & 0x1000` masks must all target `$v0`;
  variants put them in `$v1` (then CSE merges two and the JOIN tests
  `$v1`). With `$v0`, everything else is forced: the mask sinks into
  the `bne` delay slot, the `li $v0,2` setup clobbers it, the fallthrough
  recomputes it, and the JOIN tests `$v0`. Forcing `$v0` is the unsolved
  part — it needs `$v1` busy at the mask site, and nothing found does
  that (decl orders, temp-type swaps, operand swaps). Also open here:
  a `sh`-then-`beqz` vs `beqz`-then-`sh` delay-fill swap at the top.
  True-arm restucture (mask conditional) lengthens to 40 words; the
  draft's unconditional-mask shape is closer.
* **First-OR target resists flipping** (`func_8016EE94`, open, best
  56/57). For `D = arg0 | 0x04000000 | arg1`, retail folds the constant
  into `$a0` first (`or $a0,$a0,$v0`) while every variant folds into
  `$a1` first. Ruled out: top-level operand swap (cascades to 51/57),
  right-nested parens reasoning, `u32`/`s32` decl swaps on either/both
  params (all still 56/57). Same-source-different-codegen puzzle;
  likely an allocator canonicalization input not yet identified.
* **Symmetric call temps resist register swap** (`func_80140E6C`,
  open, best 32/37). Two identical-shape calls feed one OR-chain;
  retail accumulates into `$s1` (`or $s1,$s1,$s0`, first call result
  in `$s1`) while every variant accumulates into `$s0`. Ruled out:
  all 4+ declaration orders, moving the third temp between/after the
  calls (breaks the prologue instead), swapping call/assignment order
  (moves matched `jal` setups). The accumulator follows the first
  call result's home by an unknown rule.
* **Shift/address interleave rotation** (`func_8003EAB4`, `func_8003E248`,
  open). For `temp = ((x << 16) >> 14) + &D`, retail emits
  `sll, lui, addiu, sra, addu` (address materialization between the
  shifts) while every tried tree emits `sll, sra, lui, addiu, addu`.
  Scaling must be right first (`u8[]` retype, else the shifts merge to
  `sra 12`); after that, int-vs-pointer trees, operand swaps, split
  statements, `-O1`/`-O3`, and alternate toolchains all keep the
  non-interleaved order. Best 60/63 and 36/39; improved-but-unverified
  drafts banked in-tree.
* **Phantom 8-byte -O2 frame** (`func_80016110`, open, best 59/69).
  Retail frame is 24 (`vars= 0`) while every compilable variant reports
  `.frame $sp,32 (vars= 8)` with zero `$sp`-relative accesses — 8 dead
  bytes. Bisected: any *use* of a second pointer temp forces it (an
  assigned-but-unused temp keeps 24; `*tmp = *tmp` flips to 32), yet
  deleting the temp and inlining its double-deref address three times
  keeps 32 as well. Scalar temps do not trigger it. Separately, the
  middle expression resists register assignment: retail evaluates the
  `u16` index into `$a1` then the field word into `$a0`, while every
  variant puts the index in `$a0` (explicit `idx`/`f0` temps get forward-
  propagated back; a shared temp over-folds to 60 words). Retail also
  reloads the index global fresh for each of its three uses — a shared
  `idx` temp is a trap (60-word over-fold), not a fix.
* **Address-taken dead local for phantom frame bytes.** When the body
  matches but retail's frame is 8 bytes bigger with zero `$sp` traffic
  on either side (`sub $sp,32` + saves at +0x18/0x1C vs `sub $sp,24` +
  saves at +0x10/0x14), an address-taken dead local reserves exactly
  that: `s32 du; (void) &du;` emits no code (verified) but flips
  `.frame` to `vars= 8`. Probes.excluded everything else (unused
  locals/params vanish; calls, saves, alignment, `-O1`/`-O3`, `-G`
  don't move it). It likely stands in for a leftover or debug local
  in the original; file it as a codegen constraint either way.
  (`func_80141B90`, `func_8013ED6C`.) Larger gaps scale the same
  way: 16 dead bytes need e.g. a dead `s32 du[4]`
  (`func_80155E30`, `func_801576A8`); probe `.frame vars` directly
  (`--keep-intermediates`) rather than guessing.
* **Equality against a constant.** `x == 0xFF` returning 0 or 1 compiles to
  `xori`/`sltu`, not to a branch. Retail's `li $v0,255; beq` shape has not
  been reproduced from any of the sixteen source shapes tried for
  `func_8005FB70`; it is still open, and is the one idiom in this list with
  no known fix.
* **Retry loop: `goto` back-edge defeats LICM, convergent return keeps
  fall-through.** (`func_80043300`, 38/38.) Three faults, each found by
  bisecting 13/38 to 37/38 to 38/38. First, a structured `do-while` lets
  GCC hoist the loop-invariant `li` constants (`== 1`, `!= -1`) into saved
  registers, growing the frame `0x18 -> 0x20` with two extra saves; a
  `goto retry` back-edge is not recognised as a loop, so `li $v1,1` and
  `li $v0,-1` stay per-iteration immediates. Second, an early `return 1`
  in the success block gets reordered out of line (`beq`-to-success);
  converging both paths into a single `return rc` keeps the success block
  falling through from `bne`, with the 1 delivered in the delay slot of
  the `j`-to-shared-epilogue that the convergent return produces.
  Third, a tooling fault, not a source fault: an internal absolute `j`
  resolves against the link base, so the default `0x80010000` breaks
  exactly one word (37/38). `batch_match` and `verify_registry` already
  pass `--link-base 0x<VRAM>`; hand-built candidates need it too.
* **Taken-block assignment sinks into the branch delay slot.**
  (`func_80043450`, 27/27.) Retail `bnez $v0,epi` with `move $v0,$zero`
  in the delay slot, where the fall-through then sets `$v0 = 1`, means
  the taken path returns 0 and the fall-through returns 1 — the delay
  slot always executes, so a casual read that the nonzero call result
  is returned is wrong (the m2c draft had it inverted, and the boot
  narrative's "return-delay clear" already said so). Source shape:
  `rc = call(); if (rc != 0) { rc = 0; goto done; } rc = 1; ...` —
  the taken block's single assignment is what the scheduler sinks into
  the delay slot. Without the explicit `rc = 0` the match fails at
  exactly the delay words. Corollary: never trust the listing comment
  words over the extracted binary when they disagree; the EXE is truth.
* **Convergent `goto` defeats test inversion and block swap.**
  (`func_8004787C`, 15/15; `func_80043300` was the first case.)
  Structured `if/else`-return compiles the test inverted (`bgez`
  for `< 0`, `beq` for `!=`) with the taken block moved out of line,
  because the reorder pass calls the early return cold. Routing both
  paths through one `rc` variable with explicit `goto`s keeps the
  written test and the written fall-through: `bltz`-to-`neg`,
  positive path inline, call setups sinking into delay slots.
  (`func_8002A04C` is the same family: `== 0` plus `goto done` lays
  out `bnez`-to-body with the zero sunk into the join-jump delay.)
  (`func_80043450`, 27/27.) Retail `bnez $v0,epi` with `move $v0,$zero`
  in the delay slot, where the fall-through then sets `$v0 = 1`, means
  the taken path returns 0 and the fall-through returns 1 — the delay
  slot always executes, so a casual read that the nonzero call result
  is returned is wrong (the m2c draft had it inverted, and the boot
  narrative's "return-delay clear" already said so). Source shape:
  `rc = call(); if (rc != 0) { rc = 0; goto done; } rc = 1; ...` —
  the taken block's single assignment is what the scheduler sinks into
  the delay slot. Without the explicit `rc = 0` the match fails at
  exactly the delay words. Corollary: never trust the listing comment
  words over the extracted binary when they disagree; the EXE is truth.
* **Small-constant multiply is strength-reduced, not folded.**
  (`func_8003834C` family, 4/4.) Retail `sll; subu; sll` is `* 127`
  then `* 4`, never `* 0x1FC` (mult) — m2c's `arg0 * 0x1FC` is the same
  value and different instructions. Write the factored form.
* **Index/address temps force evaluation order.**
  (`func_80038638`, `func_80038668`.) `((u8 *)&D[i])[a1] |= 1` lets
  the scheduler materialize the base first and add the offset second;
  `s32 i = arg0 * 127; u8 *p = (u8 *)&D[i]; p[arg1] |= 1;` pins
  index, then base, then offset. The single statement is 2/12.
* **Shared scaled index keeps the unscaled base live.**
  (`func_8003836C`.) Two uses of `i * 4` keep `i` in `$v0`, so the
  scaled index lands in `$v1` instead of folding back; the convergent
  `rc` keeps the taken block falling through into the shared `jr`
  with the zero in the `beqz` delay slot.
* **Single-use address folds, multi-use address materializes.**
  One address use compiles to `lui $at + addu $at + memop(lo)`;
  two uses (load then store) materialize the base once
  (`lui $v1 + addiu`) with zero-offset memops. Read the use count
  before choosing the source shape; this is the same force behind
  the `func_80046564` puzzle (still open: its two uses fold where
  `func_8004359C`'s identical pair does not).

### Splat promotes jump targets to function symbols

Seven "functions" in the listing are 20 bytes and contain only an epilogue
(`move $sp,$fp; lw $fp,N($sp); addiu $sp,sp,M; jr $ra; nop`). Each one
directly follows a function whose last instruction is `j` to it. They are
not functions: they are the shared epilogue of a `-O0` body, and splat
promoted the `j` target to a `glabel`.

Confirmed at `80010A84`, `80010B2C`, `8001136C`, `80011E10` (main) and
`8013C400`, `8013C924`, `801458CC` (main_0012). Matching the merged range
works — `func_80010B10`, `func_80011350` and `func_80011DF4` all match at
48 bytes, `-O0` — so these are registered at their true size with the
fragment covered by the same range. `tools/progress.py` unions ranges per
region, so nothing is counted twice. The listing still carries the spurious
labels; drop them the next time the symbol files are regenerated.

### `--valid-syntax` is what makes a draft compilable

Of the 1,526 unregistered sources, **1,220 did not compile at all**, and one
error dominated: `request for member 'unkNNNN' in something not a structure
or union`. m2c had written `D_800AF630.unkA3D2` while the sanitizer had
declared `D_800AF630` a scalar, because neither knows the layout.

m2c's `--valid-syntax` mode emits the same access as
`M2C_FIELD(&D_800AF630, u16 *, 0xA3D2)`, carrying the width it inferred from
the load. `tools/batch_match.py` now always runs in that mode, maps the
`M2C_UNK` marker back onto the `?` the sanitizer already understands, and
includes `include/m2c_macros.h`. `tools/recarve_drafts.py` re-decompiles
exactly the drafts that fail to build, leaving verified bodies, hand-written
bodies, assembly overlays and already-building drafts untouched, and
refusing to write a file whose export words would change.

```sh
python3 tools/recarve_drafts.py --dry-run     # count, change nothing
python3 tools/recarve_drafts.py               # rewrite the failures only
```

A compilable draft is not a match. It is a draft the oracle is finally
allowed to judge.
