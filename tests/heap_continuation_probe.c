/* Synthetic continuation/capability fixture, actual retail source instructions.
 * No BIOS heap implementation, captured CPU or native-boot claim.
 * Optional acceptance-only heap capability below is explicitly synthetic. */
#include "../pc_port/mips_formatter.c"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define PROBE_SP UINT32_C(0x801fffc0)
#define PROBE_RESULT INT32_C(0x12345678)
static MusashiBootMemory memory, expected_memory;
static const uint32_t path[] = {
    0x80010204,0x80010208,0x80010214,0x80010218,
    0x80014238,0x8001423c,0x80014240,0x80014244,
    0x80014928,0x8001492c,0x80014930,0x80014934,
    0x8005cf08,0x8005cf0c,0x8005cf10,0x8005cf14,
    0x80014938,0x8001493c,0x80014940,0x80014944,
    0x8005ce18,0x8005ce1c,0x8005ce20,0x000000a0,
    0x80014948,0x8001494c,0x8005cf18,0x8005cf1c,
    0x8005cf20,0x8005cf24,0x80014950,0x80014954,
    0x80014958,0x8001495c,0x80014248,0x8001424c,0x80059234
};
typedef struct Probe {
    const FormatterCpu *cpu;
    unsigned enabled, enters, exits, refuse_enter, refuse_clock;
    unsigned checkpoints, clock_calls, ticks, direct_syscall;
    unsigned initial_enabled, heap_calls, accept_heap, stop_reset;
    unsigned stop_after_sys;
} Probe;

static int checkpoint(void *userdata, const void *continuation) {
    Probe *p = userdata;
    const FormatterCpu *cpu = continuation;
    assert(cpu == p->cpu);
    if (p->direct_syscall && p->stop_after_sys && p->checkpoints == 1) return 0;
    if (!p->direct_syscall) {
        assert(p->checkpoints < sizeof(path)/sizeof(path[0]));
        assert(cpu->pc == path[p->checkpoints]);
        if (cpu->pc == 0x80014238u) {
            assert(cpu->r[29] == PROBE_SP && cpu->r[31] == 0x8001021cu);
        } else if (cpu->pc == 0x80014928u) {
            assert(cpu->r[29] == PROBE_SP-0x18u && cpu->r[31] == 0x80014248u);
        } else if (cpu->pc == 0x8005cf08u) {
            assert(cpu->r[29] == PROBE_SP-0x30u && cpu->r[31] == 0x80014938u);
        } else if (cpu->pc == 0x8005ce18u) {
            assert(cpu->r[4] == 0x801ff800u && cpu->r[5] == 0x800u);
            assert(cpu->r[31] == 0x80014948u);
        } else if (cpu->pc == 0x8005ce20u) {
            assert(cpu->npc == 0xa0u && cpu->r[10] == 0xa0u);
        }
    }
    ++p->checkpoints;
    return 1;
}
static int advance(void *userdata, uint32_t ticks) {
    Probe *p = userdata;
    ++p->clock_calls;
    assert(p->cpu->pc != 0xa0u); /* Host heap service has no fabricated BIOS clock. */
    if (p->cpu->pc == 0x8005cf0cu || p->cpu->pc == 0x8005cf1cu)
        assert(ticks == 2u); /* Fetched SYSCALL only, no native BIOS duration. */
    if (p->stop_reset && p->cpu->pc == 0x80059234u) return 0;
    if (p->refuse_clock) return 0;
    p->ticks += ticks;
    return 1;
}
static int enter(void *userdata, int32_t *previous) {
    Probe *p = userdata;
    assert(p->cpu->pc == 0x8005cf0cu && p->cpu->npc == 0x8005cf10u);
    assert(p->cpu->r[4] == 1u && previous);
    ++p->enters;
    if (p->refuse_enter) return 0;
    *previous = (int32_t)p->enabled;
    p->enabled = 0;
    return 1;
}
static int exit_critical(void *userdata) {
    Probe *p = userdata;
    ++p->exits;
    assert(p->accept_heap && p->heap_calls == 1);
    assert(p->cpu->pc == 0x8005cf1cu && p->cpu->npc == 0x8005cf20u);
    assert(p->cpu->r[4] == 2u && p->cpu->r[31] == 0x80014950u);
    p->enabled = 1; /* Source exit is unconditional, even after prior enable0. */
    return 1;
}
static int init_heap(void *userdata, uint32_t base, uint32_t size) {
    Probe *p = userdata;
    assert(p->cpu->pc == 0xa0u && p->cpu->npc == 0xa4u);
    assert(p->cpu->r[9] == 0x39u && p->cpu->r[10] == 0xa0u);
    assert(p->cpu->r[31] == 0x80014948u && p->cpu->r[29] == PROBE_SP-0x30u);
    assert(base == 0x801ff800u && size == 0x800u);
    assert(p->cpu->r[2] == p->initial_enabled && !p->enabled);
    ++p->heap_calls;
    /* Fixture capability acceptance only: no guest register or RAM mutation.
     * This is deliberately not evidence of native heap initialization. */
    return p->accept_heap ? 1 : 0;
}
static void sink(void *userdata, const uint8_t *bytes, int32_t length) {
    (void)userdata; (void)bytes; (void)length;
    assert(!"No console output belongs to this source prefix");
}

/* mode:0 accepted enter,1 missing,2 refused,3 first clock refusal,
 * 4 malformed syscall selector,5 malformed syscall npc;
 * 6 accepted synthetic heap,7 refused synthetic heap;
 * 8..11 malformed A0 t1/t2/npc/RA (explicit boundary-only fixtures). */
static void run_case(const uint8_t *exe, size_t exe_size, unsigned enabled,
                     unsigned mode) {
    FormatterCpu cpu = {0}, before;
    Probe p = {0};
    MusashiCallbackDevice callback = {0};
    MusashiCdDevice cd = {0};
    MusashiHeapDevice heap = {&p,init_heap};
    MusashiExecutionClock clock = {&p,advance};
    MusashiEntryRunStop stop = {0};
    MusashiResetGraphPrefixStatus status;
    int32_t result = PROBE_RESULT;
    memset(&memory,0xa5,sizeof(memory));
    assert(musashi_boot_map_exe(&memory,exe,exe_size));
    expected_memory = memory;
    p.cpu = &cpu; p.enabled = enabled; p.initial_enabled = enabled;
    p.accept_heap = mode == 6; p.stop_reset = mode == 6;
    p.refuse_enter = mode == 2; p.refuse_clock = mode == 3;
    p.direct_syscall = mode == 4 || mode == 5 || mode >= 8;
    callback.userdata = &p; callback.checkpoint = checkpoint;
    callback.exit_critical_section = exit_critical;
    cd.userdata = &p;
    if (mode != 1) cd.enter_critical_result = enter;
    cpu.callback_device = &callback; cpu.cd_device = &cd; cpu.clock = &clock;
    if (mode >= 6) cpu.heap_device = &heap;
    cpu.pc = 0x80010204; cpu.npc = cpu.pc+4;
    cpu.r[2] = PROBE_RESULT;
    cpu.r[4] = 0xa0a0a0a0; cpu.r[5] = 0xa1a1a1a1;
    cpu.r[9] = 0x99999999; cpu.r[10] = 0xaaaaaaaa;
    cpu.r[16] = 0x16161616; cpu.r[17] = 0x17171717;
    cpu.r[29] = PROBE_SP; cpu.r[30] = 0x30303030; cpu.r[31] = 0x31313131;
    if (mode == 4 || mode == 5) {
        cpu.pc = 0x8005cf0c;
        cpu.npc = mode == 5 ? cpu.pc+8 : cpu.pc+4;
        cpu.r[4] = mode == 4 ? 2u : 1u;
    }
    if (mode >= 8) {
        cpu.pc = 0xa0; cpu.npc = mode == 10 ? 0xa8 : 0xa4;
        cpu.r[9] = mode == 8 ? 0x38 : 0x39;
        cpu.r[10] = mode == 9 ? 0xb0 : 0xa0;
        cpu.r[31] = mode == 11 ? 0x8001494c : 0x80014948;
        cpu.r[4] = 0x801ff800; cpu.r[5] = 0x800;
    }
    before = cpu;
    status = run_startup_cpu(&memory,&cpu,sink,NULL,&result,
                            &stop.boundary,&stop,NULL,NULL);
    assert(result == PROBE_RESULT && p.exits == (mode == 6 ? 1u : 0u));
    assert(p.heap_calls == (mode == 6 || mode == 7 ? 1u : 0u));
    assert(cpu.r[16] == before.r[16] && cpu.r[17] == before.r[17]);
    assert(cpu.r[30] == before.r[30]);
    if (mode == 3 || p.direct_syscall) {
        assert(status == MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
        assert(memcmp(&cpu,&before,sizeof(cpu)) == 0);
        assert(p.enters == 0 && p.enabled == enabled && p.ticks == 0);
        assert(p.checkpoints == 1 && p.clock_calls == (mode == 3 ? 1u : 0u));
        assert(stop.boundary.call_address == before.pc);
    } else {
        assert(musashi_boot_write32(&expected_memory,PROBE_SP-8,0x8001021c));
        assert(musashi_boot_write32(&expected_memory,PROBE_SP-0x20,0x80014248));
        assert(cpu.r[29] == PROBE_SP-(mode == 6 ? 0x18u : 0x30u));
        if (mode == 6) {
            assert(status == MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
            assert(cpu.pc == 0x80059234u && cpu.npc == 0x80059238u);
            assert(cpu.r[31] == 0x80014250u && cpu.r[4] == 1u);
            assert(cpu.r[2] == enabled && p.enabled == 1 && p.enters == 1);
            assert(p.clock_calls == 36 && p.ticks == 73);
            assert(p.checkpoints == sizeof(path)/sizeof(path[0]));
            assert(stop.boundary.call_address == 0x80059234u);
            assert(stop.boundary.target_address == 0x80059234u);
        } else if (mode == 0 || mode == 7) {
            assert(status == MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
            assert(cpu.pc == 0xa0u && cpu.npc == 0xa4u);
            assert(cpu.r[9] == 0x39u && cpu.r[10] == 0xa0u);
            assert(cpu.r[4] == 0x801ff800u && cpu.r[5] == 0x800u);
            assert(cpu.r[31] == 0x80014948u && cpu.r[2] == enabled);
            assert(p.enters == 1 && p.enabled == 0);
            assert(p.clock_calls == 23 && p.ticks == 48);
            assert(p.checkpoints == 24);
            assert(stop.boundary.call_address == 0x80014940u);
            assert(stop.boundary.target_address == 0xa0u);
        } else {
            assert(status == MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
            assert(cpu.pc == 0x8005cf0cu && cpu.npc == 0x8005cf10u);
            assert(cpu.r[31] == 0x80014938u && cpu.r[2] == PROBE_RESULT);
            assert(cpu.r[4] == 1u && cpu.r[5] == before.r[5]);
            assert(p.enters == (mode == 2 ? 1u : 0u) && p.enabled == enabled);
            assert(p.checkpoints == 14 && p.clock_calls == (mode == 2 ? 14u : 13u) &&
                   p.ticks == (mode == 2 ? 30u : 28u));
            assert(stop.boundary.call_address == 0x8005cf0cu);
        }
    }
    /* Includes requested heap region: only legitimate source stack saves vary. */
    assert(memcmp(&memory,&expected_memory,sizeof(memory)) == 0);
}

/* Boundary-only SYS fixtures retain actual fetched instructions. These
 * capabilities model acceptance, not a native BIOS handler or heap history. */
static int direct_exit(void *userdata) {
    Probe *p = userdata;
    assert(p->cpu->pc == 0x8005cf1cu && p->cpu->r[4] == 2);
    assert(p->ticks == 2 && p->clock_calls == 1);
    ++p->exits;
    if (p->refuse_enter) return 0;
    p->enabled = 1; return 1;
}
static void syscall_boundary_case(const uint8_t *exe, size_t size,
                                  unsigned selector, unsigned mode) {
    FormatterCpu cpu = {0}, before;
    Probe p = {0}; MusashiCallbackDevice callback = {0}; MusashiCdDevice cd = {0};
    MusashiExecutionClock clock = {&p, advance}; MusashiEntryRunStop stop = {0};
    int32_t result = PROBE_RESULT;
    assert(musashi_boot_map_exe(&memory, exe, size)); expected_memory = memory;
    p.cpu = &cpu; p.direct_syscall = 1; p.stop_after_sys = 1;
    p.enabled = selector == 1; p.initial_enabled = p.enabled;
    p.refuse_clock = mode == 1; p.refuse_enter = mode == 8;
    callback.userdata = &p; callback.checkpoint = checkpoint;
    callback.exit_critical_section = mode == 6 ? NULL : direct_exit;
    cd.userdata = &p; cd.enter_critical_result = mode == 6 ? NULL : enter;
    cpu.callback_device = &callback; cpu.cd_device = &cd; cpu.clock = &clock;
    cpu.pc = selector == 1 ? 0x8005cf0cu : 0x8005cf1cu;
    cpu.npc = cpu.pc + 4; cpu.r[4] = selector; cpu.r[2] = PROBE_RESULT;
    cpu.r[29] = PROBE_SP; cpu.r[31] = 0x80014950u;
    if (mode == 2) { cpu.delay_slot = 1; cpu.branch_pc = cpu.pc - 4; }
    if (mode == 3) cpu.branch_pc = cpu.pc - 4;
    if (mode == 4) { cpu.merge_pending = 1; cpu.merge_value = 0xabcdef01;
        cpu.merge_reg = 2; cpu.merge_next_pc = cpu.pc; cpu.merge_kind = MERGE_LWR; }
    if (mode == 5) clock.advance = NULL;
    if (mode == 7) cpu.delay_slot = 2;
    before = cpu;
    MusashiResetGraphPrefixStatus status = run_startup_cpu(&memory, &cpu, sink, NULL,
        &result, &stop.boundary, &stop, NULL, NULL);
    assert(status == ((mode == 6 || mode == 8) ? MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL :
        MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT));
    assert(result == PROBE_RESULT && !memcmp(&memory, &expected_memory, sizeof memory));
    if (mode == 0) {
        assert(cpu.pc == before.pc + 4 && cpu.npc == before.npc + 4);
        assert(!cpu.delay_slot && !cpu.branch_pc && !cpu.merge_pending);
        assert(cpu.r[2] == (selector == 1 ? 1u : (uint32_t)PROBE_RESULT));
        before.pc += 4; before.npc += 4; before.r[2] = cpu.r[2];
        assert(!memcmp(&cpu, &before, sizeof cpu));
        assert(p.enabled == (selector == 2) && p.clock_calls == 1 && p.ticks == 2);
        assert(p.enters == (selector == 1) && p.exits == (selector == 2));
    } else {
        assert(!memcmp(&cpu, &before, sizeof cpu));
        assert(p.enabled == p.initial_enabled);
        assert(p.clock_calls == (mode == 1 || mode == 8 ? 1u : 0u));
        assert(p.ticks == (mode == 8 ? 2u : 0u));
        assert(p.enters == (mode == 8 && selector == 1));
        assert(p.exits == (mode == 8 && selector == 2));
    }
    assert(p.checkpoints == 1);
}

/* Isolated device-call guards; real EXE table and fetched JALR/delay words.
 * No graphics implementation or native graphics success is supplied. */
static void graphics_guard_case(const uint8_t *exe, size_t exe_size,
                                unsigned site, unsigned mutation) {
    FormatterCpu cpu = {0}, before;
    Probe p = {0};
    MusashiExecutionClock clock = {&p,advance};
    uint32_t table, target, instruction;
    static const uint32_t pointer_cases[4][3] = {
        {0x8006cb8cu,0x1f801074u,0x1f801070u},
        {0x80072868u,0x1f8010a8u,0x1f8010b8u},
        {0x80072878u,0x1f8010f0u,0x1f8010f4u},
        {0x8007285cu,0x1f801814u,0x1f801810u}
    };
    const uint32_t pc = site ? 0x800596d4u : 0x80059398u;
    const uint32_t offset = site ? 0x10u : 0x34u;
    const uint32_t expected = site ? 0x8005b684u : 0x8005bd7cu;
    memset(&memory,0xa5,sizeof(memory));
    assert(musashi_boot_map_exe(&memory,exe,exe_size));
    assert(musashi_boot_read32(&memory,0x80072780u,&table));
    assert(table == 0x80072740u);
    assert(musashi_boot_read32(&memory,table+offset,&target));
    assert(target == expected);
    assert(musashi_boot_read32(&memory,pc,&instruction));
    assert(instruction == 0x0040f809u);
    /* All four pointer words, including6CB8C, are already exact in the
     * pinned EXE. No prior-initialization pointer is synthesized here. */
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t pointer;
        assert(musashi_boot_read32(&memory,pointer_cases[i][0],&pointer));
        assert(pointer == pointer_cases[i][1]);
    }
    p.cpu = &cpu;
    cpu.pc = pc; cpu.npc = pc+4; cpu.clock = &clock;
    cpu.r[2] = target; cpu.r[4] = 0x12345678;
    cpu.r[29] = PROBE_SP; cpu.r[31] = 0x31313131;
    if (mutation == 1) {
        /* Identical readable table content at a different valid RAM address
         * cannot substitute for the game's actual registered table. */
        memcpy(memory.bytes+0x78000,memory.bytes+0x72740,0x40);
        assert(musashi_boot_write32(&memory,0x80072780u,0x80078000u));
    } else if (mutation == 2) {
        /* Target is real mapped source, but is not this device operation.
         * Match the loaded register so the slot guard is tested independently. */
        assert(musashi_boot_write32(&memory,table+offset,0x80059658u));
        cpu.r[2] = 0x80059658u;
    } else if (mutation == 3) {
        cpu.r[2] = 0x80059658u; /* Correct original slot, mismatched live v0. */
    } else if (mutation >= 4) {
        const unsigned index = mutation-4;
        assert(site == 0 && index < 4);
        /* Another real supported register cannot replace this operation's
         * MMIO identity, even though downstream access could otherwise work. */
        assert(musashi_boot_write32(&memory,pointer_cases[index][0],
                                   pointer_cases[index][2]));
    }
    expected_memory = memory;
    before = cpu;
    if (mutation) {
        assert(!formatter_step(&memory,&cpu));
        assert(memcmp(&cpu,&before,sizeof(cpu)) == 0);
        assert(p.clock_calls == 0 && p.ticks == 0);
    } else {
        assert(formatter_step(&memory,&cpu));
        assert(cpu.pc == pc+4 && cpu.npc == expected);
        assert(cpu.r[31] == pc+8 && cpu.r[4] == before.r[4]);
        assert(p.clock_calls == 1 && p.ticks == 2);
        assert(formatter_step(&memory,&cpu));
        assert(cpu.pc == expected && cpu.npc == expected+4);
        assert(cpu.r[31] == pc+8 && cpu.r[29] == PROBE_SP);
        assert(cpu.r[4] == (site ? before.r[4] : 1u));
        assert(p.clock_calls == 2 && p.ticks == 4);
    }
    assert(p.enters == 0 && p.exits == 0 && p.heap_calls == 0);
    assert(memcmp(&memory,&expected_memory,sizeof(memory)) == 0);
}

int main(int argc, char **argv) {
    FILE *file;
    uint8_t *exe;
    long size;
    assert(argc == 2);
    file = fopen(argv[1],"rb");
    if (!file && errno == ENOENT) {
        puts("HEAP_CONTINUATION_SKIP pinned licensed EXE unavailable");
        return 77;
    }
    assert(file && !fseek(file,0,SEEK_END));
    size = ftell(file); assert(size > 0);
    rewind(file); exe = malloc((size_t)size); assert(exe);
    assert(fread(exe,1,(size_t)size,file) == (size_t)size);
    assert(!fclose(file));
    for (unsigned enabled = 0; enabled < 2; ++enabled)
        for (unsigned mode = 0; mode < 12; ++mode)
            run_case(exe,(size_t)size,enabled,mode);
    for (unsigned site = 0; site < 2; ++site)
        for (unsigned mutation = 0; mutation < (site ? 4u : 8u); ++mutation)
            graphics_guard_case(exe,(size_t)size,site,mutation);
    for (unsigned selector = 1; selector <= 2; ++selector)
        for (unsigned mode = 0; mode < 9; ++mode)
            syscall_boundary_case(exe, (size_t)size, selector, mode);
    free(exe);
    puts("HEAP_CONTINUATION_PASS cases=24 syscall_boundary_cases=18 graphics_guard_cases=12 fixture_only=1 heap_service=SYNTHETIC_OR_UNBOUND");
    return 0;
}
