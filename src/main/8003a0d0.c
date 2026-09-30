/* Main-exec range [8003A0D0,8003A0E4) from the SLUS executable.
 * SHA256(span)=5fdb49185fe77e9f7f02df5a25285270fb8645e06d5ba006a755578c6a757057.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

void func_8003A0D0(s32 *arg0) {
    *arg0 += 1;
}
