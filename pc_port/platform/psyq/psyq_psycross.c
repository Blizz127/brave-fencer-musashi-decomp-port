/* PsyCross-backed Psy-Q wrappers (status PSYCROSS in bfm_psyq_compat.def).
 *
 * Build with -DBFM_PLAT_WITH_PSYCROSS, C11, -include assert.h and
 * -I tools/third_party/psycross/include.
 *
 * GTE state: PsyCross's libgte works on the process-wide gteRegs bank that
 * port-int's gte_owner leases for the interpreter, so these calls share the
 * guest's cop2 state. Every wrapper that reads or writes that bank runs
 * inside bfm_psyq_gte_begin()/end() (the lease bridge) and refuses without
 * it. Pointer arguments are guest addresses (translated with size checks).
 * PsyCross declares some out-parameters as `long *` (8 bytes on LP64), so
 * those go through host temporaries and are stored back as 32-bit words. */
#include "bfm_psyq_compat.h"
#include "../bfm_plat_mods.h"

#include <psx/types.h>
#include <psx/libgte.h>
#include <psx/gtereg.h>
#include <psx/inline_c.h>

#include <string.h>

/* Defined in PsyCross LIBGTE.C but not declared in its libgte.h. Weak:
 * builds that replace PsyCross's LIBGTE with their own GTE owner (native
 * lane) don't link it, and the wrapper then refuses so the retail code
 * runs instead. */
#if defined(__GNUC__) || defined(__clang__)
extern MATRIX *CompMatrixLV(MATRIX *m0, MATRIX *m1, MATRIX *m2) __attribute__((weak));
#define BFM_HAVE_COMPMATRIXLV() (CompMatrixLV != NULL)
#else
extern MATRIX *CompMatrixLV(MATRIX *m0, MATRIX *m1, MATRIX *m2);
#define BFM_HAVE_COMPMATRIXLV() 1
#endif

#define A(n) bfm_psyq_arg(r, n)
#define P(addr, size) bfm_psyq_ptr((addr), (size))

static void put32(uint32_t addr, int32_t v) {
    uint8_t *p = (uint8_t *)P(addr, 4);
    if (!p) return;
    p[0] = (uint8_t)v; p[1] = (uint8_t)((uint32_t)v >> 8);
    p[2] = (uint8_t)((uint32_t)v >> 16); p[3] = (uint8_t)((uint32_t)v >> 24);
}

/* ---- stateless ---- */

/* ratan2 and RotMatrixX/Y/Z are exact HLE in bfm_psyq_libgte.c over the
 * guest's own tables: linking PsyCross's versions would pull LIBGTE.C's
 * rcossin/ratan tables (identical to the game's) into the port. */

/* RotMatrixYXZ is exact in bfm_psyq_libgte.c: PsyCross's differs from the
 * retail code by 1-2 in some entries. */

/* ---- GTE state (bridge) ---- */

#define GTE_BEGIN() do { if (!bfm_psyq_gte_begin()) return 0; } while (0)
#define GTE_END(cmds) bfm_psyq_gte_end(cmds)

int bfm_psyq_InitGeom(uint32_t *r) {
    (void)r;
    GTE_BEGIN();
    InitGeom();
    GTE_END(0);
    return 1;
}

int bfm_psyq_SetGeomOffset(uint32_t *r) {
    GTE_BEGIN();
    SetGeomOffset((int)(int16_t)A(0), (int)(int16_t)A(1));
    GTE_END(0);
    return 1;
}

int bfm_psyq_SetGeomScreen(uint32_t *r) {
    GTE_BEGIN();
    SetGeomScreen((int)(A(0) & 0xFFFFu));
    GTE_END(0);
    return 1;
}

int bfm_psyq_ReadGeomOffset(uint32_t *r) {
    int32_t ofx, ofy;
    if (!P(A(0), 4) || !P(A(1), 4)) return 0;
    GTE_BEGIN();
    ofx = (int32_t)C2_OFX >> 16;
    ofy = (int32_t)C2_OFY >> 16;
    GTE_END(0);
    put32(A(0), ofx);
    put32(A(1), ofy);
    return 1;
}

#define SET_MATRIX(fn, name)                                             \
    int name(uint32_t *r) {                                              \
        MATRIX *m = (MATRIX *)P(A(0), sizeof(MATRIX));                   \
        if (!m) return 0;                                                \
        GTE_BEGIN();                                                     \
        fn(m);                                                           \
        GTE_END(0);                                                      \
        return 1;                                                        \
    }
SET_MATRIX(SetRotMatrix, bfm_psyq_SetRotMatrix)
SET_MATRIX(SetTransMatrix, bfm_psyq_SetTransMatrix)
SET_MATRIX(SetLightMatrix, bfm_psyq_SetLightMatrix)

int bfm_psyq_ReadRotMatrix(uint32_t *r) {
    MATRIX *m = (MATRIX *)P(A(0), sizeof(MATRIX));
    if (!m) return 0;
    GTE_BEGIN();
    gte_ReadRotMatrix(m);
    gte_sttr(m->t);
    GTE_END(0);
    r[2] = A(0);
    return 1;
}

int bfm_psyq_MulMatrix0(uint32_t *r) {
    MATRIX *a = (MATRIX *)P(A(0), sizeof(MATRIX)), *b = (MATRIX *)P(A(1), sizeof(MATRIX));
    MATRIX *c = (MATRIX *)P(A(2), sizeof(MATRIX));
    if (!a || !b || !c) return 0;
    GTE_BEGIN();
    MulMatrix0(a, b, c);
    GTE_END(3);
    r[2] = A(2);
    return 1;
}

int bfm_psyq_MulRotMatrix(uint32_t *r) {
    MATRIX *m = (MATRIX *)P(A(0), sizeof(MATRIX));
    if (!m) return 0;
    GTE_BEGIN();
    MulRotMatrix(m);
    GTE_END(3);
    r[2] = A(0);
    return 1;
}

int bfm_psyq_CompMatrix(uint32_t *r) {
    MATRIX *a = (MATRIX *)P(A(0), sizeof(MATRIX)), *b = (MATRIX *)P(A(1), sizeof(MATRIX));
    MATRIX *c = (MATRIX *)P(A(2), sizeof(MATRIX));
    if (!a || !b || !c) return 0;
    GTE_BEGIN();
    CompMatrix(a, b, c);
    GTE_END(4);
    r[2] = A(2);
    return 1;
}

int bfm_psyq_CompMatrixLV(uint32_t *r) {
    MATRIX *a = (MATRIX *)P(A(0), sizeof(MATRIX)), *b = (MATRIX *)P(A(1), sizeof(MATRIX));
    MATRIX *c = (MATRIX *)P(A(2), sizeof(MATRIX));
    if (!a || !b || !c || !BFM_HAVE_COMPMATRIXLV()) return 0;
    GTE_BEGIN();
    CompMatrixLV(a, b, c);
    GTE_END(4);
    r[2] = A(2);
    return 1;
}

int bfm_psyq_ApplyRotMatrix(uint32_t *r) {
    SVECTOR *v0 = (SVECTOR *)P(A(0), sizeof(SVECTOR));
    VECTOR *v1 = (VECTOR *)P(A(1), sizeof(VECTOR));
    if (!v0 || !v1) return 0;
    GTE_BEGIN();
    ApplyRotMatrix(v0, v1);
    GTE_END(1);
    r[2] = A(1);
    return 1;
}

int bfm_psyq_ApplyRotMatrixLV(uint32_t *r) {
    VECTOR *v0 = (VECTOR *)P(A(0), sizeof(VECTOR));
    VECTOR *v1 = (VECTOR *)P(A(1), sizeof(VECTOR));
    if (!v0 || !v1) return 0;
    GTE_BEGIN();
    ApplyRotMatrixLV(v0, v1);
    GTE_END(3);
    r[2] = A(1);
    return 1;
}

int bfm_psyq_ApplyMatrixSV(uint32_t *r) {
    MATRIX *m = (MATRIX *)P(A(0), sizeof(MATRIX));
    SVECTOR *v0 = (SVECTOR *)P(A(1), sizeof(SVECTOR)), *v1 = (SVECTOR *)P(A(2), sizeof(SVECTOR));
    if (!m || !v0 || !v1) return 0;
    GTE_BEGIN();
    ApplyMatrixSV(m, v0, v1);
    GTE_END(1);
    r[2] = A(2);
    return 1;
}

int bfm_psyq_RotTransSV(uint32_t *r) {
    SVECTOR *v0 = (SVECTOR *)P(A(0), sizeof(SVECTOR)), *v1 = (SVECTOR *)P(A(1), sizeof(SVECTOR));
    long flag = 0;
    if (!v0 || !v1 || !P(A(2), 4)) return 0;
    GTE_BEGIN();
    RotTransSV(v0, v1, &flag);
    GTE_END(1);
    put32(A(2), (int32_t)flag);
    return 1;
}

/* RotTransPers*: PsyCross's own RotTransPers* store the PGXP float x-bits as
 * sxy when USE_PGXP is on (for its float-vertex renderer). The guest expects
 * the packed s16 screen coordinates, so these run the GTE operations on the
 * shared bank and read the integer results back from the registers:
 * SXY0..2 = data 12..14, IR0 = 8, SZ3 = 19, FLAG = control 31. */

static void ldv(int slot, const SVECTOR *v) {
    uint32_t xy = (uint32_t)(uint16_t)v->vx | ((uint32_t)(uint16_t)v->vy << 16);
    MTC2(xy, slot * 2);
    MTC2((uint32_t)(int32_t)v->vz, slot * 2 + 1);
}

/* RotTransPers(v0, sxy, p, flag) -> SZ3 >> 2 */
int bfm_psyq_RotTransPers(uint32_t *r) {
    SVECTOR *v0 = (SVECTOR *)P(A(0), sizeof(SVECTOR));
    int32_t sxy, p, flag, sz;
    if (!v0 || !P(A(1), 4) || !P(A(2), 4) || !P(A(3), 4)) return 0;
    GTE_BEGIN();
    ldv(0, v0);
    doCOP2(0x0180001);                     /* RTPS */
    sxy = (int32_t)MFC2(14);
    p = (int32_t)MFC2(8);
    flag = (int32_t)CFC2(31);
    sz = (int32_t)(MFC2(19) & 0xFFFFu) >> 2;
    GTE_END(1);
    put32(A(1), sxy);
    put32(A(2), p);
    put32(A(3), flag);
    r[2] = (uint32_t)sz;
    return 1;
}

/* RotTransPers3(v0, v1, v2, sxy0, sxy1, sxy2, p, flag) -> SZ3 >> 2 */
int bfm_psyq_RotTransPers3(uint32_t *r) {
    SVECTOR *v[3];
    int32_t sxy[3], p, flag, sz;
    unsigned i;
    for (i = 0; i < 3; i++)
        if (!(v[i] = (SVECTOR *)P(A(i), sizeof(SVECTOR)))) return 0;
    for (i = 3; i < 8; i++)
        if (!P(A(i), 4)) return 0;
    GTE_BEGIN();
    for (i = 0; i < 3; i++) ldv((int)i, v[i]);
    doCOP2(0x0280030);                     /* RTPT */
    for (i = 0; i < 3; i++) sxy[i] = (int32_t)MFC2(12 + (int)i);
    p = (int32_t)MFC2(8);
    flag = (int32_t)CFC2(31);
    sz = (int32_t)(MFC2(19) & 0xFFFFu) >> 2;
    GTE_END(1);
    for (i = 0; i < 3; i++) put32(A(3 + i), sxy[i]);
    put32(A(6), p);
    put32(A(7), flag);
    r[2] = (uint32_t)sz;
    return 1;
}

/* RTPT on v0..v2, RTPS on v3 (inside the bridge); integer results. */
static int32_t rtp4(SVECTOR *const v[4], int32_t sxy[4], int32_t *p, int32_t *flag) {
    unsigned i;
    for (i = 0; i < 3; i++) ldv((int)i, v[i]);
    doCOP2(0x0280030);                     /* RTPT */
    for (i = 0; i < 3; i++) sxy[i] = (int32_t)MFC2(12 + (int)i);
    *flag = (int32_t)CFC2(31);
    ldv(0, v[3]);
    doCOP2(0x0180001);                     /* RTPS */
    sxy[3] = (int32_t)MFC2(14);
    *p = (int32_t)MFC2(8);
    *flag |= (int32_t)CFC2(31);
    return (int32_t)(MFC2(19) & 0xFFFFu) >> 2;
}

/* RotTransPers4(v0..v3, sxy0..sxy3, p, flag) -> SZ3 >> 2 of v3 */
int bfm_psyq_RotTransPers4(uint32_t *r) {
    SVECTOR *v[4];
    int32_t sxy[4], p, flag, sz;
    unsigned i;
    for (i = 0; i < 4; i++)
        if (!(v[i] = (SVECTOR *)P(A(i), sizeof(SVECTOR)))) return 0;
    for (i = 4; i < 10; i++)
        if (!P(A(i), 4)) return 0;
    GTE_BEGIN();
    sz = rtp4(v, sxy, &p, &flag);
    GTE_END(2);
    for (i = 0; i < 4; i++) put32(A(4 + i), sxy[i]);
    put32(A(8), p);
    put32(A(9), flag);
    r[2] = (uint32_t)sz;
    return 1;
}

/* ---- libgs wrappers over the GTE (bridge) ---- */

/* GsSetLsMatrix(m) = SetRotMatrix(m); SetTransMatrix(m) */
int bfm_psyq_GsSetLsMatrix(uint32_t *r) {
    MATRIX *m = (MATRIX *)P(A(0), sizeof(MATRIX));
    if (!m) return 0;
    GTE_BEGIN();
    SetRotMatrix(m);
    SetTransMatrix(m);
    GTE_END(0);
    return 1;
}

/* GsSetProjection(h) = SetGeomScreen(h) */
int bfm_psyq_GsSetProjection(uint32_t *r) {
    GTE_BEGIN();
    SetGeomScreen((int)(A(0) & 0xFFFFu));
    GTE_END(0);
    return 1;
}

/* SetBackColor(r, g, b): RBK/GBK/BBK = component << 4 (retail func_800491FC) */
int bfm_psyq_SetBackColor(uint32_t *r) {
    GTE_BEGIN();
    CTC2(A(0) << 4, 13);
    CTC2(A(1) << 4, 14);
    CTC2(A(2) << 4, 15);
    GTE_END(0);
    return 1;
}

/* GsSetAmbient(r, g, b) = SetBackColor(r >> 4, g >> 4, b >> 4) */
int bfm_psyq_GsSetAmbient(uint32_t *r) {
    GTE_BEGIN();
    CTC2((uint32_t)(((int32_t)A(0) >> 4) << 4), 13);
    CTC2((uint32_t)(((int32_t)A(1) >> 4) << 4), 14);
    CTC2((uint32_t)(((int32_t)A(2) >> 4) << 4), 15);
    GTE_END(0);
    return 1;
}

/* ---- matrix helpers the libgs coordinate code uses ---- */

/* ApplyMatrixLV(m, v0, v1) */
int bfm_psyq_ApplyMatrixLV(uint32_t *r) {
    MATRIX *m = (MATRIX *)P(A(0), sizeof(MATRIX));
    VECTOR *v0 = (VECTOR *)P(A(1), 12), *v1 = (VECTOR *)P(A(2), 12);
    VECTOR out;
    if (!m || !v0 || !v1) return 0;
    GTE_BEGIN();
    ApplyMatrixLV(m, v0, &out);            /* temp: v1 may alias v0; pad not written */
    GTE_END(2);
    v1->vx = out.vx; v1->vy = out.vy; v1->vz = out.vz;
    r[2] = A(2);
    return 1;
}

/* The retail MulMatrix/MulMatrix2 (func_80048D9C/80048EAC) store only the
 * rotation: words 0..3 plus `swc2 IR3` at +0x10, so the pad becomes the sign
 * extension of m[2][2] and t is untouched. PsyCross's versions copy a whole
 * temporary MATRIX (t included, uninitialised) over the destination, so these
 * wrap them to restore t and set the pad the retail way. */
static void mul_rot_only(MATRIX *dst, MATRIX *a, MATRIX *b, int into_second) {
    int t[3];
    memcpy(t, dst->t, sizeof t);
    if (into_second) MulMatrix2(a, b); else MulMatrix(a, b);
    memcpy(dst->t, t, sizeof t);
    {
        uint8_t *m = (uint8_t *)(void *)dst;
        int16_t m22 = (int16_t)(m[0x10] | (m[0x11] << 8));
        m[0x12] = m[0x13] = (uint8_t)(m22 < 0 ? 0xFF : 0x00);
    }
}

/* MulMatrix(m0, m1): m0 = m0 * m1 */
int bfm_psyq_MulMatrix(uint32_t *r) {
    MATRIX *a = (MATRIX *)P(A(0), sizeof(MATRIX)), *b = (MATRIX *)P(A(1), sizeof(MATRIX));
    if (!a || !b) return 0;
    GTE_BEGIN();
    mul_rot_only(a, a, b, 0);
    GTE_END(3);
    r[2] = A(0);
    return 1;
}

/* MulMatrix2(m0, m1): m1 = m0 * m1 */
int bfm_psyq_MulMatrix2(uint32_t *r) {
    MATRIX *a = (MATRIX *)P(A(0), sizeof(MATRIX)), *b = (MATRIX *)P(A(1), sizeof(MATRIX));
    if (!a || !b) return 0;
    GTE_BEGIN();
    mul_rot_only(b, a, b, 1);
    GTE_END(3);
    r[2] = A(1);
    return 1;
}

/* ---- PushMatrix / PopMatrix on the retail guest-RAM stack ----
 * func_8004867C / func_8004871C: byte offset at D_8006DC18, 20 x 32-byte
 * slots at D_8006DC1C; a slot holds cop2 control 0..7 (rotation + translation)
 * as raw words. Overflow / underflow only print in the retail code. */
#define GTE_MSTACK_OFS  0x8006DC18u
#define GTE_MSTACK_BASE 0x8006DC1Cu
#define GTE_MSTACK_SIZE 0x280

static int get32(uint32_t a, uint32_t *v) {
    uint8_t *p = (uint8_t *)P(a, 4);
    if (!p) return 0;
    *v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return 1;
}

int bfm_psyq_PushMatrix(uint32_t *r) {
    uint32_t ofs;
    int i;
    (void)r;
    if (!get32(GTE_MSTACK_OFS, &ofs) || !P(GTE_MSTACK_BASE, GTE_MSTACK_SIZE)) return 0;
    /* full: the retail code saves $ra to D_8006DC0C and printf()s; let it */
    if ((int32_t)ofs >= GTE_MSTACK_SIZE) return 0;
    GTE_BEGIN();
    for (i = 0; i < 8; i++) put32(GTE_MSTACK_BASE + ofs + 4u * (uint32_t)i, (int32_t)CFC2(i));
    GTE_END(0);
    put32(GTE_MSTACK_OFS, (int32_t)(ofs + 0x20u));
    return 1;
}

int bfm_psyq_PopMatrix(uint32_t *r) {
    uint32_t ofs, w;
    int i;
    (void)r;
    if (!get32(GTE_MSTACK_OFS, &ofs) || !P(GTE_MSTACK_BASE, GTE_MSTACK_SIZE)) return 0;
    /* empty: the retail error path (saves $ra, printf) runs instead */
    if ((int32_t)ofs <= 0) return 0;
    if (ofs > GTE_MSTACK_SIZE) return 0;             /* corrupt offset: let the retail code decide */
    ofs -= 0x20u;
    GTE_BEGIN();
    put32(GTE_MSTACK_OFS, (int32_t)ofs);
    for (i = 0; i < 8; i++) {
        get32(GTE_MSTACK_BASE + ofs + 4u * (uint32_t)i, &w);
        CTC2(w, i);
    }
    GTE_END(0);
    return 1;
}

/* ---- GsGetLw / GsGetLs / GsGetLws ----
 * GsCOORDINATE2: +0 flag, +4 coord (local), +0x24 workm (cached world),
 * +0x44 param, +0x48 super. The walk follows the game's decomp of
 * func_80054514 exactly: the chain goes into the guest array D_800C6D48, the
 * cache stamp is the frame counter D_800C7C70, flag 0 marks a node dirty, a
 * root whose flag is neither restarts from the node above the deepest dirty
 * one (or reuses the input's cache when nothing is dirty). */
#define GS_COORD_CHAIN  0x800C6D48u
#define GS_FRAME_COUNT  0x800C7C70u
#define GS_WSMATRIX     0x800AE688u
#define GS_CHAIN_MAX    100

/* func_80053050: m1 = m1 (x) m2 (rotation product into m1, t1 += R1 * t2) */
static void coord_mul_into_first(MATRIX *m1, MATRIX *m2) {
    VECTOR v;
    ApplyMatrixLV(m1, (VECTOR *)(void *)m2->t, &v);
    mul_rot_only(m1, m1, m2, 0);
    m1->t[0] += v.vx; m1->t[1] += v.vy; m1->t[2] += v.vz;
}

/* func_80052FCC: m2 = m1 (x) m2 (rotation product into m2, t2 = R1 * t2 + t1) */
static void coord_mul_into_second(MATRIX *m1, MATRIX *m2) {
    VECTOR v;
    ApplyMatrixLV(m1, (VECTOR *)(void *)m2->t, &v);
    mul_rot_only(m2, m1, m2, 1);
    m2->t[0] = v.vx + m1->t[0]; m2->t[1] = v.vy + m1->t[1]; m2->t[2] = v.vz + m1->t[2];
}

static uint8_t *coord(uint32_t a) { return (uint8_t *)P(a, 0x4C); }
static uint32_t cflag(uint8_t *c) { return (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24); }
static void set_cflag(uint8_t *c, uint32_t v) { c[0] = (uint8_t)v; c[1] = (uint8_t)(v >> 8); c[2] = (uint8_t)(v >> 16); c[3] = (uint8_t)(v >> 24); }
static uint32_t csuper(uint8_t *c) { return (uint32_t)c[0x48] | ((uint32_t)c[0x49] << 8) | ((uint32_t)c[0x4A] << 16) | ((uint32_t)c[0x4B] << 24); }

/* Returns 1 with *out = the world matrix of `input`; 0 if a pointer is bad
 * (checked before anything is written). */
static int gs_get_lw(uint32_t input, MATRIX *out) {
    uint32_t chain[GS_CHAIN_MAX + 1], frame, a = input;
    int depth = 0, dirty = 100, n, i;
    uint8_t *c;
    /* validate the whole chain first so a refusal changes nothing */
    for (n = 0, a = input; ; n++) {
        if (n > GS_CHAIN_MAX || !(c = coord(a))) return 0;
        if (csuper(c) == 0) break;
        a = csuper(c);
    }
    if (!P(GS_COORD_CHAIN, 4u * (uint32_t)(n + 1)) || !get32(GS_FRAME_COUNT, &frame)) return 0;
    a = input;
    for (;;) {
        c = coord(a);
        chain[depth] = a;
        put32(GS_COORD_CHAIN + 4u * (uint32_t)depth, (int32_t)a);
        if (csuper(c) == 0) {
            if (cflag(c) == frame || cflag(c) == 0) {
                memcpy(c + 0x24, c + 4, 32);
                memcpy(out, c + 0x24, 32);
                set_cflag(c, frame);
            } else if (dirty == 100) {
                memcpy(out, coord(chain[0]) + 0x24, 32);
                depth = 0;
            } else {
                depth = dirty + 1;
                memcpy(out, coord(chain[depth]) + 0x24, 32);
            }
            break;
        }
        if (cflag(c) == frame) {
            memcpy(out, c + 0x24, 32);
            break;
        }
        if (cflag(c) == 0) dirty = depth;
        a = csuper(c);
        depth++;
    }
    for (i = depth; i > 0; i--) {
        uint8_t *node = coord(chain[i - 1]);
        coord_mul_into_first(out, (MATRIX *)(void *)(node + 4));
        memcpy(node + 0x24, out, 32);
        set_cflag(node, frame);
    }
    return 1;
}

/* GsGetLw(GsCOORDINATE2 *coord, MATRIX *m) */
int bfm_psyq_GsGetLw(uint32_t *r) {
    MATRIX *m = (MATRIX *)P(A(1), sizeof(MATRIX));
    MATRIX out;
    int ok;
    if (!m) return 0;
    GTE_BEGIN();
    ok = gs_get_lw(A(0), &out);
    if (ok) memcpy(m, &out, sizeof out);
    GTE_END(ok ? 5 : 0);
    return ok;
}

/* GsGetLs(coord, m): m = GsWSMATRIX (x) Lw */
int bfm_psyq_GsGetLs(uint32_t *r) {
    MATRIX *m = (MATRIX *)P(A(1), sizeof(MATRIX)), *ws = (MATRIX *)P(GS_WSMATRIX, sizeof(MATRIX));
    MATRIX out;
    int ok;
    if (!m || !ws) return 0;
    GTE_BEGIN();
    ok = gs_get_lw(A(0), &out);
    if (ok) {
        coord_mul_into_second(ws, &out);
        memcpy(m, &out, sizeof out);
    }
    GTE_END(ok ? 8 : 0);
    return ok;
}

/* GsGetLws(coord, lw, ls): lw = Lw; ls = GsWSMATRIX (x) Lw */
int bfm_psyq_GsGetLws(uint32_t *r) {
    MATRIX *lw = (MATRIX *)P(A(1), sizeof(MATRIX)), *ls = (MATRIX *)P(A(2), sizeof(MATRIX));
    MATRIX *ws = (MATRIX *)P(GS_WSMATRIX, sizeof(MATRIX));
    MATRIX out, s;
    int ok;
    if (!lw || !ls || !ws) return 0;
    GTE_BEGIN();
    ok = gs_get_lw(A(0), &out);
    if (ok) {
        memcpy(lw, &out, sizeof out);
        s = out;
        coord_mul_into_second(ws, &s);
        memcpy(ls, &s, sizeof s);
    }
    GTE_END(ok ? 8 : 0);
    return ok;
}

/* ReadGeomScreen() -> H (cfc2 $26) */
int bfm_psyq_ReadGeomScreen(uint32_t *r) {
    GTE_BEGIN();
    r[2] = CFC2(26);
    GTE_END(0);
    return 1;
}

/* ---- GsSortSprite ----
 * Follows the game's decomp of func_80051F34. GsSPRITE: +0 attribute,
 * +4 x y, +8 w h, +0xC tpage, +0xE u v, +0x10 cx cy, +0x14 r g b, +0x18 mx my,
 * +0x1C scalex scaley (s16), +0x20 rotate (s32, degrees * 4096).
 * Plain case (attribute bit 27, or no scale/rotate/flip): E1 + SPRT at
 * (x + ofs - mx, y + ofs - my). Otherwise the corners go through the GTE:
 * RotMatrix (0, 0, rotate / 360) from the guest table, ScaleMatrix
 * (scalex, scaley, 0), TransMatrix (x, y, H), SetRot/TransMatrix, RotTransPers4,
 * and a POLY_FT4 with flip-aware UVs. */
#define GS_OUT_PACKET_P 0x800A5E60u
#define GS_DRAW_OFS     0x800A6548u
#define GS_IDMATRIX     0x800AE620u

int bfm_psyq_GsSortSprite(uint32_t *r) {
    uint8_t *sp = (uint8_t *)P(A(0), 0x24), *ofs = (uint8_t *)P(GS_DRAW_OFS, 4);
    uint32_t pkt, attr, next;
    int32_t scale, rot, w, h, mx, my;
    uint8_t *pk;
    if (!sp || !ofs || !get32(GS_OUT_PACKET_P, &pkt) || !(pk = (uint8_t *)P(pkt, 40))) return 0;
    attr = (uint32_t)sp[0] | ((uint32_t)sp[1] << 8) | ((uint32_t)sp[2] << 16) | ((uint32_t)sp[3] << 24);
    w = (uint16_t)(sp[8] | (sp[9] << 8));
    h = (uint16_t)(sp[10] | (sp[11] << 8));
    if ((int32_t)attr < 0 || w == 0 || h == 0) return 1;
    scale = (int32_t)((uint32_t)sp[0x1C] | ((uint32_t)sp[0x1D] << 8) | ((uint32_t)sp[0x1E] << 16) | ((uint32_t)sp[0x1F] << 24));
    rot = (int32_t)((uint32_t)sp[0x20] | ((uint32_t)sp[0x21] << 8) | ((uint32_t)sp[0x22] << 16) | ((uint32_t)sp[0x23] << 24));
    mx = (int16_t)(sp[0x18] | (sp[0x19] << 8));
    my = (int16_t)(sp[0x1A] | (sp[0x1B] << 8));
    {
        uint32_t color = ((uint32_t)sp[0x16] << 16) | ((uint32_t)sp[0x15] << 8) | sp[0x14];
        uint32_t flags = ((attr >> 5) & 0x02000000u) | ((attr << 18) & 0x01000000u);
        uint32_t clut = ((uint32_t)(int32_t)(int16_t)(sp[0x12] | (sp[0x13] << 8)) << 22) |
                        (((uint32_t)(int32_t)(int16_t)(sp[0x10] | (sp[0x11] << 8)) << 12) & 0x3F0000u);
        uint32_t tpage = (uint32_t)(sp[0x0C] | (sp[0x0D] << 8));
        int simple = ((attr >> 27) & 1u) ||
                     (scale == 0x10001000 && rot == 0 && (attr & 0xC00000u) == 0);
        if (simple) {
            int16_t ox = (int16_t)(ofs[0] | (ofs[1] << 8)), oy = (int16_t)(ofs[2] | (ofs[3] << 8));
            int16_t x = (int16_t)(sp[4] | (sp[5] << 8)), y = (int16_t)(sp[6] | (sp[7] << 8));
            put32(pkt + 4, (int32_t)((tpage & 0x1Fu) | ((attr >> 17) & 0x180u) | 0xE1000200u | ((attr >> 23) & 0x60u)));
            put32(pkt + 8, (int32_t)(flags | 0x64000000u | color));
            put32(pkt + 12, (int32_t)(((uint32_t)(uint16_t)(x + ox - mx)) | ((uint32_t)(uint16_t)(y + oy - my) << 16)));
            put32(pkt + 16, (int32_t)(sp[0x0E] | ((uint32_t)sp[0x0F] << 8) | clut));
            put32(pkt + 20, (int32_t)((uint32_t)w | ((uint32_t)h << 16)));
            if (!bfm_psyq_gs_link(pkt, A(1), A(2), 5, &next)) return 0;
        } else {
            uint8_t m[32];
            SVECTOR c0, c1, c2, c3;
            SVECTOR *cv[4];
            int32_t sxy[4], p, flag, v3[3];
            uint8_t u0, u1, v0, v1;
            GTE_BEGIN();
            if (rot == 0) {
                uint8_t *id = (uint8_t *)P(GS_IDMATRIX, 32);
                if (!id) { GTE_END(0); return 0; }
                memcpy(m, id, 32);
            } else {
                int16_t ang[3];
                ang[0] = 0; ang[1] = 0; ang[2] = (int16_t)(rot / 360);
                memset(m, 0, sizeof m);
                if (!bfm_psyq_rotmatrix(ang, m)) { GTE_END(0); return 0; }
            }
            if (scale != 0x10001000) {
                v3[0] = (int16_t)(uint16_t)scale;
                v3[1] = (int16_t)(sp[0x1E] | (sp[0x1F] << 8));
                v3[2] = 0;
                bfm_psyq_scalematrix(m, v3);
            }
            v3[0] = (int16_t)(sp[4] | (sp[5] << 8));
            v3[1] = (int16_t)(sp[6] | (sp[7] << 8));
            v3[2] = (int32_t)CFC2(26);
            bfm_psyq_transmatrix(m, v3);
            SetRotMatrix((MATRIX *)(void *)m);
            SetTransMatrix((MATRIX *)(void *)m);
            c0.vx = (short)-mx; c0.vy = (short)-my; c0.vz = 0;
            c1.vx = (short)(w - mx); c1.vy = (short)-my; c1.vz = 0;
            c2.vx = (short)-mx; c2.vy = (short)(h - my); c2.vz = 0;
            c3.vx = (short)(w - mx); c3.vy = (short)(h - my); c3.vz = 0;
            cv[0] = &c0; cv[1] = &c1; cv[2] = &c2; cv[3] = &c3;
            rtp4(cv, sxy, &p, &flag);
            GTE_END(2);
            if (attr & 0x800000u) { u0 = (uint8_t)(sp[0x0E] + (uint8_t)w - 1); u1 = sp[0x0E]; }
            else { u0 = sp[0x0E]; u1 = (uint8_t)(sp[0x0E] + (uint8_t)w - 1); }
            if (attr & 0x400000u) { v0 = (uint8_t)(sp[0x0F] + (uint8_t)h - 1); v1 = sp[0x0F]; }
            else { v0 = sp[0x0F]; v1 = (uint8_t)(sp[0x0F] + (uint8_t)h - 1); }
            put32(pkt + 4, (int32_t)(flags | 0x2C000000u | color));
            put32(pkt + 8, sxy[0]);
            put32(pkt + 12, (int32_t)(u0 | ((uint32_t)v0 << 8) | clut));
            put32(pkt + 16, sxy[1]);
            put32(pkt + 20, (int32_t)(u1 | ((uint32_t)v0 << 8) | ((tpage & 0x1Fu) << 16) |
                                      ((attr >> 1) & 0x01800000u) | ((attr >> 7) & 0x600000u)));
            put32(pkt + 24, sxy[2]);
            put32(pkt + 28, (int32_t)(u0 | ((uint32_t)v1 << 8)));
            put32(pkt + 32, sxy[3]);
            put32(pkt + 36, (int32_t)(u1 | ((uint32_t)v1 << 8)));
            if (!bfm_psyq_gs_link(pkt, A(1), A(2), 9, &next)) return 0;
        }
        put32(GS_OUT_PACKET_P, (int32_t)next);
    }
    return 1;
}
