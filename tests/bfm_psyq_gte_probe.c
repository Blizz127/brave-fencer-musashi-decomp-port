/* PsyCross-backed libgte wrappers against PsyCross's real GTE (gteRegs),
 * through the lease bridge. Built by tests/test_bfm_plat.py only when the
 * PsyCross checkout is present. */
#include "bfm_plat.h"
#include "psyq/bfm_psyq_compat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned int CFC2(int reg);
extern unsigned int MFC2(int reg);

/* shim for the two PsyCross render-side symbols its GTE references */
int g_cfg_pgxpTextureCorrection = 0;
void PsyX_Log_Warning(const char *fmt, ...) { (void)fmt; }

#define CHECK(e) do { if (!(e)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

static uint8_t ram[2u * 1024u * 1024u];
static uint32_t R[32];
static int begins, ends, allow = 1;
static uint32_t cmds;

static int br_begin(void *u) { (void)u; begins++; return allow; }
static void br_end(void *u, uint32_t c) { (void)u; ends++; cmds += c; }

static void w16(uint32_t a, int16_t v) { a &= 0x1FFFFF; ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)((uint16_t)v >> 8); }
static void w32(uint32_t a, int32_t v) { w16(a, (int16_t)v); w16(a + 2, (int16_t)((uint32_t)v >> 16)); }
static int32_t r32(uint32_t a) {
    a &= 0x1FFFFF;
    return (int32_t)((uint32_t)ram[a] | ((uint32_t)ram[a + 1] << 8) | ((uint32_t)ram[a + 2] << 16) | ((uint32_t)ram[a + 3] << 24));
}
static int16_t r16(uint32_t a) { a &= 0x1FFFFF; return (int16_t)(ram[a] | (ram[a + 1] << 8)); }

static int call(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    const BfmPsyqEntry *t;
    size_t n = bfm_psyq_table(&t), i;
    R[4] = a0; R[5] = a1; R[6] = a2; R[7] = a3;
    for (i = 0; i < n; i++)
        if (strcmp(t[i].name, name) == 0) {
            CHECK(t[i].available);
            return t[i].fn(R);
        }
    printf("FAIL no entry %s\n", name);
    exit(1);
}

/* MATRIX in guest RAM: short m[3][3], pad, int t[3] (32 bytes) */
static void put_matrix(uint32_t a, const int16_t m[9], const int32_t t[3]) {
    int i;
    for (i = 0; i < 9; i++) w16(a + 2u * (uint32_t)i, m[i]);
    w16(a + 18, 0);
    for (i = 0; i < 3; i++) w32(a + 20u + 4u * (uint32_t)i, t[i]);
}

static void fill_sincos_table(void) {
    unsigned i;
    for (i = 0; i < 4096; i++) {
        double a = 6.283185307179586 * (double)i / 4096.0;
        int16_t sn = (int16_t)lround(4096.0 * sin(a)), cs = (int16_t)lround(4096.0 * cos(a));
        w32(0x8006DF1C + 4 * i, (int32_t)((uint32_t)(uint16_t)sn | ((uint32_t)(uint16_t)cs << 16)));
    }
}

/* GsCOORDINATE2 at a: flag, local = rotation r9 (row-major shorts) + t */
static void coord(uint32_t a, uint32_t flag, const int16_t r9[9], int32_t tx, int32_t ty, int32_t tz, uint32_t super) {
    int i;
    memset(ram + (a & 0x1FFFFF), 0, 0x50);
    w32(a, (int32_t)flag);
    for (i = 0; i < 9; i++) w16(a + 4 + 2u * (uint32_t)i, r9[i]);
    w32(a + 0x18, tx); w32(a + 0x1C, ty); w32(a + 0x20, tz);
    w32(a + 0x48, (int32_t)super);
}

int main(void) {
    static const int16_t ident[9] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
    static const int32_t t1000[3] = {0, 0, 1000};
    BfmPsyqGteBridge b;
    int32_t sx, sy;
    bfm_psyq_set_ram(ram, sizeof ram);
    R[29] = 0x801FFE00;

    /* ratan2 and RotMatrixX/Y/Z/YXZ are exact HLE over the guest tables
     * now (no PsyCross): bfm_plat_probe libgte checks them */

    /* GTE state without a bridge: refuse and change nothing */
    put_matrix(0x80002000, ident, t1000);
    CHECK(!call("SetRotMatrix", 0x80002000, 0, 0, 0));

    b.begin = br_begin; b.end = br_end; b.user = NULL;
    bfm_psyq_set_gte_bridge(&b);
    allow = 0;
    CHECK(!call("SetRotMatrix", 0x80002000, 0, 0, 0) && begins == 1 && ends == 0);
    allow = 1;

    /* setters land in the shared gteRegs bank */
    CHECK(call("InitGeom", 0, 0, 0, 0));
    CHECK(call("SetRotMatrix", 0x80002000, 0, 0, 0));
    CHECK(call("SetTransMatrix", 0x80002000, 0, 0, 0));
    CHECK(CFC2(0) == 4096u && CFC2(2) == 4096u && CFC2(4) == 4096u && CFC2(7) == 1000u);
    CHECK(call("SetGeomOffset", 160, 120, 0, 0) && call("SetGeomScreen", 256, 0, 0, 0));
    CHECK(CFC2(26) == 256u);
    CHECK(call("ReadGeomOffset", 0x80003000, 0x80003004, 0, 0) && r32(0x80003000) == 160 && r32(0x80003004) == 120);
    CHECK(call("ReadRotMatrix", 0x80003100, 0, 0, 0) && r16(0x80003100) == 4096 && r32(0x80003100 + 28) == 1000);

    /* RotTransPers: sx = 160 + 100*256/1000, sy = 120 + 50*256/1000, otz = 1000/4 */
    w16(0x80004000, 100); w16(0x80004002, 50); w16(0x80004004, 0);
    CHECK(call("RotTransPers", 0x80004000, 0x80004100, 0x80004104, 0x80004108));
    sx = r16(0x80004100); sy = r16(0x80004102);
    printf("rtps sx %d sy %d otz %d flag %08x\n", sx, sy, (int)R[2], (unsigned)r32(0x80004108));
    CHECK(sx >= 185 && sx <= 186 && sy >= 132 && sy <= 133 && R[2] == 250);
    CHECK(r32(0x8000410C) == 0);   /* 32-bit outputs only: the word after flag is untouched */

    /* RotTransPers3 / 4 (stack arguments) agree with single-vertex results */
    w16(0x80004010, -100); w16(0x80004012, 0); w16(0x80004014, 0);
    w16(0x80004020, 0); w16(0x80004022, -50); w16(0x80004024, 0);
    w16(0x80004030, 30); w16(0x80004032, 30); w16(0x80004034, 0);
    /* a3 = sxy0; the stack holds sxy1, sxy2, p, flag */
    w32(R[29] + 0x10, (int32_t)0x80004204);
    w32(R[29] + 0x14, (int32_t)0x80004208);
    w32(R[29] + 0x18, (int32_t)0x8000420C);
    w32(R[29] + 0x1C, (int32_t)0x80004210);
    CHECK(call("RotTransPers3", 0x80004000, 0x80004010, 0x80004020, 0x80004200));
    CHECK(r32(0x80004200) == r32(0x80004100) && R[2] == 250);
    CHECK(r16(0x80004204) < 160 && r16(0x8000420A) < 120);
    /* RotTransPers4(v0..v3 in a0-a3, sxy0..3 + p + flag on the stack) */
    w32(R[29] + 0x10, (int32_t)0x80004300); w32(R[29] + 0x14, (int32_t)0x80004304);
    w32(R[29] + 0x18, (int32_t)0x80004308); w32(R[29] + 0x1C, (int32_t)0x8000430C);
    w32(R[29] + 0x20, (int32_t)0x80004310); w32(R[29] + 0x24, (int32_t)0x80004314);
    CHECK(call("RotTransPers4", 0x80004000, 0x80004010, 0x80004020, 0x80004030));
    CHECK(r32(0x80004300) == r32(0x80004100) && r16(0x8000430C) > 160 && r16(0x8000430E) > 120);

    /* matrices: ApplyMatrixSV / MulMatrix0 with the identity */
    w16(0x80005000, 7); w16(0x80005002, -9); w16(0x80005004, 11);
    CHECK(call("ApplyMatrixSV", 0x80002000, 0x80005000, 0x80005008, 0) && R[2] == 0x80005008);
    CHECK(r16(0x80005008) == 7 && r16(0x8000500A) == -9 && r16(0x8000500C) == 11);
    CHECK(call("MulMatrix0", 0x80002000, 0x80001100, 0x80005100, 0) && R[2] == 0x80005100);
    CHECK(memcmp(ram + 0x5100, ram + 0x1100, 18) == 0);
    CHECK(call("CompMatrix", 0x80002000, 0x80002000, 0x80005200, 0) && r32(0x80005200 + 28) == 2000);

    /* libgs over the GTE */
    CHECK(call("GsSetLsMatrix", 0x80002000, 0, 0, 0) && CFC2(0) == 4096u && CFC2(7) == 1000u);
    CHECK(call("GsSetProjection", 512, 0, 0, 0) && CFC2(26) == 512u);
    CHECK(call("GsSetAmbient", 0x7F, 0x80, 0xFFF, 0) && CFC2(13) == 0x70u && CFC2(14) == 0x80u && CFC2(15) == 0xFF0u);
    CHECK(call("SetBackColor", 1, 2, 3, 0) && CFC2(13) == 16u && CFC2(14) == 32u && CFC2(15) == 48u);

    /* PushMatrix / PopMatrix on the guest-RAM stack */
    {
        static const int16_t rz90[9] = {0, -4096, 0, 4096, 0, 0, 0, 0, 4096};
        static const int32_t t7[3] = {7, 8, 9};
        int i;
        w32(0x8006DC18, 0);
        CHECK(call("SetRotMatrix", 0x80002000, 0, 0, 0) && call("SetTransMatrix", 0x80002000, 0, 0, 0));
        CHECK(call("PushMatrix", 0, 0, 0, 0) && r32(0x8006DC18) == 0x20);
        CHECK(r32(0x8006DC1C) == 4096 && r32(0x8006DC1C + 0x1C) == 1000);
        put_matrix(0x80002300, rz90, t7);
        CHECK(call("SetRotMatrix", 0x80002300, 0, 0, 0) && call("SetTransMatrix", 0x80002300, 0, 0, 0));
        CHECK(CFC2(7) == 9u);
        CHECK(call("PopMatrix", 0, 0, 0, 0) && r32(0x8006DC18) == 0 && CFC2(0) == 4096u && CFC2(7) == 1000u);
        /* underflow: refused, so the retail error path (saves $ra, printf) runs */
        CHECK(!call("PopMatrix", 0, 0, 0, 0) && r32(0x8006DC18) == 0);
        for (i = 0; i < 20; i++) CHECK(call("PushMatrix", 0, 0, 0, 0));
        CHECK(r32(0x8006DC18) == 0x280);
        w32(0x8006DC1C + 0x280, 0x5A5A5A5A);
        /* overflow: refused (retail error path), nothing written */
        CHECK(!call("PushMatrix", 0, 0, 0, 0) && r32(0x8006DC18) == 0x280 && r32(0x8006DC1C + 0x280) == 0x5A5A5A5A);
        w32(0x8006DC18, 0);
    }

    /* GsGetLw / GsGetLs / GsGetLws: root R (rotate 90 about Z, t 10,0,0) <- A (t 100,0,0) <- B (t 0,5,0) */
    {
        static const int16_t rz90[9] = {0, -4096, 0, 4096, 0, 0, 0, 0, 4096};
        const uint32_t Rt = 0x80006000, At = 0x80006100, Bt = 0x80006200, OUT = 0x80006300;
        w32(0x800C7C70, 7);                                  /* frame counter */
        coord(Rt, 0, rz90, 10, 0, 0, 0);
        coord(At, 0, ident, 100, 0, 0, Rt);
        coord(Bt, 0, ident, 0, 5, 0, At);
        /* 1: everything dirty -> full walk; B world = R*(tA + tB) + tR = (10-5, 100, 0) */
        CHECK(call("GsGetLw", Bt, OUT, 0, 0));
        printf("lw1 %d %d %d m01 %d\n", r32(OUT + 20), r32(OUT + 24), r32(OUT + 28), r16(OUT + 2));
        CHECK(r32(OUT + 20) == 5 && r32(OUT + 24) == 100 && r32(OUT + 28) == 0 && r16(OUT + 2) == -4096);
        CHECK(r32(Rt) == 7 && r32(At) == 7 && r32(Bt) == 7);        /* all stamped */
        CHECK(r32(At + 0x24 + 20) == 10 && r32(At + 0x24 + 24) == 100);   /* A world cached */
        CHECK(r32(0x800C6D48) == (int32_t)Bt && r32(0x800C6D4C) == (int32_t)At && r32(0x800C6D50) == (int32_t)Rt);
        /* 2: same frame -> B's cache, even after A's local changes */
        w32(At + 0x18, 200);
        CHECK(call("GsGetLw", Bt, OUT, 0, 0) && r32(OUT + 24) == 100);
        /* 3: next frame, nothing dirty, root stamped earlier -> the input's cache is reused */
        w32(0x800C7C70, 8);
        CHECK(call("GsGetLw", Bt, OUT, 0, 0) && r32(OUT + 24) == 100 && r32(Bt) == 7);
        /* 4: A dirty (flag 0) -> restart above it from R's cache: A's new t (200) shows */
        w32(At, 0);
        CHECK(call("GsGetLw", Bt, OUT, 0, 0));
        CHECK(r32(OUT + 20) == 5 && r32(OUT + 24) == 200 && r32(At) == 8 && r32(Bt) == 8);
        /* 5: root dirty -> recomputed from its local (t 50) */
        w32(0x800C7C70, 9);
        w32(Rt, 0); w32(Rt + 0x18, 50);
        CHECK(call("GsGetLw", Bt, OUT, 0, 0) && r32(OUT + 20) == 45 && r32(Rt + 0x24 + 20) == 50);
        /* GsGetLs = GsWSMATRIX (identity, t 0,0,500) (x) Lw; GsGetLws gives both */
        put_matrix(0x800AE688, ident, t1000);
        w32(0x800AE688 + 28, 500);
        CHECK(call("GsGetLs", Bt, OUT, 0, 0) && r32(OUT + 20) == 45 && r32(OUT + 28) == 500 && r16(OUT + 2) == -4096);
        CHECK(call("GsGetLws", Bt, 0x80006400, 0x80006500, 0));
        CHECK(r32(0x80006400 + 28) == 0 && r32(0x80006500 + 28) == 500 && r32(0x80006500 + 24) == 200);
        /* a broken chain pointer refuses without writing */
        w32(At + 0x48, 0x801FFFF0);
        w32(OUT + 20, 0x1234);
        CHECK(!call("GsGetLw", Bt, OUT, 0, 0) && r32(OUT + 20) == 0x1234);
    }

    /* GsSortSprite */
    {
        const uint32_t SP = 0x80007000, OT = 0x80007100, ORG = 0x80007200, PKT = 0x80008000;
        int i;
        fill_sincos_table();
        w32(0x800A5E60, (int32_t)PKT);
        w16(0x800A6548, 3); w16(0x800A654A, 4);
        w32(OT + 4, (int32_t)ORG); w32(OT + 8, 0);
        for (i = 0; i < 4; i++) w32(ORG + 4u * (uint32_t)i, i ? (int32_t)((ORG + 4u * (uint32_t)(i - 1)) & 0xFFFFFF) : 0xFFFFFF);
        put_matrix(0x800AE620, ident, t1000);                 /* GsIDMATRIX (t unused) */
        memset(ram + (SP & 0x1FFFFF), 0, 0x24);
        w16(SP + 4, 100); w16(SP + 6, 60); w16(SP + 8, 16); w16(SP + 10, 8);
        w16(SP + 12, 7); ram[(SP & 0x1FFFFF) + 14] = 32; ram[(SP & 0x1FFFFF) + 15] = 64;
        w16(SP + 0x18, 8); w16(SP + 0x1A, 4);                /* mx, my */
        w32(SP + 0x1C, 0x10001000);                           /* scale 1.0, 1.0 */
        /* plain: SPRT at (x + ofs - m) */
        CHECK(call("GsSortSprite", SP, OT, 2, 0));
        CHECK(r32(PKT + 8) == 0x64000000 && r32(PKT + 12) == ((60 + 4 - 4) << 16 | (100 + 3 - 8)) &&
              r32(PKT + 20) == (8 << 16 | 16) && (r32(PKT) >> 24) == 5);
        CHECK(r32(0x800A5E60) == (int32_t)(PKT + 24));
        /* rotated 90 degrees, x-flip: POLY_FT4, corners hand-computed:
         * screen = OF + (x, y) + Rz90 * corner, Rz90 (px, py) = (-py, px); OF = (160, 120), H = SZ */
        CHECK(call("SetGeomOffset", 160, 120, 0, 0) && call("SetGeomScreen", 256, 0, 0, 0));
        w32(SP, 0x00800000);                                  /* flip x */
        w32(SP + 0x20, 90 * 4096);
        CHECK(call("GsSortSprite", SP, OT, 2, 0));
        {
            uint32_t p = PKT + 24;
            static const int cx[4] = {-8, 8, -8, 8}, cy[4] = {-4, -4, 4, 4};
            for (i = 0; i < 4; i++) {
                int32_t sxy = r32(p + 8 + 8u * (uint32_t)i);
                int ex = 160 + 100 - cy[i], ey = 120 + 60 + cx[i];
                printf("corner %d: %d %d (want %d %d)\n", i, (int16_t)sxy, (int16_t)(sxy >> 16), ex, ey);
                CHECK((int16_t)sxy == ex && (int16_t)(sxy >> 16) == ey);
            }
            CHECK((r32(p + 4) >> 24) == 0x2C && (r32(p) >> 24) == 9);
            CHECK((r32(p + 12) & 0xFFFF) == ((64 << 8) | (32 + 15)) && (r32(p + 20) & 0xFFFF) == ((64 << 8) | 32));
            CHECK(((r32(p + 20) >> 16) & 0x1F) == 7);
            CHECK((r32(p + 28) & 0xFFFF) == ((71 << 8) | 47) && (r32(p + 36) & 0xFFFF) == ((71 << 8) | 32));
            CHECK(r32(0x800A5E60) == (int32_t)(p + 40));
        }
    }

    /* bad pointer refuses before the bridge is entered */
    {
        int before = begins;
        CHECK(!call("SetRotMatrix", 0x801FFFF0, 0, 0, 0) && begins == before);
    }
    printf("bridge begins %d ends %d commands %u\n", begins, ends, (unsigned)cmds);
    CHECK(ends == begins - 1);
    printf("ok gte\n");
    return 0;
}
