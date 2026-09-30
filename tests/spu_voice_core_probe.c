#include "../pc_port/include/musashi_spu_cd_audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Sink {
    int16_t pcm[8192*2];
    size_t frames, calls, fail_call;
    uint64_t token;
    int ready, reenter;
    MusashiSpuCdAudio *audio;
} Sink;
static int ready(void *p) { return ((Sink *)p)->ready; }
static uint64_t thread(void *p) { return ((Sink *)p)->token; }
static int status(void *p, size_t *n, int *playing) {
    (void)p; *n=0; *playing=1; return 1;
}
static int queue(void *p,const int16_t *pcm,size_t n) {
    Sink *s=p;
    if(++s->calls==s->fail_call) return 0;
    assert(n<=8192-s->frames);
    memcpy(s->pcm+s->frames*2,pcm,n*4); s->frames+=n;
    if(s->reenter) {
        uint16_t value=0xbeef; s->reenter=0;
        assert(!musashi_spu_cd_audio_read16(s->audio,0x1f801daa,&value));
        assert(value==0xbeef);
    }
    return 1;
}
static MusashiSpuCdAudio *create(Sink *s) {
    MusashiSpuCdAudioBackend b={s,ready,thread,status,queue};
    s->ready=1; s->token=1; s->audio=musashi_spu_cd_audio_create();
    assert(s->audio && musashi_spu_cd_audio_init_bios_muted(s->audio,&b,0));
    return s->audio;
}
static void wr(MusashiSpuCdAudio *a,unsigned r,unsigned v) {
    assert(musashi_spu_cd_audio_write16(a,0x1f801000+r,(uint16_t)v));
}
static unsigned rd(MusashiSpuCdAudio *a,unsigned r) {
    uint16_t v=0xffff; assert(musashi_spu_cd_audio_read16(a,0x1f801000+r,&v)); return v;
}
static MusashiSpuVoiceSnapshot voice(MusashiSpuCdAudio *a,unsigned i) {
    MusashiSpuVoiceSnapshot v; assert(musashi_spu_cd_audio_voice_snapshot(a,i,&v)); return v;
}
static MusashiSpuCdAudioSnapshot state(MusashiSpuCdAudio *a) {
    MusashiSpuCdAudioSnapshot s; assert(musashi_spu_cd_audio_snapshot(a,&s)); return s;
}
static void advance(MusashiSpuCdAudio *a,unsigned n) {
    assert(musashi_spu_cd_audio_advance(a,(uint64_t)n*768));
}
static void upload(MusashiSpuCdAudio *a,unsigned flags,unsigned data) {
    unsigned i; wr(a,0xdac,4); wr(a,0xda6,0x200); wr(a,0xda8,7|(flags<<8));
    for(i=0;i<7;i++) wr(a,0xda8,data|(data<<8));
}
static void configure(MusashiSpuCdAudio *a,unsigned i,unsigned l,unsigned r) {
    unsigned base=0xc00+i*16;
    wr(a,base,l); wr(a,base+2,r); wr(a,base+4,0x1000); wr(a,base+6,0x200);
    wr(a,base+8,0); wr(a,base+10,0);
}
static void play(MusashiSpuCdAudio *a,unsigned data) {
    upload(a,7,data); configure(a,0,0x3fff,0x2000);
    wr(a,0xd80,0x3fff); wr(a,0xd82,0x3fff); wr(a,0xdaa,0xc000); wr(a,0xd88,1);
}
static void test_ram_and_manual_status(void) {
    Sink sink={0}; MusashiSpuCdAudio *a=create(&sink); unsigned i; unsigned char bytes[16];
    assert(!musashi_spu_cd_audio_copy_ram(a,0x1000,bytes,16));
    assert(!musashi_spu_cd_audio_write16(a,0x1f801da8,0x1234));
    upload(a,7,7);
    assert(musashi_spu_cd_audio_copy_ram(a,0x1000,bytes,16));
    for(i=0;i<16;i++) assert(bytes[i]==7);
    assert(state(a).manual_halfwords==8 && state(a).transfer_cursor==0x1010);
    assert(rd(a,0xda6)==0x200 && rd(a,0xdae)==0);
    wr(a,0xdaa,0x10); assert(rd(a,0xdae)==0x10 && state(a).sample_index==0);
    wr(a,0xdaa,0); assert(rd(a,0xdae)==0);
    assert(!musashi_spu_cd_audio_write16(a,0x1f801dac,0));
    /* Retail 8003AB84 sets SPUCNT transfer-mode DMA-write (bit5). */
    assert(musashi_spu_cd_audio_write16(a,0x1f801daa,0x20));
    assert(rd(a,0xdae)==0x20);
    assert(musashi_spu_cd_audio_write16(a,0x1f801daa,0xc0a1));
    wr(a,0xda6,0xffff); for(i=0;i<5;i++) wr(a,0xda8,0x1234+i);
    assert(state(a).transfer_cursor==2 && rd(a,0xda6)==0xffff);
    assert(musashi_spu_cd_audio_copy_ram(a,0,bytes,2)); assert(bytes[0]==0x38 && bytes[1]==0x12);
    assert(!musashi_spu_cd_audio_copy_ram(a,0x7ffff,bytes,2));
    assert(musashi_spu_cd_audio_destroy(a));
}
static void test_keys_envelope_and_disabled_entry(void) {
    static const unsigned levels[]={0,14336,28672,32767,16383,8191,4095,2047};
    Sink sink={0}; MusashiSpuCdAudio *a=create(&sink); unsigned i;
    upload(a,7,7); configure(a,0,0x3fff,0x3fff); wr(a,0xd88,1);
    assert(voice(a,0).pending_on && voice(a,0).phase==MUSASHI_SPU_VOICE_OFF);
    assert(musashi_spu_cd_audio_advance(a,767)); assert(voice(a,0).pending_on);
    for(i=0;i<8;i++) { advance(a,i+1); assert(rd(a,0xc0c)==levels[i]); }
    assert(voice(a,0).phase==MUSASHI_SPU_VOICE_SUSTAIN);
    assert(sink.frames==8); for(i=0;i<16;i++) assert(sink.pcm[i]==0);
    advance(a,80); assert(voice(a,0).decoded_blocks>=3 && voice(a,0).endx);
    assert(voice(a,0).envelope>0); /* flags7 loops; never force stopped after28 */
    wr(a,0xc0c,1000); assert(rd(a,0xc0c)==1000); advance(a,81); assert(rd(a,0xc0c)==15336);
    wr(a,0xd8c,1); advance(a,82); assert(voice(a,0).phase==MUSASHI_SPU_VOICE_RELEASE);
    advance(a,84); assert(rd(a,0xc0c)==0 && voice(a,0).phase==MUSASHI_SPU_VOICE_OFF);
    wr(a,0xdaa,0xc000); wr(a,0xd88,1); advance(a,86); assert(rd(a,0xc0c)>0);
    wr(a,0xd88,1); wr(a,0xdaa,0); assert(rd(a,0xc0c)==0 && voice(a,0).pending_on);
    advance(a,87); assert(voice(a,0).phase==MUSASHI_SPU_VOICE_ATTACK);
    advance(a,88); assert(rd(a,0xc0c)==14336); /* pending key survives disable edge */
    wr(a,0xd8c,1); wr(a,0xd88,1); advance(a,89); assert(rd(a,0xc0c)==0);
    assert(voice(a,0).phase==MUSASHI_SPU_VOICE_ATTACK); /* simultaneous KON wins */
    assert(musashi_spu_cd_audio_destroy(a));
}
static void test_pcm_mutations_and_chunking(void) {
    Sink sa={0}, sb={0}; MusashiSpuCdAudio *a=create(&sa),*b=create(&sb); unsigned i; int nonzero=0;
    play(a,0x77); play(b,0x77); advance(a,120);
    for(i=1;i<=120;i++) advance(b,i);
    assert(!memcmp(sa.pcm,sb.pcm,120*4));
    for(i=0;i<120;i++) { if(sa.pcm[i*2]>0) nonzero=1; assert(sa.pcm[i*2]>=sa.pcm[i*2+1]); }
    assert(nonzero && voice(a,0).decoded_blocks==voice(b,0).decoded_blocks);
    {
        /* PSX-SPX p=0 coefficients 4807,22963,4871,-1, decoded224;
         * explicit independent arithmetic vectors include envelope and both gains. */
        static const int16_t expected[]={0,0,0,0,12,6,162,81,217,108,107,53,
                                        52,26,25,12,11,5,107,53,204,102,217,108};
        assert(!memcmp(sa.pcm,expected,sizeof(expected)));
    }
    upload(a,7,0x99); advance(a,200); advance(b,200);
    assert(memcmp(sa.pcm+160*2,sb.pcm+160*2,40*4));
    assert(sa.pcm[199*2]<0 && sb.pcm[199*2]>0);
    wr(a,0xc00,0); advance(a,201); assert(sa.pcm[200*2]==0 && sa.pcm[200*2+1]<0);
    wr(a,0xc02,0x6000); advance(a,202); assert(sa.pcm[201*2+1]>0); /* signed fixed volume */
    wr(a,0xc02,0); advance(a,203); assert(sa.pcm[202*2+1]==0);
    assert(musashi_spu_cd_audio_destroy(a)); assert(musashi_spu_cd_audio_destroy(b));
}
static void test_twenty_four_and_refusals(void) {
    Sink sink={0}; MusashiSpuCdAudio *a=create(&sink); unsigned i;
    upload(a,7,0x77); wr(a,0xd80,0x3fff); wr(a,0xd82,0x3fff); wr(a,0xdaa,0xc000);
    for(i=0;i<24;i++) configure(a,i,0x3fff,0x3fff);
    wr(a,0xd88,0xffff); wr(a,0xd8a,0xffff); advance(a,20);
    for(i=0;i<24;i++) assert(voice(a,i).envelope==32767);
    /* 24 * low-amplitude filter0 sample does not clip; increase source amplitude. */
    wr(a,0xda6,0x200); wr(a,0xda8,0x0700); advance(a,60);
    assert(sink.pcm[59*2]==32765);
    wr(a,0xd8c,0xffff); wr(a,0xd8e,0xffff); advance(a,64);
    for(i=0;i<24;i++) { assert(rd(a,0xc0c+16*i)==0); wr(a,0xc08+16*i,0x80ff); wr(a,0xc0a+16*i,0x4000); }
    /* Configured exponential attack now runs the XG-derived envelope machine.
     * At frame65 KON applies; the next three attack steps are14336,28672,32256. */
    wr(a,0xd88,1); advance(a,68);
    assert(voice(a,0).phase==MUSASHI_SPU_VOICE_ATTACK && voice(a,0).envelope==32256);
    assert(sink.pcm[67*2]>0 && sink.pcm[67*2+1]>0);
    assert(!musashi_spu_cd_audio_write16(a,0x1f801d94,1));
    assert(!musashi_spu_cd_audio_write16(a,0x1f801c00,0x8000));
    assert(!musashi_spu_cd_audio_write16(a,0x1f801c04,0x4000));
    assert(!musashi_spu_cd_audio_write16(a,0x1f801c0c,0x8000));
    assert(musashi_spu_cd_audio_destroy(a));
    memset(&sink,0,sizeof(sink)); a=create(&sink); configure(a,0,0,0);
    assert(!musashi_spu_cd_audio_write16(a,0x1f801d88,1)); /* no invented fresh RAM */
    upload(a,0,7); wr(a,0xd88,1); assert(!musashi_spu_cd_audio_advance(a,80*768));
    assert(state(a).faulted && state(a).sample_index==0 && voice(a,0).pending_on);
    assert(musashi_spu_cd_audio_destroy(a));
    memset(&sink,0,sizeof(sink)); a=create(&sink); upload(a,1,7); configure(a,0,0,0); wr(a,0xd88,1);
    advance(a,80); assert(voice(a,0).endx && voice(a,0).envelope==0);
    assert(musashi_spu_cd_audio_destroy(a));
}
static void test_capture_status_and_filter_refusal(void) {
    Sink sink={0}; MusashiSpuCdAudio *a=create(&sink); unsigned char bytes[2];
    advance(a,256); assert(rd(a,0xdae)==0);
    wr(a,0xdac,4); assert(rd(a,0xdae)==0x800);
    advance(a,512); assert(rd(a,0xdae)==0);
    assert(musashi_spu_cd_audio_copy_ram(a,0,bytes,2));
    assert(bytes[0]==0 && bytes[1]==0);
    upload(a,7,7); configure(a,0,0,0); wr(a,0xda6,0x200); wr(a,0xda8,0x0717);
    assert(!musashi_spu_cd_audio_write16(a,0x1f801d88,1));
    assert(!voice(a,0).pending_on);
    assert(musashi_spu_cd_audio_destroy(a));
}
static void test_queue_transaction(void) {
    Sink sa={0}, sb={0}; MusashiSpuCdAudio *a=create(&sa),*b=create(&sb);
    MusashiSpuVoiceSnapshot va,vb; unsigned char ca[4096],cb[4096];
    play(a,0x77); play(b,0x77); sa.fail_call=2;
    assert(!musashi_spu_cd_audio_advance(a,2048*768)); advance(b,1024);
    assert(state(a).sample_index==1024 && state(a).cycle==1024*768 && state(a).faulted);
    va=voice(a,0); vb=voice(b,0); assert(!memcmp(&va,&vb,sizeof(va)));
    assert(musashi_spu_cd_audio_copy_ram(a,0,ca,4096)); assert(musashi_spu_cd_audio_copy_ram(b,0,cb,4096));
    assert(!memcmp(ca,cb,4096) && !memcmp(sa.pcm,sb.pcm,1024*4));
    assert(musashi_spu_cd_audio_destroy(a)); assert(musashi_spu_cd_audio_destroy(b));
    memset(&sa,0,sizeof(sa)); memset(&sb,0,sizeof(sb)); a=create(&sa); b=create(&sb);
    play(a,7); play(b,7); sa.reenter=1;
    assert(!musashi_spu_cd_audio_advance(a,80*768)); advance(b,80);
    va=voice(a,0); vb=voice(b,0); assert(!memcmp(&va,&vb,sizeof(va)));
    assert(state(a).sample_index==80 && sa.frames==80);
    assert(musashi_spu_cd_audio_destroy(a)); assert(musashi_spu_cd_audio_destroy(b));
}
int main(void) {
    test_ram_and_manual_status(); test_keys_envelope_and_disabled_entry();
    test_pcm_mutations_and_chunking(); test_twenty_four_and_refusals(); test_capture_status_and_filter_refusal(); test_queue_transaction();
    puts("SPU_VOICE_CORE_PASS"); return 0;
}
