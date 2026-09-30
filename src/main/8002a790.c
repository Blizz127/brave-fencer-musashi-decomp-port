/* Main-exec range [8002A790,8002A7B4) from the SLUS executable.
 * SHA256(span)=34ef2c99c478094924ff51223fd7c57449c0e90c5d05bf4131c03e19ab8756bf.
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
void func_8002A7B4();
typedef struct { u8 pad[0xC9]; u8 v; } T;
void func_8002A790(T *p) { func_8002A7B4(p->v); }
