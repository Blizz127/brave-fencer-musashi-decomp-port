/* Explicit synthetic FIFO/device fixture. This does not establish native boot
 * or physical transfer timing; the common absolute cycle is test-owned. */
#include "musashi_cd_dma3.h"
#include "musashi_dma_controller.h"
#include "musashi_boot_memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MADR 0x1f8010b0u
#define BCR 0x1f8010b4u
#define CHCR 0x1f8010b8u
#define DELAY 0x1f801018u
#define DICR 0x1f8010f4u

typedef struct Fixture {
    MusashiCdDma3 *owner;
    MusashiBootMemory *ram;
    MusashiIrqController irq;
    MusashiDmaController dma;
    uint64_t thread;
    uint8_t fifo[2340];
    size_t cursor, available;
    int requested, healthy, reject, nested_read, nested_identity, nested_close, calls;
} Fixture;
static uint64_t current(void *p) {
    Fixture *f=p;
    if (f->nested_identity && f->owner) {
        f->nested_identity=0;
        assert(!musashi_cd_dma3_close(f->owner));
    }
    return f->thread;
}
static int request(void *p,int *out) { *out=((Fixture *)p)->requested; return *out>=0; }
static int healthy(void *p) { return ((Fixture *)p)->healthy; }
static int available(void *p,size_t *out) {
    Fixture *f=p; *out=f->available-f->cursor; return 1;
}
static int data(void *p,uint8_t *out,size_t size) {
    Fixture *f=p;
    ++f->calls;
    if(f->nested_read) {
        uint32_t value=0xdeadbeef;
        assert(!musashi_cd_dma3_read32(f->owner,CHCR,&value));
        assert(value==0xdeadbeef);
    }
    if(f->nested_close) assert(!musashi_cd_dma3_close(f->owner));
    if(f->reject || size>f->available-f->cursor) return 0;
    memcpy(out,f->fifo+f->cursor,size); f->cursor+=size;
    return 1;
}
static void init(Fixture *f) {
    MusashiCdDma3Device device;
    size_t i;
    memset(f,0,sizeof(*f));
    f->ram=malloc(sizeof(*f->ram)); assert(f->ram);
    memset(f->ram->bytes,0xa5,sizeof(f->ram->bytes));
    f->thread=7; f->healthy=1; f->requested=1; f->available=sizeof(f->fifo);
    for(i=0;i<sizeof(f->fifo);++i) f->fifo[i]=(uint8_t)(i*37u+11u);
    f->irq.status=4; f->irq.mask=0;
    musashi_dma_controller_init(&f->dma,&f->irq);
    f->dma.control|=0x8000;
    device.userdata=f; device.current_thread=current; device.healthy=healthy; device.data_available=available; device.read_data=data; device.request_asserted=request;
    f->owner=musashi_cd_dma3_open(f->ram,&f->dma,&device,100); assert(f->owner);
}
static void finish(Fixture *f) {
    assert(musashi_cd_dma3_close(f->owner)); free(f->ram);
}
static MusashiCdDma3State state(Fixture *f) {
    MusashiCdDma3State s; assert(musashi_cd_dma3_get_state(f->owner,&s)); return s;
}
static void config(Fixture *f,uint32_t madr,uint32_t bcr) {
    assert(musashi_cd_dma3_write32(f->owner,DELAY,0x20943));
    assert(musashi_cd_dma3_write32(f->owner,MADR,madr));
    assert(musashi_cd_dma3_write32(f->owner,BCR,bcr));
}
static void success_and_masks(void) {
    Fixture f; MusashiCdDma3State s; uint32_t value; unsigned i;
    init(&f); config(&f,0x80001000,0x10003);
    /* Channel enabled but master disabled must not remember this completion. */
    assert(musashi_dma_controller_write32(&f.dma,DICR,0x00080000));
    assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
    s=state(&f); assert(s.chcr==0x01000000 && s.due==103 && s.cycle==100);
    for(i=0;i<20;++i) {
        assert(musashi_cd_dma3_read32(f.owner,CHCR,&value)); assert(value==0x01000000);
    }
    assert(f.cursor==0 && f.ram->bytes[0x1000]==0xa5 && f.irq.status==4);
    assert(musashi_cd_dma3_advance(f.owner,102)); assert(f.calls==0);
    assert(musashi_cd_dma3_advance(f.owner,103));
    s=state(&f); assert(s.chcr==0 && s.madr==0x1000 && s.bcr==0x10003);
    assert(s.transfers==1 && s.bytes==12 && f.cursor==12);
    assert(!memcmp(f.ram->bytes+0x1000,f.fifo,12));
    assert(f.ram->bytes[0xfff]==0xa5 && f.ram->bytes[0x100c]==0xa5);
    assert(f.dma.interrupt==0x80000 && f.irq.status==4);
    assert(musashi_dma_controller_write32(&f.dma,DICR,0x00880000));
    assert(f.irq.status==4 && f.dma.interrupt==0x00880000);
    config(&f,0xa0002000,0x10200);
    assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
    assert(state(&f).due==615);
    assert(musashi_cd_dma3_advance(f.owner,615));
    assert(!memcmp(f.ram->bytes+0x2000,f.fifo+12,2048));
    assert(f.ram->bytes[0x1fff]==0xa5 && f.ram->bytes[0x2800]==0xa5);
    s=state(&f); assert(s.bytes==2060 && s.transfers==2 && s.chcr==0);
    assert(f.dma.interrupt==0x88880000 && f.irq.status==12 && f.irq.mask==0);
    /* CPU and DMA flag acknowledgements are different owners. */
    f.irq.status=4;
    assert(musashi_dma_controller_write32(&f.dma,DICR,0x00880000));
    assert(f.irq.status==4 && f.dma.interrupt==0x88880000);
    assert(musashi_dma_controller_write32(&f.dma,DICR,0x08880000));
    assert(f.dma.interrupt==0x00880000 && f.irq.status==4);
    assert(musashi_cd_dma3_advance(f.owner,999)); assert(f.calls==2);
    finish(&f);
}
static void manual_count_ignores_high_half(void) {
    const uint32_t high[]={0u,1u,0xffffu};unsigned i;
    for(i=0;i<3;i++) {
        Fixture f;uint32_t delay;init(&f);
        assert(musashi_cd_dma3_write32(f.owner,DELAY,0x20843));
        assert(musashi_cd_dma3_read32(f.owner,DELAY,&delay)&&delay==0x20843);
        config(&f,0x1000,(high[i]<<16)|8u);
        assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
        assert(state(&f).due==108);
        assert(musashi_cd_dma3_advance(f.owner,108));
        assert(f.cursor==32 && state(&f).bytes==32 && state(&f).transfers==1);
        assert(!memcmp(f.ram->bytes+0x1000,f.fifo,32));finish(&f);
    }
}
static void chopped_stream_payload(void) {
    Fixture f;init(&f);config(&f,0x1000,3);
    assert(musashi_cd_dma3_write32(f.owner,DELAY,0x21020843));
    assert(musashi_dma_controller_write32(&f.dma,DICR,0x00880000));
    assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11400100));
    assert(state(&f).due==101);
    assert(musashi_cd_dma3_advance(f.owner,101));
    assert(f.cursor==4 && state(&f).madr==0x1004 && state(&f).bcr==2);
    assert(state(&f).transfers==0 && !(f.irq.status&8));
    assert(musashi_cd_dma3_advance(f.owner,117));assert(f.cursor==4);
    assert(musashi_cd_dma3_advance(f.owner,118));assert(f.cursor==8);
    assert(musashi_cd_dma3_advance(f.owner,135));
    assert(f.cursor==12 && state(&f).madr==0x100c && state(&f).bcr==0);
    assert(state(&f).transfers==1 && !(state(&f).chcr&0x01000000) && (f.irq.status&8));
    assert(!memcmp(f.ram->bytes+0x1000,f.fifo,12));finish(&f);
    init(&f);config(&f,0x1000,3);
    assert(musashi_cd_dma3_write32(f.owner,DELAY,0x21020843));
    assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11400100));
    assert(musashi_cd_dma3_advance(f.owner,101));f.reject=1;
    assert(!musashi_cd_dma3_advance(f.owner,118));
    assert(f.cursor==4 && state(&f).bytes==4 && state(&f).transfers==0);
    assert(state(&f).chcr&0x01000000);
    assert(!memcmp(f.ram->bytes+0x1000,f.fifo,4) && f.ram->bytes[0x1004]==0xa5);
    finish(&f);
}
static void chopped_request_pause(void) {
    Fixture f; MusashiCdDma3State paused;
    init(&f); config(&f,0x1000,3);
    assert(musashi_cd_dma3_write32(f.owner,DELAY,0x21020843));
    assert(musashi_dma_controller_write32(&f.dma,DICR,0x00880000));
    assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11400100));
    assert(musashi_cd_dma3_advance(f.owner,101));
    f.requested=0;
    assert(musashi_cd_dma3_advance(f.owner,1000));
    paused=state(&f);
    assert(paused.waiting_for_request && paused.fault==MUSASHI_CD_DMA3_OK);
    assert(paused.madr==0x1004 && paused.bcr==2 && paused.bytes==4);
    assert(paused.transfers==0 && (paused.chcr&0x01000000) && !(f.irq.status&8));
    assert(f.cursor==4 && f.ram->bytes[0x1004]==0xa5);
    f.requested=1;
    assert(musashi_cd_dma3_advance(f.owner,1001));
    assert(!state(&f).waiting_for_request && f.cursor==8);
    assert(musashi_cd_dma3_advance(f.owner,1017)); assert(f.cursor==8);
    assert(musashi_cd_dma3_advance(f.owner,1018));
    assert(f.cursor==12 && state(&f).transfers==1 && (f.irq.status&8));
    assert(!memcmp(f.ram->bytes+0x1000,f.fifo,12)); finish(&f);
}
static void chopped_delay_write(void) {
    Fixture f; MusashiCdDma3State before,after;
    uint32_t invalid[] = { MADR, BCR, CHCR, DELAY };
    unsigned i;
    init(&f); config(&f,0x1000,3);
    assert(musashi_cd_dma3_write32(f.owner,DELAY,0x21020843));
    assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11400100));
    assert(musashi_cd_dma3_advance(f.owner,101));
    f.requested=0; assert(musashi_cd_dma3_advance(f.owner,200));
    f.requested=1; assert(musashi_cd_dma3_advance(f.owner,201));
    before=state(&f);
    /* Retail80046EAC writes memory-control delay after reasserting BFRD. */
    assert(musashi_cd_dma3_write32(f.owner,DELAY,0x20943));
    after=state(&f);
    assert(after.cdrom_delay==0x20943 && after.fault==MUSASHI_CD_DMA3_OK);
    assert(after.chcr==before.chcr && after.bcr==before.bcr && after.madr==before.madr);
    assert(after.cycle==before.cycle && after.due==before.due);
    assert(after.bytes==before.bytes && after.transfers==before.transfers);
    assert(f.cursor==8 && f.ram->bytes[0x1008]==0xa5 && !(f.irq.status&8));
    assert(musashi_cd_dma3_advance(f.owner,218));
    assert(state(&f).transfers==1 && !memcmp(f.ram->bytes+0x1000,f.fifo,12));
    finish(&f);
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        init(&f); config(&f,0x1000,3);
        assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
        before=state(&f);
        assert(!musashi_cd_dma3_write32(f.owner,invalid[i],0));
        after=state(&f);
        assert(after.fault==MUSASHI_CD_DMA3_ACCESS && after.chcr==before.chcr);
        assert(after.madr==before.madr && after.bcr==before.bcr && f.cursor==0);
        finish(&f);
    }
}
static void chopped_request_invalid(void) {
    int requested;
    for(requested=-1;requested<=2;requested+=3) {
        Fixture f; init(&f); config(&f,0x1000,3);
        assert(musashi_cd_dma3_write32(f.owner,DELAY,0x21020843));
        assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11400100));
        f.requested=requested;
        assert(!musashi_cd_dma3_advance(f.owner,101));
        assert(state(&f).fault==MUSASHI_CD_DMA3_BACKEND && state(&f).transfers==0);
        assert(f.cursor==0 && f.ram->bytes[0x1000]==0xa5); finish(&f);
    }
    {
        Fixture f; init(&f); config(&f,0x1000,3);
        assert(musashi_cd_dma3_write32(f.owner,DELAY,0x21020843));
        assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11400100));
        f.requested=0;
        assert(musashi_cd_dma3_advance(f.owner,UINT64_MAX-2));
        f.requested=1;
        assert(!musashi_cd_dma3_advance(f.owner,UINT64_MAX-1));
        assert(state(&f).fault==MUSASHI_CD_DMA3_OVERFLOW && f.cursor==0);
        finish(&f);
    }
}
static void refusal_cases(void) {
    unsigned i;
    for(i=0;i<12;++i) {
        Fixture f; MusashiCdDma3State before,after; int result=1;
        init(&f); config(&f,0x80001000,0x10003); before=state(&f);
        switch(i) {
        case 0: result=musashi_cd_dma3_write32(f.owner,MADR,0x80001001); break;
        case 1: result=musashi_cd_dma3_write32(f.owner,MADR,0x80200000); break;
        case 2: result=musashi_cd_dma3_write32(f.owner,MADR,0x1f801000); break;
        case 3: result=musashi_cd_dma3_write32(f.owner,BCR,0x10000); break;
        case 4: result=musashi_cd_dma3_write32(f.owner,BCR,0x1024a); break; /* 586 words */
        case 5: result=musashi_cd_dma3_write32(f.owner,CHCR,0x11400100); break;
        case 6: f.dma.control&=~0x8000u; result=musashi_cd_dma3_write32(f.owner,CHCR,0x11000000); break;
        case 7: result=musashi_cd_dma3_write32(f.owner,DELAY,0x21030843); break;
        case 8: assert(musashi_cd_dma3_write32(f.owner,MADR,0x801ffff8));
                before=state(&f); result=musashi_cd_dma3_write32(f.owner,CHCR,0x11000000); break;
        case 9: result=musashi_cd_dma3_advance(f.owner,99); break;
        case 10: f.healthy=0; result=musashi_cd_dma3_write32(f.owner,CHCR,0x11000000); break;
        case 11: f.thread=8; result=musashi_cd_dma3_write32(f.owner,CHCR,0x11000000); f.thread=7; break;
        }
        assert(!result); after=state(&f); assert(after.fault);
        assert(after.madr==before.madr && after.bcr==before.bcr && after.chcr==before.chcr);
        assert(after.cycle==before.cycle && f.cursor==0 && f.calls==0 && f.irq.status==4);
        assert(f.ram->bytes[0x1000]==0xa5 && f.ram->bytes[0x1ffff8]==0xa5);
        finish(&f);
    }
}
static void all_positive_word_counts(void) {
    unsigned words;
    for (words=1; words<=585; ++words) {
        Fixture f;
        MusashiCdDma3State s;
        size_t bytes=(size_t)words*4;
        init(&f);
        config(&f,0x3000,0x10000u|words);
        assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
        s=state(&f);
        assert(s.due==100+words && s.bytes==0 && s.transfers==0);
        assert(musashi_cd_dma3_advance(f.owner,100+words));
        s=state(&f);
        assert(s.chcr==0 && s.bytes==bytes && s.transfers==1);
        assert(f.cursor==bytes && !memcmp(f.ram->bytes+0x3000,f.fifo,bytes));
        assert(f.ram->bytes[0x2fff]==0xa5 && f.ram->bytes[0x3000+bytes]==0xa5);
        finish(&f);
    }
}
static void four_hundred_word_partial_copy(void) {
    Fixture f;
    MusashiCdDma3State s;
    const size_t bytes=400u*4u;
    init(&f);
    config(&f,0x5000,0x10190); /* 400 words; 1600 bytes, no trailer copy. */
    assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
    assert(musashi_cd_dma3_advance(f.owner,500));
    s=state(&f);
    assert(s.bytes==bytes && s.transfers==1 && f.cursor==bytes);
    assert(!memcmp(f.ram->bytes+0x5000,f.fifo,bytes));
    assert(f.ram->bytes[0x4fff]==0xa5 && f.ram->bytes[0x5000+bytes]==0xa5);
    finish(&f);
}
static void completion_refusal_and_reentry(void) {
    unsigned i;
    for(i=0;i<7;++i) {
        Fixture f; MusashiCdDma3State s;
        init(&f); config(&f,0x1000,0x10003);
        assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
        if(i==0) f.available=11;
        if(i==1) f.reject=1;
        if(i==2 || i==3) assert(musashi_dma_controller_write32(&f.dma,DICR,0x00880000));
        if(i==2) f.nested_read=1;
        if(i==3) f.nested_close=1;
        if(i==4) f.dma.control&=~0x8000u;
        if(i==5) f.healthy=0;
        if(i==6) f.nested_identity=1;
        assert(!musashi_cd_dma3_advance(f.owner,103));
        s=state(&f); assert(s.fault);
        if(i==2 || i==3) {
            assert(f.cursor==12 && s.transfers==1 && s.chcr==0);
            assert(!memcmp(f.ram->bytes+0x1000,f.fifo,12));
        } else {
            assert(f.cursor==0 && s.transfers==0 && s.chcr==0x01000000);
            assert(f.ram->bytes[0x1000]==0xa5);
        }
        assert(f.irq.status==((i==2 || i==3)?12:4));
        if(i==2 || i==3) assert(f.dma.interrupt==0x88880000 && s.cycle==103);
        assert(!musashi_cd_dma3_advance(f.owner,200));
        finish(&f);
    }
}
static void preflight_and_completion_masks(void) {
    Fixture f;
    MusashiCdDma3State before,after;
    MusashiCdDma3Device device;
    uint32_t value=0xdeadbeef;
    unsigned i;
    init(&f); config(&f,0xa0001000,0x10003);
    f.available=11; before=state(&f);
    assert(!musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
    after=state(&f);
    assert(after.chcr==before.chcr && after.due==before.due && after.cycle==before.cycle);
    assert(f.cursor==0 && f.calls==0);
    for(i=0;i<MUSASHI_RAM_SIZE;++i) assert(f.ram->bytes[i]==0xa5);
    finish(&f);
    init(&f);
    assert(musashi_cd_dma3_read32(f.owner,DELAY,&value) && value==0);
    assert(musashi_cd_dma3_write32(f.owner,MADR,0));
    assert(musashi_cd_dma3_write32(f.owner,BCR,0x10003));
    assert(!musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
    assert(f.calls==0 && f.cursor==0); finish(&f);
    init(&f);
    assert(musashi_dma_controller_write32(&f.dma,DICR,0x00800000));
    config(&f,0x801ffff4,0x10003); /* Exact valid final word range. */
    assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
    assert(musashi_cd_dma3_advance(f.owner,103));
    assert(!memcmp(f.ram->bytes+0x1ffff4,f.fifo,12));
    assert(f.dma.interrupt==0x00800000 && f.irq.status==4); /* Channel masked. */
    assert(!musashi_dma_controller_complete(&f.dma,7));
    assert(f.dma.interrupt==0x00800000 && f.irq.status==4);
    finish(&f);
    init(&f);
    device.userdata=&f; device.current_thread=current; device.healthy=healthy;
    device.data_available=available; device.read_data=data; device.request_asserted=request;
    f.thread=0;
    assert(!musashi_cd_dma3_open(f.ram,&f.dma,&device,0));
    f.thread=7; f.healthy=0;
    assert(!musashi_cd_dma3_open(f.ram,&f.dma,&device,0));
    f.healthy=1; device.data_available=NULL;
    assert(!musashi_cd_dma3_open(f.ram,&f.dma,&device,0));
    finish(&f);
}
static void lifecycle(void) {
    Fixture f; MusashiCdDma3State s; uint32_t value=0xdeadbeef;
    init(&f); config(&f,0x1000,0x10003);
    assert(musashi_cd_dma3_advance(f.owner,UINT64_MAX-2));
    assert(!musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
    assert(state(&f).fault==MUSASHI_CD_DMA3_OVERFLOW); finish(&f);
    init(&f); config(&f,0x1000,0x10003);
    assert(musashi_cd_dma3_write32(f.owner,CHCR,0x11000000));
    assert(!musashi_cd_dma3_write32(f.owner,MADR,0x2000));
    assert(f.cursor==0 && state(&f).chcr==0x01000000); finish(&f);
    init(&f); assert(!musashi_cd_dma3_read32(f.owner,CHCR+1,&value));
    assert(value==0xdeadbeef); s=state(&f); assert(s.fault);
    f.nested_identity=1; assert(!musashi_cd_dma3_close(f.owner));
    finish(&f);
    init(&f); f.thread=8; memset(&s,0x5a,sizeof(s));
    assert(!musashi_cd_dma3_get_state(f.owner,&s));
    assert(s.cycle==UINT64_C(0x5a5a5a5a5a5a5a5a));
    assert(!musashi_cd_dma3_close(f.owner)); f.thread=7; finish(&f);
}
#ifdef DMA3_PINNED_MEDIA
#include "musashi_cd_controller.h"
#include "musashi_disc_media.h"
typedef struct Mounted {
    MusashiCdOwned *cd;
    MusashiIrqController irq;
} Mounted;
static uint64_t mounted_thread(void *p) { (void)p; return 17; }
static int mounted_cd_healthy(void *p) { (void)p; return 1; }
static int mounted_raise(void *p) { ((Mounted *)p)->irq.status |= 4; return 1; }
static int mounted_healthy(void *p) {
    MusashiCdOwnedState s;
    return musashi_cd_owned_get_state(((Mounted *)p)->cd,&s) && !s.fault;
}
static int mounted_request(void *p,int *out) {
    MusashiCdOwnedState s;
    if (!musashi_cd_owned_get_state(((Mounted *)p)->cd,&s) || s.fault) return 0;
    *out=s.data_requested != 0; return 1;
}
static int mounted_available(void *p,size_t *out) {
    MusashiCdOwnedState s;
    if(!musashi_cd_owned_get_state(((Mounted *)p)->cd,&s) || s.fault || s.fifo_cursor>s.fifo_size) return 0;
    *out=(s.data_requested && s.fifo_valid)?(size_t)(s.fifo_size-s.fifo_cursor):0;
    return 1;
}
static int mounted_data(void *p,uint8_t *out,size_t size) {
    return musashi_cd_owned_read_data(((Mounted *)p)->cd,out,size);
}
static void cdw(Mounted *m,unsigned reg,unsigned value) {
    assert(musashi_cd_owned_write8(m->cd,0x1f801800+reg,(uint8_t)value));
}
static MusashiCdOwnedState mounted_state(Mounted *m) {
    MusashiCdOwnedState s; assert(musashi_cd_owned_get_state(m->cd,&s)); return s;
}
static void cd_packet(Mounted *m,unsigned kind,unsigned expected) {
    uint8_t byte;
    assert(mounted_state(m).interrupt==kind);
    assert(musashi_cd_owned_read8(m->cd,0x1f801801,&byte) && byte==expected);
    cdw(m,0,1); cdw(m,3,7); cdw(m,0,0);
}
static void cd_command(Mounted *m,unsigned command) {
    cdw(m,1,command);
    assert(musashi_cd_owned_advance(m->cd,mounted_state(m).due));
}
static void mounted_pvd(const char *cue,const char *bin) {
    Mounted m={0};
    MusashiDiscMedia *media=musashi_disc_media_open_pinned(cue,bin);
    MusashiBootMemory *memory=malloc(sizeof(*memory));
    MusashiDmaController dma;
    MusashiCdDma3 *transfer;
    MusashiCdDma3State final;
    MusashiCdOwnedHardware hardware={
        .userdata=&m, .current_thread=mounted_thread, .healthy=mounted_cd_healthy,
        .raise_irq4=mounted_raise, .data_transfer_idle=NULL
    };
    MusashiCdDma3Device device={&m,mounted_thread,mounted_healthy,mounted_available,mounted_data,mounted_request};
    uint8_t raw[2352]; uint64_t due;
    assert(media && memory); memset(memory->bytes,0xa5,sizeof(memory->bytes));
    m.cd=musashi_cd_owned_open_bios_idle(media,&hardware,0); assert(m.cd);
    cdw(&m,2,0); cdw(&m,2,2); cdw(&m,2,0x16); cd_command(&m,2); cd_packet(&m,3,2);
    cdw(&m,2,0xa0); cd_command(&m,0x0e); cd_packet(&m,3,2);
    cd_command(&m,6); cd_packet(&m,3,0x22);
    due=mounted_state(&m).sector_due;
    assert(musashi_cd_owned_advance(m.cd,due)); cd_packet(&m,1,0x22); cdw(&m,3,0x80);
    assert(musashi_disc_media_read_sector(media,16,raw,sizeof(raw)));
    assert(!memcmp(raw+25,"CD001",5));
    musashi_dma_controller_init(&dma,&m.irq); dma.control|=0x8000;
    assert(musashi_dma_controller_write32(&dma,DICR,0x00880000));
    transfer=musashi_cd_dma3_open(memory,&dma,&device,due); assert(transfer);
    assert(musashi_cd_dma3_write32(transfer,DELAY,0x20943));
    assert(musashi_cd_dma3_write32(transfer,MADR,0x80001000));
    assert(musashi_cd_dma3_write32(transfer,BCR,0x10003));
    assert(musashi_cd_dma3_write32(transfer,CHCR,0x11000000));
    assert(mounted_state(&m).fifo_cursor==0 && memory->bytes[0x1000]==0xa5);
    assert(musashi_cd_owned_advance(m.cd,due+3));
    assert(musashi_cd_dma3_advance(transfer,due+3));
    assert(!memcmp(memory->bytes+0x1000,raw+12,12));
    assert(mounted_state(&m).fifo_cursor==12 && m.irq.status==12);
    assert(musashi_dma_controller_write32(&dma,DICR,0x08880000));
    assert(m.irq.status==12); /* DICR ack does not acknowledge CD or CPU I_STAT. */
    assert(musashi_cd_dma3_write32(transfer,MADR,0xa0002000));
    assert(musashi_cd_dma3_write32(transfer,BCR,0x10200));
    cdw(&m,3,0x80); assert(mounted_state(&m).fifo_cursor==12);
    assert(musashi_cd_dma3_write32(transfer,CHCR,0x11000000));
    assert(musashi_cd_owned_advance(m.cd,due+515));
    assert(musashi_cd_dma3_advance(transfer,due+515));
    assert(!memcmp(memory->bytes+0x2000,raw+24,2048));
    assert(mounted_state(&m).fifo_cursor==2060 && mounted_state(&m).data_bytes_read==2060);
    assert(musashi_cd_dma3_get_state(transfer,&final));
    assert(final.cycle==mounted_state(&m).cycle && final.bytes==2060 && final.transfers==2);
    assert(memory->bytes[0x100c]==0xa5 && memory->bytes[0x2800]==0xa5);
    /* Real BFRD clear rewinds the CD FIFO and pauses an in-flight chop. */
    cdw(&m,3,0); cdw(&m,3,0x80);
    assert(musashi_cd_dma3_write32(transfer,DELAY,0x21020843));
    assert(musashi_cd_dma3_write32(transfer,MADR,0x3000));
    assert(musashi_cd_dma3_write32(transfer,BCR,3));
    assert(musashi_cd_dma3_write32(transfer,CHCR,0x11400100));
    assert(musashi_cd_dma3_advance(transfer,due+516));
    assert(mounted_state(&m).fifo_cursor==4);
    cdw(&m,3,0);
    assert(musashi_cd_dma3_advance(transfer,due+1000));
    assert(musashi_cd_dma3_get_state(transfer,&final));
    assert(final.waiting_for_request && final.transfers==2 && final.bcr==2);
    assert(memory->bytes[0x3004]==0xa5 && mounted_state(&m).fifo_cursor==0);
    cdw(&m,3,0x80);
    assert(musashi_cd_dma3_advance(transfer,due+1001));
    assert(musashi_cd_dma3_advance(transfer,due+1018));
    assert(!memcmp(memory->bytes+0x3000,raw+12,4));
    assert(!memcmp(memory->bytes+0x3004,raw+12,8));
    assert(musashi_cd_dma3_get_state(transfer,&final));
    assert(!final.waiting_for_request && final.transfers==3 && final.bytes==2072);
    assert(musashi_cd_dma3_close(transfer)); assert(musashi_cd_owned_close(m.cd));
    musashi_disc_media_close(media); free(memory);
}
#endif
int main(int argc,char **argv) {
    success_and_masks(); all_positive_word_counts(); four_hundred_word_partial_copy();
    chopped_delay_write();chopped_request_invalid();chopped_request_pause();chopped_stream_payload();manual_count_ignores_high_half();refusal_cases(); completion_refusal_and_reentry();
    preflight_and_completion_masks(); lifecycle();
#ifdef DMA3_PINNED_MEDIA
    assert(argc==3); mounted_pvd(argv[1],argv[2]);
#else
    (void)argc; (void)argv;
#endif
    puts("CD_DMA3_SOURCE_PROFILE_PASS"); return 0;
}
