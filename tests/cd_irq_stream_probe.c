/* Explicit state fixtures; real mounted CD FIFO, DMA3 and CD IRQ source.
 * No native lifecycle, audio audition or physical timing claim. */
#define main previous_cd_irq_main
#include "cd_irq_probe.c"
#undef main
#include "musashi_cd_dma3.h"
#include "musashi_dma_controller.h"
#include "musashi_spu_cd_audio.h"
static MusashiSpuCdAudio *stream_spu;
static MusashiCdSpuDevice stream_spu_device;
static unsigned stream_pcm_frames;
static unsigned stream_mode=0xa0;
static MusashiEventDevice stream_events;
static int stream_event(void *p,int32_t h,int32_t *v){return musashi_bios_events_test_sync_spu(&((Fixture*)p)->events,h,v);}
static int pcm_status(void *p,size_t *q,int *playing){(void)p;*q=0;*playing=1;return 1;}
static int pcm_queue(void *p,const int16_t *data,size_t n){(void)p;assert(data);stream_pcm_frames+=(unsigned)n;return 1;}
static MusashiCdDma3 *stream_dma;
static MusashiDmaController stream_controller;
static uint32_t common_delay;
static MusashiCallbackDevice stream_callbacks;
static int stream_read8(void *p,uint32_t a,uint8_t *v){return musashi_cd_owned_read8(((Fixture*)p)->cd,a,v);}
static int stream_write8(void *p,uint32_t a,uint8_t v){return musashi_cd_owned_write8(((Fixture*)p)->cd,a,v);}
static int stream_read32(void *p,uint32_t a,uint32_t *v){
    (void)p;if(a==0x1f8010f0)return musashi_dma_controller_read32(&stream_controller,a,v);
    if(a==0x1f801020){*v=common_delay;return 1;}
    return musashi_cd_dma3_read32(stream_dma,a,v);
}
static int stream_write32(void *p,uint32_t a,uint32_t v){
    (void)p;if(a==0x1f8010f0)return musashi_dma_controller_write32(&stream_controller,a,v);
    if(a==0x1f801020){common_delay=v;return 1;}
    return musashi_cd_dma3_write32(stream_dma,a,v);
}
static int stream_available(void *p,size_t *n){MusashiCdOwnedState s;if(!musashi_cd_owned_get_state(((Fixture*)p)->cd,&s))return 0;*n=s.fifo_size-s.fifo_cursor;return 1;}
static int stream_data(void *p,uint8_t *v,size_t n){return musashi_cd_owned_read_data(((Fixture*)p)->cd,v,n);}
static int stream_request(void *p,int *v){MusashiCdOwnedState s;if(!musashi_cd_owned_get_state(((Fixture*)p)->cd,&s))return 0;*v=s.data_requested;return 1;}
static int stream_tick(void *p,uint32_t cost) {
    Fixture *f=p;MusashiCdOwnedState cd;
    assert(cost==2||cost==3);if(f->refuse_clock)return 0;
    ++f->costs;f->cycle+=cost;
    assert(musashi_cd_owned_get_state(f->cd,&cd));
    /* Same real incoming-sector backpressure as native_boot's CD clock.
     * Do not overwrite queued sectors during the long retail PIO delay. */
    if(cd.incoming_valid) {
        uint64_t target=cd.cycle;
        if(cd.phase&&cd.due>cd.cycle&&cd.due<=f->cycle)target=cd.due;
        if(!cd.interrupt&&!cd.response_count&&(!cd.announced_valid||cd.announced_claimed)&&
           cd.sector_publish_due>cd.cycle&&cd.sector_publish_due<=f->cycle&&
           (target==cd.cycle||cd.sector_publish_due<target))target=cd.sector_publish_due;
        if(target>cd.cycle&&!musashi_cd_owned_advance_without_fetch(f->cd,target))return 0;
    } else if(!musashi_cd_owned_advance(f->cd,musashi_cd_owned_bounded_cycle(&cd,f->cycle)))return 0;
    return musashi_cd_dma3_advance(stream_dma,f->cycle)&&
           (!stream_spu||musashi_spu_cd_audio_advance(stream_spu,f->cycle));
}
static void stream_init(Fixture *f,const MusashiDiscMedia *media,unsigned state,unsigned count) {
    init(f,media);f->services.callback_device=&stream_callbacks;put(f->memory,0x800737cc,0x80043f54);
    write_cd(f,0,0);write_cd(f,2,0);write_cd(f,2,2);write_cd(f,2,0x16);
    command(f,2);assert(execute(f));command_with_parameter(f,0x0e,(int)stream_mode);assert(execute(f));
    command(f,6);assert(execute(f));
    command_with_parameter(f,0x0e,(int)stream_mode);assert(execute(f));
    musashi_dma_controller_init(&stream_controller,&f->runtime.controller);
    MusashiCdDma3Device device={.userdata=f,.current_thread=tid,.healthy=health,
        .data_available=stream_available,.read_data=stream_data,.request_asserted=stream_request};
    stream_dma=musashi_cd_dma3_open(f->memory,&stream_controller,&device,f->cycle);assert(stream_dma);
    common_delay=0;
    f->registers=(MusashiCdRegisterDevice){.userdata=f,.read8=stream_read8,.write8=stream_write8,
        .read32=stream_read32,.write32=stream_write32};
    f->clock.advance=stream_tick;
    const uint32_t slots[][2]={{0x8006cf5c,0x1f801020},{0x8006cf80,0x1f801018},
        {0x8006cf84,0x1f8010f0},{0x8006cf88,0x1f8010b0},{0x8006cf8c,0x1f8010b4},{0x8006cf90,0x1f8010b8}};
    for(unsigned i=0;i<6;i++)put(f->memory,slots[i][0],slots[i][1]);
    put(f->memory,0x8006cc88,0x800377d8);put(f->memory,0x80076228,0x80090004);
    put(f->memory,0x8007623c,16);put(f->memory,0x80076240,(state<<24)|count);
    put(f->memory,0x80076294,0);put(f->memory,0x80076238,0x1000);
    put(f->memory,0x80076248,0);put(f->memory,0x8006aef4,0xa7);
    memset(musashi_boot_ram_span(f->memory,0x80090000,0x810),0xa5,0x810);
    MusashiCdOwnedState s;assert(musashi_cd_owned_get_state(f->cd,&s));
    f->cycle=s.sector_due;assert(musashi_cd_owned_advance(f->cd,f->cycle));
    assert(musashi_cd_dma3_advance(stream_dma,f->cycle));
    assert(musashi_cd_owned_get_state(f->cd,&s)&&s.interrupt==1);
}
static void stream_close(Fixture *f){if(stream_spu){assert(musashi_spu_cd_audio_destroy(stream_spu));stream_spu=NULL;}assert(musashi_cd_dma3_close(stream_dma));stream_dma=NULL;close_fixture(f);}
static void stream_case(const MusashiDiscMedia *media,unsigned state,unsigned count,unsigned expected_state,unsigned expected_count) {
    Fixture f;stream_init(&f,media,state,count);
    int ok=execute(&f);
    if(!ok)fprintf(stderr,"stream state=%u count=%u stop=%08x\n",state,count,f.runtime.cd_stop.call_address);
    assert(ok);
    assert(ram_byte(&f,0x80076243)==expected_state);
    assert(get(f.memory,0x8007623c)==17);
    uint16_t actual;assert(musashi_boot_read16(f.memory,0x80076240,&actual)&&actual==expected_count);
    MusashiCdDma3State dma;assert(musashi_cd_dma3_get_state(stream_dma,&dma));
    assert(dma.transfers==2&&dma.bytes==2060&&!dma.fault);
    uint8_t sector[MUSASHI_DISC_RAW_SECTOR_SIZE];
    assert(musashi_disc_media_read_sector(media,16,sector,sizeof(sector)));
    assert(!memcmp(musashi_boot_ram_span(f.memory,0x800762a0,12),sector+12,12));
    assert(!memcmp(musashi_boot_ram_span(f.memory,0x80090004,0x800),sector+24,0x800));
    assert(get(f.memory,0x80076228)==0x80090804);
    assert(get(f.memory,0x80090000)==0xa5a5a5a5&&get(f.memory,0x80090804)==0xa5a5a5a5);
    assert(!f.runtime.cd_frame.faulted&&!f.runtime.cd_frame.busy);
    stream_close(&f);
}
static void stream_spu_case(const MusashiDiscMedia *media,unsigned chunks) {
    assert(chunks>=2&&chunks<=40);
    const unsigned total=(chunks-1)*0x800+0x40;
    Fixture f;stream_init(&f,media,3,0);
    MusashiSpuCdAudioBackend backend={&f,health,tid,pcm_status,pcm_queue};
    stream_spu=musashi_spu_cd_audio_create();assert(stream_spu);
    assert(musashi_spu_cd_audio_init_bios_muted(stream_spu,&backend,f.cycle));
    stream_spu_device=(MusashiCdSpuDevice){stream_spu,musashi_spu_cd_audio_read16,musashi_spu_cd_audio_write16};
    f.services.cd_spu=&stream_spu_device;
    int32_t handle,result;
    assert(musashi_bios_events_open(&f.events,0xf0000009u,0x20,0x2000,0,&handle));
    assert(musashi_bios_events_enable(&f.events,handle,&result)&&result);
    stream_events=(MusashiEventDevice){.userdata=&f,.b0_0b=stream_event};
    f.services.event=&stream_events;put(f.memory,0x8006b0e0,(uint32_t)handle);
    /* Explicit software PIO transfer profile, address units of eight bytes. */
    put(f.memory,0x8006b54c,0x1f801c00);put(f.memory,0x8006b568,1);
    /* Retail init 3A69C..3A6D8: alignment2/shift3/unit8/mask7/control4. */
    put(f.memory,0x8006b570,2);put(f.memory,0x8006b574,3);
    put(f.memory,0x8006b578,8);put(f.memory,0x8006b57c,7);
    assert(musashi_spu_cd_audio_write16(stream_spu,0x1f801dac,4));
    put(f.memory,0x80076238,0x1010);
    put(f.memory,0x80090000,0x00400000u|chunks); /* finitebank, final64bytes */
    int ok=execute(&f);
    if(!ok){MusashiSpuCdAudioSnapshot snap;assert(musashi_spu_cd_audio_snapshot(stream_spu,&snap));
        fprintf(stderr,"stream state3 stop=%08x budget=%u instructions=%llu cursor=%x control=%x valid=%d halfwords=%llu\n",f.runtime.cd_stop.call_address,f.runtime.cd_frame.budget_exhausted,(unsigned long long)f.runtime.cd_frame.instructions,snap.transfer_cursor,snap.transfer_control,snap.transfer_address_valid,(unsigned long long)snap.manual_halfwords);}
    assert(ok);
    uint8_t *audio=malloc(total);assert(audio);
    assert(musashi_spu_cd_audio_copy_ram(stream_spu,0x1010,audio,total));
    uint8_t sector[MUSASHI_DISC_RAW_SECTOR_SIZE];
    for(unsigned i=0;i<chunks;i++) {
        assert(musashi_disc_media_read_sector(media,16+i,sector,sizeof(sector)));
        assert(!memcmp(audio+i*0x800,sector+24,i+1==chunks?0x40:0x800));
    }
    free(audio);
    fprintf(stderr,"stream completed chunks=%u next=%u instructions=%llu\n",chunks,get(f.memory,0x8007623c),(unsigned long long)f.runtime.cd_frame.instructions);
    MusashiCdDma3State dma;assert(musashi_cd_dma3_get_state(stream_dma,&dma));
    /* Long PIO leaves queued notifications; state4 consumes headers only. */
    unsigned headers=chunks==40?45:chunks;
    assert(get(f.memory,0x8007623c)==16+headers);
    assert(dma.transfers==headers+chunks&&dma.bytes==headers*12u+chunks*0x800u);
    MusashiSpuCdAudioSnapshot snapshot;assert(musashi_spu_cd_audio_snapshot(stream_spu,&snapshot));
    assert(!snapshot.faulted&&snapshot.manual_halfwords==total/2&&snapshot.transfer_cursor==0x1010+total);
    uint8_t guard[8];memset(guard,0xa5,8);
    /* Canonical owner refuses reads of unwritten RAM; adjacent bytes stay unwritten. */
    assert(!musashi_spu_cd_audio_copy_ram(stream_spu,0x1008,guard,8));
    assert(!musashi_spu_cd_audio_copy_ram(stream_spu,0x1010+total,guard,8));
    for(unsigned i=0;i<8;i++)assert(guard[i]==0xa5);
    assert(ram_byte(&f,0x80076243)==4);
    assert(get(f.memory,0x80076238)==0x1010+(chunks-1)*0x800);
    assert(ram_byte(&f,0x80076250)==1&&get(f.memory,0x80076294)==1);
    stream_close(&f);
}
static void stream_failures(const MusashiDiscMedia *media) {
    for(unsigned which=0;which<4;which++) {
        Fixture f;stream_init(&f,media,1,1);
        if(which==0)put(f.memory,0x80076228,0x801ffc04); /* crossing RAM end */
        if(which==1)put(f.memory,0x8006cf88,0x1f8010b4); /* wrong DMA pointer */
        if(which==2)f.registers.read32=NULL;
        if(which==3)f.registers.write32=NULL;
        assert(!execute(&f)&&f.runtime.cd_frame.faulted);
        MusashiCdDma3State dma;assert(musashi_cd_dma3_get_state(stream_dma,&dma));
        assert(dma.bytes==(which==0?12u:0u));
        assert(get(f.memory,0x80090004)==0xa5a5a5a5);
        stream_close(&f);
    }
    /* State3's real address validator rejects below1010 and takes error5. */
    Fixture f;stream_init(&f,media,3,0);
    assert(execute(&f));assert(ram_byte(&f,0x80076243)==5);
    assert((ram_byte(&f,0x8006aef4)&2)==0);
    MusashiCdDma3State dma;assert(musashi_cd_dma3_get_state(stream_dma,&dma));
    assert(dma.transfers==2&&dma.bytes==2060);
    stream_close(&f);
}
static void stream_header_failure(const MusashiDiscMedia *media) {
    Fixture f;stream_init(&f,media,1,1);put(f.memory,0x8007623c,17);
    assert(execute(&f));assert(ram_byte(&f,0x80076243)==5);
    assert((ram_byte(&f,0x8006aef4)&2)==0);
    MusashiCdDma3State dma;assert(musashi_cd_dma3_get_state(stream_dma,&dma));
    assert(dma.transfers==1&&dma.bytes==12);
    assert(get(f.memory,0x80090004)==0xa5a5a5a5);
    stream_close(&f);
}
int main(int argc,char **argv) {
    assert(argc==3);assert(SDL_Init(0)==0);g_intrMutex=SDL_CreateMutex();assert(g_intrMutex);
    MusashiDiscMedia *media=musashi_disc_media_open_pinned(argv[1],argv[2]);assert(media);
    stream_case(media,1,1,1,2);stream_case(media,1,13,2,0);
    stream_case(media,2,1,2,2);stream_case(media,2,6,3,0);
    stream_header_failure(media);stream_failures(media);stream_spu_case(media,2);stream_spu_case(media,40);
    musashi_disc_media_close(media);SDL_DestroyMutex(g_intrMutex);SDL_Quit();
    puts("CD_STREAM_SOURCE_PASS fixture_only=1 state3_pio=1");return 0;
}
