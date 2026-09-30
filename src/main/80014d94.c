/* Main-exec range [80014D94,80014DC0) from the SLUS executable.
 * SHA256(span)=af4cb06e73c84643d316b137441ebdad7deb06798fe318a2272f00f054a8b1cb.
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
typedef struct { u16 v; u8 pad[0x4A]; } T_func_80014D94;
extern T_func_80014D94 D_80078DD2[];
u16 func_80014D94(u8 i) { return D_80078DD2[i].v; }
