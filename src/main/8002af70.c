/* Main-exec range [8002AF70,8002B00C) from the SLUS executable.
 * SHA256(span)=7d6dc14356781c49a5ddf7661b34152f2ea28b0fce0f472ddc1f92f1791a1e28.
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
typedef struct { u32 w[183]; } T_8002AF70;
extern T_8002AF70 D_80075CC0;
extern s32 D_80076040;
void func_80016714();
void func_8002AF70(T_8002AF70 *src, s32 extra) {
    func_80016714(&D_80075CC0, 0x300);
    D_80075CC0 = *src;
    D_80076040 = extra;
}
