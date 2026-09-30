# Startup scratch-stack boundary audit

Static investigation, 2026-09-04. Native substitution is **NOT_PROVEN**.
Authority is the extracted `SLUS_007.26`, SHA-256
`66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a`.
Addresses below refer to that image and its generated `asm/main.s`.

Startup saves its ordinary stack pointer at `0x1F8003FC` and enters each
scratch callee with `$sp = 0x1F8003F8`. It restores the ordinary stack after
each return. This is a shared saved-SP cell, not a reentrant host-call API.
See [MATCHING.md](MATCHING.md) for the three exact assembly fragments.

## Observed direct call paths

| Function | Frame bytes | Direct callees / unresolved edge |
| --- | --- | --- |
| `80015498` | `0x28` | `80015608`, `80015760`, `80058B40` |
| `80015608` | `0x40` | `80015908` |
| `80015760` | `0x48` | `80015A74`, `80015908` |
| `8001C00C` | `0x18` | `8001F9F8`, `8001D1C4` |
| `8001F9F8` | `0x28` | `8001FB8C` |
| `8001D1C4` | `0x28` | `8001D388` |
| `8001FB8C` | `0x18` | indirect call at `8001FBDC` |
| `8001D388` | `0x18` | indirect call at `8001D3E4` |
| `800D25FC` | unresolved | not identified in the current disassembly |

The listed frame adjustments are balanced in their inspected bodies.
The observed fixed frames along `15498 -> 15760` total `0x70`; the prefix
`1C00C -> 1F9F8 -> 1FB8C` totals `0x58` **before** its indirect callee.
These sums are not a whole-runtime stack bound or proof that accesses through
other pointers cannot alias scratch memory. `15908`, `15A74`, and `58B40`
have no direct or indirect calls in their current bounded disassembly.

## Indirect dispatch authority

`8001FB8C` takes an unsigned halfword selector from argument offset 2,
guards it with `< 9`, then loads a word from `80063500 + selector * 4`.
`8001D388` similarly guards `< 11` and uses `8006351C`. Both contain other
object-state guards; a selector in range alone does not cause dispatch.

The following are initial retail image words, not observed live targets:

| Index | `80063500` view | `8006351C` view |
| --- | --- | --- |
| 0 | `8001FC08` | `8001E7E0` |
| 1 | `8001FC08` | `8001D3FC` |
| 2 | `8001FC08` | `8001DA34` |
| 3 | `8001EFE0` | `8001EA14` |
| 4 | `800221A8` | `80021284` |
| 5 | `8001FC08` | `800215F4` |
| 6 | `8001FC08` | `80021D38` |
| 7 | `8001E7E0` | `80023570` |
| 8 | `8001D3FC` | `80023BF0` |
| 9 | outside guarded range | `8001EA14` |
| 10 | outside guarded range | `8002374C` |

The first view's final two entries overlap the second view's first two.
Do not allocate two independent native arrays and silently erase that alias.
Whether runtime writes alter these words remains unverified.

## Confirmed scratch-data and GTE dependency

Dispatch target `8001FC08` has a `0x100`-byte frame and calls `80020248`
at `8001FECC`. That callee is incorrectly split by the generated labels:
its full observed range is `[80020248,80020598)`, with a `0x68` frame.
The jump at `80020284` enters `80020290`; `8002028C` is the loop increment,
not an independent function. The shared epilogue restores the frame at
`80020560`–`80020594`.

The loop counts nonzero words in the list passed as argument 0 until its
zero terminator. At `800202A0` the signed comparison `count < 8` selects
scratch buffers; counts of eight or more select RAM. The selection is:

| Path | First buffer | Second buffer | Return selector |
| --- | --- | --- | --- |
| `count < 8` | `1F800000` | `1F8000E0` | 1 |
| `count >= 8` | `80074818` | `80075018` | 0 |

The first scratch base is **not** `1F80000C` or `1F800020`: those names
appear in split relocation annotations, but `lui` initially produces
`1F800000`. The later `+0x20` is the per-record stride. Both buffers use
`0x20`-byte records; the second includes word stores at offsets `0x14`,
`0x18`, and `0x1C`. Seven records occupy at most the ranges
`[1F800000,1F8000E0)` and `[1F8000E0,1F8001C0)`.

`80020248` loads GTE control registers (`80020334` onward), executes GTE
operations, and stores GTE results into these buffers, including `swc2`
at `8002053C`–`80020544`. The caller consumes the return selector at
`8001FED4`, recreates the selected bases, and passes them to `80052F04`
and `80052E38`. Thus scratch is shared producer/consumer data, not merely
temporary stack storage. Some work is conditionally skipped; the reserved
record ranges do not assert every byte is written on every path.

The corresponding path through `80020A28` has the same count threshold
and base selection at `80020A80`–`80020AB0`. Its observed full range is
`[80020A28,80020DA4)`, with a `0x78` frame; `80020A6C` is another split
loop-increment label. `8001FC08` consumes its selector at `8002001C`.
Do not use the generated short `endlabel` ranges as independent functions.

Along the inspected dispatch prefix, the stack at entry to `80020248`
after its allocation is `1F800238` (`3F8 - 18 - 28 - 18 - 100 - 68`).
This lies above the two seven-record scratch ranges, but nested calls,
other handlers, and interrupt behavior still require auditing. It is not
a proof of a global maximum stack depth or safe native substitution.

## Recovery probes

The local `staging/startup/80020248-full.s` coalesces the verified full
range for m2c. Decompilation succeeds as a process, but emits unsupported
`ctc2`, `mtc2`, `mfc2`, `mvmva`, `lwc2`, and `swc2` markers. Its guessed
pointer types and resulting arithmetic are not safe implementation input.
Do not sanitize these markers into no-ops or count this as recovered C.

The matrix-producing callee `[80020DA4,80020F34)` can be recovered further
without GTE support. Its inputs are three unsigned halfwords masked with
`0xFFF`; `800636D4` holds a pointer to four-byte lookup records. Instructions
read both whole signed words and signed low/high halfwords from those records.
The output consists of nine halfwords at offsets 0 through `0x10`.
This establishes storage views, not original type names or axis conventions.

The initial retail pointer at `800636D4` is `8006DF1C`. Its 4096 four-byte
records occupy 16384 bytes, SHA-256
`b106234165685d79392c926192d1b2f9e30125307abde6040747e401a35d0e87`.
All signed halfwords fall in `[-4096,4096]`. At indices 0, 1024, 2048,
and 3072 the low/high pairs are `(0,4096)`, `(4096,0)`, `(0,-4096)`, and
`(-4096,0)`, respectively. These support the trigonometric lookup
interpretation without replacing the retail table with host floating-point
functions. Runtime pointer/table mutability remains unverified.

`staging/startup/rotation-context.c` supplies a packed-word union view and
those input/output layouts. `80020da4-typed.c` is the resulting local draft.
The context corrects an untyped m2c error: a full-word load at `80020E20`
was previously rendered as a halfword dereference, losing its high half.
With the pinned default toolchain at `-O2`, the typed draft is 400 bytes,
but the oracle reports **MISMATCH, 25/100 words**, first difference at
`80020DA4`. Neither draft was promoted to `src/` or the registry.

The follow-up `80020da4-products.c` spells out intermediate products in
the observed retail multiply order. Default `-O2` produces 400 bytes and
**44/100** equal instruction words, still a mismatch. The typed draft at
`-O1` is 356 bytes. Testing the product-order draft with the other pinned
compilers did not resolve the mismatch: 2.6.0 yields 408 bytes; CDK 2.7.2
yields 400 bytes with 41/100 words equal; 2.8.0 and 2.8.1 yield 396 bytes;
2.91.66 yields 404 bytes. No compiler change or artificial padding was
applied to production sources.

```sh
python3 tools/m2c/m2c.py --context staging/startup/rotation-context.c \
  -f func_80020DA4 asm/main.s
python3 tools/build_candidate.py staging/startup/80020da4-typed.c \
  --symbol func_80020DA4 --link-base 0x80020DA4 \
  --output /tmp/musashi-rotation-typed.bin
python3 tools/match_function.py --vram 0x80020DA4 --size 0x190 \
  --candidate /tmp/musashi-rotation-typed.bin
```

## Native GTE mapping reconnaissance

The inspected local PsyCross checkout is
`e56e4cde1c2b8a15e0d4e38b26cdd9202e0d17e6`. Its `psx/inline_c.h`
defines `gte_rtir()` as `doCOP2(0x049E012)` and `gte_rt()` as
`doCOP2(0x0480012)`. These are the command fields of the six retail
`4A49E012` instructions and the final `4A480012` in `80020248`.
`src/psx/INLINE_C.C::doCOP2` forwards to `GTE_operator`. This establishes
an available implementation path, not tested native parity.

Do not substitute macros solely by their names:

- `gte_SetRotMatrix` reads five words, including the word at byte 16.
  The recovery matrix view describes only nine written halfwords (18 bytes).
  A native caller must establish the complete backing storage before passing
  that view to a 20-byte reader; widening a cast does not establish it.
- The inspected `gte_ldlv0` macro writes data register **12**, while retail
  at `80020524` uses `mtc2` to register **0**. The similarly named macro
  therefore cannot be assumed equivalent. Preserve the actual register
  transfers when lifting this fragment.
- PsyCross `RotMatrixYXZ` computes a different expression layout from the
  recovered matrix routine, while `RotMatrixZYX_gte` uses successive host
  rotation helpers and carries a TODO about exact Psy-Q implementation.
  Neither is authority to replace the retail formula or its rounding order.

## Executable native boundary probe

The storage distinction is now explicit in
`config/context/render_transform.c`: an 18-byte rotation view and a separate
32-byte transform view with an unresolved halfword at offset `0x12` and
three translation words at `0x14`. Host layout tests guard the sizes,
offsets, and word widths, with a narrowed-translation negative control.
These are recovery context types, not a promoted C implementation.

Specifically, `80020248` passes its stack address `sp+0x10` to `80020DA4`;
the resulting 18 bytes are read through the column pointers `sp+0x10`,
`sp+0x12`, and `sp+0x14` using halfword offsets 0, 6, and 12. The five-word
GTE control loads instead read argument 2 and argument 3 (saved at `sp+0x30`
and `$fp`). Argument 3 also supplies translation words. The inspected
`8001FC08` call supplies separate backing records at its `sp+0x30` and
`sp+0x50`. Thus the possible native overread is a type-substitution hazard,
not evidence that retail reads twenty bytes from the 18-byte temporary.

`tests/startup_gte_probe.c`, built as `musashi_startup_gte_probe`, exercises
the two mapped command fields through the actual linked native GTE layer.
Synthetic Q12 rotation maps `(123,-234,345)` to `(234,123,345)` in IR1–3.
Identity rotation plus translation `(10,20,-30)` maps the input to
`(133,-214,315)` in MAC1–3. Matrix control words are loaded explicitly,
without reading beyond an 18-byte recovery struct.

The `--wrong-v0` negative control writes the packed input to register 12
instead of 0 after poisoning V0. It fails with `(17,29,315)`, demonstrating
that the test detects this transfer error rather than accidentally passing
with stale matching state. CTest runs both the positive test and an expected-
failure negative control. The vendor checkout is unchanged.

These cases are native integration evidence only: no retail game execution,
hardware comparison, exhaustive overflow/saturation verification, or game
startup is implied.

`pc_port/compat/startup_gte.h::musashi_gte_rotate_columns` now lifts the
three-column register sequence from `80020354`–`8002040C` (also repeated
at `8002043C`–`800204F4`). It reads exactly nine halfwords, uses the already
loaded GTE rotation, and stores the resulting IR values in retail column
order. It neither reads transform padding nor replaces GTE arithmetic with
host multiplication. The contract explicitly retains shared GTE side effects
and requires serialized access; only disjoint or identical arrays are
supported.

The native probe exercises this helper with a non-symmetric signed matrix,
guard halfwords, exact in-place operation, final IR/MAC state, and positive
and negative saturation (`+40000 -> 32767`, `-40000 -> -32768` in IR while
MAC retains the unsaturated results). It is currently integrated into the
probe, not a game startup path. This platform helper is not counted as a
byte-matched PS1 function.

The adjacent `musashi_gte_translate_vector` lifts `800204F8`–`80020544`:
it loads translation control words, packs the low halves of X/Y into VXY0,
loads Z into VZ0, executes `gte_rt`, and stores full MAC1–3 words. Its
interface intentionally does not promise a full-width vector transform.
Probe cases preserve high-word input noise, output guards, exact in-place
operation, and MAC results `(40123,-40234,100345)` while IR saturates.
A combined rotation-then-translation case verifies the retained rotation
control state with no reset between fragments. Both helpers remain connected
only to the native probe pending recovery of the owning game routine.

## Owning routine native recovery draft

`staging/startup/80020248-native.c` reconstructs the full transform-list
control flow around the GTE fragments. This ignored draft is not a registry
match or a game startup integration. It requires the recovered, still-
unmatched `80020DA4` matrix callee and an explicit host address-resolution
boundary for the four retail output addresses. The draft uses the current
little-endian host layout when loading control words; cross-endian support
is not established.

The associated `80020248-harness.c` links that draft with the product-order
matrix draft and the real native GTE library. Synthetic identity lookup data
is a test fixture, not a replacement for the pinned retail table. Six cases
(counts 0, 7, 8 crossed with first-output enabled/skipped) pass, checking:

- the returned scratch selector and both resolved retail addresses;
- 12-byte pose records and 32-byte output records;
- generated rotation and translated position values;
- preservation of unused buffers, trailing records, unresolved padding,
  skipped first-output rotations, and first-output translation fields.

The resolver currently exists only in the synthetic harness. Runtime address
aliasing, malformed-list handling, original function machine-code parity,
and full retail/native replay remain unverified. Do not promote or wire the
draft into game startup on the strength of these synthetic cases alone.

## Retail-table matrix axis probe

The local `staging/startup/rotation-axis-harness.c` reads the 4096 packed
lookup records directly from the externally hash-verified retail EXE at file
offset `0x5E71C`. It tests the product-order matrix draft at every angle on
each isolated axis, both with and without upper bits `0xF000`, using the
single-axis sine/cosine matrix forms and output guard halfwords.

All **24,576** cases passed in ordinary host execution and with Clang
undefined-behavior sanitization (`-fno-sanitize-recover=all`). GCC's sanitizer
link failed because `/usr/lib64/libubsan.so.1.0.0` was absent; the Clang run
is the sanitizer evidence, not a claim that GCC's failed run passed.
A separate wrong-sign draft changed output `[2][0]` from `-y` to `y` and
was rejected at axis 1, angle `0001`, exit 1. The correct draft is unchanged.

Reproduce after independently verifying the EXE SHA-256 above:

```sh
clang -std=c99 -Wall -Wextra -Werror -fsanitize=undefined \
  -fno-sanitize-recover=all staging/startup/rotation-axis-harness.c \
  staging/startup/80020da4-products.c -o /tmp/musashi-rotation-axis-ubsan
/tmp/musashi-rotation-axis-ubsan extracted/disc/files/SLUS_007.26
```

This checks all isolated-axis lookup values, signs, layout, and masking.
It does **not** prove combined-angle intermediate rounding, equivalence to
executed retail instructions, runtime table immutability, or machine-code
matching. The matrix draft remains unverified for promotion.

## Combined-angle differential probe

`staging/startup/rotation-differential.py` interprets the actual 100 retail
instruction words with a deliberately restricted scalar model. It accepts
only the leaf's observed arithmetic/load/store subset and final `jr ra / nop`;
unsupported operations, load-delay consumers, unaligned/out-of-map accesses,
and missing terminal returns are rejected. It models 32-bit wrapping,
signed halfword loads, arithmetic shifts, and low-word signed multiplication.
It does not model CPU timing, multiply latency, interrupts, caches, or GTE.

The EXE hash is checked in the runner. The retail image/table and identical
angle inputs feed the model and a separately compiled native product-order
draft. All **4,608** cases (512 boundary-grid triples plus 4096 deterministic
random 16-bit triples, seed `0x20DA4`) produced identical nine-halfword
matrices. The model also checks output guard halfwords and restoration of
the leaf's saved registers and stack pointer. Four local model-control tests
cover signed shifts, multiplication/truncation, load widths, and rejection
paths. The wrong-sign native draft is rejected at `(0,1,0)` with `+6`
instead of the model's `-6` in output `[2][0]`.

This is **restricted-model differential evidence**, not independently
validated R3000 emulation, hardware execution, exhaustive combined-angle
coverage, or an exact compiler match. The scalar model and C draft can still
share an interpretation error. The default compiler result remains only
44/100 words equal; promotion requirements have not been lowered.

```sh
cc -std=c99 -fPIC -shared staging/startup/80020da4-products.c \
  staging/startup/rotation-table-binding.c -o /tmp/musashi-rotation-differential.so
python3 staging/startup/rotation-model-tests.py
python3 staging/startup/rotation-differential.py \
  extracted/disc/files/SLUS_007.26 /tmp/musashi-rotation-differential.so
```

## Remaining port prerequisites

- Inspect the dispatch handlers and descendants, including indirect calls,
  scratch aliases, GTE/register dependencies, and stack-derived pointers.
- Identify the retail overlay authority and loading path for `800D25FC`.
- Verify whether dispatch-table contents are immutable over the boot flow.
- Establish runtime observations before replacing scratch-stack transitions
  with ordinary native calls. Static frame balance is insufficient.

No native runtime behavior changed as part of this audit.
