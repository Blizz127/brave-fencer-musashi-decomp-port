/* Exact retail word export for [80011818,800118AC); EXE and assembly verified. */
#include "psx_types.h"

extern u8 D_800AF630[];

void func_80011818(u16 val) {
    register u8 *p = D_800AF630;

    *(u16 *)(p + 0xA3AE) = val;
    *(u16 *)(p + 0xA3B4) = 0;
    *(u16 *)(p + 0xA3BA) = 0;
    *(u16 *)(p + 0xA3B8) = 0;
    *(u16 *)(p + 0xA3BE) = 0;
    *(u16 *)(p + 0xA3B0) = 0;
    *(u16 *)(p + 0xA3B6) = 0;
    *(u16 *)(p + 0xA3BC) = 0;
}
