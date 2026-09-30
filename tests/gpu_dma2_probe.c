/* General DMA2 owner fixture. No host rendering or native source return proof. */
#include "musashi_gpu_dma2.h"
#include "musashi_boot_memory.h"
#include "musashi_dma_controller.h"
#include "musashi_gpu_controller.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MADR 0x1f8010a0u
#define BCR 0x1f8010a4u
#define CHCR 0x1f8010a8u
#define START 0x01000401u
static MusashiBootMemory ram, before_ram;
static MusashiIrqController irq;
static MusashiDmaController dma;
static MusashiGpuController gpu;
static MusashiGpuDma2 *owner;
static uint64_t thread;
static int health, render_ready, refuse_at, nested_at, accepted;
static uint32_t words[1024];
static int identity_nested, health_nested, ready_nested, gpu_nested_at, allocation_refuse;
static int duplicate_open;
static MusashiGpuDma2Device device;
void *__real_realloc(void *,size_t);
void *__wrap_realloc(void *p,size_t n) { return allocation_refuse?NULL:__real_realloc(p,n); }
static uint64_t identity(void *p) {
    (void)p;
    if (identity_nested) {
        identity_nested=0;
        assert(!musashi_gpu_dma2_close(dma.gpu_owner));
    }
    if (duplicate_open) {
        duplicate_open=0;
        assert(!musashi_gpu_dma2_open(&ram,&dma,&gpu,&device,0));
    }
    return thread;
}
static int healthy(void *p) {
    (void)p;
    if (health_nested) {
        health_nested=0;
        assert(!musashi_gpu_dma2_advance(dma.gpu_owner,0));
    }
    return health;
}
static int reset(void *p) { (void)p;return 1; }
static int display(void *p,int enabled) { (void)p;(void)enabled;return 1; }
static int ready(void *p) {
    (void)p;
    if (ready_nested) {
        ready_nested=0;
        assert(!musashi_gpu_dma2_advance(dma.gpu_owner,0));
    }
    return render_ready;
}
static int word(void *p,uint32_t value) {
    (void)p;
    if (accepted==refuse_at) return 0;
    if (accepted==nested_at) assert(!musashi_gpu_dma2_advance(owner,100));
    if (accepted==gpu_nested_at)
        assert(!musashi_gpu_controller_write32(&gpu,MUSASHI_GPU_GP0,0xe1000777));
    assert(accepted<1024);
    words[accepted++]=value;
    return 1;
}
static void put(uint32_t a,uint32_t v) {
    assert(!(a&3) && a<=MUSASHI_RAM_SIZE-4);
    for(unsigned i=0;i<4;i++) ram.bytes[a+i]=(uint8_t)(v>>(8*i));
}
static MusashiGpuDma2State state(void) {
    MusashiGpuDma2State s;assert(musashi_gpu_dma2_get_state(owner,&s));return s;
}
static void setup(uint64_t epoch) {
    thread=1;health=render_ready=1;refuse_at=nested_at=gpu_nested_at=-1;accepted=0;
    identity_nested=health_nested=ready_nested=duplicate_open=allocation_refuse=0;
    memset(&ram,0xa5,sizeof(ram));memset(&gpu,0,sizeof(gpu));
    irq=(MusashiIrqController){0};
    musashi_dma_controller_init(&dma,&irq);
    MusashiGpuBackend b={.reset=reset,.draw_mode=word,.display_enable=display,
                         .clear_fifo=reset,.ready=ready,.environment=word};
    assert(musashi_gpu_controller_init(&gpu,&b));
    assert(musashi_gpu_controller_write32(&gpu,MUSASHI_GPU_GP1,0x04000002));
    dma.control|=0x800;
    device=(MusashiGpuDma2Device){NULL,identity,healthy};
    owner=musashi_gpu_dma2_open(&ram,&dma,&gpu,&device,epoch);assert(owner);
}
static void close_owner(void) { assert(musashi_gpu_dma2_close(owner));owner=NULL; }
static void config(uint32_t address,uint32_t bcr) {
    assert(musashi_gpu_dma2_write32(owner,MADR,address));
    assert(musashi_gpu_dma2_write32(owner,BCR,bcr));
}
static void simple(void) {
    setup(100);
    put(0x100,0x02000200);put(0x104,0xe1000001);put(0x108,0xe1000123);
    put(0x200,0x00000300);put(0x300,0x01ffffff);put(0x304,0xe1000007);
    before_ram=ram;config(0x80000100,0xfedc1234);
    assert(musashi_gpu_dma2_write32(owner,CHCR,START));
    MusashiGpuDma2State s=state();
    assert(s.nodes==3 && s.header_words==3 && s.payload_words==3);
    assert(s.starts==1 && s.transfers==0 && s.accepted_words==3 && s.fetched_words==3);
    assert(s.accepted_in_transfer==3 && s.madr==0xffffff && s.bcr==0xfedc1234);
    assert(s.chcr==START && s.due==107 && accepted==3);
    assert(words[0]==0xe1000001 && words[1]==0xe1000123 && words[2]==0xe1000007);
    assert(gpu.draw_mode==7 && !memcmp(&ram,&before_ram,sizeof(ram)));
    for(unsigned n=0;n<4;n++) {uint32_t v=0;assert(musashi_gpu_dma2_read32(owner,CHCR,&v));assert(v==START);}
    assert(state().cycle==100 && !irq.status);
    assert(musashi_gpu_dma2_advance(owner,106));assert(state().chcr==START);
    assert(musashi_gpu_dma2_advance(owner,107));s=state();
    assert(s.chcr==0x401 && s.transfers==1 && !irq.status);
    assert(musashi_gpu_dma2_advance(owner,900));assert(state().transfers==1);
    close_owner();
}
static int fill(void *p,const MusashiGpuFill *f) {
    (void)p;
    assert(f->command==0x02ff0000u && f->xy==0 && f->wh==0x00010010u);
    assert(f->width==16 && f->height==1 && f->color==0x7c00);
    ++accepted;
    return 1;
}
static void split_fill(void) {
    /* Actual GPU partial packet persists over DMA transfer completion. */
    setup(0);gpu.backend.fill_vram=fill;
    put(0x100,0x01ffffff);put(0x104,0x02ff0000u);
    config(0x100,0);
    assert(musashi_gpu_dma2_write32(owner,CHCR,START));
    assert(gpu.fill_words==1 && !accepted && state().accepted_words==1);
    assert(musashi_gpu_dma2_advance(owner,3));
    assert(state().transfers==1 && gpu.fill_words==1);
    uint32_t status=0;
    assert(musashi_gpu_controller_read32(&gpu,MUSASHI_GPU_GPUSTAT,&status));
    assert(!(status&0x04000000u) && (status&0x12000000u)==0x12000000u);
    put(0x200,0x01000300);put(0x204,0); /* XY andWH on distinctnodes. */
    put(0x300,0x01ffffff);put(0x304,0x00010010u);
    config(0x200,0xfedcba98u);
    assert(musashi_gpu_dma2_write32(owner,CHCR,START));
    assert(accepted==1 && !gpu.fill_words && gpu.completed_fills==1);
    assert(state().accepted_words==3 && state().transfers==1 && state().due==8);
    assert(musashi_gpu_dma2_advance(owner,8));
    assert(state().transfers==2 && gpu.accepted_gp0_words==3 && !state().fault);
    close_owner();
}
static void partial(void) {
    for(int nested=0;nested<2;nested++) {
        setup(0);put(0x100,0x03ffffff);
        put(0x104,0xe1000001);put(0x108,0xe1000002);put(0x10c,0xe1000003);
        config(0x100,0);if(nested) nested_at=1;else refuse_at=1;
        assert(!musashi_gpu_dma2_write32(owner,CHCR,START));
        MusashiGpuDma2State s=state();
        assert(s.fault==(nested?MUSASHI_GPU_DMA2_REENTRY:MUSASHI_GPU_DMA2_BACKEND));
        assert(s.accepted_words==(unsigned)(nested?2:1) && accepted==(nested?2:1));
        assert(gpu.draw_mode==(unsigned)(nested?2:1));
        assert(s.chcr==START && !s.transfers && !irq.status);
        assert(!musashi_gpu_dma2_advance(owner,10000));
        assert(state().transfers==0 && !irq.status);close_owner();
    }
}
static void packet_and_lengths(void) {
    static const uint32_t packet[]={0x06ffffff,0xe3000000,0xe4000000,0xe5000000,
                                    0xe1000000,0xe2000000,0xe6000000};
    setup(5);
    for(unsigned n=0;n<7;n++) put(0xa6454+n*4,packet[n]);
    config(0x800a6454,0xffffffff);before_ram=ram;
    assert(musashi_gpu_dma2_write32(owner,CHCR,START));
    assert(state().due==13 && accepted==6 && state().accepted_words==6);
    for(unsigned n=0;n<6;n++) assert(words[n]==packet[n+1]);
    assert(!memcmp(&ram,&before_ram,sizeof(ram)));
    /* Real bytes were consumed at start; future RAM edits cannot retcon them. */
    put(0xa645c,0xe403bd3f);
    assert(musashi_gpu_dma2_advance(owner,13));assert(words[1]==0xe4000000);
    config(0xa00a6454,0x12345678);
    assert(musashi_gpu_dma2_write32(owner,CHCR,START));
    assert(words[7]==0xe403bd3f && accepted==12 && state().due==21);
    assert(musashi_gpu_dma2_advance(owner,21));assert(state().transfers==2);
    close_owner();
    for(unsigned count=0;count<=255;count+=count?254:1) {
        setup(0);put(0x100,(count<<24)|0xffffff);
        for(unsigned n=0;n<count;n++) put(0x104+n*4,0xe1000000|n);
        config(0x100,0x11111111);
        assert(musashi_gpu_dma2_write32(owner,CHCR,START));
        assert(accepted==(int)count && state().due==count+2 && state().payload_words==count);
        assert(musashi_gpu_dma2_advance(owner,count+2));assert(state().transfers==1);
        close_owner();
    }
}
static void malformed(void) {
    for(unsigned c=0;c<10;c++) {
        setup(0);put(0x100,0x01ffffff);put(0x104,0xe1000001);config(0x100,0);
        switch(c) {
        case 0:put(0x100,0x01000100);break; /* self-link */
        case 1:put(0x100,0x01000200);put(0x200,0x00000100);break;
        case 2:put(0x100,0x01000201);break;
        case 3:put(0x100,0x01200000);break;
        case 4:put(0x100,0x01800000);break; /* Error marker, not normal end. */
        case 5:config(0x1ffffc,0);put(0x1ffffc,0x01ffffff);break;
        case 6:put(0x100,0x01000200);put(0x200,0x001ffffc);put(0x1ffffc,0x01ffffff);break;
        case 7:
            for(unsigned n=0;n<2049;n++)
                put(0x100+4*n,0xff000000u|(n==2048?0xffffffu:0x104+4*n));
            break;
        case 8:allocation_refuse=1;break;
        case 9:put(0x100,0x01000104);put(0x104,0x00000100);break;
        }
        before_ram=ram;
        assert(!musashi_gpu_dma2_write32(owner,CHCR,START));
        MusashiGpuDma2State s=state();
        assert(s.fault==(c==7||c==8?MUSASHI_GPU_DMA2_RESOURCE:MUSASHI_GPU_DMA2_CHAIN));
        assert(!accepted && !s.starts && !s.transfers && !s.chcr && !irq.status);
        assert(!memcmp(&ram,&before_ram,sizeof(ram)));close_owner();
    }
}
static void access_refusals(void) {
    for(unsigned c=0;c<19;c++) {
        setup(c==16?UINT64_MAX-2:0);put(0x100,0x01ffffff);put(0x104,0xe1000001);config(0x100,0);
        uint32_t address=CHCR,value=START;
        switch(c) {
        case 0:dma.control&=~0x800u;break;
        case 1:gpu.dma_direction=0;break;
        case 2:value=0x01000201;break;
        case 3:value=0x11000401;break;
        case 4:value=0x01000501;break;
        case 5:address=MADR;value=0x80000101;break;
        case 6:address=MADR;value=0x80200100;break;
        case 7:address=MADR;value=0xc0000100;break;
        case 8:address=BCR+1;break;
        case 9:render_ready=0;break;
        case 10:render_ready=-1;break;
        case 11:health=0;break;
        case 12:thread=2;break;
        case 13:identity_nested=1;break;
        case 14:health_nested=1;break;
        case 15:ready_nested=1;break;
        case 16:break;
        case 17:gpu.accepted_gp0_words=UINT64_MAX;break;
        case 18:dma.irq=NULL;break;
        }
        assert(!musashi_gpu_dma2_write32(owner,address,value));thread=1;
        MusashiGpuDma2State s=state();
        assert(s.fault!=MUSASHI_GPU_DMA2_OK && !s.starts && !s.accepted_words && !s.chcr);
        assert(!accepted && !irq.status);close_owner();
    }
}
static void ownership(void) {
    setup(0);
    assert(dma.gpu_owner==owner);
    assert(!musashi_gpu_dma2_open(&ram,&dma,&gpu,&device,0));
    for(unsigned n=0;n<2;n++) {
        uint32_t v=n?0x401:0;
        assert(!musashi_dma_controller_write32(&dma,CHCR,v));
        assert(musashi_gpu_dma2_write32(owner,CHCR,v));
        assert(dma.gpu_channel_control==v);
    }
    close_owner();assert(!dma.gpu_owner);
    assert(musashi_dma_controller_write32(&dma,CHCR,0));
    for(unsigned n=0;n<4;n++) {
        if(n==0) thread=0;
        if(n==1) {thread=1;health=0;}
        if(n==2) {health=1;identity_nested=1;}
        if(n==3) health_nested=1;
        assert(!musashi_gpu_dma2_open(&ram,&dma,&gpu,&device,0));
        assert(!dma.gpu_owner);
    }
    thread=1;health=1;duplicate_open=1;
    owner=musashi_gpu_dma2_open(&ram,&dma,&gpu,&device,0);assert(owner);close_owner();
    setup(0);uint32_t v=0xdeadbeef;
    assert(!musashi_gpu_dma2_read32(owner,CHCR+1,&v));assert(v==0xdeadbeef);close_owner();
    setup(10);assert(!musashi_gpu_dma2_advance(owner,9));assert(state().fault==MUSASHI_GPU_DMA2_CLOCK);close_owner();
    setup(0);put(0x100,0x00ffffff);config(0x100,0);
    assert(musashi_gpu_dma2_write32(owner,CHCR,START));
    assert(!musashi_dma_controller_write32(&dma,CHCR,0));
    assert(!musashi_gpu_dma2_write32(owner,CHCR,0));assert(state().chcr==START);close_owner();
    assert(dma.gpu_channel_control==START && !dma.gpu_owner && !irq.status);
    assert(!musashi_dma_controller_write32(&dma,CHCR,0));
}
static void irq_gates(void) {
    for(unsigned mode=0;mode<4;mode++) {
        setup(7);put(0x100,0x01ffffff);put(0x104,0xe1000012);config(0x100,0);
        uint32_t enables=((mode&1)?0x800000:0)|((mode&2)?0x40000:0);
        assert(musashi_dma_controller_write32(&dma,0x1f8010f4,enables));
        assert(musashi_gpu_dma2_write32(owner,CHCR,START));assert(!irq.status);
        assert(musashi_gpu_dma2_advance(owner,9));assert(!irq.status);
        assert(musashi_gpu_dma2_advance(owner,10));assert(state().chcr==0x401);
        assert(irq.status==(mode==3?8:0));
        assert((dma.interrupt&0x04000000u)==(mode==3?0x04000000u:0));
        if(mode==3) {
            assert(musashi_dma_controller_write32(&dma,0x1f8010f4,enables|0x04000000));
            assert(!(dma.interrupt&0x84000000u) && irq.status==8);
            irq.status=0;assert(musashi_gpu_dma2_advance(owner,999));assert(!irq.status);
        }
        close_owner();
    }
}
static void nonzero_environment(void) {
    static const uint32_t commands[]={0xe2001357,0xe3040102,0xe403bd3f,
                                     0xe53ff7ff,0xe6000003,0xe100060a};
    setup(50);put(0x100,0x06ffffff);
    for(unsigned i=0;i<6;i++) put(0x104+4*i,commands[i]);
    config(0x100,0);
    assert(musashi_gpu_dma2_write32(owner,CHCR,START));
    assert(gpu.texture_window==0x1357 && gpu.drawing_area_start==0x40102);
    assert(gpu.drawing_area_end==0x3bd3f && gpu.drawing_offset==0x3ff7ff);
    assert(gpu.mask_flags==3 && gpu.draw_mode==0x60a && accepted==6);
    for(unsigned i=0;i<6;i++) assert(words[i]==commands[i]);
    assert(musashi_gpu_dma2_advance(owner,58));close_owner();
}
static void overlap_and_unsupported_word(void) {
    setup(0);
    /* Header104 is also payload of node100. This is not a header cycle. */
    put(0x100,0x02000104);put(0x104,0x00000300);put(0x108,0xe1000001);
    put(0x300,0x01ffffff);put(0x304,0xe1000002);config(0x100,0);
    assert(musashi_gpu_dma2_write32(owner,CHCR,START));
    assert(state().nodes==3 && state().accepted_words==3 && state().due==7);
    assert(accepted==2 && words[0]==0xe1000001 && words[1]==0xe1000002);
    assert(musashi_gpu_dma2_advance(owner,7));close_owner();
    setup(0);put(0x100,0x02ffffff);put(0x104,0xe1000007);put(0x108,0x02000000);
    config(0x100,0);assert(!musashi_gpu_dma2_write32(owner,CHCR,START));
    assert(state().accepted_words==1 && gpu.draw_mode==7 && accepted==1);
    assert(state().fault_node==0x100 && state().fault_word==0x108);
    close_owner();
}
static void gpu_reentry_commit(void) {
    setup(0);put(0x100,0x02ffffff);put(0x104,0xe1000003);put(0x108,0xe1000004);
    config(0x100,0);gpu_nested_at=1;
    assert(!musashi_gpu_dma2_write32(owner,CHCR,START));
    assert(gpu.faulted && accepted==2 && gpu.draw_mode==4);
    assert(state().accepted_words==2 && !state().transfers && !irq.status);
    close_owner();
}
static int stored;
static int store_pixel(void *p, uint16_t x, uint16_t y, uint16_t pixel) {
    (void)p; (void)x; (void)y; (void)pixel;
    ++stored;
    return 1;
}
static void image_block(void) {
    MusashiGpuDma2State s;
    setup(100);
    stored = 0;
    gpu.backend.store_vram = store_pixel;
    assert(musashi_gpu_controller_write32(&gpu, MUSASHI_GPU_GP0, 0xa0000000u));
    assert(musashi_gpu_controller_write32(&gpu, MUSASHI_GPU_GP0, 0));
    assert(musashi_gpu_controller_write32(&gpu, MUSASHI_GPU_GP0, 0x00020002u));
    put(0x400, 0x22221111u);
    put(0x404, 0x44443333u);
    config(0x400, 0x00010002u);
    assert(musashi_gpu_dma2_write32(owner, CHCR, 0x01000201u));
    assert(stored == 4);
    s = state();
    assert(s.payload_words == 2 && s.accepted_in_transfer == 2 && s.starts == 1);
    assert(s.chcr == 0x01000201u && s.transfers == 0);
    assert(musashi_gpu_dma2_advance(owner, 102));
    assert(state().transfers == 0);
    assert(musashi_gpu_dma2_advance(owner, 103));
    assert(state().transfers == 1 && (state().chcr & 0x01000000u) == 0);
    close_owner();
}
int main(void) {
    split_fill();
    simple();partial();packet_and_lengths();malformed();access_refusals();ownership();irq_gates();nonzero_environment();overlap_and_unsupported_word();gpu_reentry_commit();image_block();puts("GPU_DMA2_OWNED_PASS fixture_renderer=synthetic");return 0;
}
