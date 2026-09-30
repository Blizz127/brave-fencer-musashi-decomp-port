# PROJECT GOAL AND PLAN — read this before planning any work

_Set 2026-09-17. This file records what the work is **aiming at**;
`docs/DEFINITION-OF-DONE.md` remains the authority on what "matched"
and "complete" **mean**, and the milestone docs (`docs/MILESTONE-*.md`, `docs/MENU-BOOT-CONTINUATION.md`) record where the work stands.
Nothing here loosens the Definition of Done._

---

## The goal

**A native PC port of Brave Fencer Musashi in the mould of Ship of Harkinian
(Zelda: OoT) and the Silent Hill decomp/port projects — running natively,
moddable, and ultimately re-rendered with new graphics (HD-2D is the stated
ambition).**

The decompilation is how you get there, not the destination. `DEFINITION-OF-DONE.md`
already states the two deliverables correctly ("the decompilation is the
authority, the port consumes it"). This file adds the part that was implicit:
**the port is the point, and its needs set the priority order.**

## What that means for prioritisation

The single most useful question when choosing work:

> **Does this put C behind a code path the port executes and will need to
> restyle?**

A function recovered as *assembly* satisfies byte-for-byte parity and is worth
**nothing** to the native port — you cannot execute PS1 MIPS on x86. This
repo's own progress meter already distinguishes these, and the distinction
matters more than the headline number:

```
Qualifying unique coverage: 256,348 / 806,272 bytes (31.79%)   [2026-09-17]
  declared recovery:  c 2,427 ranges / 175,500 B
                      assembly 267 ranges / 81,828 B   <-- not portable
                      mixed 10 ranges / 2,792 B
```

**~82 KB of recovered-but-assembly code is coverage that does not advance the
port.** That is not wasted work — it is verified retail behaviour and it keeps
the rebuild honest — but it must not be mistaken for progress toward a playable
native build. When a range can be recovered as C at reasonable cost, prefer C.

## Architecture: where this project stands against Ship of Harkinian

| Ship of Harkinian | This project | Notes |
|---|---|---|
| `libultraship` — SDL/GL/AL platform layer | `pc_port/` (+ `pc_port/compat/`, `spu_reverb_core`, `musashi_irq_scheduler`) | the platform layer exists and is being driven by real IRQ/GPU/CD work |
| **Fast3D** — translates N64 display lists to OpenGL | PS1 GPU primitive translation in the port's GPU backend | verify state before planning graphics work |
| OTR assets — enables HD texture packs | `extracted/`, `assets/` + the CD device integration | see `docs/ASSETS.md`, `CD-DEVICE-INTEGRATION.md` |
| zeldaret/oot decomp — **100% C** | 31.79% qualifying coverage, of which ~82 KB is assembly | ← the gap |
| *(nothing — never needed one)* | **no MIPS interpreter — good.** Keep it that way. | see below |

**A note on what this project got right.** Xenogears' port (a sibling project)
took a shortcut Musashi has not: it runs a 1,159-line MIPS interpreter over the
un-decompiled battle overlay so the game boots today. It works, but code
executing through an interpreter is a black box that **can never be re-rendered
or restyled**. If an interpreter/recompiler bridge is ever proposed here, treat
it as a debugging aid with an expiry date, never as a way to declare a system
"ported".

## Plan

### Phase A — C coverage of executed paths (the real unlock)

Grow *portable* coverage, not just coverage. Ordered by port value:

1. **The boot-to-menu path** and everything `docs/MENU-BOOT-CONTINUATION.md` /
   `docs/NATIVE-BOOT-ENTRY.md` already track — the port's stated near-term
   milestone, and the cheapest visible win.
2. **Anything currently recovered as `assembly` that sits on an executed
   path.** Re-recovering those ranges as C converts existing verified work
   into port-usable code. Query the registry for `recovery: assembly` +
   `extent: function` and rank by whether the port calls them.
3. **Rendering and input paths**, because those are what Phase B restyles.

Gate: the port's stub count and any remaining non-C bodies on the boot path
trending to zero.

### Phase B — renderer abstraction (where HD-2D actually lives)

Two things to plan for early, both learned from the sibling Xenogears port:

- **Geometry precision.** PS1 rasterisation snaps vertices to integers. A
  naive display-list translator inherits that wobble; PGXP-style recovered
  subpixel precision is what makes an HD render look intentional rather than
  upscaled. Decide this at the GPU-backend boundary, not later.
- **There is no depth buffer.** HD-2D post-processing (depth of field,
  tilt-shift, bloom) needs depth; the PS1 sorts with an ordering table, which
  gives draw *order*, not depth. Depth must be synthesised from the OT index or
  recovered from GTE transform output. **This is the largest technical unknown
  in the HD-2D goal — budget for it rather than discovering it late.**

### Phase C — asset replacement, then art

Higher-resolution textures/models through the asset layer (the OTR equivalent).
Only meaningful once A and B land.

## When to byte-match

Keep `DEFINITION-OF-DONE.md`'s bar — this project's byte-for-byte discipline and
its registry/oracle workflow (`tools/verify_registry.py`, the m2c-draft +
oracle-judge loop) are genuine assets, and the honest-metering warnings in
`docs/PROGRESS.md` are exactly right. Do not relax them.

Do, however, weigh cost. Byte parity is typically far more expensive than
correctness: on the sibling Xenogears project, five byte-exact functions in one
session took minutes to *understand* and hours to match register allocation —
roughly 90% of the effort went into byte parity alone. Where a range is on the
critical port path and matching is fighting the compiler, recovering correct C
plus a differential test is the better trade, recorded honestly as such rather
than claimed as matched.

## Measuring progress

```
python3 tools/progress.py          # registry claims (qualifying unique coverage)
python3 tools/verify_registry.py   # re-earn the byte matches
```

Report **two** numbers from now on, never one: qualifying coverage, and the
**C-only** subset of it. The first says how much of the game is understood; the
second says how much of the port can exist. Only the second predicts a playable,
restylable build.
