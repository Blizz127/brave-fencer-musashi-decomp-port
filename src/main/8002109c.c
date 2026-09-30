/* Main-exec range [8002109C,80021120) from the SLUS executable.
 * SHA256(span)=89fa041c8b77181d54dfc9844e136ce20a596d44766f1c41e25c59d64ba97dec.
 * Word export for the native seam; the C body below keeps its
 * own oracle match claim. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_800491FC();
typedef struct { s16 a, b, c, d; } V;
typedef struct { V *p; } T;
void func_8002109C(void *unused, T *t) {
    V *v = t->p;
    if (v->d == -2) {
        func_800491FC(v->a, v->b, v->c);
    } else if (unused == (void *)(s32)v->d) {
        func_800491FC(v->a, v->b, v->c);
        t->p = (V *)((u8 *)t->p + 8);
    }
}
