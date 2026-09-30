/* Main-exec range [80013228,80013294) from the SLUS executable.
 * SHA256(span)=dcad9aa070381ffefb3953a65900a90363052888fc66ad7f29d7fd8deaa984ba.
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
typedef struct { s32 x, y, z; } VEC;
void func_80049324();
s32 func_80013228(VEC *a, VEC *b) {
    VEC d;
    VEC out;
    d.x = a->x - b->x;
    d.y = a->y - b->y;
    d.z = a->z - b->z;
    func_80049324(&d, &out);
    return out.x + out.y + out.z;
}
