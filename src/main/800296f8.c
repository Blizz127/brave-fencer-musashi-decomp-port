/* Main-exec range [800296F8,80029774) from the SLUS executable.
 * SHA256(span)=b9cde4ce64c8a52c6a00d48ac2f909f5bb1f5e90d07f9d6e5f1d7aa622bc4f40.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
typedef struct { u32 w[9]; } T;
extern T D_80079204;
extern s32 D_800A5E5C;
void func_80029774(s32);
void func_800296F8(T *src) {
    func_80029774(1);
    D_80079204 = *src;
    D_800A5E5C = 1;
}
