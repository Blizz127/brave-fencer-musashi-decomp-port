/* Main-exec range [80015A08,80015A2C) from the SLUS executable.
 * SHA256(span)=cd5d78fdcb58b68d36088e830480dea79d8be27968a1c5b60b7b6ea5270aa054.
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

void func_80015A08(u16 *arg0, u16 *arg1) {
    arg1[0] = arg0[0];
    arg1[2] = arg0[2];
    arg1[4] = arg0[4];
}
