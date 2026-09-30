/* Reuse only the synthetic EXE/SPU/clock fixture adapters. This test starts
 * after the two SPU initializers, with its own explicit CPU and device state;
 * it is not continuation proof for native cold boot. */
#define main startup_fixture_main
#include "spu_startup_source_probe.c"
#undef main

typedef struct Write { uint32_t pc, address; uint16_t value; uint64_t cycle; } Write;
typedef struct Jump { uint32_t pc, index, target; } Jump;
static Write writes[8];
static Jump jumps[3];
static unsigned write_count, jump_count, attempts;

static int record_write(void *u,uint32_t address,uint16_t value) {
    assert(write_count<8);
    ++attempts;
    if(!musashi_spu_cd_audio_write16(((Fixture *)u)->audio,address,value)) return 0;
    writes[write_count++]=(Write){f.cpu.pc,address,value,f.cycles};
    return 1;
}
static void common_init(void) {
    init();
    memset(writes,0,sizeof(writes)); memset(jumps,0,sizeof(jumps));
    write_count=jump_count=attempts=0;
    f.cpu.pc=0x8002c90c; f.cpu.npc=f.cpu.pc+4;
    /* Explicit incoming fixture policy. No BIOS/retail snapshot is loaded. */
    assert(musashi_spu_cd_audio_write16(f.audio,0x1f801daa,0xc000));
    f.spu.write16=record_write;
}
static int common_run(uint32_t target) {
    unsigned budget;
    for(budget=0;budget<1000;budget++) {
        if(f.cpu.pc==target) return 1;
        if(f.cpu.pc==0x8003c5ec || f.cpu.pc==0x8003c6b4) {
            assert(jump_count<3);
            jumps[jump_count++]=(Jump){f.cpu.pc,f.cpu.r[3],f.cpu.r[2]};
        }
        if(!formatter_step(&f.memory,&f.cpu)) return 0;
    }
    return 0;
}
static MusashiSpuCdAudioSnapshot common_state(void) {
    MusashiSpuCdAudioSnapshot s;
    assert(musashi_spu_cd_audio_snapshot(f.audio,&s)); return s;
}
static void common_success(void) {
    static const uint32_t addresses[]={0x1f801d80,0x1f801d82,0x1f801db0,
        0x1f801db2,0x1f801daa,0x1f801daa,0x1f801daa};
    static const uint16_t values[]={0x3fff,0x3fff,0x3fff,0x3fff,0xc000,0xc001,0xc001};
    MusashiSpuCdAudioSnapshot s; uint64_t source_cycles; unsigned i;
    common_init();
    assert(common_run(0x8002c974));
    assert(word(0x800a4ea4)==0x23cf && word(0x800a4ea8)==0x3fff3fff);
    assert(word(0x800a4eac)==0 && word(0x800a4eb4)==0x3fff3fff);
    assert(f.cpu.r[4]==0x800a4ea4 && f.cpu.r[29]==0x801ff000);
    assert(f.cpu.r[31]==0x8002c974 && f.cpu.hi==0 && f.cpu.lo==0);
    assert(jump_count==2 && jumps[0].pc==0x8003c5ec && jumps[1].pc==0x8003c6b4);
    assert(jumps[0].index==0 && jumps[0].target==0x8003c62c);
    assert(jumps[1].index==0 && jumps[1].target==0x8003c6f4);
    assert(write_count==7 && attempts==7);
    for(i=0;i<7;i++) {
        assert(writes[i].address==addresses[i] && writes[i].value==values[i]);
        assert(writes[i].cycle>0 && (!i || writes[i].cycle>writes[i-1].cycle));
        printf("COMMON_WRITE pc=%08x address=%08x value=%04x cycle=%llu\n",
            writes[i].pc,writes[i].address,writes[i].value,(unsigned long long)writes[i].cycle);
    }
    for(i=0;i<2;i++) printf("COMMON_JR pc=%08x index=%u target=%08x\n",
        jumps[i].pc,jumps[i].index,jumps[i].target);
    s=common_state(); source_cycles=f.cycles;
    assert(s.cycle==source_cycles && s.control==0xc001 && !s.faulted);
    assert(s.main_left_configured==0x7ffe && s.main_right_configured==0x7ffe);
    assert(s.cd_left_gain==0x3fff && s.cd_right_gain==0x3fff && s.manual_halfwords==0);
    assert(source_cycles<768 && s.sample_index==0 && s.main_left_current==0 && s.main_right_current==0);
    /* Separate checked device-clock boundary after the source return; no
     * invented CPU instructions, host sleep or read-driven settlement. */
    assert(musashi_spu_cd_audio_advance(f.audio,767));
    s=common_state(); assert(s.main_left_current==0 && s.main_right_current==0);
    assert(musashi_spu_cd_audio_advance(f.audio,768));
    s=common_state(); assert(s.main_left_current==0x7ffe && s.main_right_current==0x7ffe);
    assert(s.sample_index==1 && s.submitted_frames==1 && f.frames==1);
    assert(write_count==7 && f.opens==0 && f.enables==0);
    done();
}
static void common_refusals(void) {
    MusashiSpuCdAudioSnapshot s; FormatterCpu before; uint64_t clock;
    common_init(); put(0x8006b54c,0x1f801c02);
    assert(!common_run(0x8002c974) && f.cpu.pc==0x8003c598);
    assert(word(0x800a4ea4)==0x23cf && !write_count && !attempts); done();
    common_init(); put(0x80073494,0x8003c6f4);
    assert(!common_run(0x8002c974) && f.cpu.pc==0x8003c5ec);
    assert(jump_count==1 && jumps[0].index==0 && jumps[0].target==0x8003c6f4);
    assert(!write_count && !attempts);
    before=f.cpu;clock=f.cycles;
    assert(!formatter_step(&f.memory,&f.cpu));
    assert(!memcmp(&before,&f.cpu,sizeof(before)) && f.cycles==clock); done();
    common_init(); put(0x800734b4,0x8003c62c);
    assert(!common_run(0x8002c974) && f.cpu.pc==0x8003c6b4);
    assert(jump_count==2 && jumps[1].index==0 && jumps[1].target==0x8003c62c);
    assert(write_count==1 && writes[0].address==0x1f801d80 && writes[0].value==0x3fff);
    s=common_state(); assert(s.main_left_configured==0x7ffe && s.main_right_configured==0);
    before=f.cpu;clock=f.cycles;
    assert(!formatter_step(&f.memory,&f.cpu));
    assert(!memcmp(&before,&f.cpu,sizeof(before)) && f.cycles==clock); done();
    common_init(); assert(common_run(0x8003c598));
    /* Deliberately mutated source-created fixture record selects mode1.
     * The actual sweep branch clamps3FFF to7F, combines8000, then the real
     * fixed-gain-only SPU owner refuses807F. Never patch production flags. */
    assert(musashi_boot_write16(&f.memory,0x800a4eac,1));
    assert(!common_run(0x8002c974) && f.cpu.pc==0x8003c674);
    assert(jump_count==1 && jumps[0].index==1 && jumps[0].target==0x8003c5f4);
    assert(attempts==1 && !write_count && f.cpu.r[2]==0x807f);
    s=common_state(); assert(!s.faulted && s.main_left_configured==0 && s.main_right_configured==0); done();
}
int main(int argc,char **argv) {
    FILE *file;
    assert(argc==2);file=fopen(argv[1],"rb");assert(file);
    assert(fread(exe,1,sizeof(exe),file)==sizeof(exe) && fgetc(file)==EOF);
    assert(!fclose(file));
    common_success();common_refusals();
    puts("SPU_COMMON_SOURCE_PASS fixture_only=1");return 0;
}
