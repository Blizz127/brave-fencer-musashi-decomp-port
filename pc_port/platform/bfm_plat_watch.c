#include "bfm_plat_watch.h"
#include "bfm_plat_mods.h"
#include "bfm_plat_events.h"

#include <string.h>

#define RAM_MASK 0x1FFFFFu
#define PAGE_SHIFT 12
#define PAGES ((RAM_MASK + 1u) >> PAGE_SHIFT)   /* 512 x 4 KiB */

typedef struct Watch {
    int id;
    uint32_t addr;                 /* physical */
    uint32_t len;
    BfmPlatWatchFn fn;
    void *user;
    uint8_t snap[BFM_PLAT_WATCH_LEN_MAX];
    int have_snap;
} Watch;

static Watch watches[BFM_PLAT_WATCH_MAX];
static size_t count;
static int next_id = 1;
static uint32_t lo = 0xFFFFFFFFu, hi;
static uint8_t pages[PAGES / 8];

static void rebuild(void) {
    size_t i;
    uint32_t p;
    lo = 0xFFFFFFFFu;
    hi = 0;
    memset(pages, 0, sizeof pages);
    for (i = 0; i < count; i++) {
        uint32_t a = watches[i].addr, e = a + watches[i].len;   /* exclusive */
        if (a < lo) lo = a;
        if (e > hi) hi = e;
        for (p = a >> PAGE_SHIFT; p <= (e - 1) >> PAGE_SHIFT; p++)
            pages[p >> 3] |= (uint8_t)(1u << (p & 7));
    }
}

static void snapshot(Watch *w) {
    w->have_snap = bfm_plat_guest_read(w->addr | 0x80000000u, w->snap, w->len) == BFM_PLAT_OK;
}

int bfm_plat_watch_add(uint32_t address, uint32_t len, BfmPlatWatchFn fn,
                       void *user) {
    Watch *w;
    uint32_t a = address & RAM_MASK;
    if (!fn || len == 0 || len > BFM_PLAT_WATCH_LEN_MAX || a + len > RAM_MASK + 1u)
        return BFM_PLAT_INVALID;
    if (count >= BFM_PLAT_WATCH_MAX) return BFM_PLAT_NO_SPACE;
    w = &watches[count++];
    memset(w, 0, sizeof *w);
    w->id = next_id++;
    w->addr = a;
    w->len = len;
    w->fn = fn;
    w->user = user;
    snapshot(w);
    rebuild();
    return w->id;
}

int bfm_plat_watch_remove(int id) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (watches[i].id == id) {
            memmove(&watches[i], &watches[i + 1], (count - i - 1) * sizeof watches[0]);
            count--;
            rebuild();
            return BFM_PLAT_OK;
        }
    }
    return BFM_PLAT_NOT_FOUND;
}

size_t bfm_plat_watch_count(void) { return count; }

int bfm_plat_watch_drop_from(int first_id);
int bfm_plat_watch_drop_from(int first_id) {
    size_t i, j = 0;
    int dropped = 0;
    for (i = 0; i < count; i++) {
        if (watches[i].id >= first_id) { dropped++; continue; }
        watches[j++] = watches[i];
    }
    count = j;
    rebuild();
    return dropped;
}

int bfm_plat_watch_next_id(void);
int bfm_plat_watch_next_id(void) { return next_id; }

int bfm_plat_watch_hit(uint32_t address, uint32_t len) {
    uint32_t a = address & RAM_MASK, p, e;
    if (!count || len == 0) return 0;
    e = a + len;
    if (e <= lo || a >= hi) return 0;
    for (p = a >> PAGE_SHIFT; p <= (e - 1) >> PAGE_SHIFT && p < PAGES; p++)
        if (pages[p >> 3] & (1u << (p & 7))) return 1;
    return 0;
}

void bfm_plat_watch_on_write(uint32_t address, uint32_t len,
                             const void *old_bytes, const void *new_bytes) {
    uint32_t a = address & RAM_MASK;
    size_t i, n = count;
    const uint8_t *ob = (const uint8_t *)old_bytes, *nb = (const uint8_t *)new_bytes;
    if (!old_bytes || !new_bytes || len == 0) return;
    for (i = 0; i < n && i < count; i++) {
        Watch *w = &watches[i];
        uint8_t before[BFM_PLAT_WATCH_LEN_MAX], after[BFM_PLAT_WATCH_LEN_MAX];
        uint32_t s = a > w->addr ? a : w->addr;
        uint32_t e = (a + len < w->addr + w->len) ? a + len : w->addr + w->len;
        if (s >= e) continue;
        if (!w->have_snap) snapshot(w);
        memcpy(before, w->snap, w->len);
        memcpy(before + (s - w->addr), ob + (s - a), e - s);   /* store's view wins */
        memcpy(after, before, w->len);
        memcpy(after + (s - w->addr), nb + (s - a), e - s);
        memcpy(w->snap, after, w->len);
        w->have_snap = 1;
        if (memcmp(before, after, w->len) != 0)
            w->fn(w->user, w->addr | 0x80000000u, w->len, before, after);
    }
}

int bfm_plat_watch_poll(void) {
    size_t i, n = count;
    int fired = 0;
    for (i = 0; i < n && i < count; i++) {
        Watch *w = &watches[i];
        uint8_t now[BFM_PLAT_WATCH_LEN_MAX], before[BFM_PLAT_WATCH_LEN_MAX];
        if (bfm_plat_guest_read(w->addr | 0x80000000u, now, w->len) != BFM_PLAT_OK) continue;
        if (!w->have_snap) {
            memcpy(w->snap, now, w->len);
            w->have_snap = 1;
            continue;
        }
        if (memcmp(now, w->snap, w->len) == 0) continue;
        memcpy(before, w->snap, w->len);
        memcpy(w->snap, now, w->len);
        w->fn(w->user, w->addr | 0x80000000u, w->len, before, now);
        fired++;
    }
    bfm_plat_events_clear_code_marks();
    return fired;
}

void bfm_plat_watch_resync(void) {
    size_t i;
    for (i = 0; i < count; i++) snapshot(&watches[i]);
}

void bfm_plat_watch_reset(void) {
    count = 0;
    next_id = 1;
    rebuild();
}

/* ---------------------------------------------------------- derived */

static int32_t s16le(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | (p[1] << 8)); }
static int32_t s32le(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

static int32_t read_u16(uint32_t addr) {
    uint8_t b[2];
    if (bfm_plat_guest_read(addr, b, 2) != BFM_PLAT_OK) return 0;
    return (int32_t)(uint16_t)(b[0] | (b[1] << 8));
}

typedef struct Gauge {
    BfmEvent event;
    uint32_t max_addr;
} Gauge;

static const Gauge hp_gauge = {BFM_EVENT_DAMAGE, BFM_GUEST_HP_MAX};
static const Gauge bp_gauge = {BFM_EVENT_BP_USE, BFM_GUEST_BP_MAX};

static void on_gauge(void *user, uint32_t a, uint32_t len, const uint8_t *o,
                     const uint8_t *n) {
    const Gauge *g = (const Gauge *)user;
    BfmEventStat e;
    (void)a; (void)len;
    e.previous = s16le(o);
    e.value = s16le(n);
    if (e.value >= e.previous) return;          /* only drops */
    if (bfm_plat_events_take_code_mark(g->event)) return;   /* code site reported it */
    e.max = read_u16(g->max_addr);
    e.flags = BFM_EVENT_FLAG_DERIVED;
    bfm_plat_mods_emit(g->event, &e);
}

static void on_money(void *user, uint32_t a, uint32_t len, const uint8_t *o,
                     const uint8_t *n) {
    BfmEventStat e;
    (void)user; (void)a; (void)len;
    e.previous = s32le(o);
    e.value = s32le(n);
    e.max = 99999;
    e.flags = BFM_EVENT_FLAG_DERIVED;
    bfm_plat_mods_emit(BFM_EVENT_MONEY, &e);
}

static void on_inventory(void *user, uint32_t a, uint32_t len, const uint8_t *o,
                         const uint8_t *n) {
    uint32_t i;
    int code = bfm_plat_events_take_code_mark(BFM_EVENT_ITEM_GET);
    (void)user; (void)a;
    for (i = 0; i < len; i++) {
        if (o[i] == 0 && n[i] != 0 && !code) {
            BfmEventItem e;
            e.item = n[i];
            e.count = 1;
            e.kind = BFM_ITEM_KIND_INVENTORY;
            e.flags = BFM_EVENT_FLAG_DERIVED;
            bfm_plat_mods_emit(BFM_EVENT_ITEM_GET, &e);
        }
    }
}

static void on_figures(void *user, uint32_t a, uint32_t len, const uint8_t *o,
                       const uint8_t *n) {
    uint32_t i;
    (void)user; (void)a;
    for (i = 0; i < len; i++) {
        if (!(o[i] & BFM_GUEST_FIGURE_OWNED) && (n[i] & BFM_GUEST_FIGURE_OWNED)) {
            BfmEventItem e;
            e.item = i;
            e.count = 1;
            e.kind = BFM_ITEM_KIND_FIGURE;
            e.flags = BFM_EVENT_FLAG_DERIVED;
            bfm_plat_mods_emit(BFM_EVENT_ITEM_GET, &e);
        }
    }
}

int bfm_plat_derived_events_enable(void) {
    int n = 0;
    n += bfm_plat_watch_add(BFM_GUEST_HP, 2, on_gauge, (void *)&hp_gauge) > 0;
    n += bfm_plat_watch_add(BFM_GUEST_BP, 2, on_gauge, (void *)&bp_gauge) > 0;
    n += bfm_plat_watch_add(BFM_GUEST_MONEY, 4, on_money, NULL) > 0;
    n += bfm_plat_watch_add(BFM_GUEST_SCRIPT_VARS + BFM_GUEST_FIGURE_VAR0,
                            BFM_GUEST_FIGURE_COUNT, on_figures, NULL) > 0;
    n += bfm_plat_watch_add(BFM_GUEST_SCRIPT_VARS + BFM_GUEST_INV_VAR0,
                            BFM_GUEST_INV_SLOTS, on_inventory, NULL) > 0;
    return n;
}
