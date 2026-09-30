/* Main-exec range [80031D70,80031DEC) from the SLUS executable.
 * SHA256(span)=cb875c21a95eea50d3f46eb5ce48d77daf4675d72a0ed7cdff014410b95a609e.
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
typedef struct { u16 flags; u8 pad[8]; u8 unkA; u8 rest[0x54-11]; } T;
extern T D_800A46E8[];
void func_8003350C();
void func_80031D70(void) {
    s32 i;
    T *p = D_800A46E8;
    for (i = 0; i < 8; i++, p++) {
        if ((p->flags & 0x3F) == 1 && p->unkA)
            func_8003350C(i, 0);
    }
}
