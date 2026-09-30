/* Exact retail word export for [80010A08,80010A84); EXE and assembly verified. */
#include "psx_types.h"

extern u8 D_800AF630[];
extern s32 D_800A5E60;

void *func_80010A08(s32 size) {
    register u8 *p = D_800AF630;
    s32 ret;
    s32 words;

    words = (size + 3) / 4;
    ret = D_800A5E60;
    D_800A5E60 = D_800A5E60 + (words << 2);
    return (void *)ret;
}
