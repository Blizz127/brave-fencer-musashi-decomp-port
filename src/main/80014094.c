/* Main-exec range [80014094,800140B8) from the SLUS executable.
 * SHA256(span)=58c0723a574364f3ef0424064256eadbdceb466da160a283f74bf222b065acdd.
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

void func_80014094(s32 *arg0, s16 *arg1) {
    arg1[0] = (s16) arg0[0];
    arg1[1] = (s16) arg0[1];
    arg1[2] = (s16) arg0[2];
}
