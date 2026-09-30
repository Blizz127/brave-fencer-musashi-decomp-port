/* Main-exec range [8002A4D8,8002A4FC) from the SLUS executable.
 * SHA256(span)=51e398802ce156e59676e987c10fbd70ef37073705fbf70ac2babf70c730557b.
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
extern s32 D_80078EEC;
typedef struct { u16 v; u8 pad[14]; } T;
extern T D_800638FC[];
u16 func_8002A4D8(void) {
    return D_800638FC[D_80078EEC].v;
}
