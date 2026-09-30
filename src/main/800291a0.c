#include "psx_types.h"


/* Decompiled from main.s. The byte table element is the only observed effect;
 * the inferred array type is not evidence of the original declaration. */
extern u8 D_800BA1B8[];

void func_800291A0(s32 selector, s32 value) {
    D_800BA1B8[selector] = value;
}
