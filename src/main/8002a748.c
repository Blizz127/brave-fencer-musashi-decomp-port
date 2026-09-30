/* Main-exec range [8002A748,8002A76C) from the SLUS executable.
 * SHA256(span)=4ee53099a257aab8d4edb6f0b30109b6b2a0e072e179fd129edb6dfbfca6c72f.
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
extern s32 D_80078EFC;
typedef struct { u16 v; u8 pad[14]; } T;
extern T D_80063900[];
u16 func_8002A748(void) {
    return D_80063900[D_80078EFC].v;
}
