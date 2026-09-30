/* Main-exec range [8002A2B0,8002A2D4) from the SLUS executable.
 * SHA256(span)=2b41cdc1048cf10fb7787613bb660957904a75418bb5a26d900d4ea3cf3eab35.
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
extern T D_800638FA[];
u16 func_8002A2B0(void) {
    return D_800638FA[D_80078EE4].v;
}
