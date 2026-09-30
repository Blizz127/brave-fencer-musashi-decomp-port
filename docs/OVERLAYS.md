# Overlays

`SLUS_007.26` is 413,696 bytes and holds 1334 functions. Most of the game is
not in it: code and data stream from `.CD` archives into RAM above the
executable's text end at `0x80074800`. Reaching that code is what this document
covers.

## Extracting

`tools/extract_cd.py` unpacks an archive; the format is described in
[ROADMAP.md](ROADMAP.md). `MAIN.CD` yields 49 members.

```sh
python3 tools/extract_cd.py extracted/disc/files/MAIN.CD \
  --output extracted/overlays/main --manifest extracted/overlays/main.json
```

Members are named by index because the archive carries no filenames.

## Finding which members are code

An opcode-density heuristic is too weak — data hits common opcode bits too. A
much sharper test is to decode every `jal` and check where it points. Real MIPS
code sends essentially all of them into PS1 RAM; data produces mostly nonsense.

For `MAIN.CD` member `0007` (9,600 bytes): 35 `jal` instructions, **zero**
landing outside RAM, and 19 stack-frame prologues. Member `0005`, by contrast,
has 5,192 `jal` of which 5,119 are junk — clearly not code.

## Deriving a load address

**Load addresses must be observed, never copied from an external memory map.**
Splitting a dozen functions against a wrong base wastes all of that work, and a
wrong base fails silently rather than loudly.

Two independent lines of evidence fixed member `0007` at **`0x800CEDF8`**:

**1. Self-referencing calls must land on function starts.** The member contains
three `jal` targets above `0x80074800` — `0x800CF3B0`, `0x800CF408` and
`0x800CF6D0` — which can only be calls into the overlay itself. Of every load
address that would place any of them on a stack-frame prologue, only
`0x800CEDF8` places *all three* there, at offsets `0x5B8`, `0x610` and `0x8D8`.
Every rival candidate accounts for at most two.

**2. Calls out of the overlay must land on real functions.** Independently, the
member's `jal` targets *into* the static executable hit genuine function starts:
9 of 10 match functions splat detected, and the tenth, `0x8001AA98`, is a start
splat missed by merging two functions into `func_8001AA88`. So all ten are real.

The byte-exact match below then confirms `0x5B8` is genuinely a function
boundary. Combined with the constraint that the `jal` target `0x800CF3B0` must
land on a function start, the base is pinned: `0x800CF3B0 - 0x5B8 = 0x800CEDF8`.

Worth recording: an external note circulating for this game gives `0x800CDF58`
for a resident overlay, which differs from the observed value by exactly
`0xEA0` — the documented US/JP region delta for this title. That is consistent
with the note describing the Japanese release while this disc is `SLUS-00726`,
but it is a coincidence worth noting rather than evidence, and nothing here
depends on it.

## Splitting and matching

`config/overlay_main_0007.yaml` splits the member at its load address. splat
labels only what it can infer without symbols — 2 functions from the internal
`jal` targets — so a symbol file will be needed to reach the other 17 prologues.

Overlay code is not in the executable, so `match_function.py` compares against
the member instead, pinned by hash exactly as the executable is:

```sh
python3 tools/build_candidate.py src/overlays/main_0007/800cf3b0.c \
  --symbol func_800CF3B0 --link-base 0x800CF3B0 --output /tmp/ov.bin

python3 tools/match_function.py \
  --retail-file extracted/overlays/main/0007.bin \
  --base 0x800CEDF8 \
  --sha256 4d1bb156c5fb6569eec465de21c05a1bdd26e3223e8718ca2e02f3621a0184d9 \
  --vram 0x800CF3B0 --size 0x58 --candidate /tmp/ov.bin
```

## Matched

| vram | words | source | verdict |
| --- | --- | --- | --- |
| `0x800CF3B0` | 22/22 | `src/overlays/main_0007/800cf3b0.c` | MATCH |

Getting there needed one correction, and it is the recurring lesson: the first
attempt wrote the guard as an early-out on the negative condition, which emits
`beqz` where retail has `bnez` and swaps the order of the two return paths.
Branch *sense* is part of the shape being matched, not a free choice.
