#include "musashi_cd_controller.h"
#include "musashi_disc_media.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Hardware {
    uint64_t thread;
    int healthy;
    unsigned raises;
    MusashiCdOwned *reenter;
} Hardware;
static uint64_t owner(void *p) { return ((Hardware *)p)->thread; }
static int healthy(void *p) {
    Hardware *h=p;
    if (h->reenter) { uint8_t b; assert(!musashi_cd_owned_read8(h->reenter,0x1f801800,&b)); }
    return h->healthy;
}
static int dma_busy;
static int transfer_idle(void *p,int *idle) { (void)p; *idle=!dma_busy; return 1; }
static int irq(void *p) { ++((Hardware *)p)->raises; return 1; }
static MusashiCdOwned *create(const MusashiDiscMedia *m, Hardware *h, uint64_t epoch) {
    MusashiCdOwnedHardware hw={
        .userdata=h, .current_thread=owner, .healthy=healthy, .raise_irq4=irq,
        .data_transfer_idle=transfer_idle
    };
    MusashiCdOwned *c=musashi_cd_owned_open_bios_idle(m,&hw,epoch); assert(c); return c;
}
static MusashiCdOwnedState state(MusashiCdOwned *c) {
    MusashiCdOwnedState s; assert(musashi_cd_owned_get_state(c,&s)); return s;
}
static void w(MusashiCdOwned *c,unsigned reg,unsigned value) {
    assert(musashi_cd_owned_write8(c,0x1f801800+reg,(uint8_t)value));
}
static unsigned r(MusashiCdOwned *c,unsigned reg) {
    uint8_t b=0xcc; assert(musashi_cd_owned_read8(c,0x1f801800+reg,&b)); return b;
}
static void packet(MusashiCdOwned *c,unsigned kind,unsigned status) {
    assert(state(c).interrupt==kind && state(c).response_count==1);
    assert(r(c,1)==status); w(c,0,1); w(c,3,7); w(c,0,0);
}
static void command(MusashiCdOwned *c,unsigned cmd) {
    w(c,1,cmd); assert(musashi_cd_owned_advance(c,state(c).due));
}
static uint64_t start(MusashiCdOwned *c) {
    w(c,2,0); w(c,2,2); w(c,2,0x16); command(c,2); packet(c,3,2);
    w(c,2,0xa0); command(c,0x0e); packet(c,3,2);
    command(c,6); packet(c,3,0x22); return state(c).sector_due;
}
static void data_only(const MusashiDiscMedia *m, unsigned read_command) {
    Hardware h={1,1,0,NULL}; MusashiCdOwned *c=create(m,&h,0);
    uint8_t raw[2352],out[2048];
    w(c,2,0);w(c,2,2);w(c,2,0x16);command(c,2);packet(c,3,2);
    w(c,2,0xc0);command(c,0x0e);packet(c,3,2);
    assert(state(c).mode==0xc0);
    command(c,read_command);packet(c,3,0x22);
    assert(musashi_cd_owned_advance(c,state(c).sector_due));
    packet(c,1,0x22);w(c,3,0x80);
    assert(state(c).fifo_size==2048);
    assert(musashi_cd_owned_read_data(c,out,sizeof out));
    assert(musashi_disc_media_read_sector(m,16,raw,sizeof raw));
    assert(!memcmp(out,raw+24,sizeof out));
    assert(!(r(c,0)&0x40));
    assert(musashi_cd_owned_close(c));
}
static void restart_after_pause(const MusashiDiscMedia *m) {
    Hardware h={1,1,0,NULL}; MusashiCdOwned *c=create(m,&h,0);
    uint8_t out[12]; uint64_t due=start(c);
    assert(musashi_cd_owned_advance(c,due));packet(c,1,0x22);w(c,3,0x80);
    assert(musashi_cd_owned_read_data(c,out,sizeof out));
    command(c,9);packet(c,3,0x22);
    assert(musashi_cd_owned_advance(c,state(c).due));packet(c,2,2);
    w(c,2,0);w(c,2,2);w(c,2,0x16);command(c,2);packet(c,3,2);
    assert(state(c).fifo_cursor==12 && state(c).data_requested);
    command(c,0x1b);packet(c,3,0x22);
    assert(!state(c).fifo_valid && !state(c).data_requested);
    assert(state(c).data_bytes_read==12 && state(c).buffer_retirements==1);
    assert(musashi_cd_owned_advance(c,state(c).sector_due));packet(c,1,0x22);
    w(c,3,0x80);assert(state(c).fifo_cursor==0 && state(c).fifo_lba==16);
    assert(musashi_cd_owned_close(c));
    /* A promised active DMA view cannot be retired by another read. */
    c=create(m,&h,0);due=start(c);
    assert(musashi_cd_owned_advance(c,due));packet(c,1,0x22);w(c,3,0x80);
    command(c,9);packet(c,3,0x22);
    assert(musashi_cd_owned_advance(c,state(c).due));packet(c,2,2);
    dma_busy=1;
    assert(!musashi_cd_owned_write8(c,0x1f801801,0x1b));
    assert(state(c).fifo_valid && state(c).fifo_cursor==0 && !state(c).buffer_retirements);
    dma_busy=0;assert(musashi_cd_owned_close(c));
}
static unsigned xa_calls;
static int capture_xa(void *p,uint64_t cycle,uint64_t first,const int16_t *pcm,size_t frames) {
    unsigned i,nonzero=0;(void)p;assert(cycle && first==0 && frames==2352);
    for(i=0;i<frames*2;i++)nonzero+=pcm[i]!=0;
    assert(nonzero);
    ++xa_calls;return 1;
}
static void xa_audio_delivery(const MusashiDiscMedia *m) {
    Hardware h={1,1,0,NULL};MusashiCdOwned *c=create(m,&h,0);
    assert(musashi_cd_owned_set_xa_sink(c,capture_xa,NULL));
    /* First real opening audio sector4664 must produce PCM, not a data sector. */
    w(c,2,1);w(c,2,4);w(c,2,0x14);command(c,2);packet(c,3,2);
    w(c,2,0xc0);command(c,0x0e);packet(c,3,2);command(c,0x1b);packet(c,3,0x22);
    assert(musashi_cd_owned_advance(c,state(c).sector_due));
    assert(xa_calls==1 && state(c).xa_sectors==1 && state(c).xa_frames==2352);
    assert(!state(c).interrupt && !state(c).incoming_valid && !state(c).announced_valid);
    assert(state(c).next_lba==4665 && state(c).sectors_fetched==1 && !state(c).sectors_published);
    assert(musashi_cd_owned_close(c));
}
/* Setfilter + mode C8: the opening's XA sector 4664 plays under its own
 * file/channel and is skipped (no PCM, no data) under any other channel. */
static void xa_filter(const MusashiDiscMedia *m) {
    MusashiDiscMediaInfo info;uint8_t raw[2352];unsigned pass;
    assert(musashi_disc_media_get_info(m,&info));
    assert(musashi_disc_media_read_sector(m,4664+info.tracks[0].index01_frame,raw,sizeof raw));
    assert((raw[18]&0x44)==0x44);
    for(pass=0;pass<2;++pass){
        Hardware h={1,1,0,NULL};MusashiCdOwned *c=create(m,&h,0);
        unsigned before=xa_calls;
        assert(musashi_cd_owned_set_xa_sink(c,capture_xa,NULL));
        w(c,2,raw[16]);w(c,2,pass?(raw[17]+1u)&0x1f:raw[17]);command(c,0x0d);packet(c,3,2);
        assert(state(c).filter_file==raw[16]);
        w(c,2,1);w(c,2,4);w(c,2,0x14);command(c,2);packet(c,3,2);
        w(c,2,0xc8);command(c,0x0e);packet(c,3,2);assert(state(c).mode==0xc8);
        command(c,0x1b);packet(c,3,0x22);
        assert(musashi_cd_owned_advance(c,state(c).sector_due));
        assert(xa_calls==before+(pass?0u:1u));
        assert(state(c).xa_sectors==(pass?0u:1u) && state(c).xa_filtered==(pass?1u:0u));
        assert(!state(c).interrupt && !state(c).incoming_valid && state(c).next_lba==4665);
        assert(musashi_cd_owned_close(c));
    }
}
static void pvd(const MusashiDiscMedia *m) {
    Hardware h={1,1,0,NULL}; MusashiCdOwned *c=create(m,&h,97);
    uint64_t due=start(c); uint8_t raw[2352],out[2068],trailer[280];
    MusashiCdOwnedState s;
    assert(musashi_disc_media_read_sector(m,16,raw,sizeof raw));
    assert(raw[12]==0 && raw[13]==2 && raw[14]==0x16 && raw[15]==2);
    assert(!memcmp(raw+25,"CD001",5));
    assert(due==97+3*0x800+451584);
    assert(musashi_cd_owned_advance(c,due-1)); assert(!(r(c,0)&0x40));
    assert(state(c).sectors_fetched==0);
    assert(musashi_cd_owned_advance(c,due));
    s=state(c); assert(s.interrupt==1 && s.sectors_fetched==1 && s.next_lba==17);
    assert(!(r(c,0)&0x40)); packet(c,1,0x22); /* Source ACK precedes request. */
    w(c,3,0x80); assert(r(c,0)&0x40);
    memset(out,0xcd,sizeof out);
    assert(musashi_cd_owned_read_data(c,out+4,12)); assert(!memcmp(out+4,raw+12,12));
    w(c,3,0x80); assert(state(c).fifo_cursor==12);
    assert(musashi_cd_owned_read_data(c,out+16,2048));
    assert(!memcmp(out+16,raw+24,2048));
    assert(out[0]==0xcd && out[3]==0xcd && out[2064]==0xcd && out[2067]==0xcd);
    assert(state(c).fifo_cursor==2060 && state(c).data_bytes_read==2060);
    assert(state(c).cycle==due); /* Neither requests nor copies supplied time. */
    command(c,9); packet(c,3,0x22); s=state(c);
    assert(!s.reading && s.phase==2 && s.due==due+0x800+1000000);
    assert(musashi_cd_owned_advance(c,s.due-1)); assert(!state(c).interrupt);
    assert(musashi_cd_owned_advance(c,s.due)); packet(c,2,2);
    assert(state(c).sectors_fetched==1 && state(c).fifo_cursor==2060);
    assert(musashi_cd_owned_read_data(c,trailer,sizeof trailer));
    assert(!memcmp(trailer,raw+2072,sizeof trailer)); assert(!(r(c,0)&0x40));
    assert(musashi_cd_owned_close(c));
}
static void requests(const MusashiDiscMedia *m) {
    Hardware h={1,1,0,NULL}; MusashiCdOwned *c=create(m,&h,0);
    uint64_t due=start(c); uint8_t raw[2352],out[12];
    assert(musashi_disc_media_read_sector(m,16,raw,sizeof raw));
    assert(musashi_cd_owned_advance(c,due)); packet(c,1,0x22);
    assert(state(c).announced_valid && state(c).announced_lba==16);
    assert(!state(c).fifo_valid && !state(c).data_requested);
    w(c,3,0x80); assert(r(c,2)==raw[12]);
    w(c,3,0); assert(!(r(c,0)&0x40)); assert(state(c).fifo_cursor==0);
    w(c,3,0x80); assert(musashi_cd_owned_read_data(c,out,12)); assert(!memcmp(out,raw+12,12));
    /* An incoming following sector must not overwrite requested bytes. */
    assert(musashi_cd_owned_advance(c,due+225792));
    assert(state(c).sectors_fetched==2 && state(c).fifo_lba==16);
    assert(state(c).announced_valid && state(c).announced_lba==17);
    assert(musashi_cd_owned_read_data(c,out,12)); assert(!memcmp(out,raw+24,12));
    assert(musashi_cd_owned_advance(c,due+2*225792));
    assert(!state(c).fault && state(c).incoming_valid && state(c).incoming_lba==18);
    assert(state(c).fifo_lba==16 && state(c).fifo_cursor==24);
    assert(musashi_cd_owned_close(c));
}
static void refusals(const MusashiDiscMedia *m) {
    unsigned i; Hardware h={1,1,0,NULL};
    for(i=0;i<14;++i) {
        MusashiCdOwned *c=create(m,&h,10); uint8_t out[2344],before[2344]; uint64_t due;
        memset(out,0x5c,sizeof out); memcpy(before,out,sizeof out);
        if(i<6) {
            switch(i) {
            case 0: assert(!musashi_cd_owned_write8(c,0x1f801801,2)); break;
            case 1: w(c,2,0);w(c,2,0x6a);w(c,2,0);assert(!musashi_cd_owned_write8(c,0x1f801801,2));break;
            case 2: w(c,2,0);w(c,2,0);w(c,2,0);assert(!musashi_cd_owned_write8(c,0x1f801801,2));break;
            case 3: w(c,2,0xe0);assert(!musashi_cd_owned_write8(c,0x1f801801,0x0e));break;
            case 4: assert(!musashi_cd_owned_write8(c,0x1f801801,6));break;
            case 5: assert(!musashi_cd_owned_read_data(c,out,4));break;
            }
        } else {
            due=start(c); assert(musashi_cd_owned_advance(c,due)); packet(c,1,0x22); w(c,3,0x80);
            switch(i) {
            case 6: assert(!musashi_cd_owned_read_data(c,out,2341));break;
            case 7: assert(!musashi_cd_owned_read_data(c,NULL,4));break;
            case 8: assert(!musashi_cd_owned_read_data(c,out,0));break;
            case 9: h.thread=2;assert(!musashi_cd_owned_read_data(c,out,4));h.thread=1;break;
            case 10:h.healthy=0;assert(!musashi_cd_owned_read_data(c,out,4));h.healthy=1;break;
            case 11:h.reenter=c;assert(!musashi_cd_owned_read_data(c,out,4));h.reenter=NULL;break;
            case 12:assert(!musashi_cd_owned_advance(c,due-1));break;
            /* Setmode during reads is already supported by the LIST owner.
             * Other mode bits (here 04, CD-DA report) still refuse. */
            case 13:w(c,2,0xc4);assert(!musashi_cd_owned_write8(c,0x1f801801,0x0e));break;
            }
            assert(state(c).fifo_cursor==0);
        }
        assert(!memcmp(before,out,sizeof out)); assert(state(c).fault);
        assert(musashi_cd_owned_close(c));
    }
}
static void holds(const MusashiDiscMedia *m) {
    Hardware h={1,1,0,NULL}; MusashiCdOwned *c=create(m,&h,0); uint64_t due;
    w(c,2,0);w(c,2,2);w(c,2,0x16);command(c,2);packet(c,3,2);
    w(c,2,0xa0);command(c,0x0e);packet(c,3,2);
    command(c,6);due=state(c).sector_due;
    assert(musashi_cd_owned_advance(c,due));assert(state(c).interrupt==3);
    assert(state(c).sectors_fetched==1);packet(c,3,0x22);
    assert(musashi_cd_owned_advance(c,due+255));assert(!state(c).interrupt);
    assert(musashi_cd_owned_advance(c,due+256));packet(c,1,0x22);
    assert(musashi_cd_owned_close(c));
}
static void protected_unrequested(const MusashiDiscMedia *m) {
    Hardware h={1,1,0,NULL}; MusashiCdOwned *c=create(m,&h,0);
    uint64_t due=start(c); uint8_t raw[2352],out[12];
    assert(musashi_disc_media_read_sector(m,16,raw,sizeof raw));
    assert(musashi_cd_owned_advance(c,due));packet(c,1,0x22);
    /* An acknowledged packet still names its sector before BFRD. */
    assert(musashi_cd_owned_advance(c,due+225792));
    assert(!state(c).fifo_valid && !state(c).data_requested);
    assert(state(c).announced_valid && state(c).announced_lba==16);
    assert(state(c).incoming_valid && state(c).incoming_lba==17);
    assert(!state(c).interrupt);
    w(c,3,0x80);assert(musashi_cd_owned_read_data(c,out,sizeof out));
    assert(!memcmp(out,raw+12,sizeof out));
    assert(musashi_cd_owned_close(c));
}
static void pause_first_and_overflow(const MusashiDiscMedia *m) {
    Hardware h={1,1,0,NULL}; MusashiCdOwned *c=create(m,&h,0);
    uint64_t due=start(c); uint8_t all[2340];
    assert(!musashi_cd_owned_write8(c,0x1f801801,9));assert(musashi_cd_owned_close(c));
    c=create(m,&h,0);due=start(c);assert(musashi_cd_owned_advance(c,due));packet(c,1,0x22);
    w(c,3,0x80);assert(musashi_cd_owned_read_data(c,all,sizeof all));command(c,9);packet(c,3,0x22);
    assert(musashi_cd_owned_advance(c,state(c).due));packet(c,2,2);
    start(c); /* A second read's first sector is independent of lifetime count. */
    assert(!musashi_cd_owned_write8(c,0x1f801801,9));assert(musashi_cd_owned_close(c));
    c=create(m,&h,UINT64_MAX-3*0x800-451584);
    due=start(c);assert(due==UINT64_MAX);
    assert(!musashi_cd_owned_advance(c,due));assert(state(c).fault==MUSASHI_CD_OWNED_OVERFLOW);
    assert(!state(c).sectors_fetched);assert(musashi_cd_owned_close(c));
}
static void stream_unrequested_publication(const MusashiDiscMedia *m) {
    Hardware h={1,1,0,NULL}; MusashiCdOwned *c=create(m,&h,0);
    uint8_t raw[2352],out[2048];uint64_t due;
    w(c,2,0);w(c,2,2);w(c,2,0x16);command(c,2);packet(c,3,2);
    w(c,2,0xc0);command(c,0x0e);packet(c,3,2);
    command(c,0x1b);packet(c,3,0x22);due=state(c).sector_due;
    assert(musashi_cd_owned_advance(c,due));packet(c,1,0x22);
    /* Deliberately ignore the first data notification, as the stream
     * callback does while its frame ring is full. A later INT1 still arrives. */
    assert(musashi_cd_owned_advance(c,due+225792));
    assert(state(c).announced_lba==17 && state(c).sectors_published==2);
    assert(!state(c).incoming_valid && !state(c).data_bytes_read);
    packet(c,1,0x22);w(c,3,0x80);
    assert(musashi_cd_owned_read_data(c,out,sizeof out));
    assert(musashi_disc_media_read_sector(m,17,raw,sizeof raw));
    assert(!memcmp(out,raw+24,sizeof out));
    assert(musashi_cd_owned_close(c));
}

int main(int argc,char **argv) {
    MusashiDiscMedia *m; assert(argc==3);m=musashi_disc_media_open_pinned(argv[1],argv[2]);assert(m);
    stream_unrequested_publication(m);xa_audio_delivery(m);xa_filter(m);restart_after_pause(m);data_only(m,6);data_only(m,0x1b);pvd(m);requests(m);refusals(m);holds(m);protected_unrequested(m);pause_first_and_overflow(m);musashi_disc_media_close(m);
    puts("CD_SECTOR_PASS");return 0;
}
