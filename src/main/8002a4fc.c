/* Main-exec range [8002A4FC,8002A520) from the SLUS executable.
 * SHA256(span)=ec2fadc050398538411bcb188992c5b0103aae3d5fb980ccca78cbb51838e947.
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
extern T D_800638FE[];
u16 func_8002A4FC(void) {
    return D_800638FE[D_80078EEC].v;
}
