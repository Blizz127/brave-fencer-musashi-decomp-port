/* Pinned image, real CRT/caller words, deliberately unavailable IRQ devices.
 * This proves continuous CPU/frame behavior before the device refusal only. */
#include "musashi_boot_memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Probe {
    MusashiBootMemory memory, expected;
    const void *continuation;
    unsigned checkpoints, crt_returns, graphics_entries;
} Probe;

static int checkpoint(void *userdata, const void *continuation) {
    Probe *probe = userdata;
    assert(continuation);
    if (probe->continuation) assert(probe->continuation == continuation);
    probe->continuation = continuation;
    probe->checkpoints++;
    return 1;
}

static void observe(void *userdata, const MusashiEntryCpuSnapshot *cpu) {
    Probe *probe = userdata;
    uint32_t value;
    assert(cpu->r[0] == 0 && cpu->hi == 0 && cpu->lo == 0);
    if (cpu->pc == 0x80010178u) {
        probe->crt_returns++;
        assert(!memcmp(&probe->memory, &probe->expected, sizeof(probe->memory)));
        assert(cpu->npc == 0x8001017cu);
        assert(cpu->r[29] == 0x801ffff8u && cpu->r[30] == 0x801ffff8u);
        assert(cpu->r[28] == 0x80074750u && cpu->r[31] == 0x8001009cu);
        assert(cpu->r[4] == 0x800c7f08u && cpu->r[5] == 0x001374f0u);
        assert(cpu->r[2] == 0x001ffff8u && cpu->r[3] == 0x00000c00u);
    } else if (cpu->pc == 0x800141f0u) {
        probe->graphics_entries++;
        assert(cpu->r[29] == 0x801fffc0u && cpu->r[30] == 0x801fffc0u);
        assert(cpu->r[31] == 0x800101c8u && cpu->r[28] == 0x80074750u);
        assert(musashi_boot_read32(&probe->memory, 0x80062998u, &value) && value == 1);
        assert(musashi_boot_read32(&probe->memory, 0x80074778u, &value) && value == 0x3e0u);
    }
}

static void sink(void *userdata, const uint8_t *bytes, int32_t length) {
    (void)userdata;
    (void)bytes;
    assert(length >= 0);
}

int main(int argc, char **argv) {
    static Probe probe, overflow;
    MusashiEntryState expected_entry;
    MusashiEntryRunStop stop;
    MusashiCallbackDevice device = {0};
    uint8_t *exe;
    FILE *file;
    assert(argc == 2);
    file = fopen(argv[1], "rb");
    if (!file) return 77;
    exe = malloc(0x65000u);
    assert(exe && fread(exe, 1, 0x65000u, file) == 0x65000u && fgetc(file) == EOF);
    assert(fclose(file) == 0);
    assert(musashi_boot_map_exe(&probe.memory, exe, 0x65000u));
    assert(musashi_boot_load_entry(&probe.expected, exe, 0x65000u, 0, &expected_entry));
    free(exe);
    device.userdata = &probe;
    device.checkpoint = checkpoint;
    assert(musashi_boot_run_entry(&probe.memory, 0, &device, NULL, sink, NULL,
        observe, &probe, &stop) == MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    assert(probe.crt_returns == 1 && probe.graphics_entries == 1);
    assert(probe.checkpoints > 100000u);
    assert(stop.cpu.pc == stop.boundary.target_address);
    assert(stop.cpu.r[29] != 0x801fff80u); /* No old diagnostic frame reset. */
    /* Test-owned malformed CRT stack metadata exercises ADDI's real signed
     * overflow refusal. The production image is never modified this way. */
    overflow.memory = probe.expected;
    assert(musashi_boot_write32(&overflow.memory, 0x800629bcu, 0x80000000u));
    device.userdata = &overflow;
    assert(musashi_boot_run_entry(&overflow.memory, 0, &device, NULL, sink, NULL,
        observe, &overflow, &stop) == MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    assert(stop.cpu.pc == 0x80010030u && stop.cpu.npc == 0x80010034u);
    assert(stop.cpu.r[2] == 0x80000000u && stop.cpu.r[29] == 0);
    assert(overflow.crt_returns == 0 && overflow.graphics_entries == 0);
    puts("ENTRY CPU verified: exact CRT effects and continuous constructor/caller frame; IRQ device refused");
    return 0;
}
