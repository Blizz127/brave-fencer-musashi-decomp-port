/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
s32 func_8004CFEC();
typedef struct { s16 x, y, z; } V;
void func_800129CC(V *p, V *out) {
    if (p->z) {
        out->x = (func_8004CFEC(-p->y, p->z) - 0x400) & 0xFFF;
    } else {
        out->x = 0;
    }
    out->y = 0;
    if (p->x) {
        out->z = (func_8004CFEC(p->y, p->x) + 0x400) & 0xFFF;
    } else {
        out->z = 0;
    }
}
