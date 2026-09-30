/* Main-exec range [80013350,800133BC) from the SLUS executable.
 * SHA256(span)=84c5cb4bc22c9ab0256f724c960dfae40bd9c571b8cf50323adecb38b53c60f1.
 * Word export for the native seam; the C body below keeps its
 * own oracle match claim. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_80049324();
typedef struct { s32 x, y, z; } V;
s32 func_80013350(s16 *a, s16 *b) {
    V in, out;
    in.x = a[1] - b[1];
    in.y = a[3] - b[3];
    in.z = a[5] - b[5];
    func_80049324(&in, &out);
    return out.x + out.y + out.z;
}
