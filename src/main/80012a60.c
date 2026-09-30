/* Main-exec range [80012A60,80012AB0) from the SLUS executable.
 * SHA256(span)=1a8d9a530b895a160dd30b9cf9488e5089fb743d58ff583a9737aa38c44ef297.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s16 func_80012AB0(s16, s16);                        /* static */

s16 func_80012A60(s16 arg0, s16 arg1) {
    s16 var_v1;

    var_v1 = func_80012AB0(arg0, arg1);
    if (var_v1 >= 0x800) {
        var_v1 = 0xFFF - var_v1;
    }
    return var_v1;
}
