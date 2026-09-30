/* Exact retail word export for [8001C044,8001C0C8); EXE and assembly verified. */
/* Verified byte-exact against retail by tools/match_function.py
 * (33/33 words at 0x8001C044). Types and signatures are whatever
 * reproduces the bytes; they are not evidence of the original
 * declaration. */
#include "psx_types.h"

void func_80016714(void *, s32);
extern u8 D_800AF630[];

void func_8001C044(void) {
    u8 *base;
    u8 *p;
    s32 i;

    base = D_800AF630;
    p = base + 0x65A8;
    for (i = 0; i < 0x100; i++) {
        func_80016714(p, 0x38);
        p += 0x38;
    }
    p = base + 0x2A8;
    for (i = 0; i < 0xC0; i++) {
        func_80016714(p, 0x84);
        p += 0x84;
    }
    *(u16 *) (base + 0x19C) = 0;
}
