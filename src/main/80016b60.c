/* Main-exec range [80016B60,80016C28) from the SLUS executable.
 * SHA256(span)=1364b8e048d4c6cdc3296d26f8b44a147795775357b56e1a219ff11d7ab9282d.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void *func_80010A08();
void func_80058EE4();
void func_80017E8C();
s32 func_8004958C();
void func_80018094();
typedef struct {
    u8 pad[8]; s32 u8;
    u8 padC[4]; s32 u10;
    u8 pad14[4]; s32 u18, u1C, u20, u24;
} T;
void func_80016B60(T *p, void *a1) {
    s32 *q = func_80010A08(0x20);
    s32 t0;
    s32 t1;
    s32 v;
    q[1] = p->u18;
    q[3] = p->u1C;
    q[5] = p->u20;
    func_80058EE4(q);
    func_80017E8C(a1);
    v = func_8004958C(p, &p->u8, &p->u10, q + 2, q + 4, q + 6, &t0, &t1);
    if ((t1 & ~0x1000) == 0) {
        func_80018094(q, v, p->u24);
    }
}
