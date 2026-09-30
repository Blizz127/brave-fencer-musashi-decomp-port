/* libgs HLE: the packet-building parts of libgs over guest RAM.
 *
 * libgs's 2D sort calls build GPU packets in the packet area whose pointer
 * lives at GsOUT_PACKET_P (0x800A5E60) and link them into a guest GsOT. The
 * HLE does the same, so the packets render in OT order through DrawOTag (the
 * HLE decoder or the interpreter's GPU path), exactly like the retail code.
 * Layouts and field arithmetic follow the game's decompiled C for each entry
 * (repo src/main/<addr>.c); no Psy-Q code is used.
 *
 *   GsOT         +0 u16 length, +4 GsOT_TAG *org, +8 u16 offset, +10 u16 point
 *                (the retail code reads offset and point as one 32-bit word)
 *   GsLINE       +0 attribute, +4 x0 y0 x1 y1 (s16), +12 r g b
 *   GsSPRITE     +0 attribute, +4 x y, +8 w h, +0xC tpage, +0xE u v, +0x10 cx cy,
 *                +0x14 r g b
 *   GsCOORDINATE2 +0 flag, +4 MATRIX coord, +0x24 MATRIX workm, +0x44 param,
 *                +0x48 super, +0x4C sub */
#include "bfm_psyq_compat.h"
#include "../bfm_plat.h"

#include <stdio.h>
#include <string.h>

#define GS_OUT_PACKET_P 0x800A5E60u   /* u32: next free packet address */
#define GS_DRAW_OFS_X   0x800A6548u   /* s16: current buffer's draw offset */
#define GS_DRAW_OFS_Y   0x800A654Au
#define GS_IDMATRIX     0x800AE620u   /* MATRIX GsIDMATRIX */
#define GS_LIGHT_MODE   0x800C6DC8u   /* s32 */

#define A(n) bfm_psyq_arg(r, n)

static uint8_t *g(uint32_t addr, uint32_t len) { return (uint8_t *)bfm_psyq_ptr(addr, len); }

static int get32(uint32_t a, uint32_t *v) {
    uint8_t *p = g(a, 4);
    if (!p) return 0;
    *v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return 1;
}

static int put32(uint32_t a, uint32_t v) {
    uint8_t *p = g(a, 4);
    if (!p) return 0;
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    return 1;
}

static uint16_t u16at(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* The libgs OT link (func_80051EA0): slot = org + (pri - offset)*4;
 * packet tag = old slot word with byte 3 = len; slot = packet, byte 3 = 0.
 * Returns the address after the packet (header + len words). */
int bfm_psyq_gs_link(uint32_t pkt, uint32_t ot, uint32_t pri, uint8_t len, uint32_t *next) {
    uint32_t org, ofs, slot, old;
    int32_t idx;
    if (!get32(ot + 4, &org) || !get32(ot + 8, &ofs)) return 0;
    idx = (int32_t)(pri & 0xFFFFu) - (int32_t)ofs;
    slot = org + (uint32_t)idx * 4u;
    if (!get32(slot, &old)) return 0;
    if (idx < 0) bfm_plat_mods_log("libgs", 1, "GsOT priority below the OT offset");
    put32(pkt, (old & 0x00FFFFFFu) | ((uint32_t)len << 24));
    put32(slot, pkt & 0x00FFFFFFu);
    *next = pkt + (uint32_t)len * 4u + 4u;
    return 1;
}

static int draw_offset(int16_t *x, int16_t *y) {
    uint8_t *px = g(GS_DRAW_OFS_X, 4);
    if (!px) return 0;
    *x = (int16_t)u16at(px);
    *y = (int16_t)u16at(px + 2);
    return 1;
}

/* GsSortLine(GsLINE *lp, GsOT *ot, u16 pri): E1 + LINE_F2, 4 words after the tag */
int bfm_psyq_GsSortLine(uint32_t *r) {
    uint8_t *lp = g(A(0), 15);
    uint32_t pkt, next, attr;
    int16_t ox, oy;
    if (!lp || !get32(GS_OUT_PACKET_P, &pkt) || !g(pkt, 20) || !draw_offset(&ox, &oy)) return 0;
    attr = (uint32_t)lp[0] | ((uint32_t)lp[1] << 8) | ((uint32_t)lp[2] << 16) | ((uint32_t)lp[3] << 24);
    if ((int32_t)attr < 0) return 1;                     /* display-off bit: nothing drawn */
    put32(pkt + 4, ((attr >> 23) & 0x60u) | 0xE1000200u);
    put32(pkt + 8, (uint32_t)lp[12] | ((uint32_t)lp[13] << 8) | ((uint32_t)lp[14] << 16) |
                   ((((attr >> 29) & 2u) | 0x40u) << 24));
    put32(pkt + 12, (uint16_t)(u16at(lp + 4) + (uint16_t)ox) | ((uint32_t)(uint16_t)(u16at(lp + 6) + (uint16_t)oy) << 16));
    put32(pkt + 16, (uint16_t)(u16at(lp + 8) + (uint16_t)ox) | ((uint32_t)(uint16_t)(u16at(lp + 10) + (uint16_t)oy) << 16));
    if (!bfm_psyq_gs_link(pkt, A(1), A(2), 4, &next)) return 0;
    put32(GS_OUT_PACKET_P, next);
    return 1;
}

/* GsSortFastSprite(GsSPRITE *sp, GsOT *ot, u16 pri): E1 + SPRT, 5 words after the tag */
int bfm_psyq_GsSortFastSprite(uint32_t *r) {
    uint8_t *sp = g(A(0), 0x17);
    uint32_t pkt, attr, org, ofs, slot, old;
    int16_t ox, oy;
    if (!sp || !get32(GS_OUT_PACKET_P, &pkt) || !g(pkt, 24) || !draw_offset(&ox, &oy)) return 0;
    attr = (uint32_t)sp[0] | ((uint32_t)sp[1] << 8) | ((uint32_t)sp[2] << 16) | ((uint32_t)sp[3] << 24);
    if ((int32_t)attr < 0 || u16at(sp + 8) == 0 || u16at(sp + 10) == 0) return 1;
    if (!get32(A(1) + 4, &org) || !get32(A(1) + 8, &ofs)) return 0;
    slot = org + (A(2) & 0xFFFFu) * 4u - ofs * 4u;
    if (!get32(slot, &old)) return 0;
    put32(pkt + 4, (u16at(sp + 12) & 0x1Fu) | ((attr >> 17) & 0x180u) | 0xE1000200u | ((attr >> 23) & 0x60u));
    put32(pkt + 12, ((uint32_t)(uint16_t)(u16at(sp + 4) + (uint16_t)ox)) |
                    ((uint32_t)(uint16_t)(u16at(sp + 6) + (uint16_t)oy) << 16));
    put32(pkt + 8, ((attr >> 5) & 0x02000000u) | ((attr << 18) & 0x01000000u) | 0x64000000u |
                   ((uint32_t)sp[0x16] << 16) | ((uint32_t)sp[0x15] << 8) | sp[0x14]);
    put32(pkt + 16, sp[0x0E] | ((uint32_t)sp[0x0F] << 8) |
                    ((uint32_t)(int32_t)(int16_t)u16at(sp + 0x12) << 22) |
                    (((uint32_t)(int32_t)(int16_t)u16at(sp + 0x10) << 12) & 0x3F0000u));
    put32(pkt + 20, u16at(sp + 8) | ((uint32_t)u16at(sp + 10) << 16));
    put32(pkt, old + 0x05000000u);
    put32(slot, pkt & 0x00FFFFFFu);
    put32(GS_OUT_PACKET_P, pkt + 0x18u);
    return 1;
}

/* GsInitCoordinate2(GsCOORDINATE2 *super, GsCOORDINATE2 *base) */
int bfm_psyq_GsInitCoordinate2(uint32_t *r) {
    uint32_t super = A(0), base = A(1);
    uint8_t *c = g(base, 0x50), *id = g(GS_IDMATRIX, 32);
    if (!c || !id) return 0;
    if (super != 0 && super != 1 && !g(super + 0x4C, 4)) return 0;
    memcpy(c + 4, id, 32);
    put32(base + 0x48, super);
    put32(base, 0);
    if (super != 0 && super != 1) put32(super + 0x4C, base);
    return 1;
}

/* GsMapModelingData(u_long *p): p = &tmd.flags; relocates the object table
 * (vert_top, normal_top, primitive_top of each 28-byte object) once. */
int bfm_psyq_GsMapModelingData(uint32_t *r) {
    uint32_t p = A(0), flags, count, table, i;
    if (!get32(p, &flags) || !get32(p + 4, &count)) return 0;
    if (flags & 1u) return 1;
    table = p + 8;
    if ((int32_t)count > 0 && !g(table, count * 28u)) return 0;
    put32(p, flags | 1u);
    for (i = 0; (int32_t)i < (int32_t)count; i++) {
        uint32_t e = table + i * 28u, v = 0, k;
        for (k = 0; k < 24u; k += 8u)   /* vert_top, normal_top, primitive_top */
            if (get32(e + k, &v)) put32(e + k, v + table);
    }
    return 1;
}

/* GsSetLightMode(mode): 0..3 stored; others logged (and ignored) */
int bfm_psyq_GsSetLightMode(uint32_t *r) {
    uint32_t mode = A(0);
    if (mode <= 3u) return put32(GS_LIGHT_MODE, mode);
    bfm_plat_mods_log("libgs", 1, "GsSetLightMode: unknown mode");
    return 1;
}
