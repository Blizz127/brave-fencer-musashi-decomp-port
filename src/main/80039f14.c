/* Main-exec range [80039F14,80039F50) from the SLUS executable.
 * SHA256(span)=18415ba3b50f5f8e628136a2ceb8954b09e06201bd739260a1d1d123e2341933.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
typedef struct { u8 pad[0x20]; u8 u20, u21, u22; } S;
void func_80039F14(S *p, s32 idx, s32 a2) {
    p = (S *)((u8 *)p + (s16)idx * 26);
    p->u20 = a2;
    p->u22 = a2 + 1;
    p->u21 |= 2;
}
