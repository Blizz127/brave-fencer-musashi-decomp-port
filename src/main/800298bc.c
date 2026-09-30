/* Main-exec range [800298BC,8002992C) from the SLUS executable.
 * SHA256(span)=dd81f10c680ea828aea98b7387ea46d18708960e743519760bbbce0e162aa342.
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
typedef struct { u32 w[9]; } T_800298BC;
extern T_800298BC D_80079204;
void func_8002992C(s32);
void func_800298BC(T_800298BC *dst) {
    func_8002992C(1);
    *dst = D_80079204;
}
