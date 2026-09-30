/* Main-exec range [8002A28C,8002A2B0) from the SLUS executable.
 * SHA256(span)=5cc564d76ec73cadc0110c44b8f4df18a702f633d91acecb6b51617f9af76609.
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
extern s32 D_80078EE4;
typedef struct { u16 v; u8 pad[14]; } T;
extern T D_800638F8[];
u16 func_8002A28C(void) {
    return D_800638F8[D_80078EE4].v;
}
