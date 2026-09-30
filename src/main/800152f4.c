/* Main-exec range [800152F4,80015310) from the SLUS executable.
 * SHA256(span)=41e3ba5dc5949480fb11bcd37a912b138a87dfca8c09d0c2830c5e654f64567b.
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

typedef struct {
    s16 x;
    s16 pad[7];
} T_800152F4;

extern T_800152F4 D_800B93D8[];

void func_800152F4(s16 i) {
    D_800B93D8[i].x = 0;
}
