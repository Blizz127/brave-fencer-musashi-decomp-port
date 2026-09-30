# Asset containers

`SC01.CD` is the first archive whose contents were walked end to end. Every
statement here was derived from the archive's own bytes; see the correction at
the bottom for what an external note claimed instead.

## `SC01.CD` is a `.CD` archive, not a flat PAC file

The `.CD` extension is literal. `SC01.CD` carries the same `0x800` header that
`MAIN.CD` does and parses with the same `tools/extract_cd.py`:

```text
20,078,592 bytes, 86 members
```

78 of those 86 members begin with a `PAC\0` header. The remaining 8 begin with
`.sqv` and are not PAC containers:

| member | size | magic |
| --- | --- | --- |
| 2, 75, 78, 79, 81, 82, 83, 85 | 282,624 - 305,152 | `.sqv` |

Nothing here interprets `.sqv`. It is recorded as an observed magic so that a
future walk does not mistake it for a malformed PAC chunk.

## PAC chunk header

A PAC chunk is a 16-byte header followed by its payload, padded out to the
next `0x800` boundary:

| offset | width | meaning |
| --- | --- | --- |
| +0 | 4 | magic `PAC\0` |
| +4 | 1 | type |
| +5 | 1 | flag, observed as 0 or 1 |
| +6 | 2 | zero in all 199 chunks |
| +8 | 4 | zero in 143 of 199 chunks, otherwise a small value under 0x40 |
| +12 | 4 | total chunk length **including this header**, unpadded |
| +16 | .. | payload |

The stride to the next chunk is `+12` rounded up to `0x800`. The bytes between
the declared end and that boundary are zero.

Type byte at `+4`, over all 199 chunks:

| type | count |
| --- | --- |
| 0 | 56 |
| 1 | 64 |
| 2 | 28 |
| 3 | 28 |
| 4 | 11 |
| 7 | 12 |

The type byte is not decoded. No payload magic distinguishes the types, so
assigning them names would be invention.

## The walk closes

Walking each PAC member by that stride:

```text
199 chunks across 78 members
78 of 78 members close on their exact declared end
17,707,008 of 17,707,008 bytes covered = 100.0000%
```

Zero gaps and zero overlaps. Every chunk boundary is accounted for by the
preceding chunk's own length field, and every member ends exactly where its
`.CD` entry says it does.

## Correction

An external note held that the `u32` at `+12` is an **offset to the next
chunk**. It is not; it is a length. The two coincide whenever a payload happens
to fill its last sector, which is why the claim survived casual checking: it
holds for 56 of the 198 measurable chunks and fails for the other 142.

A flat scan of `SC01.CD` for `PAC\0` also finds all 199 headers, which makes a
flat walk look nearly right until it stops dead at member 2. That member is one
of the eight `.sqv` members, and no length field leads out of it. The archive
had to be recognised as nested before the walk could close.

This is the fourth externally sourced claim about this game to fail against the
bytes, after the overlay load address, the TMD offset base, and the earlier PAC
layout note. Derive from the data.
