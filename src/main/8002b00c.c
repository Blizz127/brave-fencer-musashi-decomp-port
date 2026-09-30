/* Main-exec range [8002B00C,8002B064) from the SLUS executable.
 * SHA256(span)=47b79207fa143970ad1c4bb981b42ea9254b96db48e73f29ba122cd9d3f6da84.
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
typedef struct { u32 w[183]; } T_8002B00C;
extern T_8002B00C D_80075CC0;
void func_8002B00C(T_8002B00C *dst) { *dst = D_80075CC0; }
