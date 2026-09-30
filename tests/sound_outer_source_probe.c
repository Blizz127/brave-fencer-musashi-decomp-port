/* Synthetic source fixture, never native lifecycle/captured-RAM proof.
 * Actual earlier startup/common/reverb source derives every sound prerequisite.
 * Only the imported fixture's explicit IRQ slot/guard and CPU stack are supplied. */
#define main startup_fixture_main
#include "spu_startup_source_probe.c"
#undef main

typedef struct Visit { uint32_t pc; unsigned count; } Visit;
static Visit visits[]={
    {0x80037e34,0},{0x80037e6c,0},{0x8002ca48,0},{0x8002ca84,0},
    {0x8002caa4,0},{0x8002cac8,0},{0x8002cc7c,0},{0x8002c8cc,0},
    {0x8002c8d8,0},{0x8003b244,0},{0x80034cb0,0},{0x8002cc64,0}};
static const uint32_t calls[]={0x80037d98,0x8003d424,0x8002cc4c,
    0x8002fae0,0x8002c8bc,0x80037cc8,0x8003be74,0x8003be98,
    0x8003b1ec,0x80034c24,0x800430b8};
static unsigned call_count, write_count, dma_count;
static uint32_t writes_pc[2], writes_address[2];
static uint16_t writes_value[2];
static uint32_t refuse_address;
static MusashiDmaController saved_dma;
static MusashiBiosEventTable saved_events;
static uint64_t stage_cycle;
static uint32_t toc_before[4],track_count_before;
static int record_spu(void *u,uint32_t address,uint16_t value) {
    assert(write_count<2);
    if(address==refuse_address) return 0;
    if(!musashi_spu_cd_audio_write16(((Fixture *)u)->audio,address,value)) return 0;
    writes_pc[write_count]=f.cpu.pc; writes_address[write_count]=address;
    writes_value[write_count++]=value; return 1;
}
static void record_dma(void *u,uint32_t address,uint32_t value) {
    ++dma_count; dma_write(u,address,value);
}
static uint16_t half(uint32_t address) {
    uint16_t value; assert(musashi_boot_read16(&f.memory,address,&value)); return value;
}
static uint8_t byte(uint32_t address) {
    uint32_t value=word(address&~3u); return (uint8_t)(value>>((address&3u)*8u));
}
static MusashiSpuReverbSnapshot reverb_state(void) {
    MusashiSpuReverbSnapshot s;
    assert(musashi_spu_cd_audio_reverb_snapshot(f.audio,&s)); return s;
}
static void outer_prepare(void) {
    unsigned i;
    init();
    assert(run_to(0x8002c98c));
    assert(word(0x8006b0fc)==4 && word(0x8006b0f4)==0xf204);
    assert(word(0x8006b5b0)==0x800762d0 && word(0x8006b544)==0);
    assert(reverb_state().enabled && reverb_state().base==0xf204);
    assert(f.cpu.r[29]==0x801fefe0 && word(0x801feff8)==0x12345678);
    for(i=0;i<sizeof(visits)/sizeof(visits[0]);++i) visits[i].count=0;
    call_count=write_count=dma_count=0; refuse_address=0;
    saved_dma=f.dma; saved_events=f.kernel.events; stage_cycle=f.cycles;
    for(i=0;i<4;++i) toc_before[i]=word(0x800c7d30+i*4);
    track_count_before=word(0x800a5bc8);
    f.spu.write16=record_spu; f.callbacks.write32=record_dma;
}
static int outer_run(uint32_t stop) {
    unsigned budget,i;
    for(budget=0;budget<20000;++budget) {
        if(f.cpu.pc==stop) return 1;
        for(i=0;i<sizeof(visits)/sizeof(visits[0]);++i)
            if(f.cpu.pc==visits[i].pc) ++visits[i].count;
        for(i=0;i<sizeof(calls)/sizeof(calls[0]);++i) if(f.cpu.pc==calls[i]) {
            assert(call_count==i); ++call_count;
        }
        if(!formatter_step(&f.memory,&f.cpu)) return 0;
    }
    return 0;
}
static void no_dma_or_event(void) {
    assert(dma_count==0 && !memcmp(&saved_dma,&f.dma,sizeof(saved_dma)));
    assert(!memcmp(&saved_events,&f.kernel.events,sizeof(saved_events)));
    assert(f.opens==1 && f.enables==1 && f.enters==1 && f.exits==1);
}
static void records(void) {
    unsigned i,j;
    assert(word(0x800762b0)==0x800a4efe && word(0x80079a68)==4);
    for(i=0;i<16;++i) {
        uint32_t stride=i*0x60;
        assert(word(0x800c6de0+stride)==(1u<<i));
        assert(word(0x800c6de4+stride)==0);
        assert(half(0x800c6dec+stride)==0 && half(0x800c6dee + stride)==0);
        for(j=0;j<5;++j) assert(byte(0x800c6e2a+stride+j)==0);
    }
    for(i=0;i<2;++i) {
        uint32_t stride=i*0x1fc;
        assert(half(0x800b9ec8+stride)==i);
        assert(byte(0x800b9ed2+stride)==0 && byte(0x800b9ed3+stride)==0);
    }
    for(i=0;i<8;++i) {
        assert(word(0x800a64b0+i*12)==0);
        assert(half(0x800a46e8+i*0x54)==0);
        assert(byte(0x800a49d9+i*0x54)==i);
        assert(half(0x800a4992+i*0x54)==0x10+i);
        assert(word(0x800a4c68+i*0x48)==0x10+i);
        assert(byte(0x800a4c6c+i*0x48)==0 && byte(0x800a4c6d+i*0x48)==0);
    }
    for(i=0;i<5;++i) {
        assert(half(0x800a4642+i*0x18)==0xffff);
        assert(half(0x800a4644+i*0x18)==0 && byte(0x800a4650+i*0x18)==1);
    }
    assert(word(0x800a4654)==0x10000 && word(0x800a466c)==0x14000);
    assert(word(0x800a4684)==0x18000 && word(0x800a469c)==0x39f00);
    for(i=0;i<4;++i) {
        uint32_t count=word(0x80065504+i*4),base=word(0x8006a970+i*4);
        for(j=0;j<count;++j) assert(byte(base+10+j*12)==0);
    }
    for(i=0;i<121;++i) assert(word(0x800c5328+i*4)==0xffffffff);
    assert(half(0x800a46cc)==0 && word(0x800ba0f8)==0);
}
static void outer_complete(void) {
    static const unsigned expected[]={16,2,8,5,8,8,96,121,121,1,5,4};
    unsigned i;
    outer_prepare();
    if(!outer_run(0x800430dc)) {
        fprintf(stderr,"OUTER_REFUSED pc=%08x ra=%08x calls=%u writes=%u\n",
            f.cpu.pc,f.cpu.r[31],call_count,write_count); abort();
    }
    assert(call_count==sizeof(calls)/sizeof(calls[0])); records();
    for(i=0;i<sizeof(visits)/sizeof(visits[0]);++i) {
        assert(visits[i].count==expected[i]);
        printf("OUTER_SITE pc=%08x count=%u\n",visits[i].pc,visits[i].count);
    }
    assert(write_count==2 && writes_pc[0]==0x8003c060 && writes_pc[1]==0x8003c078);
    assert(writes_address[0]==0x1f801d98 && writes_address[1]==0x1f801d9a);
    assert(writes_value[0]==0 && writes_value[1]==0 && reverb_state().eon==0);
    assert(word(0x8006b544)==0 && word(0x801feff0)==1 && word(0x801feff4)==0);
    for(i=0;i<5;++i) assert(word(0x80078f10+i*4)==0);
    for(i=0;i<4;++i) assert(word(0x800c7d30+i*4)==toc_before[i]);
    assert(word(0x800a5bc8)==track_count_before);
    assert(f.cpu.r[4]==1 && f.cpu.r[5]==0x800c7d30 && f.cpu.r[31]==0x800430cc);
    assert(f.cpu.r[29]==0x801fefb0 && word(0x801fefc0)==0x80034cd0);
    assert(word(0x801fefd8)==0x8002cc34 && word(0x801feff8)==0x12345678);
    assert(f.cycles>stage_cycle && reverb_state().ram_writes>0);
    no_dma_or_event();
    printf("SOUND_OUTER_TOC_ENTRY pc=%08x a0=%u a1=%08x same_cpu=1 fixture_only=1\n",
        f.cpu.pc,f.cpu.r[4],f.cpu.r[5]); done();
}
static void outer_refusals(void) {
    unsigned i;
    for(i=0;i<3;++i) {
        FormatterCpu before; uint64_t cycles;
        outer_prepare(); assert(outer_run(0x8003be98)); records();
        assert(f.cpu.r[4]==0 && f.cpu.r[5]==0xffffff);
        assert(f.cpu.r[6]==0xcc && f.cpu.r[7]==0xcd);
        if(i==0) put(0x8006b54c,0x1f801c02);
        else f.cpu.r[i==1?6:7]=0xca;
        before=f.cpu; cycles=f.cycles;
        assert(!formatter_step(&f.memory,&f.cpu));
        assert(!memcmp(&before,&f.cpu,sizeof(before)) && f.cycles==cycles);
        assert(write_count==0); no_dma_or_event(); done();
    }
    outer_prepare();
    /* Explicit negative hardware fixture: real nonzero EON writes before
     * the source clear, then refuse its second device write. No completion. */
    assert(musashi_spu_cd_audio_write16(f.audio,0x1f801d98,0x1234));
    assert(musashi_spu_cd_audio_write16(f.audio,0x1f801d9a,0xab));
    refuse_address=0x1f801d9a;
    assert(!outer_run(0x800430dc) && f.cpu.pc==0x8003c078);
    assert(write_count==1 && writes_address[0]==0x1f801d98 && writes_value[0]==0);
    assert(reverb_state().eon==0xab0000); records();
    assert(visits[9].count==0 && visits[10].count==0);
    no_dma_or_event(); done();
}
int main(int argc,char **argv) {
    FILE *file;
    assert(argc==2); file=fopen(argv[1],"rb"); assert(file);
    assert(fread(exe,1,sizeof(exe),file)==sizeof(exe) && fgetc(file)==EOF);
    assert(!fclose(file)); outer_complete(); outer_refusals();
    puts("SOUND_OUTER_SOURCE_PASS fixture_only=1"); return 0;
}
