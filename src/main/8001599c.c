/* Main-exec range [8001599C,800159C0) from the SLUS executable.
 * SHA256(span)=6e9c7e2cff5013e98955a64c048e968156685fcda55e871c5e7a9bc8d79a3172.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

/* HAND MODEL from m2c draft of func_8001599C (main.s): NOT
 * verified against retail. C89-gated only; promotion requires an
 * oracle MATCH. Three-word copy (lw/sw x3). NOTE: func_800159C0 disassembles identically; shared body. Body is m2c's verbatim with types
 * filled in. */

struct data_8001599C {
    s32 unk0;
    s32 unk4;
    s32 unk8;
};

void func_8001599C(struct data_8001599C *arg0, struct data_8001599C *arg1) {
    arg1->unk0 = (s32) arg0->unk0;
    arg1->unk4 = (s32) arg0->unk4;
    arg1->unk8 = (s32) arg0->unk8;
}

