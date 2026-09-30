/* Main-exec range [800159E4,80015A08) from the SLUS executable.
 * SHA256(span)=4d661f6397c80e07791548c21d3cb49d5390da7f0b81b0cf859fdf8422a0495d.
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

void func_800159E4(u16 *arg0, u16 *arg1) {
    arg1[1] = arg0[1];
    arg1[3] = arg0[3];
    arg1[5] = arg0[5];
}
