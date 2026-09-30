/* Main-exec range [8002A76C,8002A790) from the SLUS executable.
 * SHA256(span)=2f739a4b48d4b92ec7dd8ca552cdb560be8b70e1fdf3a5c01b7cf53cfbfde531.
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
extern s32 D_80078EFC;
typedef struct { u16 v; u8 pad[14]; } T;
extern T D_80063902[];
u16 func_8002A76C(void) {
    return D_80063902[D_80078EFC].v;
}
