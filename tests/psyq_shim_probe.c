/* Differential check of PsyQ compat shims against the interpreted retail code.
 *
 * The port keeps the PsyQ layer as its own code (PsyCross or bfm_plat HLE),
 * never decompiled PsyQ. So each shim must be proven faithful to the
 * retail library instructions it replaces. For every shim listed below and
 * many inputs (random plus edge cases), each case:
 *   1. restores the pinned retail EXE image and a seeded arena,
 *   2. runs the retail function on the interpreter only (device-less CPU),
 *   3. restores the same state and calls the shim's wrapper directly
 *      (bfm_psyq_<name>(r), over the same o32 register file and stack),
 * then compares v0 (for returning functions) and all of guest RAM outside
 * the stack frame. VRAM is untouched by these entries (none issues a GPU
 * command), so RAM plus v0 is the whole observable effect.
 *
 * Outcomes per case: same, DIFFERENT, shim_refused (the wrapper returned 0,
 * so the lane would fall back to the retail code: faithful by construction),
 * not_comparable (the interpreter refused: device access, BIOS call).
 * Exit 0 when no comparable case differs; 77 without the EXE or the host
 * window. Prints one summary line per function.
 *
 * Synthetic inputs only; the retail code comes from the user's own EXE. */
#define _GNU_SOURCE
#include "musashi_boot_memory.h"
#include "musashi_native_lane.h"
#include "musashi_native_lane_psyq.h"
#include "psyq/bfm_psyq_compat.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

enum { ARENA = 0x80180000u, ARENA_SIZE = 0x40000u, STACK_TOP = 0x801fe000u,
       FRAME_GUARD = 0x10000u, CASES = 96 };
enum Outcome { SAME, DIFFERENT, SHIM_REFUSED, NOT_COMPARABLE, CRASHED };

typedef int (*Wrapper)(uint32_t *r);
typedef struct Shim {
    uint32_t pc;
    const char *name;
    unsigned argc;
    int returns;
    Wrapper wrap;
    void (*inputs)(unsigned k, uint32_t *a);
} Shim;

static MusashiBootMemory *g_memory;
static uint32_t rng_state;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static uint32_t arena_ptr(uint32_t align) {
    return (ARENA + 0x100u + rng() % (ARENA_SIZE - 0x2000u)) & ~(align - 1u);
}

/* ---- per-function inputs (k = case index; the first cases are edges) ---- */
static void in_memcpy(unsigned k, uint32_t *a) {
    static const uint32_t n_edge[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 255, 256};
    a[0] = arena_ptr(1);
    a[2] = k < 14 ? n_edge[k] : rng() % 600u;
    if (k % 5 == 1) a[1] = a[0] + 1u + rng() % 8u;          /* src after dst (overlap) */
    else if (k % 5 == 2) a[1] = a[0] - 1u - rng() % 8u;     /* src before dst (overlap) */
    else a[1] = arena_ptr(k % 3 ? 1 : 4);
    if (k == 20) a[0] = 0;                                  /* NULL dst */
    if (k == 21) a[1] = 0;                                  /* NULL src */
    if (k == 22) a[2] = 0x80000000u;                        /* negative n */
}
static void in_memset(unsigned k, uint32_t *a) {
    a[0] = arena_ptr(k % 2 ? 1 : 4);
    a[1] = k < 4 ? (uint32_t[]){0, 0xff, 0x1234, 0xffffff80u}[k] : rng();
    a[2] = k < 12 ? k : rng() % 600u;
    if (k == 20) a[0] = 0;
    if (k == 21) a[2] = 0xffffffffu;
}
static void in_bzero(unsigned k, uint32_t *a) {
    a[0] = k == 20 ? 0 : arena_ptr(k % 2 ? 1 : 4);
    a[1] = k < 12 ? k : k == 21 ? 0xffffffffu : rng() % 600u;
}
static void in_clearotagr(unsigned k, uint32_t *a) {
    a[0] = arena_ptr(4);
    a[1] = k < 6 ? k : rng() % 1200u;
}
static void in_catprim(unsigned k, uint32_t *a) {
    a[0] = arena_ptr(4);
    a[1] = k == 0 ? 0 : k == 1 ? 0x80ffffffu : arena_ptr(4);
}
static void in_cdinttopos(unsigned k, uint32_t *a) {
    static const uint32_t edge[] = {0, 1, 74, 75, 149, 150, 4499, 4500, 269999, 270000,
                                    0xffffffffu, 0xffffff6au, 0x7fffffffu, 0x80000000u};
    a[0] = k < 14 ? edge[k] : (k % 2 ? rng() % 400000u : rng());
    a[1] = arena_ptr(1);
}
static void in_cdpostoint(unsigned k, uint32_t *a) {
    uint32_t p = arena_ptr(1), b;
    unsigned i;
    a[0] = p;
    /* valid BCD for half the cases, arbitrary bytes for the rest */
    for (i = 0; i < 4; ++i) {
        b = rng() & 0xffu;
        if (k % 2 == 0) b = ((b >> 4) % 10u) << 4 | (b & 0xfu) % 10u;
        g_memory->bytes[(p + i) & 0x1fffffu] = (uint8_t)b;
    }
}
static void in_setdefdispenv(unsigned k, uint32_t *a) {
    unsigned i;
    a[0] = arena_ptr(k % 2 ? 2 : 4);
    for (i = 1; i < 5; ++i) a[i] = k < 2 ? (k ? 0xffffffffu : 0) : (k % 3 ? rng() % 1024u : rng());
}
static void in_gsinitcoordinate2(unsigned k, uint32_t *a) {
    a[0] = k % 3 == 0 ? 0 : arena_ptr(4);
    a[1] = arena_ptr(4);
}
static void in_gssetlightmode(unsigned k, uint32_t *a) {
    a[0] = k < 6 ? k : rng();
}

#define SHIM(pc, n, argc, ret, in) {pc, #n, argc, ret, bfm_psyq_##n, in}
static const Shim kShims[] = {
    SHIM(0x8005C324u, memcpy, 3, 1, in_memcpy),
    SHIM(0x8005C358u, memset, 3, 1, in_memset),
    SHIM(0x8005C2C8u, bzero, 2, 1, in_bzero), /* void in C, but retail sets v0 */
    SHIM(0x80059BFCu, ClearOTagR, 2, 1, in_clearotagr),
    SHIM(0x80058CE4u, CatPrim, 2, 0, in_catprim),
    SHIM(0x80043A18u, CdIntToPos, 2, 1, in_cdinttopos),
    SHIM(0x80043B1Cu, CdPosToInt, 1, 1, in_cdpostoint),
    SHIM(0x80058B04u, SetDefDispEnv, 5, 1, in_setdefdispenv),
    SHIM(0x80052D90u, GsInitCoordinate2, 2, 0, in_gsinitcoordinate2),
    SHIM(0x800538ECu, GsSetLightMode, 1, 0, in_gssetlightmode),
};

static void seed(MusashiBootMemory *memory, const uint8_t *image, uint32_t salt) {
    uint32_t p;
    memcpy(memory->bytes, image, MUSASHI_RAM_SIZE);
    rng_state = salt | 1u;
    for (p = ARENA; p < ARENA + ARENA_SIZE; p += 4u)
        musashi_boot_write32(memory, p, rng());
}

static int run_case(MusashiBootMemory *memory, const uint8_t *image, const Shim *s,
                    unsigned k, uint8_t *reference, uint8_t *start) {
    uint32_t a[8] = {0}, v0_ref = 0, r[32];
    unsigned i;
    int ok;
    seed(memory, image, s->pc ^ (k * 0x9e3779b9u));
    s->inputs(k, a);
    memcpy(start, memory->bytes, MUSASHI_RAM_SIZE);
    alarm(5);
    ok = musashi_native_lane_reference_call(memory, s->pc, a, s->argc, STACK_TOP, 0, &v0_ref, NULL);
    alarm(0);
    if (!ok) return NOT_COMPARABLE;
    memcpy(reference, memory->bytes, MUSASHI_RAM_SIZE);
    memcpy(memory->bytes, start, MUSASHI_RAM_SIZE);
    /* Same entry state as the reference CPU: args in a0-a3, the rest at
     * sp+0x10, gp as the reference call sets it. */
    memset(r, 0, sizeof r);
    for (i = 0; i < 4 && i < s->argc; ++i) r[4 + i] = a[i];
    for (i = 4; i < s->argc; ++i) musashi_boot_write32(memory, STACK_TOP + 4u * i, a[i]); /* arg4 at sp+0x10 */
    r[28] = 0x80074750u;
    r[29] = STACK_TOP;
    r[31] = 0x80001000u;
    if (!s->wrap(r)) return SHIM_REFUSED;
    if (s->returns && r[2] != v0_ref) {
        fprintf(stderr, "psyq_shim: %s case %u args %08x %08x %08x v0 retail=%08x shim=%08x\n",
                s->name, k, a[0], a[1], a[2], v0_ref, r[2]);
        return DIFFERENT;
    }
    {
        const uint32_t lo = STACK_TOP - FRAME_GUARD - 0x80000000u;
        const uint32_t hi = STACK_TOP + 0x10u + 4u * 8u - 0x80000000u;
        uint32_t off, shown = 0;
        for (off = 0; off < MUSASHI_RAM_SIZE; ++off) {
            if (off >= lo && off < hi) continue;
            if (memory->bytes[off] != reference[off]) {
                if (shown++ < 4)
                    fprintf(stderr, "psyq_shim: %s case %u args %08x %08x %08x RAM %08x retail=%02x shim=%02x\n",
                            s->name, k, a[0], a[1], a[2], 0x80000000u + off, reference[off],
                            memory->bytes[off]);
            }
        }
        if (shown) return DIFFERENT;
    }
    return SAME;
}

int main(int argc, char **argv) {
    MusashiBootMemory *memory;
    uint8_t *image, *reference, *start, *exe;
    FILE *file;
    size_t exe_size, i;
    unsigned bad_functions = 0;
    const char *only = getenv("PSYQ_SHIM_PROBE_ONLY");
    if (argc != 2) {
        fprintf(stderr, "usage: %s SLUS_007.26\n", argv[0]);
        return 2;
    }
    file = fopen(argv[1], "rb");
    if (!file) { fprintf(stderr, "psyq_shim: EXE unavailable; NOT_RUN\n"); return 77; }
    exe = malloc(0x65000u);
    exe_size = exe ? fread(exe, 1, 0x65000u, file) : 0;
    fclose(file);
    memory = mmap((void *)(uintptr_t)MUSASHI_NATIVE_LANE_GUEST_BASE, sizeof(*memory),
                  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (memory == MAP_FAILED || (uintptr_t)memory != MUSASHI_NATIVE_LANE_GUEST_BASE) {
        fprintf(stderr, "psyq_shim: host window at 0x80000000 unavailable; NOT_RUN\n");
        return 77;
    }
    if (!musashi_boot_map_exe(memory, exe, exe_size)) {
        fprintf(stderr, "psyq_shim: pinned EXE rejected\n");
        return 1;
    }
    g_memory = memory;
    musashi_native_lane_psyq_bind(memory, NULL);
    image = malloc(MUSASHI_RAM_SIZE);
    reference = malloc(MUSASHI_RAM_SIZE);
    start = malloc(MUSASHI_RAM_SIZE);
    if (!image || !reference || !start) return 1;
    memcpy(image, memory->bytes, MUSASHI_RAM_SIZE);
    for (i = 0; i < sizeof kShims / sizeof kShims[0]; ++i) {
        const Shim *s = &kShims[i];
        unsigned n[5] = {0}, k;
        if (only && strcmp(only, s->name) != 0) continue;
        for (k = 0; k < CASES; ++k) {
            int status, outcome;
            pid_t child;
            fflush(stderr);
            child = fork();
            if (child == 0) {
                signal(SIGALRM, SIG_DFL);
                _exit(run_case(memory, image, s, k, reference, start));
            }
            if (child < 0 || waitpid(child, &status, 0) != child) return 1;
            outcome = WIFEXITED(status) && WEXITSTATUS(status) <= NOT_COMPARABLE ? WEXITSTATUS(status)
                                                                                  : CRASHED;
            if (outcome == CRASHED)
                fprintf(stderr, "psyq_shim: %s case %u crashed (status %d)\n", s->name, k, status);
            n[outcome]++;
        }
        printf("psyq_shim: %-18s %08x same=%u different=%u shim_refused=%u not_comparable=%u crashed=%u -> %s\n",
               s->name, s->pc, n[SAME], n[DIFFERENT], n[SHIM_REFUSED], n[NOT_COMPARABLE], n[CRASHED],
               n[DIFFERENT] || n[CRASHED] ? "DIFFERS" : n[SAME] ? "faithful on tested inputs"
                                                              : "untested (nothing comparable)");
        if (n[DIFFERENT] || n[CRASHED]) bad_functions++;
    }
    return bad_functions ? 1 : 0;
}
