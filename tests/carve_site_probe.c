#include "../pc_port/mips_formatter.c"
/* Former compiled word tables: words now come from the local extract. */
#include "probe_code_words.h"
#define kMain80020598Words PROBE_WORDS(0x80020598u, "MAIN", kMain80020598Words)
#define kMain80021174Words PROBE_WORDS(0x80021174u, "MAIN", kMain80021174Words)
#define kOverlaySc02_80128158Words PROBE_WORDS(0x80128158u, "SC02_031", kOverlaySc02_80128158Words)
#define kOverlaySc02_8013CB84Words PROBE_WORDS(0x8013cb84u, "SC02_031", kOverlaySc02_8013CB84Words)

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { DATA_BASE = 0x80078000u };

typedef struct Clock {
    unsigned calls;
    unsigned total;
    MusashiExecutionClock adapter;
} Clock;

static int advance_clock(void *userdata, uint32_t cost) {
    Clock *clock = userdata;
    ++clock->calls;
    clock->total += cost;
    return 1;
}

static int stub_write_control(void *u, const MusashiCpuContext *c,
                              uint32_t sel, uint32_t v) {
    (void)u;
    (void)c;
    (void)sel;
    (void)v;
    return 1;
}
static int stub_write_data(void *u, const MusashiCpuContext *c,
                           uint32_t sel, uint32_t v) {
    (void)u;
    (void)c;
    (void)sel;
    (void)v;
    return 1;
}
static int stub_read_data(void *u, const MusashiCpuContext *c,
                          uint32_t sel, uint32_t *v) {
    (void)u;
    (void)c;
    (void)sel;
    *v = 0x11111111u;
    return 1;
}
static int stub_read_control(void *u, const MusashiCpuContext *c,
                             uint32_t sel, uint32_t *v) {
    (void)u;
    (void)c;
    (void)sel;
    *v = 0x22222222u;
    return 1;
}
static int stub_command(void *u, const MusashiCpuContext *c, uint32_t word) {
    (void)u;
    (void)c;
    (void)word;
    return 1;
}

static void init_cpu(FormatterCpu *cpu, Clock *clock, uint32_t pc,
                     const MusashiCpuTransferDevice *transfer) {
    memset(cpu, 0, sizeof(*cpu));
    memset(clock, 0, sizeof(*clock));
    cpu->pc = pc;
    cpu->npc = pc + 4u;
    clock->adapter.userdata = clock;
    clock->adapter.advance = advance_clock;
    cpu->clock = &clock->adapter;
    cpu->cpu_transfer = transfer;
}

static void step_ok(MusashiBootMemory *memory, uint32_t pc, uint32_t rs,
                    uint32_t rs_value) {
    FormatterCpu cpu;
    Clock clock = {0};
    MusashiCpuTransferDevice transfer = {
        NULL,
        NULL,
        NULL,
        stub_write_control,
        stub_write_data,
        stub_read_data,
        stub_read_control,
        stub_command,
    };
    init_cpu(&cpu, &clock, pc, &transfer);
    cpu.r[rs] = rs_value;
    cpu.r[29] = DATA_BASE;
    assert(formatter_step(memory, &cpu));
    assert(cpu.pc == pc + 4u);
    assert(clock.calls == 1u);
}

int main(void) {
    MusashiBootMemory memory = {0};
    uint32_t value = 0;
    unsigned i;

    assert(kOverlaySc02_8013CB84Words[(0x8013cc34u - 0x8013cb84u) / 4u] ==
           0x882355a3u);
    /* The sc02 fetch is gated on the resident image: stage the blanket the
     * way the loader would, then step the overlay entry to arm the gate.
     * The entry word is addiu sp,sp,-0x18 (register-only, safe to step). */
    for (i = 0; i < sizeof(kOverlaySc02_80128158Words) / sizeof(uint32_t); ++i)
        assert(musashi_boot_write32(&memory, 0x80128158u + 4u * i,
                                    kOverlaySc02_80128158Words[i]));
    /* The post-fetch verifier checks every overlay pc against RAM: stage the
     * stepped range as well. */
    for (i = 0; i < sizeof(kOverlaySc02_8013CB84Words) / sizeof(uint32_t); ++i)
        assert(musashi_boot_write32(&memory, 0x8013cb84u + 4u * i,
                                    kOverlaySc02_8013CB84Words[i]));
    step_ok(&memory, 0x80128158u, 0u, 0u);
    assert(kMain80020598Words[(0x800207c0u - 0x80020598u) / 4u] == 0x48cc0000u);
    assert(kMain80021174Words[(0x800211f8u - 0x80021174u) / 4u] == 0xebae0000u);

    assert(musashi_boot_write32(&memory, DATA_BASE, 0x10203040u));
    assert(musashi_boot_write32(&memory, DATA_BASE + 4u, 0x50607080u));
    assert(musashi_boot_write32(&memory, DATA_BASE + 8u, 0xaabbccddu));

    /* 8013CB84 unaligned 9-byte copy. */
    step_ok(&memory, 0x8013cc34u, 1u, DATA_BASE - 0x55a3u);
    step_ok(&memory, 0x8013cc40u, 1u, DATA_BASE - 0x55a0u);
    step_ok(&memory, 0x8013cc4cu, 1u, DATA_BASE - 0x55a7u);
    step_ok(&memory, 0x8013cc58u, 1u, DATA_BASE - 0x55a4u);
    step_ok(&memory, 0x8013cc68u, 6u, DATA_BASE);
    step_ok(&memory, 0x8013cc6cu, 6u, DATA_BASE);
    step_ok(&memory, 0x8013cc70u, 6u, DATA_BASE);
    step_ok(&memory, 0x8013cc74u, 6u, DATA_BASE);

    /* func_80020598 / func_80021174 COP2, LWC2 and SWC2. */
    step_ok(&memory, 0x800207c0u, 12u, 1u);
    step_ok(&memory, 0x800207ecu, 12u, 1u);
    step_ok(&memory, 0x80020800u, 0u, 0u);
    step_ok(&memory, 0x80020804u, 0u, 0u);
    step_ok(&memory, 0x800209bcu, 2u, DATA_BASE);
    step_ok(&memory, 0x800209d0u, 2u, DATA_BASE);
    step_ok(&memory, 0x8002119cu, 12u, 1u);
    step_ok(&memory, 0x800211e8u, 5u, DATA_BASE);
    step_ok(&memory, 0x800211f4u, 0u, 0u);
    step_ok(&memory, 0x800211f8u, 29u, DATA_BASE);
    step_ok(&memory, 0x80021200u, 0u, 0u);
    step_ok(&memory, 0x80021210u, 0u, 0u);

    assert(musashi_boot_read32(&memory, DATA_BASE, &value));
    (void)value;
    puts("CARVE_SITE_PASS");
    return 0;
}
