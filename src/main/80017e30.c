/* Main-exec range [80017E30,80017E68) from the SLUS executable.
 * SHA256(span)=6f2c1787fdc6fd7f5e07d05f4e643bb2b312068f1f6e5ce879a7ccfa65909e75.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_80049CAC();
void func_80048EAC();
typedef struct { s32 v[8]; } Tmp;
void func_80017E30(void *a0, void *a1) {
    Tmp tmp;
    func_80049CAC(a0, &tmp);
    func_80048EAC(&tmp, a1);
}
