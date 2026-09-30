/* Main-exec range [8002A9B8,8002A9DC) from the SLUS executable.
 * SHA256(span)=c5ddd9b16913fff4fc127704784d1f493567e488cdbfdf19975ef068720ad937.
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
extern s32 D_80078EF4;
typedef struct { u16 v; u8 pad[14]; } T;
extern T D_80063904[];
u16 func_8002A9B8(void) {
    return D_80063904[D_80078EF4].v;
}
