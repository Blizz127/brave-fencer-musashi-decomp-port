/* Main-exec range [800132BC,80013328) from the SLUS executable.
 * SHA256(span)=f10c0baebdad460ec8dbf54574fbb7d4b374687f2afc3bf80faae71953f7a70c.
 * Word export for the native seam; the C body below keeps its
 * own oracle match claim. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
typedef struct { s16 x, y, z; } SVEC;
typedef struct { s32 x, y, z; } VEC;
void func_80049324();
s32 func_800132BC(SVEC *a, SVEC *b) {
    VEC d;
    VEC out;
    d.x = a->x - b->x;
    d.y = a->y - b->y;
    d.z = a->z - b->z;
    func_80049324(&d, &out);
    return out.x + out.y + out.z;
}
