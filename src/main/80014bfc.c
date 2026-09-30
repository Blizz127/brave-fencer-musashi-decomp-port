/* Main-exec range [80014BFC,80014C28) from the SLUS executable.
 * SHA256(span)=e648f62b3f76378dac7b2fa950d2f1e5e0f2ef88bdbfe13d596e0862b21bf3c7.
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
typedef struct { u16 v; u8 pad[0x4A]; } T_func_80014BFC;
extern T_func_80014BFC D_80078DDA[];
u16 func_80014BFC(u8 i) { return D_80078DDA[i].v; }
