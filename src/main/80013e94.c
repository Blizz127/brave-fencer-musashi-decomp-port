/* Main-exec range [80013E94,80013ED0) from the SLUS executable.
 * SHA256(span)=2078f93a351173212a9152953d135bb629980e56bc6031254a7e3a8e63357bc8.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

/* HAND MODEL from a fresh m2c run on func_80013E94 (main.s): NOT
 * verified against retail. C89-gated only; promotion requires an
 * oracle MATCH. m2c shapes: arg0->unk0/unk2/unk4 packed with
 * <<0x13>>0x16 shifts into *arg1. The disassembly loads all three as
 * halfwords (lhu +0x0/+0x2/+0x4) and stores one word (sw +0x0($a1)
 * in the delay slot), so the model declares three u16 members and an
 * s32 destination — m2c's shift expression then matches the
 * sll/srl/or cascade instruction for instruction. */

struct data_80013E94 {
    u16 unk0;
    u16 unk2;
    u16 unk4;
};

void func_80013E94(struct data_80013E94 *arg0, s32 *arg1) {
    *arg1 = (((u32) (arg0->unk0 << 0x13) >> 0x16) << 0x14) | (((u32) (arg0->unk2 << 0x13) >> 0x16) << 0xA) | ((u32) (arg0->unk4 << 0x13) >> 0x16);
}
