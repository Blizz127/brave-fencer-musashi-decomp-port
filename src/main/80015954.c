/* Main-exec range [80015954,80015978) from the SLUS executable.
 * SHA256(span)=076934524ef110f14458759f1eaad5b5043023fa2e0427a51e141f087df51b74.
 * Word export for the native seam; the C body below keeps its
 * own oracle match claim. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

void func_80015954(u16 *arg0, u16 *arg1) {
    arg1[1] = arg0[0];
    arg1[3] = arg0[1];
    arg1[5] = arg0[2];
}
