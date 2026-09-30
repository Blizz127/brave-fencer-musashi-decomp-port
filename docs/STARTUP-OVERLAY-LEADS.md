# Startup overlay investigation

Static leads, 2026-09-04. **Load placement and live execution unverified.**
Do not promote these candidates using a guessed load address.

Windowed call-target inspection finds code inside mixed MAIN.CD members
that a whole-member opcode ratio misses. The existing PAC parser closes
exactly over members 0003, 0004, 0010 and 0011. Member 0004 is byte-for-byte
contained in member 0003 at offset `0x51800`, including its following chunks.

| Member | SHA-256 |
| --- | --- |
| 0004 | `ca57be67e662b10b2265fe7a8913f75a53ae5c9a6f0fea6d6a6c928714392b66` |
| 0010 | `97933aae35fd27e3917866e4f9c7ef5716850c7efda3f96719496c9ba5b86246` |
| 0011 | `b74d2c3cf2a659a88a10b7ac936acc6f830f50f1fdae2abb3f7cebb48e025272` |

Member 0010 has a type-0 PAC chunk of length `0x1F800`, followed by a
type-1/flag-1 chunk at `0x1F800`, length `0x59B5C`, padded stride `0x5A000`.
Member 0011 has the same first chunk length, followed by type-1/flag-1,
length `0x66B0`, stride `0x6800`. Code/data begins around member offset
`0x20000`; that is not necessarily the loader's copy start.

## Provisional address constraints

Voting self-region JAL targets against negative `addiu sp,sp` prologues
gives these leading member-relative bases (a heuristic, not loader proof):

| Member | Leading base | Supporting target/prologue pairs | Runner-up |
| --- | --- | --- | --- |
| 0004 | `800CE5F8` | 25 | 4 |
| 0010 | `800AEDF8` | 24 | 6 |
| 0011 | `800AEDF8` | 12 | 2 |

The latter two map offset `0x20000` to `800CEDF8`; member 0004 maps
offset `0x800` there. This shared relationship warrants tracing the PAC
loader. It does not establish that all these variants are simultaneously
resident or that the heuristic catches every function boundary.

## Candidate for startup's 800D25FC call

Under member 0010's provisional base, offset `0x23804` maps to `800D25FC`.
It is a 40-byte indirect-call wrapper. The scalar load computes callback
cell **8011DB24**, not 8012DB24: `lui 8012` plus signed offset `DB24`.
Its initial member word is zero at provisional offset `0x6ED2C`.

`staging/startup/800d25fc-member10.c` recovers this as a call through
`D_8011DB24`. The pinned default compiler emits exactly the 40 member bytes
at `[0x23804,0x2382C)`. This comparison corroborates the local function
shape and callback-cell address. It does **not** prove placement: this
wrapper contains no absolute reference to its own code address.

Member 0011's bytes at that same provisional address are different and do
not begin this wrapper. Overlay identity/state therefore matters.

Next required evidence: trace PAC type-1/flag-1 loading, establish which
member supplies startup's nonzero `fieldA3E3` branch, and find the writer
that initializes the callback cell before the indirect call. No new registry
entry or native game wiring was created from this lead.

## Loader destination chain

Subsequent inspection establishes a conditional loader-backed destination,
stronger than the prologue votes but still not a live overlay-selection proof:

1. The pinned EXE word at `80072C70` is `800CEDF8`. Callers including
   `80010C7C` and `80010ED4` load it into argument 1 for `80019A24`.
2. `80019A24` preserves that argument in `$s1`, then writes it to
   `800AE72C` at `80019A8C` after its admission guards.
3. The streaming handler `8001A338` sets its state base to `800AE6F0`.
   Therefore `800AE72C` is state offset `0x3C`.
4. The handler validates PAC magic at `8001A424`, reads type at header
   offset 4 and flag at offset 5, and dispatches via `80072AB4`.
   Entry 1 at `80072AB8` is `8001A5FC`.
5. That type-1 branch sets state 3, reads the destination from state `0x3C`,
   and computes `(chunk_length - 0x7FD) >> 2`. For a valid type-1 length
   this is `ceil((chunk_length - 0x800)/4)`: the streamed body excludes the
   first `0x800` bytes, not merely the generic 16-byte PAC header.
   At `8001A670` it records remaining words at `0x58` and destination
   at `0x38`.
6. State 3 dispatches through `80072AA0` to `8001A7F4`. It transfers at
   most `0x200` words per call to `80043994`, advancing destination by
   `0x800` and reducing remaining words by `0x200` for full blocks.
   `80043994` forwards to `80045090`, which writes argument 0 through
   the pointer at `8006CF88` (`1F8010B0`) and argument 1 OR `0x10000`
   through `8006CF8C` (`1F8010B4`). The actual destination/count path is
   thus visible in the executable, not inferred from opcode density alone.

The 84 instruction words covering the admission function, type-1 branch,
state assignments, and block-transfer branch were checked directly against
the pinned executable. Relevant jump-table and pointer words were also read
directly from it.

For a type-1 chunk at member0010 offset `0x1F800`, this chain places the
body at member offset `0x20000` at destination `800CEDF8`, **if that member
is selected through one of these loader calls with the observed destination**.
The resulting body-relative wrapper offset `0x3804` gives `800D25FC`.
The generic PAC extractor's 16-byte header convention is unchanged: its
payload includes the additional loader-specific padding.

The flag is retained at state offset `0xA8` and tested on completion; it
does not by itself establish which caller selects member0010. Callback-cell
initialization was initially unresolved. The earlier narrow scan was
incomplete: although no literal `8011DB24` was found, a subsequent aligned
instruction scan finds four direct stores in member0012 (details below).

## Descriptor selection from LIST.CD

The pinned EXE names `\\LIST.CD;1` at `80062C24`; its file descriptor
starts at `80062C38`. `8001971C` discovers the file records using
`80045374`, then requests this descriptor at `800197FC`, destination
`80180000`, length argument `0xE40`. The following descriptor-construction
loop begins at `8001982C` after the loader reports success.

The extracted 4096-byte `LIST.CD` has SHA-256
`75e3e1dbe883903d56670cb9b37338cb80d85f2ed598e9a115d0e852628d73f6`.
Its first little-endian pair is `(49, 0)`, followed by 49 pairs of
archive-relative sector offsets and byte lengths. Every one agrees with
the corresponding MAIN extraction manifest entry after multiplying the
sector offset by `0x800`.

The outer loop visits eight archive file records at stride `0x30`, starting
at descriptor `80062C68` (filename `\\MAIN.CD;1` at `80062C54`). It skips
each eight-byte count header, then advances the destination descriptor by
eight bytes per member. The flat member index starts at zero. For each
nonzero sector offset it adds the archive's location converted by
`80043B1C`, converts the sum back through `80043A18`, and stores the byte
length at descriptor offset 4. The conversion routines decode/encode the
three BCD minute/second/frame bytes with the 150-sector adjustment.

Thus, after this initialization and absent a later descriptor rewrite:

| Descriptor | MAIN member | Relative sector | Byte length | Loader caller |
| --- | --- | --- | --- | --- |
| `800AE838` | 0001 | 12 | 106496 | `80010C7C` |
| `800AE848` | 0003 | 99 | 512000 | `80010ED4` |
| `800AE870` | 0008 | 1012 | 104448 | `800112C8` |
| `800AE880` | 0010 | 1388 | 497664 | `800110CC` |
| `800AE888` | 0011 | 1631 | 155648 | `80011144` |

The eight count headers are 49, 86, 43, 140, 31, 30, 39 and 29; walking
their records consumes `0xE38` bytes and yields 447 flat descriptors.
These counts fit the requested `0xE40` length; they are not all MAIN
members. The first archive's 49 entries establish the mappings above.

`800110CC` passes `800AE880` and the destination from `80072C70` to
`80019A24` at `80011100`. Together with the type-1 copy path, this provides
a static loader-backed path from MAIN member0010 body offset `0x20000`
to `800CEDF8`, and hence its wrapper at offset `0x23804` to `800D25FC`.
The 290 instruction words spanning initialization, the member0010/0011
callers, and both location conversions were checked against the pinned EXE.

This does not establish that `800110CC` executes before startup's guarded
indirect call, that descriptors remain unchanged, or that callback cell
`8011DB24` is initialized. Those state-ordering questions and live execution
remain unverified; no candidate was promoted on this evidence alone.

## Callback writers and startup guard

The startup guard is byte `800B9A13`, exactly `800AF630 + 0xA3E3`.
At `800103D4` startup reads it; a nonzero value branches to `80010644`,
which leads to the scratch-stack call of `800D25FC`. The zero branch
instead calls dispatcher `80010B40` at `800105E4`. That dispatcher indexes
the initial table at `800629F4` using halfword `800AF630 + 0xA3AE`.
Table indices 10 and 15 select `800110CC`, the member0010 loader.
On successful load it calls `80011778`, which increments that selector
and clears several adjacent halfwords. This is static control flow, not
evidence that either index was reached in a running game.

Member0012 SHA-256:
`7bf686da174e6bd98cab8e742af629ba4168cff76e3be4e367cb69f783af070c`.
Using its existing configured mapping (body offset `0x28000` at `80128158`),
the following routines store to `8011DB24`:

| Writer | Store member offset | Callback target | Guard setup |
| --- | --- | --- | --- |
| `8013E558` | `0x3E41C` | `80141788` | Calls `800D24A0(2)` first |
| `8013E588` | `0x3E47C` | `80141874` | Writes guard 2 if `800D0EC4()` returns zero |
| `8013E67C` | `0x3E540` | `8013E5E8` | Calls `800D24A0(0)` first |
| `8017C008` | `0x7BF0C` | `8017C080` | Writes guard 5 |

All four stores use `lui $at,0x8012` followed by
`sw $v0,-0x24DC($at)`. The 78 instruction words spanning the four complete
writer routines agree with the extracted member bytes. This corrects the
earlier inference that only indirect or copied initialization remained.

In member0010, under the loader-backed mapping, `800D24A0` saves its
argument in `$s0` in the delay slot at `800D24B0`. At `800D2530` it computes
`$s0 + 1`; `800D254C/800D2550` write that value's low byte to `800B9A13`.
The two calls above therefore set guard values 3 and 1 respectively before
their caller installs its callback. These observations use member offsets
`0x236A8` through `0x23758`, not a fabricated native initialization state.

The guard and callback updates are separate stores, so this is not a claim
of atomicity or a complete interrupt-ordering proof. Caller reachability,
member0012 residency, descriptor mutations, and the live ordering of guard
enablement versus the first scratch callback remain to be established.
The next investigation should trace these writer callers and the selector
transition out of the member0010 load, rather than keep searching for a
missing callback store.

## Executable-to-writer path and C recovery

Startup calls `80015208` at `800103C4`, before the guard read at
`800103D4`. This routine can call `801281D8` at `800152DC`, which calls
`8013E67C`; the latter runs `800D24A0(0)` and then installs callback
`8013E5E8`. Assuming these calls return normally and the required overlays
are resident, the callback store therefore precedes the guard read in the
same startup iteration. It is not necessary to assume a later frame to
explain this path.

The path is conditional on all of the following retail state checks:

- bytes `800B9A64 == 0`, `800B9A10 == 1`, `800B9A17 == 1`;
- byte `800B9A15 != 1` and byte `800B9A16 == 0`;
- callback guard `800B9A13 == 0`;
- either halfword `80078DD2` has bit `0x800` set, or word `800A5E78 == 0`.

Before calling the wrapper, `80015208` writes byte `800B9A15 = 1` through
the state view at offset `0xA3E5`. If `800B9A16` is nonzero after the first
four tests, it instead writes that same byte and returns without installing
a callback. These addresses retain numeric names: no input-button or
gameplay-mode meaning is established by this control-flow inspection.

`src/overlays/main_0012/8013e67c.c` now recovers the callback installer.
The pinned default compiler (`gcc-2.7.2-psx`, ASPSX 2.56, `-O2 -G0`)
reproduces all 12 words / 48 bytes, and the oracle reports MATCH at
`8013E67C` against member0012. Function-range SHA-256:
`709298e7b4efaa95c215861226a4fa227ee5a0882dcf1852b6fa74c2bab3e561`.
As a negative control, comparing that candidate to the sibling at
`8013E558` reports MISMATCH (10/12 words; first difference at word 3).
This distinguishes the zero argument / `8013E5E8` callback from the
sibling's argument 2 / `80141788` callback.

The registry addition is a byte-match claim for the existing member0012
target, not proof of native execution or member0010 residency. The native
port must preserve `8011DB24` as shared address-backed storage rather than
allocating a private callback cell for each translation unit.

Verification after this promotion: all **363/363** registry entries rebuilt
and matched; the Python suite passed **318 tests and 5 subtests**. The 59
instruction words of `80015208` were also checked against the pinned EXE.
These checks do not exercise the conditional path in a running game.

## Member0012 loader and dispatch ordering

An aligned scan for descriptor `800AE890` and destination-table word
`80072C74` locates their joint use inside member0010 at `800CF94C`:

1. `800CF950/800CF954` construct descriptor address `800AE890`, flat
   descriptor index 12. LIST.CD pair 13 is `(1707, 548864)`, identifying
   MAIN member0012 (the count header occupies pair zero).
2. `800CF95C/800CF960` load the destination from `80072C74`; its pinned
   EXE value is `80128158`. Arguments 2 and 3 are zero.
3. `800CF968` calls `80019A24`. A nonzero result calls `80011A3C` at
   `800CF978`; a zero result leaves the selector unchanged.
4. `80011A3C` increments halfword `800AF630 + 0xA3C0 = 800B99F0`
   and clears adjacent subordinate fields. All 40 words of that EXE routine
   were checked against the pinned executable.

Member0012's PAC walk has a type-0 chunk of length `0x27800`, then a
type-1/flag-1 chunk of length `0x5E327` at offset `0x27800`. The established
type-1 transfer excludes its first sector, so body offset `0x28000` maps
to destination `80128158`. This corroborates the existing member0012
mapping through the retail loader rather than only self-call votes.

Member0010 entry `800CEDFC`, called by executable state handler
`8001125C`, bounds-checks `800B99F0 < 20` and calls table
`800D3430[index]`. The observed entries are:

| Index | Target | Role established here |
| --- | --- | --- |
| 11 | `800CF94C` | Load member0012 |
| 12 | `80128228` | Subsequent member0012 entry |
| 13 | `800CF94C` | Load member0012 |
| 14 | `80128248` | Subsequent member0012 entry |

Thus the successful-load selector increment connects each loader state to
a member0012 entry on subsequent dispatch. Reaching index 11 or 13 and
remaining resident thereafter are not yet runtime observations.

`staging/startup/800cf94c-member12-loader.c` reproduces all 17 words /
68 bytes at `800CF94C` with the pinned default compiler. The oracle reports
MATCH against member0010 at base `800AEDF8`, function-range SHA-256
`c82750bd3aa557def2a5382e43fa0102dcbdddabebb9943f713bd287021da659`.
It remains an unpromoted draft: the registry has not yet acquired a
member0010 target, and this static path does not establish live residency.

## Post-load member0012 entry recovery

`80128228` calls `80128A28`; `80128248` calls `80128AF4`. These are
distinct handlers, not alternate names for the callback installer. The
first handler switches on halfword `800B99F6`:

| State | Action |
| --- | --- |
| 0 | Call `80011C10` |
| 1 | Call through cell `8017E354`, then call `80011C10` |
| 2 | Call through cell `8017E358`; interpret the signed low 16 bits |
| Other | Return without these calls |

For state 2, result zero leaves the state unchanged. A negative result
calls `80011B7C(0)` then writes halfword `800B9A00 = 1`; a positive result
calls `80010DE0`, whose matched implementation calls `80011818(5)`.
This specifies numeric transitions without assigning unverified menu or
gameplay meanings.

The initial member words at `8017E354` and `8017E358` point to `8017BEBC`
and `8017BEE4`, respectively. The first calls `80016714(801853F0, 8)`.
The second is not the eight-byte leaf suggested by the generated function
split: it falls through into `8017BEEC`, dispatches through table
`80181D6C` using the signed halfword at `801853F0`, and returns a value
derived from halfword `801853F2`. Its full recovery must preserve that
fallthrough. Runtime pointer mutations are not excluded by initial words.

`src/overlays/main_0012/80128a28.c` now reproduces all 51 words / 204 bytes
at `80128A28` with the pinned default compiler. Function-range SHA-256:
`f6e1921ee22b9e7a4738cf42e9bf405e597d76364f8a387ffae5dfc05ad8618d`.
The recovery uses actual function-pointer objects for the two cells; the
raw m2c output incorrectly suggested direct function declarations there.
An unsigned-result negative control compiles to 164 bytes and the oracle
rejects it for length mismatch, because the negative-result path disappears.
The staged source was restored to its signed form afterward.

This increment recovers a post-load state handler, not an observed start
menu. The second handler and its callbacks, state progression, and live
boot still require verification.

## Post-load callback and guard-5 path

`src/overlays/main_0012/8017bee4.c` now covers the full range
`[8017BEE4,8017BF38)`, not the generated eight-byte prefix. The pinned
compiler reproduces all 21 words / 84 bytes. Function-range SHA-256:
`1aac62f1c27db6dc58f55af56da053fcb612e03c8a7fc8e57f302a8a19cfaac6`.
A negative control declaring result cell `801853F2` unsigned preserves the
84-byte length but fails at word 12 (`8017BF14`, unsigned versus signed
halfword load). The signed declaration was restored before promotion.

The callback indexes function-pointer table `80181D6C` with the signed
halfword at `801853F0`, invokes that entry, and only then reads the signed
halfword at `801853F2` for its result. There is no bounds check in this
routine; the recovery does not invent one or assume all indices are valid.
Two initial table entries resolve to `8017BF38` and `8017BF70`:

- Entry 0 calls `8017C008(0, 0)` and increments halfword `801853F0`.
  The previously inspected installer writes guard `800B9A13 = 5` and
  callback cell `8011DB24 = 8017C080` before returning.
- Entry 1 calls `8017BFF8`, stores its low halfword at `801853F2`, and
  takes a cleanup path if that halfword is negative. `8017BFF8` reads the
  signed halfword at `801857E8`.

This links the post-load state-2 handler to a concrete callback installer
without assigning a UI meaning to the state. On the ordinary dispatch path,
startup's guard read at `800103D4` precedes dispatch at `800105E4`; an
installation during that dispatch does not retroactively change the branch
already taken. A subsequent iteration can select the guard-5 callback path,
provided the guard and overlay storage remain intact. This differs from
the `80015208` installation path, which occurs before the guard read.

The next unresolved steps are initialization and allowed transitions of
`801853F0`, execution of `8017C080`, and the observed runtime sequence.
No native game wiring or live-boot claim follows from these byte matches.

## Installed callback recovery and guard transition

`src/overlays/main_0012/8017c080.c` recovers the full callback range
`[8017C080,8017C0DC)`, including the generated split at `8017C088`.
All 23 words / 92 bytes match with the pinned compiler; range SHA-256:
`314c86d8660ad490619c07854488e2283d809bbd70f369721d620195542971be`.
Changing its index declaration to signed is a negative control: the length
remains 92 bytes, but word 1 at `8017C084` mismatches (`lh` versus retail
`lhu`). The production declaration and restored draft remain unsigned.

It calls `801827C0[unsigned_halfword(80115112)]`, calls the already-matched
empty routine `80141C04`, then increments halfword `80115116`. Neither a
table bound nor a guard-value-specific dispatch exists in this function.
The first six observed initial table words are:

| Index | Target |
| --- | --- |
| 0 | `8017C0DC` |
| 1 | `8017C24C` |
| 2 | `8017C268` |
| 3 | `8017C348` |
| 4 | `8017C364` |
| 5 | `800D2624` |

This is a partial table observation, not a declaration that its extent is
six. Target `8017C0DC` calls `800D24A0(5)`, `8017C120`, and `8002AF18`,
then increments halfword `80115112`. Its 17 words were checked against
member0012. The previously traced `800D24A0(argument)` writes guard
`argument + 1`, so the first callback state changes guard 5 to guard 6.
It does not replace callback cell `8011DB24`; startup tests the guard only
for nonzero, not equality with 5. A native implementation must therefore
keep the guard, callback pointer, dispatch index, and counter distinct.

The initializer's other calls and later table entries remain to be
recovered and runtime-verified. This extends the static callback path;
it is not a native boot or visible-menu acceptance result.

## Initializer layout recovery (initial mismatch)

The initial `staging/startup/8017c120-init.c` recovery of
`[8017C120,8017C24C)` had the correct 300-byte
length but matched 57/75 words. The first differing word was at `8017C188`;
the configuration store and intervening load/register ordering differ.
The retail range digest is
`c632a494620de216b2b0ec7f7f2deb1f5962b5f37b380ee1945747098aa461e1`.
No registry entry was added at that stage; the subsequent match is below.

The instructions establish these accesses:

- clear word `80185AD4`; fill eight bytes starting at `80185AE4` with FF;
- clear halfwords `801857E8`, `801857F4`, and `80115118`;
- if signed halfword `801857EC` is zero, clear `8011512C` and copy word
  `801825A0` to `80182638`; otherwise write halfword 9 and word `0x1A`;
- clear word `80115130`, and select a 32-bit address from table `801826E4`
  using signed halfword `80115126`, storing it at `80115134`;
- for six records at stride 32, read unsigned halfwords at offsets 0 and 2;
  truncate the first to a byte at `80115138 + index`, and store the second
  at `80115158 + 2*index`.

The addresses in `80115134` and table `801826E4` are PS1 words, not
host-sized pointers. The draft's casts are for the 32-bit matching compiler,
not a native resolver. Likewise the writes based on `80115130 + 0x28`
refer to shared retail storage, not a separately allocated scalar object.

Initial member words provide an important aliasing constraint: both
`801825A0` and `80182638` contain `80183F30`, while the first two address
table entries at `801826E4` and `801826E8` contain `80182624`. Thus the
configuration cell `80182638` lies inside that selected record region
(`80182624 + 0x14`). Splitting these into independent native globals would
lose the relationship. The nonzero branch deliberately stores `0x1A` in
that same cell; a pointer interpretation alone is insufficient to explain
its uses. Subsequent consumers should establish the record layout before
native storage is designed.

Simplifying the first loop and keeping address arithmetic as 32-bit words
improved the initial 312-byte draft to the present 300-byte candidate.
An alternative byte-array declaration for the configuration cell did not
improve the match and was reverted. No volatile qualifier, padding, or
instruction stub was added to force the remaining scheduling differences.

## Initializer match and configuration consumers

The remaining mismatch was resolved by writing `80182638` inside each
conditional branch instead of selecting a temporary and assigning it after
the branch. With those branch-local assignments, the pinned compiler emits
all **75/75 words (300 bytes)** exactly. The range digest remains the one
above. `src/overlays/main_0012/8017c120.c` is now registered; the staged
draft retains the matching form. No compiler flags or match criteria changed.

Inspection of later consumers reinforces the mixed-use storage constraint:
`8017C588..8017C598` compares the word with `0x1D` and `0x21`;
`8017C608..8017C614` compares it with `0x0B`; and
`8017C648..8017C668` selects `0x1D` or `0x21` and stores the chosen value.
These are numeric comparisons and assignments, not pointer dereferences.
The pointer-shaped initial word therefore cannot justify declaring this
cell as a native pointer or relocating every value stored there.

The byte match covers the initializer only. Its selected record table still
requires address resolution in the native port; independent allocations for
overlapping globals would not preserve the retail layout. Live execution
and visible-menu acceptance remain unverified.

## Matched initialization state

`src/overlays/main_0012/8017c0dc.c` now matches all 17 words / 68 bytes
of table entry zero. Range SHA-256:
`6f5f94caedf5a66d58481042f71e3396766af0bad9585d0ad36931731d701df0`.
Its call order is `800D24A0(5)`, `8017C120()`, `8002AF18()`, followed
by incrementing the halfword at `80115112`. A scalar declaration emitted
72 bytes; an array view with index zero reproduces the retail address-based
load/store and epilogue. Changing `+= 1` to postincrement alone did not fix
the scalar draft. These are matching views of one address, not permission
to allocate incompatible scalar and array globals in the native port.

`8002AF18` is already registered in `src/main/8002af18.c`: it clears words
`800760AC`, `8007609C`, `800760A0`, `800760A4`, and `800760A8` in that
order and makes no calls. An independent draft comparison also matched its
48 bytes; the existing source and registry entry were preserved. Its range
digest is `fefe3b383c04f5cc0c992fe89c9e94d4ce235c252837aeb16b0429aff4e8eac4`.

This completes matching for the state wrapper and its initializer/reset
callees, but not member0010 helper `800D24A0`. That helper's callees and
shared-storage effects still need full recovery before native execution of
the state can be claimed.

## Member0010 shared-state helper draft

`staging/startup/800d24a0-shared-init.c` recovers the complete observed
range `[800D24A0,800D25FC)` (348 bytes / 87 words). It is unpromoted:
the pinned build currently matches 43/87 words, first differing at
`800D2514`. The range digest is
`963ddd7322351a2c22f82cb0744768d882233cca9488d627c1bc4fefe7453f0d`.
The first 29 words, including the five-word copy loop, match exactly.

The helper calls `80018FC8()` then `800596F4(0)` before copying five words
from `800A6518 + (index XOR 1)*20` to `800A6518 + index*20`, where index
is the unsigned halfword at `800B9A02`. It clears halfwords `80115112`,
`8011512A`, and `80115124`, stores the mode's low halfword at `80115110`,
stores its low-byte `mode + 1` at guard `800B9A13`, and copies the index
to halfword `80115114`.

The address calculation is `index * 80000` (decimal, `0x13880`). It stores
`8007BA70 + offset` at `801151C8`, and **800856B0 + offset** at `801151CC`.
The second base was initially mistranscribed as `800756B0` in the draft;
the `lui 8008` / positive `addiu 56B0` pair and the byte comparison caught
and corrected that error. These are 32-bit PS1 addresses, not host pointers.

Finally it clears eight words in each of the adjacent regions
`[80115168,80115188)` and `[80115188,801151A8)`, interleaving one write
to the higher region then one to the lower region per index, and calls
`8002D4C8(0x14, 0)`. The draft's remaining differences include materializing
the state base and lower clear-region address, register allocation, and
instruction scheduling; matching function length alone is not a pass.

Dependency inspection also shows why replacing these calls with no-ops
would be wrong: `80018FC8` resets several bytes and two stride-`0x4C`
records, while `800596F4` contains indirect calls through `80072784` and
through offset `0x3C` of the object addressed by `80072780`. Their runtime
effects still need source-backed recovery and verification.

## Synchronization dispatch recovered

`src/main/800596f4.c` now matches all 27 words / 108 bytes of the helper's
`800596F4` dependency. Range SHA-256:
`d75f69838ec8a9987d918a6d087471ab9467d90e9d85509986c2f6d95f6e41fb`.
The pinned EXE string at `80074140` is `DrawSync(%d)...` followed by a
newline. Its initial byte `8007278A` is zero; values at least two enable
the diagnostic call through `80072784`, initially `8005C604`.

The main dispatch pointer `80072780` initially contains `80072740`, whose
slot at offset `0x3C` contains `8005BED8`. The wrapper passes its mode to
that slot and preserves the returned result. An offset-`0x38` negative
control has the same length but fails at word 18 (`8005973C`); the correct
offset was restored before promotion. Its C view is for the 32-bit matching
compiler, not a layout to read directly into native function pointers.

The target at `8005BED8` is an unlabelled prologue inside the generated
assembly, not evidence that the call enters the preceding function. For
mode zero it waits on queue indices at `8007288C/80072890` and status
reads through pointer cells `80072868` and `8007285C`, with an error path
returning -1. Therefore substituting an empty function for this dependency
would discard observable synchronization and result behavior. The default
targets are static EXE observations; runtime table replacement and parity
with the native graphics implementation remain unverified.

## Default DrawSync target matched

`src/main/8005bed8.c` now matches the complete unlabelled entry range
`[8005BED8,8005C020)`: **82 words / 328 bytes**, range SHA-256
`26937f82d25fe990aabf33a39a6e5f68c20cec2fce94c5363473844c4724e089`.
The first draft matched 79/82 words; reversing the final zero/nonzero
return condition reproduced the retail branch direction and delay-slot
value without changing the predicate's meaning.

The recovered behavior is:

| Mode | Work | Return |
| --- | --- | --- |
| Zero | Set up timeout state; drain unequal queue indices; wait for hardware readiness | 0 when ready; -1 on either timeout-check failure |
| Nonzero | Snapshot `(queue_write - queue_read) & 0x3F`; call queue service if nonzero; inspect hardware | Snapshot count, or 1 if hardware is busy while snapshot is zero |

The polling result uses the count computed **before** its queue-service
call, not a newly sampled count. The two MMIO pointer cells initially
contain `1F8010A8` (`80072868`) and `1F801814` (`8007285C`), verified in
the pinned EXE. Busy means bit `0x01000000` is set in the first register,
or bit `0x04000000` is clear in the second. The reads short-circuit in
that order and remain volatile in the C recovery.

The already-matched timeout setup `8005C020` stores
`8004239C(-1) + 0xF0` at `800728A0` and clears `800728A4`. The queue
service `8005BA90` and timeout/error handler `8005C054` are still required
dependencies; matching the outer wait loop is not proof of their behavior
or of equivalent synchronization in the native backend. No retail MMIO
address is wired directly into the native executable by this promotion.

## Timeout recovery draft and reset sequence

The initial `staging/startup/8005c054-timeout.c` draft covered
`[8005C054,8005C1C0)` (364 retail bytes). Its first build was 360 bytes;
the oracle rejects the length mismatch. The missing address-materialization
instruction is around the second diagnostic's load at `8005C0F4` onward.
Array and byte-array views of that word did not improve the result and
were reverted. Retail range digest:
`91d7706ee9c0cf83c02411628ca3c4c3947407b4a886f4426a02c140381f47c1`.

The timeout triggers if signed deadline `800728A0` is less than the value
returned by `8004239C(-1)`, or if the **old** signed counter `800728A4`
exceeds `0xF0000`. The counter is incremented only when the deadline test
does not already trigger. Equality with either threshold does not trigger
that test; these strict comparisons must not become `>=` in a native port.

On failure it emits two diagnostics using format strings at `80074238`
and `8007426C`; the first identifies a GPU timeout and reports queue,
status, channel-control, and DMA-address values. There is an observable
status read before the argument reads, retained explicitly in the draft.
The symbol named `func_80072880` is loaded as a data word here, not called.

The recovery then calls `800426FC(0)`, saves its return at `8007289C`,
clears queue word `80072890`, and copies its reloaded value to `8007288C`.
The instruction order clears/reloads `80072890` before storing the saved
return. It performs these MMIO operations in order:

| Pointer cell | Initial destination | Operation |
| --- | --- | --- |
| `80072868` | `1F8010A8` | Write `0x401` |
| `80072878` | `1F8010F0` | Read-modify-write OR `0x800` |
| `8007285C` | `1F801814` | Write `0x02000000`, then `0x01000000` |

The diagnostic's DMA-address pointer `80072860` initially contains
`1F8010A0`. All these initial destinations and both diagnostic strings
were read from the pinned EXE. After the reset sequence, it calls
`800426FC(saved_value)` and returns -1; the no-timeout path returns zero.
This is more than a deadline predicate and cannot be replaced by a simple
native timer without preserving the queue/reset behavior. No production
source or registry entry was added from this incomplete draft.

## Timeout register exchange matched

`src/main/800426fc.c` now matches all seven words / 28 bytes of the
timeout path's register exchange. Range SHA-256:
`5927d5aee12979ca2ae2f546976e64768c8c9b31b849b56f30bad2d4905112be`.
The pinned EXE pointer at `8006CB8C` contains `1F801074`, the interrupt-mask
register address. The routine reads its unsigned halfword, writes the low
halfword of its argument, and returns the old value zero-extended.
The recovery retains volatile read-before-write ordering.

Together with the timeout handler's calls, this identifies the sequence
as saving and clearing the interrupt mask before queue/hardware reset,
then restoring the saved mask afterward. It does not disable interrupts
through a CPU-status instruction; a native mutex or empty function cannot
be assumed equivalent without a platform-level contract.

The diagnostic-state investigation also found the queue service writing
words `80072880` and `80072884` from fields at `800C5514 + index*96` and
`800C5518 + index*96`. In particular `func_80072880` is a misleading
generated name for a data word. A trial callback-member view for `8007287C`
did not fix the timeout draft's four-byte mismatch and was reverted; its
source remains unpromoted. The register-exchange match is the only new
registry entry in this increment.

## Timeout handler matched

The subsequent recovery in `src/main/8005c054.c` matches all 91 words /
364 retail bytes, with the range digest recorded above. Declaring the
diagnostic callback word as `extern volatile u32 D_8007287C[]` and reading
`D_8007287C[0]` reproduces the missing address-materialization instruction.
The earlier nonvolatile array and callback-member attempts remain failed
experiments, not the current status. This declaration reproduces the retail
access sequence; it does not establish the original source type.

The queue service supports the diagnostic-word interpretation: after
calling the function loaded from `800C5510 + index*96`, it reloads that
entry's function word and stores it at `8007287C` (`8005BC14`). The next
two diagnostic words come from the associated argument fields, as noted
above. This is static instruction evidence, not an observed queue run.

The timeout handler is now registered. Fresh verification passes for
372/372 registry entries and 318 Python tests plus five subtests. Queue
service `8005BA90` remains unrecovered; diagnostic output and native
synchronization still require their own implementation and verification.
Neither this byte match nor the test suite proves retail runtime parity,
native game boot, or start-menu launch.

## DrawSync queue service matched

`src/main/8005ba90.c` recovers `[8005BA90,8005BD7C)`: all 187 words /
748 bytes match the pinned EXE, and the assembly listing's 187 instruction
words were independently compared with that EXE. Range SHA-256:
`33b6d8aa484d6916d03a1b84816627f7bb033e995821c92f2b6244696945624d`.

The matching queue view is a 96-byte record starting at `800C5510`, with
callback at offset zero and two argument words at offsets four and eight.
The remaining 84 bytes are opaque here. The queue indices and record
fields are volatile in the recovery. A nonvolatile record produced the
same 748-byte size but only 173/187 matching instructions, starting at
`8005BB7C`; changing the field observations recovered the callback-load
schedule. Earlier separate-array views produced 760/772-byte failures.
These are matching declarations, not proof of original source types, and
the pointer-bearing record must not be overlaid on retail bytes using a
64-bit native layout.

On entry, channel-control bit `0x01000000` immediately returns one without
touching the interrupt mask. Otherwise the service saves/clears that mask
through `800426FC(0)` and processes entries while indices differ and the
channel remains idle. For the last queued entry, absence of completion
callback `80072794` causes a call to `800425E0(2, 0)` before submission.
The precise behavior of that callee still needs recovery.

Each submission waits for GPU-status bit `0x04000000` without calling the
timeout predicate inside that wait. It separately reloads the index for
the callback and each argument, invokes the callback, then reloads the
index/record fields again to update the three diagnostic words. It advances
the read index modulo 64 only afterward. Caching a single index or record
across the call would discard observable retail reads.

After restoring the saved interrupt mask, an empty, hardware-idle queue
with nonzero `80072790` and nonnull callback `80072794` clears that flag
and invokes the completion callback. The return count is sampled after
this callback, unlike the outer nonblocking DrawSync count snapshot.
The queue service is registered, but native synchronization, actual
retail queue execution, and game boot remain unverified.

Fresh regression results after promotion: 373/373 registered matches
(17,920 bytes), 318 Python tests and five subtests passed, and
`git diff --check` passed. The next dependency, `800425E0`, is a 48-byte
indirect wrapper loading the function at offset four of the object pointed
to by `8006CB84`; resolving that table target is required before assigning
semantics to its `(2, 0)` call.

## Queue pre-submission dispatch and initialized target

`src/main/800425e0.c` now matches all 12 words / 48 bytes of the indirect
dispatch wrapper. Range SHA-256:
`4366b679650ae7deb7d03eaec22aa25be388102117ed041a679271ee54999ce5`.
It preserves the slot call and returned word rather than hardwiring an
implementation. Its callback argument is a 32-bit address word; it is not
a native pointer contract.

In the pinned EXE, `8006CB84` points to `8006CB64`, but the dispatched slot
at `8006CB68` is initially zero. Initializer `80042718` calls `80042DB8`
and stores its return into table offset four at `800427D0`.
`80042DB8` clears eight words at `8006CBC4` through `80043034`, clears the
register addressed by `8006CBC0`, calls `800425B0(3, 80042E08)`, and returns
`80042F8C`. This establishes the installed target conditional on that
initialization path, not proof that startup has executed it.

The full target is `[80042F8C,80043034)`, 168 bytes. Generated labels split
it at `80042FF0` and `80043004`; the first split even separates a jump
from its delay slot. It reads the old callback word at
`8006CBC4 + channel*4` and returns it. If the new word equals the old,
there is no table or register write. Otherwise it stores the new callback
and updates the register addressed by `8006CBC0` (initially `1F8010F4`):

- Nonzero callback: `(old_register & 0xFFFFFF) | (1 << (channel+16)) | 0x800000`.
- Zero callback: `((old_register & 0xFFFFFF) | 0x800000) & ~(1 << (channel+16))`.

Thus the queue's `(2, 0)` clears callback slot two and bit 18 only when
the old slot was nonzero. It is not an unconditional hardware write.
All 55 initializer words, 20 installer words, and 42 target words were
compared with the pinned executable. Runtime initialization and register
effects remain unobserved.

`staging/startup/80042f8c-channel-callback.c` remains an unpromoted draft:
both unsigned and signed word views compile to 164 rather than 168 bytes.
The oracle rejects that length mismatch. Only the wrapper was registered
in this increment; no native callback or hardware emulation was added.

Fresh verification: 374/374 registered entries (17,968 bytes), 318 Python
tests and five subtests passed; `git diff --check` passed. Native game
boot and start-menu launch remain unverified.

## Callback installer matched; setter still unresolved

`src/main/80042db8.c` now matches `[80042DB8,80042E08)`, all 20 words /
80 bytes. Range SHA-256:
`6872300c2d5aee5957eceb28bcdd05bfd00fb72629bd2686949177f224e23147`.
It preserves the initializer sequence described above and returns the
setter address for the caller to install. No direct native function-table
substitution was introduced. Its callees and the installed setter still
need independent recovery/verification.

The setter's pointer-typed callback view, explicit slot-pointer view,
nonvolatile register view, and signed/unsigned mask trials did not resolve
the 164-versus-168-byte mismatch. Commuting the nonzero branch's OR operands
produced 168 bytes but only 11/42 matching words, beginning at the entry;
that form was not retained. The current ignored draft retains explicit
volatile MMIO and an unsigned left-shift operand in the nonzero branch,
but remains rejected by the length oracle and unregistered. No padding,
compiler-setting changes, or extra register writes were used to force size.

## Installed DMA handler: bounded recovery evidence

The installer registers `80042E08`, whose complete range is
`[80042E08,80042F8C)`: 97 words / 388 bytes. All instruction words were
compared with the hash-pinned EXE. Range SHA-256:
`5488901836814ec7c85658db64f5d82c91f92e32af5b134b9bb4ce17af75c9a9`.

The handler samples `(*D_8006CBC0 >> 24) & 0x7F`, then walks the sampled
bits from channel zero upward, bounded to seven channels. Before invoking
a nonnull callback in `8006CBC4 + channel*4`, it reads the control register
and writes `old_value & ((1 << (channel+24)) | 0xFFFFFF)`. It advances the
callback slot and shifts the sampled bits, then resamples the register
after the pass and repeats while pending bits remain. A separate arithmetic
check of all 128 initial masks confirmed ascending flagged-channel
enumeration; it did not simulate callback mutations or MMIO effects.

The register write is an observed bus value, not proof that a native
ordinary-memory AND assignment models the device. In particular, device
acknowledgment semantics must be established independently before using
this loop in a native backend. No host backing word was added here.

After draining, the error path tests whether the register's high byte is
exactly `0x80`, or whether a separate read has bit `0x8000` set. It prints
the retail string `DMA bus error: code=%08x\n`, then seven diagnostics
using `MADR[%d]=%08x\n`. The second diagnostic reads words through
`8006CBE4` with a 16-byte stride; that pointer initially contains
`1F801080`. Both strings and the pointer were read from the pinned EXE.

The ignored draft `staging/startup/80042e08-channel-handler.c` is not
registered: natural nested loops compiled to 380 bytes, a goto-shaped
trial to 372, and the current conditional do-loop to 384, all rejected
against 388. Disassembly shows differences in loop rotation and allocation
of the callback-table base, not an absent retail operation that can safely
be replaced by padding. This remains a recovery draft, not runtime proof.

A separate setter trial added branch-local returns and grew to 180 bytes
instead of 168; those returns were reverted. No production source or
registry entry changed in this investigation, and the already-passing
production test suite was not rerun on unchanged code.

## Installed DMA handler matched

The next recovery in `src/main/80042e08.c` matches all 97 words / 388 bytes
of the handler, with the range digest recorded above. Direct array indexing
`D_8006CBC4[channel]` lets the compiler derive the retail slot pointer and
hoist its base; manually maintaining the pointer had prevented that match.
Moving the diagnostic counter initialization after the first diagnostic
call yielded 95/97 matching words. Incrementing the channel before shifting
the sampled pending bits then recovered the exact final two instructions
at `80042EB0` and `80042EB4`. The retained source has ordinary nested loops,
no goto scaffolding, padding, or compiler-setting changes.

The earlier 372/380/384-byte attempts above are historical failures, not
the current handler status. Only this now-matching handler is promoted;
the callback setter `80042F8C` remains unresolved. MMIO acknowledgment,
interrupt delivery, callback mutation timing, and native boot still need
runtime evidence. Matching the emitted bus writes does not implement the
device behind those registers.

## Startup reaches the initialization wrapper: static chain

The startup call at `800101DC` targets `80018918`, which calls
`80042580` at `8001896C`. That 48-byte wrapper dispatches through offset
`0x0C` of the object pointed to by `8006CB84`. In the pinned EXE that
object is `8006CB64`, and its offset-`0x0C` word is `80042718`, the
initializer traced above. The initial halfword guard `8006BAFC` is zero.
The startup call and delay slot, all 36 words of `80018918`, all 12 words
of the wrapper, the table pointer/slot, and initial guard were compared
with the pinned executable. Together with the previously checked
initializer, this connects startup to installation of `80042F8C` and
registration of `80042E08`, conditional on normal control flow and the
observed initial data. It is not evidence that startup actually ran.

A further setter pass tried commuted inner OR operands, unsigned channel
type, PS1 signed/unsigned-long register views, and a volatile callback
table; these still produced 164 bytes. Separating control and enable
temporaries produced 168 bytes but only 11/42 matching words; disassembly
showed distinct stores but different register allocation/scheduling. Those
trials were reverted to the simple signed-word draft with volatile MMIO,
freshly reconfirmed as 164 bytes and rejected by the 168-byte oracle.
No production source or registry entry changed in this pass. Native
startup and device behavior remain unverified.

## Startup record initializer matched

`src/main/80018918.c` now matches all 36 words / 144 bytes of
`[80018918,800189A8)`. Range SHA-256:
`80c9d32fcd9f764840439578ee3d89922f25831fdaa73f20ebc329e3fe9a81da`.
It clears `800AE610`, calls `80016714` for each of two 76-byte records,
and writes eight to each record's offset-four word. The record bases are
`80078D98` and `80078DE4`. Only the observed layout is named in the source;
the other fields remain opaque.

After the two-record loop, it calls `80018FC8`, `80042580`,
`8005F0C8(80078DA0, 80078DEC)`, then `8005D0F8`, in that order. The two
arguments to `8005F0C8` are offset-eight aliases into the same records.
Native bindings must preserve this overlap, not allocate unrelated globals
for each generated symbol. The call to `80042580` preserves the startup
initialization path traced above; this match does not recover its callees.

An indexed draft with field-address expressions compiled to 168 bytes.
The pointer-walking loop and explicit alias symbol produced 144 bytes with
31/36 matching words. Naming the loop's initial state value before pointer
setup reproduced the five prologue instructions, reaching 36/36. An
explicit two-element array declaration was separately rebuilt and retained
with the same exact match. No padding or compiler-setting changes were used.

Fresh verification: 377/377 registry entries (18,580 bytes), 318 Python
tests and five subtests passed, and `git diff --check` passed. Retail
runtime execution, native boot, and start-menu launch remain unverified.

## Shared startup/overlay record reset matched

`src/main/80018fc8.c` now matches `[80018FC8,80019018)`, all 20 words /
80 bytes. Range SHA-256:
`f6f90fa86f3ca1216dd6585de81f794231cdfabb540d5c59d0af1664f73d8d3b`.
This is the reset called by the newly matched startup routine and by the
still-unmatched member-0010 shared initializer `800D24A0`.

It writes zero to bytes `800747B9` and `800747B8`, writes `0x40` to
`80062BBC`, then zero to `80062BBD`. In each of the two 76-byte records
starting at `80078D98`, it clears offset `0x4B` before offset `0x4A`.
It has no calls and does not clear the other record fields. This partial
view and the initializer's record view refer to the same storage.

The first pointer-loop draft matched 16/20 words: the compiler rebased the
pointer to offset `0x4A` and advanced the counter before the stores. A
volatile field view fixed the base but left three scheduling differences.
Using the `for` update clause instead of an explicit do-loop counter
increment recovered all 20 words. Indexed and volatile chained-assignment
trials were rejected at 92 and 84 bytes, respectively. The retained source
keeps separate byte writes; its qualifiers are matching declarations, not
proof of the original source types or native atomicity.

## Startup buffer/state binding: retail trace and draft

The next startup callee `8005F0C8` spans `[8005F0C8,8005F228)`, 352 bytes.
All 88 instruction words were checked against the pinned EXE. Range SHA-256:
`687bb5771aacbd640533d0fad9c90d2e7797b7e3db3efbda0a60ff124e7907bf`.
It clears words `80072994` and `800729A8`, calls `8005F728`, installs the
following address words, and binds `80072990` to the state block at
`80078A48`:

| Cell | Installed address |
| --- | --- |
| `8007295C` | `8005F290` |
| `80072960` | `8005F228` |
| `80072964` | `8005F394` |
| `80072968` | `8005F450` |
| `8007296C` | `8005F6CC` |
| `80072970` | `8005F704` |
| `80072980` | `8005F384` |

It passes `80078A48` and `0x1E0` to `8005C2C8`, whose nonnull,
positive-length path writes zero bytes. The two state blocks are 240 bytes
each, at `80078A48` and `80078B38`. Each offset-`0x30` word stores its
corresponding caller buffer. With startup's arguments, these are the
existing record aliases `80078DA0` and `80078DEC`, not new allocations.
The loop clears state word `0x0C`, stores a self-pointer at `0x10`, and
writes `FF 00` through the buffer pointer. It reloads that pointer between
the two byte writes. Bytes `[0x5D,0x63)` in each state receive `FF`.

State pointer fields `0x3C` and `0x40` receive work areas based at
`800789B8` and `80078A00`, respectively. Those work pointers advance by
`0x23` per state, while the state pointer advances by `0xF0`. These
different strides must not be conflated in native bindings. Other state
bytes remain unnamed in the draft.

The helper `8005F728` installs three further words: `80072974=8005F75C`,
`80072978=8005FB70`, and `8007297C=8005F830`. Its 13 instruction words were
also checked against the EXE. `func_80072974` is a misleading generated
name for an address-storage cell, not a directly called function.

Finally, `8005F0C8` calls `8005D6A0` and sets `80072994` to one. The
callee's 11 checked instructions only install addresses `8005D734` and
`8005D6CC` at `8007898C` and `80078990`, and clear `80078988`/`80078994`.
It does not consume the argument registers. The decompiler's apparent
four-argument call was residual register state, not a recovered interface.

`staging/startup/8005f0c8-buffer-init.c` is an unregistered draft. It builds
to 364 instead of 352 bytes; disassembly differs in saved-register
allocation, absolute versus state-relative stores, and epilogue scheduling.
Numeric argument and scalar base-symbol trials did not improve it and
were reverted. No native buffers or dispatch tables were installed, and
the matching production sources and their tests were unchanged this pass.

## Buffer binding compiler/scheduling investigation

Separating the buffer initializer's base and moving state pointers still
produces 364 bytes. Moving pointer initialization earlier and a `register`
hint did not improve it. A volatile state view produced 356 bytes but
changed the observed loop addressing/read schedule; it was reverted.
The current draft retains ordinary state fields and separate base/moving
pointers, not a promoted implementation.

The subordinate initializer draft
`staging/startup/8005f728-callback-init.c` exposes a smaller scheduling
question. Retail `[8005F728,8005F75C)` is 52 bytes: three address stores,
with the last store in the return delay slot. The default pinned
GCC 2.7.2 / ASPSX 2.56 build emits 56 bytes, placing the last store before
the return and leaving a `nop` delay slot. The first 11 words agree; the
tail does not. This is not grounds for deleting the extra instruction.

Local pinned-pair probes, without modifying build tooling or registry
settings, gave these results for the same void-return draft at `-O2 -G0`:

| Compiler / assembler | Candidate | Oracle result |
| --- | --- | --- |
| GCC 2.6.0 / ASPSX 2.34 | 56 bytes | Length mismatch |
| GCC 2.7.2 / ASPSX 2.56 | 56 bytes | Length mismatch |
| GCC 2.7.2 CDK / ASPSX 2.67 | 52 bytes | 1/13 words |
| GCC 2.8.0 / ASPSX 2.77 | 52 bytes | 1/13 words |
| GCC 2.8.1 / ASPSX 2.79 | 52 bytes | 1/13 words |
| GCC 2.91.66 / ASPSX 2.81 | 52 bytes | 3/13 words |

For example, the CDK candidate materializes each destination's high half
in `v1` before materializing the source address, while retail uses the
assembler temporary after the source-address pair. Correct length alone
therefore does not select that toolchain. A returned-word trial under the
default pair also remained 56 bytes; its `-O0` build was 88 bytes and
`-O1`/`-O3` builds were 56. That trial was reverted. These limited probes
do not identify the original compiler/assembler or establish a build-tool
bug. Both drafts remain unregistered; no production code changed.

## Callback initializer: intermediate assembly diagnosis

A volatile scalar view of the three callback cells also emits 56 bytes
and was reverted. Inspection of the compiler's intermediate assembly
located the tail difference: it emits the final symbolic `sw`, then
`j $31`, in reorder mode without a supplied delay-slot instruction.
The local assembler adapter forces noreorder output for functions and
explicitly appends a `nop` to jumps encountered while its input state is
reorder (`tools/maspsx/maspsx/__init__.py`, branch/jump handling). Thus the
extra instruction is accounted for by the current pipeline, not linker
alignment or extraction of a following function.

An isolated in-process command-construction probe added
`-fdelayed-branch` without changing build-tool files, source instructions,
or registry settings. The compiler's emitted option comment confirms the
flag was received, but the output still had 56 bytes and failed the oracle.
A second probe requested `-mno-gas` plus delayed scheduling after the normal
flags; this compiler rejected `no-gas` with exit code 33, and no candidate
was produced. Neither probe establishes the retail build flags or a defect
in the assembler adapter. No return instruction or store was manually
rescheduled, and no production verification claim was advanced.

Fresh verification after promotion: 378/378 registered entries (18,660
bytes), 318 Python tests and five subtests passed; `git diff --check`
passed. Native boot and start-menu launch remain unverified.

Fresh verification after handler promotion: 376/376 registered entries
(18,436 bytes), 318 Python tests and five subtests passed; `git diff --check`
passed. Native game boot and start-menu launch remain unverified.

After installer promotion: 375/375 registered entries re-verified (18,048
bytes), 318 Python tests and five subtests passed, and `git diff --check`
passed. These checks do not establish native boot or start-menu launch.
