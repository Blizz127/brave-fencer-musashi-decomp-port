> **PROJECT GOAL:** a native PC port in the mould of Ship of Harkinian /
> the Silent Hill decomp ports, ultimately re-rendered (HD-2D).
> **Read `docs/PORT_GOAL_AND_PLAN.md` before planning any work.**
> The port is the deliverable; the decompilation is how you get there.
> Recovered *assembly* is worth nothing to the port -- you cannot run PS1 MIPS
> on x86. Report C-only executed-path coverage, not just total coverage.

# Interpreting progress

`python3 tools/progress.py` reports registered byte ranges and sums their sizes.
It also reports unique qualifying coverage: the overlap-union of complete
functions (`extent` `function`) recovered as reviewed `c` or `assembly`,
counted once per region, against the identified split-code denominator
(the per-region code sizes in `IDENTIFIED_REGION_BYTES` in `tools/progress.py`:
SLUS_007.26 `t_size`, the identified code ranges of the MAIN.CD members that
have a splat disassembly, and the SC md scene modules set up so far; the
tool prints the current total). The SC ov scene overlays (about 350 KB each,
mostly engine code shared between scenes) are reported on a separate line
against `SCENE_OVERLAY_REGION_BYTES`, never folded into the headline. Every registered SC
region is in that table or, for the small SC md members, in `IDENTIFIED_REGION_BYTES`; the
report names any SC region in neither (`NOT COUNTED`). The 30% bar is 30% of that total.
`partial`/`unclassified`/`mixed` rows and non-function extents are excluded
from that numerator, and so are rows in regions that are not yet part of the
denominator: a region's matches count only once its code size is added.
Adding a region grows the denominator, so the percentage can fall while the
registered bytes rise; compare byte counts across changes. (Member 0010 was
added on 2026-09-28 once it had a splat disassembly; before that its
registered ranges were counted against a denominator that omitted it, which
slightly overstated coverage.)

Alongside it, always reported and never alone, is the portable C-only
subset: the same overlap-union restricted to reviewed `c` functions. Only
that subset can execute natively — assembly recovery, however exactly
byte-matched, runs solely through the word-stepping CPU. The two numbers
answer different questions (how much is understood vs how much of the
port can exist) and must travel together.

Owner decision (2026-09-29, 13:40 CDT): genuinely handwritten mnemonic
assembly may be registered with `recovery: "assembly"`, its vendor origin,
and a reviewed complete-function extent after the normal byte-match and
retail-data gates. Report its unique bytes as **mnemonic assembly**, separately
from portable matched C. It remains eligible for the combined qualifying
coverage, and does not establish native execution. Raw retail word dumps are
still unclassified. A record tagged `fakematch: true` is reported as **FAKEMATCH**
and excluded from both qualifying subsets and their union. Separate C and
assembly unions can overlap; the combined total unions them once.

It reads claims from `provenance/matches.json`; it does not compile or compare
anything. Run `python3 tools/verify_registry.py` to re-earn the byte matches.
Summed sizes are not a union of covered addresses or a percentage of the game.

The old report called every entry a function rebuilt from C. That is incorrect:
for example, `src/main/800100a0.c` contains an assembly implementation of the
constructor guard. A `.c` suffix, a matching binary, or a range larger than eight
bytes does not prove C recovery, function completeness, or native portability.

Optional fields on each match record make reviewed classifications explicit:

| Field | Values | Meaning |
| --- | --- | --- |
| `recovery` | `c`, `assembly`, `mixed`, `unclassified` | Implementation producing the matched range |
| `extent` | `function`, `partial`, `unclassified` | Whether the range covers the complete identified function |

Missing fields default to `unclassified`; invalid values are refused.
Classification is a reviewed claim, not something the reporting tool verifies.
Before setting fields, inspect the exact source/configuration producing the
range and the retail control-flow boundaries. Embedded native word exports
alone do not classify the separately compiled PS1 implementation. Empty compiler
constraints alone do not turn a C implementation into assembly transcription.
An assembly implementation does not establish original handwritten provenance.
An interior label or prefix must not be called a complete function simply
because the matching harness can export it. Keep uncertain cases unclassified.

Initial reviewed entries were `80012AB0` (12 bytes, C function) and `800100A0`
(112 bytes, assembly function). Subsequent review and fresh byte comparisons
classified `800429DC` (340 bytes), `80042F8C` (168 bytes), and their dispatch
wrappers `800425B0` and `800425E0` (48 bytes each) as complete C functions
while binding their shared bodies to the native port. Other existing
entries have deliberately not been assigned bulk classifications. The same
review subsequently classified `80042CE8` (120 bytes) as a complete C function
when binding its live-slot handler to guest RAM.
The retail-observed VBlank callback `800616D0` (160 bytes) and its direct helpers
`8006291C` (108 bytes) and `80062988` (16 bytes) were subsequently recovered,
byte-verified, and registered as C functions. Their shared native bindings now
execute through the VBlank slot router, with external request/completion calls
requiring checked execution. This does not establish interrupt delivery.
Unclassified does not revoke their byte
matches; it keeps the recovery audit visibly incomplete. Instruction fragments
outside the registry, such as `80044670_prefix.c`, are not added to these totals.

The source and extent totals are independent; a C range may still be partial.
None of these figures proves native startup, an interactive menu, or full retail
parity. The historical `function_count`, `trivial_count`, and `substantive_count`
Python summary keys remain for existing callers, but mean range count and the
two size buckets only. The command-line output uses those accurate labels.

## The function ceiling: why the meter cannot reach 100%

The denominator is identified *code-segment* bytes, not
function bytes: the flat SLUS `.text`, all of member 0007, and member
0012's code chunk all embed data-in-text (jump tables, pointer tables,
globals, string/const pools) plus alignment padding. Those bytes can never
be complete functions, so they can never enter the qualifying numerator.
The reachable maximum is the splat-enumerated function total, audited
2026-09-11 with `/tmp/gap_analysis.py` (every non-function byte classified):

| Region | Denominator | Function bytes | Proven non-function remainder |
| --- | --- | --- | --- |
| main | 411648 | 341688 (1531 fns) | ~70KB: BIOS A0-call stub chains, jump tables (e.g. 6E930), globals (e.g. D_80062998), const data, zero padding. Only `jal`-evidenced starts were added (8005CE38, 80062988); no prologue pattern and no `j`/`jal` target exists anywhere else in the holes. |
| main_0007 | 9600 | 3208 (19 fns) | 0x34-byte string header, ~6.3KB data tail (pointer table at 800D134C included) with zero `jr`, zero real `j`, zero prologues, zero call targets. |
| main_0012 | 385024 | 352736 (2412 fns) | One 32260-byte tail hole `[8017E354,80186158)`: 1961 pointer-like words, 2576 zeros, game-data shorts/IDs, uniform-random opcode spread, zero prologues, zero call targets. |
| Total | 806272 | 697632 (3962 fns) | **Ceiling 86.53%.** |

Do not move the denominator to hit a target: the gap is documented
non-function bytes, and redefining the meter around them would be exactly
the inflation this file guards against. The honest end-state is every one
of the 3962 functions matched (≈86%), with the residual identified above.

Two mechanical notes from the audit: spimdisasm renders a function symbol
as `.word` data (with an `enddlabel`) when its span contains a single
invalid word, so recovered starts abutting data need exact `size:` bounds
in the symbol_addrs files (see `config/symbol_addrs.main.auto.txt`); and
three member-0007 functions open with a scheduled global-load pair above
the stack adjust, so their symbols sit at the `lui`, not the prologue
pattern (`config/symbol_addrs.main_0007.auto.txt`).
