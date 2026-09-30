/* Main-exec range [8002AF48,8002AF60) from the SLUS executable.
 * SHA256(span)=0d200b121e990ce62bf2a78e84c5f7d935d902cc8d468b340f31d1332017c514.
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
extern u8 D_80075A50[];
void *func_8002AF48(s32 i) { return D_80075A50 + ((i + 1) << 7); }
