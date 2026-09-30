/* Exact retail word export for [800143AC,80014444); EXE and assembly verified. */
#include "psx_types.h"

typedef struct {
    s32 unk0;
    u8 *buf;
    s32 unk8;
    s32 unkc;
    s32 unk10;
} Struct_800A6518;

extern Struct_800A6518 D_800A6518[2];
extern u8 D_800A6610;
extern u8 D_800AF7C2;
extern u8 D_800AF7C3;
extern u8 D_800AF7C4;

void func_800145EC(s32);
void func_8004239C(s32);

void func_800143AC(void) {
    s32 val;
    u8 *buf;
    s32 i;

    func_800145EC(1);
    func_8004239C(0);

    i = 0;
    val = 12;
    buf = &D_800A6610;
    D_800AF7C4 = 0;
    D_800AF7C3 = 0;
    D_800AF7C2 = 0;

    for (i = 0; i < 2; i++) {
        D_800A6518[i].buf = buf;
        buf += 0x4000;
        D_800A6518[i].unk0 = val;
        D_800A6518[i].unk8 = 0;
        D_800A6518[i].unkc = 0;
    }
}
