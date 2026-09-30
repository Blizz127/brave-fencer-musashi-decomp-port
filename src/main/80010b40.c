/* Exact retail word export for [80010B40,80010BB4); EXE and assembly verified. */
#include "psx_types.h"

extern u8 D_800AF630[];
extern void (*D_800629F4[])(void);

void func_80010B40(void) {
    register u8 *p = D_800AF630;
    register void (*fn)(void);

    fn = D_800629F4[*(u16 *)(p + 0xA3AE)];
    fn();
}
