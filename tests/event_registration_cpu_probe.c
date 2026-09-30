/* Synthetic frame exercises the same source CPU loop as continuous entry.
 * This fixture does not claim a retail boot or BIOS register snapshot. */
#include "../pc_port/mips_formatter.c"
#include "musashi_bios_events.h"
#include <assert.h>
#include <stdio.h>

typedef struct EventProbe {
    MusashiBiosEvents table;
    unsigned calls, refuse, enters, exits, enabled;
    const void *continuation;
} EventProbe;

static int probe_checkpoint(void *userdata, const void *continuation) {
    EventProbe *p = userdata;
    if (p->continuation) assert(p->continuation == continuation);
    p->continuation = continuation;
    return 1;
}
static int probe_enter(void *userdata, int32_t *previous) {
    EventProbe *p = userdata;
    p->enters++;
    *previous = p->enabled;
    p->enabled = 0;
    return 1;
}
static int probe_exit(void *userdata) {
    EventProbe *p = userdata;
    p->exits++;
    p->enabled = 1;
    return 1;
}
static int probe_open(void *userdata, uint32_t cl, uint32_t spec,
                      uint32_t mode, uint32_t callback, int32_t *handle) {
    EventProbe *p = userdata;
    assert(!p->enabled);
    if (++p->calls == p->refuse) return 0;
    return musashi_bios_events_open(&p->table, cl, spec, mode, callback, handle);
}
static int probe_enable(void *userdata, int32_t handle, int32_t *result) {
    EventProbe *p = userdata;
    assert(!p->enabled);
    if (++p->calls == p->refuse) return 0;
    return musashi_bios_events_enable(&p->table, handle, result);
}
static int probe_test(void *userdata, int32_t handle, int32_t *result) {
    EventProbe *p = userdata;
    assert(!p->enabled);
    if (++p->calls == p->refuse) return 0;
    return musashi_bios_events_test(&p->table, handle, result);
}
static void probe_case(unsigned refuse, unsigned initial_enabled, int full) {
    static MusashiBootMemory memory;
    EventProbe p = {0};
    MusashiCallbackDevice callback = {0};
    MusashiCdDevice cd = {0};
    MusashiEventDevice events = {0};
    FormatterCpu cpu = {0};
    MusashiEntryRunStop stop = {0};
    int32_t result = 0x12345678;
    unsigned i;
    unsigned expected_refusal = refuse ? refuse : full ? 9u : 0u;
    memset(&memory, 0, sizeof(memory));
    p.refuse = refuse;
    p.enabled = initial_enabled;
    if (full)
        for (i = 0; i < MUSASHI_BIOS_EVENTS_CAPACITY; ++i)
            p.table.records[i].status = 0x1000;
    for (i = 0; i < 8; ++i) {
        assert(musashi_boot_write32(&memory, 0x80078c3cu + i * 4u, 0xdeadbeef));
        assert(musashi_boot_write32(&memory, 0x80078c5cu + i * 4u, 7));
    }
    callback.userdata = &p;
    callback.checkpoint = probe_checkpoint;
    callback.exit_critical_section = probe_exit;
    cd.userdata = &p;
    cd.enter_critical_result = probe_enter;
    events.userdata = &p;
    events.open_event = probe_open;
    events.enable_event = probe_enable;
    events.b0_0b = probe_test;
    /* The legacy aggregate critical callbacks remain absent deliberately. */
    cpu.callback_device = &callback;
    cpu.cd_device = &cd;
    cpu.event_device = &events;
    cpu.pc = EVENT_REGISTER;
    cpu.npc = cpu.pc + 4;
    cpu.r[16] = 0xaabbccdd;
    cpu.r[28] = 0x80074750;
    cpu.r[29] = 0x801fff80;
    cpu.r[31] = UINT_MAX;
    {
        MusashiResetGraphPrefixStatus status = run_startup_cpu(&memory, &cpu, NULL, NULL,
            &result, &stop.boundary, &stop, NULL, NULL);
        if (status != (expected_refusal ? MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL : MUSASHI_RESETGRAPH_PREFIX_COMPLETE))
            fprintf(stderr, "case refuse=%u enabled=%u full=%d status=%d pc=%08x target=%08x calls=%u\n",
                refuse, initial_enabled, full, status, cpu.pc, stop.boundary.target_address, p.calls);
        assert(status == (expected_refusal ? MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL : MUSASHI_RESETGRAPH_PREFIX_COMPLETE));
    }
    assert(p.enters == 1 && p.calls == (expected_refusal ? expected_refusal : 24));
    assert(p.exits == (!expected_refusal && initial_enabled));
    assert(p.enabled == (!expected_refusal && initial_enabled));
    assert(p.continuation == &cpu);
    if (!expected_refusal) {
        /* Actual comparison literal, even when SYS2 is skipped. */
        assert(result == 1 && cpu.r[16] == 0xaabbccdd);
        assert(cpu.r[29] == 0x801fff80 && cpu.r[28] == 0x80074750);
    } else {
        assert(result == 0x12345678);
        assert(stop.boundary.target_address == (expected_refusal <= 8 ? 0x8005ce78u :
                                                expected_refusal <= 16 ? 0x8005ceb8u : 0x8005cea8u));
    }
    for (i = 0; i < 8; ++i) {
        uint32_t handle, flag;
        unsigned opened = !refuse || i + 1 < refuse;
        assert(musashi_boot_read32(&memory, 0x80078c3cu + i * 4u, &handle));
        assert(musashi_boot_read32(&memory, 0x80078c5cu + i * 4u, &flag));
        assert(handle == (opened ? (full ? UINT_MAX : 0xf1000000u + i) : 0xdeadbeef));
        assert(flag == (expected_refusal ? 7u : 0u));
        if (!full)
            assert(p.table.records[i].status == (!opened ? 0u :
                (!refuse || i + 9 < refuse) ? 0x2000u : 0x1000u));
    }
}
/* SOURCE_IRQ_EXECUTE uses run_startup_cpu with entry_stop=NULL.
 * TestEvent (8005CEA8) must still deliver B0:0B when event_device is bound. */
static int irq_testevent(void *userdata, int32_t handle, int32_t *result) {
    unsigned *calls = userdata;
    ++*calls;
    if (handle != (int32_t)0xf1000008) return 0;
    *result = 0;
    return 1;
}

static void probe_source_irq_testevent(void) {
    static MusashiBootMemory memory;
    MusashiCallbackDevice callback = {0};
    MusashiEventDevice events = {0};
    FormatterCpu cpu = {0};
    MusashiResetGraphPrefixStop stop = {0};
    int32_t result = 0x12345678;
    unsigned calls = 0;
    MusashiResetGraphPrefixStatus status;
    memset(&memory, 0, sizeof(memory));
    events.userdata = &calls;
    events.b0_0b = irq_testevent;
    cpu.event_device = &events;
    cpu.callback_device = &callback;
    cpu.pc = 0x8005cea8u;
    cpu.npc = cpu.pc + 4u;
    cpu.r[4] = 0xf1000008u;
    cpu.r[31] = UINT_MAX;
    status = run_startup_cpu(&memory, &cpu, NULL, NULL, &result, &stop,
                             NULL, NULL, NULL);
    assert(status == MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
    assert(calls == 1 && result == 0 && cpu.r[2] == 0 && cpu.pc == UINT_MAX);
}

int main(void) {
    unsigned refuse;
    probe_source_irq_testevent();
    probe_case(0, 1, 0);
    probe_case(0, 0, 0);
    probe_case(0, 1, 1); /* Open=-1 stored eight times, invalid handle then refused. */
    for (refuse = 1; refuse <= 24; ++refuse) probe_case(refuse, 1, 0);
    puts("EVENT SOURCE CPU: normal/disabled/full pool and all 24 refusal positions verified");
    return 0;
}
