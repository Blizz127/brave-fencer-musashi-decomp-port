/* Main-exec range [80014D68,80014D94) from the SLUS executable.
 * SHA256(span)=b5b61fd63da5aaff1593eb789583b82ce0ecd841a23e4a4d107d86cbad5faca0.
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
typedef struct { u16 v; u8 pad[0x4A]; } T_func_80014D68;
extern T_func_80014D68 D_80078DCA[];
u16 func_80014D68(u8 i) { return D_80078DCA[i].v; }
