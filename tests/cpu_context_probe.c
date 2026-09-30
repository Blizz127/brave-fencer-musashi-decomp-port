/* Real exported branches in a declared synthetic CPU frame. No IRQ/menu proof. */
#include "../pc_port/mips_formatter.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static MusashiBootMemory memory;
static unsigned cycles;
static int accept_clock = 1;
static int advance(void *p, uint32_t cost) {
    (void)p;
    if (!accept_clock) return 0;
    cycles += cost;
    return 1;
}
static MusashiExecutionClock clock_device = {NULL, advance};
static int table(void *userdata, uint32_t *out) {
    (void)userdata; (void)out;
    assert(0); /* Classification must not invoke service capabilities. */
    return 0;
}
static void setup(FormatterCpu *cpu, uint32_t pc) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->pc = pc; cpu->npc = pc + 4; cpu->clock = &clock_device;
    cycles = 0; accept_clock = 1;
}
static void branch(uint32_t pc, unsigned reg, uint32_t value, uint32_t target) {
    FormatterCpu cpu, before;
    MusashiCpuContext context;
    setup(&cpu, pc); cpu.r[reg] = value;
    assert(musashi_boot_cpu_context(&cpu, MUSASHI_CPU_CONTEXT_SOURCE, &context));
    assert(context.pc == pc && !context.delay_slot && !context.branch_pc);
    assert(context.gpr == cpu.r && context.identity == &cpu);
    assert(formatter_step(&memory, &cpu));
    assert(cpu.pc == pc + 4 && cpu.npc == target && cycles == 2);
    assert(musashi_boot_cpu_context(&cpu, MUSASHI_CPU_CONTEXT_SOURCE, &context));
    assert(context.delay_slot == 1 && context.branch_pc == pc);
    assert(context.pc == pc + 4 && context.npc == target);
    before = cpu; accept_clock = 0;
    assert(!formatter_step(&memory, &cpu));
    assert(!memcmp(&before, &cpu, sizeof(cpu)) && cycles == 2);
    accept_clock = 1;
    assert(formatter_step(&memory, &cpu));
    assert(cpu.pc == target && !cpu.delay_slot && !cpu.branch_pc);
    assert(cycles == 4);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long size = ftell(file); assert(size > 0);
    rewind(file); uint8_t *exe = malloc((size_t)size); assert(exe);
    assert(fread(exe, 1, (size_t)size, file) == (size_t)size); assert(!fclose(file));
    assert(musashi_boot_map_exe(&memory, exe, (size_t)size)); free(exe);
    branch(0x8001001c, 1, 1, 0x80010010); /* BNE taken */
    branch(0x8001001c, 1, 0, 0x80010024); /* BNE untaken, still BD */
    branch(0x800100d8, 17, 0, 0x800100f8); /* BEQ taken */
    branch(0x800100d8, 17, 1, 0x800100e0); /* BEQ untaken */
    branch(0x80043080, 31, 0x80010000, 0x80010000); /* JR */
    branch(0x80010094, 0, 0, 0x80010178); /* JAL */
    branch(0x80014668, 0, 0, 0x80014674); /* J */
    FormatterCpu cpu; MusashiCpuContext context, sentinel;
    setup(&cpu, 0x8001001c); cpu.r[1] = 1;
    assert(musashi_boot_write32(&memory, cpu.pc, 0x4a000001));
    assert(musashi_boot_cpu_context(&cpu, MUSASHI_CPU_CONTEXT_SOURCE, &context));
    assert(context.instruction == 0x1420fffc); /* Same immutable word as execution. */
    cpu.r[3] = 0x12345678; assert(context.gpr[3] == 0x12345678);
    memset(&sentinel, 0x5a, sizeof(sentinel)); context = sentinel;
    cpu.delay_slot = 1; cpu.branch_pc = cpu.pc - 8;
    assert(!musashi_boot_cpu_context(&cpu, MUSASHI_CPU_CONTEXT_SOURCE, &context));
    assert(!memcmp(&context, &sentinel, sizeof(context)));
    cpu.branch_pc = cpu.pc - 4;
    FormatterCpu before = cpu;
    assert(!formatter_step(&memory, &cpu)); /* Branch in delay slot refuses. */
    assert(!memcmp(&before, &cpu, sizeof(cpu)) && !cycles);
    cpu.pc = 0x800dead0; cpu.npc = cpu.pc + 4;
    assert(!musashi_boot_cpu_context(&cpu, MUSASHI_CPU_CONTEXT_SOURCE, &context));
    assert(!memcmp(&context, &sentinel, sizeof(context)));
    assert(musashi_boot_cpu_context(&cpu, MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE, &context));
    assert(context.identity == &cpu && context.gpr == cpu.r);
    assert(!context.pc && !context.npc && !context.instruction_valid &&
           !context.instruction && !context.delay_slot && !context.branch_pc);
    formatter_return(&cpu); assert(!cpu.delay_slot && !cpu.branch_pc);
    setup(&cpu, 0x80010001u); context = sentinel;
    assert(!musashi_boot_cpu_context(&cpu, MUSASHI_CPU_CONTEXT_SOURCE, &context));
    assert(!memcmp(&context, &sentinel, sizeof(context)));
    setup(&cpu, 0x800dead0u);
    assert(!musashi_boot_continuation_host_context(&cpu, &context));
    assert(!memcmp(&context, &sentinel, sizeof(context)));
    setup(&cpu, FORMATTER_SEARCH);
    assert(musashi_boot_continuation_host_context(&cpu, &context));
    assert(context.identity == &cpu && context.gpr == cpu.r &&
        context.provenance == MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE);
    assert(!context.pc && !context.npc && !context.instruction_valid &&
        !context.instruction && !context.delay_slot && !context.branch_pc);
    setup(&cpu, 0xb0u); cpu.r[9] = 0x56u; cpu.r[10] = 0xb0u;
    cpu.r[31] = 0x8004d188u; context = sentinel;
    assert(!musashi_boot_continuation_host_context(&cpu, &context));
    MusashiBiosImageDevice image_device = {.table=table};
    cpu.bios_image = &image_device;
    assert(musashi_boot_continuation_host_context(&cpu, &context));
    context = sentinel; cpu.r[9] = 0x57u;
    assert(!musashi_boot_continuation_host_context(&cpu, &context));
    cpu.r[9] = 0x56u; cpu.r[31] += 4u;
    assert(!musashi_boot_continuation_host_context(&cpu, &context));
    cpu.r[31] -= 4u; cpu.npc += 4u;
    assert(!musashi_boot_continuation_host_context(&cpu, &context));
    cpu.npc -= 4u; cpu.delay_slot = 1; cpu.branch_pc = cpu.pc - 4u;
    assert(!musashi_boot_continuation_host_context(&cpu, &context));
    assert(!memcmp(&context, &sentinel, sizeof(context)) && !cycles);
    puts("CPU_CONTEXT_PASS source_branches=7 host_physical_context=UNCLAIMED");
    return 0;
}
