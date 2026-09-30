/* Main-exec range [80012E0C,80012E6C) from the SLUS executable.
 * SHA256(span)=76d3441be669ab4575b1d73ee17dafbf873c42a58fbd3a94f85b1733ddd22d2c.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
s32 func_80012E6C();
s32 func_80012E0C(s32 a0, s32 a1, s32 a2, s32 a3) {
    s16 tmp = 4;
    if ((s16)a0 == (s16)a1) {
        return 0;
    }
    return (s16)func_80012E6C((s16)a0, (s16)a1, (s16)a2, (s16)a3, &tmp);
}
