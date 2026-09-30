/* libgte HLE for the stateless matrix routines, exact to the retail code.
 *
 * RotMatrix uses the game's own packed sin/cos table in guest RAM
 * (D_8006DF1C: 4096 words, sin in the low half, cos in the high half,
 * indexed by angle & 0xFFF), not host trigonometry, and reproduces the
 * retail composition and >>12 rounding of func_8004978C term by term.
 * ScaleMatrix (func_8004901C) and TransMatrix (func_80052430) likewise.
 * MATRIX in guest RAM: short m[3][3] (+0..+0x11), pad (+0x12), int t[3] (+0x14). */
#include "bfm_psyq_compat.h"

#include <stdint.h>

#include <string.h>

#define RCOSSIN_TABLE 0x8006DF1Cu

#define A(n) bfm_psyq_arg(r, n)

static int32_t s16at(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | (p[1] << 8)); }
static void w16(uint8_t *p, int32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((uint32_t)v >> 8); }
static int32_t s32at(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static void w32(uint8_t *p, int32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)((uint32_t)v >> 8);
    p[2] = (uint8_t)((uint32_t)v >> 16); p[3] = (uint8_t)((uint32_t)v >> 24);
}

/* 32-bit product (mflo of multu), arithmetic >> 12 */
static int32_t mul12(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a * (uint32_t)b) >> 12;
}

/* sin/cos of one angle from the guest table, with the retail sign handling */
static int sincos(int32_t angle, int32_t *s, int32_t *c) {
    const uint8_t *tbl = (const uint8_t *)bfm_psyq_ptr(RCOSSIN_TABLE, 4096u * 4u);
    const uint8_t *e;
    if (!tbl) return 0;
    if (angle >= 0) {
        e = tbl + 4u * ((uint32_t)angle & 0xFFFu);
        *s = s16at(e);
    } else {
        e = tbl + 4u * ((uint32_t)(-angle) & 0xFFFu);
        *s = -s16at(e);
    }
    *c = s16at(e + 2);
    return 1;
}

int bfm_psyq_rotmatrix(const int16_t ang[3], uint8_t *m) {
    int32_t sx, cx, sy, cy, sz, cz, t;
    if (!sincos(ang[0], &sx, &cx) || !sincos(ang[1], &sy, &cy) || !sincos(ang[2], &sz, &cz))
        return 0;
    w16(m + 0x04, sy);                            /* m[0][2] */
    w16(m + 0x0A, (int32_t)(0u - (uint32_t)cy * (uint32_t)sx) >> 12);   /* m[1][2] = -(cy*sx) >> 12 */
    w16(m + 0x10, mul12(cy, cx));                 /* m[2][2] */
    w16(m + 0x00, mul12(cz, cy));                 /* m[0][0] */
    w16(m + 0x02, (int32_t)(0u - (uint32_t)sz * (uint32_t)cy) >> 12);   /* m[0][1] */
    t = mul12(cz, -sy);
    w16(m + 0x06, mul12(sz, cx) - mul12(t, sx));  /* m[1][0] */
    w16(m + 0x0C, mul12(sz, sx) + mul12(t, cx));  /* m[2][0] */
    t = mul12(sz, -sy);
    w16(m + 0x08, mul12(cz, cx) + mul12(t, sx));  /* m[1][1] */
    w16(m + 0x0E, mul12(cz, sx) - mul12(t, cx));  /* m[2][1] */
    return 1;
}

void bfm_psyq_scalematrix(uint8_t *m, const int32_t v[3]) {
    int k;
    for (k = 0; k < 8; k++) w16(m + 2 * k, mul12(s16at(m + 2 * k), v[k % 3]));
    w32(m + 0x10, mul12(s16at(m + 0x10), v[2]));  /* m[2][2]: the whole word (pad included) */
}

void bfm_psyq_transmatrix(uint8_t *m, const int32_t v[3]) {
    w32(m + 0x14, v[0]);
    w32(m + 0x18, v[1]);
    w32(m + 0x1C, v[2]);
}

/* RotMatrixX/Y/Z(long angle, MATRIX *m) -> m: retail func_80049F3C /
 * func_8004A0DC / func_8004A27C multiply m in place by one axis rotation:
 * rows (p, q) become ((c*p - t*q) >> 12, (t*p + c*q) >> 12) from the
 * original values, the subtraction before the shift. t is the sine (X, Z)
 * or its negation (Y). */
static int rot_axis(uint32_t *r, int row_p, int row_q, int negate_sin) {
    uint8_t *m = (uint8_t *)bfm_psyq_ptr(A(1), 32);
    int32_t s, c, t, p[3], q[3];
    int j;
    if (!m || !sincos((int32_t)A(0), &s, &c)) return 0;
    t = negate_sin ? -s : s;
    for (j = 0; j < 3; j++) {
        p[j] = s16at(m + row_p * 6 + 2 * j);
        q[j] = s16at(m + row_q * 6 + 2 * j);
    }
    for (j = 0; j < 3; j++) {
        uint32_t cp = (uint32_t)c * (uint32_t)p[j], tq = (uint32_t)t * (uint32_t)q[j];
        uint32_t tp = (uint32_t)t * (uint32_t)p[j], cq = (uint32_t)c * (uint32_t)q[j];
        w16(m + row_p * 6 + 2 * j, (int32_t)(cp - tq) >> 12);
        w16(m + row_q * 6 + 2 * j, (int32_t)(tp + cq) >> 12);
    }
    r[2] = A(1);
    return 1;
}

int bfm_psyq_RotMatrixX(uint32_t *r) { return rot_axis(r, 1, 2, 0); }
int bfm_psyq_RotMatrixY(uint32_t *r) { return rot_axis(r, 0, 2, 1); }
int bfm_psyq_RotMatrixZ(uint32_t *r) { return rot_axis(r, 0, 1, 0); }

/* ratan2(y, x): retail func_8004CFEC over the game's atan table D_80071F1C
 * (1026 halfwords, 0x400 = 90 degrees). INT_MIN inputs refuse (the retail
 * negu leaves them negative). */
#define RATAN_TABLE 0x80071F1Cu
int bfm_psyq_ratan2(uint32_t *r) {
    int32_t y = (int32_t)A(0), x = (int32_t)A(1), idx, v;
    int nx = 0, ny = 0;
    const uint8_t *tbl = (const uint8_t *)bfm_psyq_ptr(RATAN_TABLE, 1026u * 2u);
    if (!tbl || x == INT32_MIN || y == INT32_MIN) return 0;
    if (x < 0) { nx = 1; x = -x; }
    if (y < 0) { ny = 1; y = -y; }
    if (x == 0 && y == 0) { r[2] = 0; return 1; }
    if (y < x) {
        idx = (y & 0x7FE00000) ? y / (x >> 10) : (int32_t)((uint32_t)y << 10) / x;
        if (idx < 0 || idx > 1025) return 0;
        v = s16at(tbl + 2 * idx);
    } else {
        idx = (x & 0x7FE00000) ? x / (y >> 10) : (int32_t)((uint32_t)x << 10) / y;
        if (idx < 0 || idx > 1025) return 0;
        v = 0x400 - s16at(tbl + 2 * idx);
    }
    if (nx) v = 0x800 - v;
    if (ny) v = -v;
    r[2] = (uint32_t)v;
    return 1;
}

/* RotMatrixYXZ(SVECTOR *r, MATRIX *m) -> m: retail func_80049A1C term by
 * term (same table and sign handling as RotMatrix; multu low 32 bits, then
 * an arithmetic >> 12, stored as halfwords). */
int bfm_psyq_RotMatrixYXZ(uint32_t *r) {
    uint8_t *v = (uint8_t *)bfm_psyq_ptr(A(0), 6), *m = (uint8_t *)bfm_psyq_ptr(A(1), 32);
    int32_t sx, cx, sy, cy, sz, cz, t;
    if (!v || !m) return 0;
    if (!sincos(s16at(v), &sx, &cx) || !sincos(s16at(v + 2), &sy, &cy) ||
        !sincos(s16at(v + 4), &sz, &cz))
        return 0;
    w16(m + 0x0A, -sx);                           /* m[1][2] */
    w16(m + 0x04, mul12(sy, cx));                 /* m[0][2] */
    w16(m + 0x10, mul12(cy, cx));                 /* m[2][2] */
    w16(m + 0x06, mul12(sz, cx));                 /* m[1][0] */
    w16(m + 0x08, mul12(cz, cx));                 /* m[1][1] */
    t = mul12(sy, sx);
    w16(m + 0x00, mul12(cy, cz) + mul12(t, sz));  /* m[0][0] */
    w16(m + 0x02, -mul12(cy, sz) + mul12(t, cz)); /* m[0][1] */
    t = mul12(cy, sx);
    w16(m + 0x0E, mul12(sy, sz) + mul12(t, cz));  /* m[2][1] */
    w16(m + 0x0C, -mul12(sy, cz) + mul12(t, sz)); /* m[2][0] */
    r[2] = A(1);
    return 1;
}

/* RotMatrix(SVECTOR *r, MATRIX *m) -> m */
int bfm_psyq_RotMatrix(uint32_t *r) {
    uint8_t *v = (uint8_t *)bfm_psyq_ptr(A(0), 6), *m = (uint8_t *)bfm_psyq_ptr(A(1), 32);
    int16_t ang[3];
    if (!v || !m) return 0;
    ang[0] = (int16_t)s16at(v); ang[1] = (int16_t)s16at(v + 2); ang[2] = (int16_t)s16at(v + 4);
    if (!bfm_psyq_rotmatrix(ang, m)) return 0;
    r[2] = A(1);
    return 1;
}

/* ScaleMatrix(MATRIX *m, VECTOR *v) -> m */
int bfm_psyq_ScaleMatrix(uint32_t *r) {
    uint8_t *m = (uint8_t *)bfm_psyq_ptr(A(0), 32), *vp = (uint8_t *)bfm_psyq_ptr(A(1), 12);
    int32_t v[3];
    if (!m || !vp) return 0;
    v[0] = s32at(vp); v[1] = s32at(vp + 4); v[2] = s32at(vp + 8);
    bfm_psyq_scalematrix(m, v);
    r[2] = A(0);
    return 1;
}

