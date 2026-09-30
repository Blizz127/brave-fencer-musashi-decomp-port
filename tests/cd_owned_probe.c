#include "musashi_cd_controller.h"
#include "musashi_disc_media.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Hardware {
    uint64_t thread;
    int healthy, irq_ok;
    unsigned raises;
    uint16_t istat, imask;
} Hardware;
static MusashiCdOwned *reenter_irq;
static MusashiCdOwned *reenter_identity;
static uint64_t thread_id(void *p) {
    if (reenter_identity) assert(!musashi_cd_owned_close(reenter_identity));
    return ((Hardware *)p)->thread;
}
static int healthy(void *p) { return ((Hardware *)p)->healthy; }
static int irq4(void *p) {
    Hardware *h = p;
    if (!h->irq_ok) return 0;
    if (reenter_irq) assert(!musashi_cd_owned_advance(reenter_irq, 0));
    h->istat |= 4; ++h->raises; return 1;
}
static MusashiCdOwned *open_cd(const MusashiDiscMedia *m, Hardware *h, uint64_t e) {
    MusashiCdOwnedHardware hw = {.userdata=h,.current_thread=thread_id,.healthy=healthy,.raise_irq4=irq4};
    return musashi_cd_owned_open_bios_idle(m, &hw, e);
}
static MusashiCdOwnedState state(MusashiCdOwned *c) {
    MusashiCdOwnedState s; assert(musashi_cd_owned_get_state(c, &s)); return s;
}
static void w(MusashiCdOwned *c, unsigned reg, unsigned v) {
    assert(musashi_cd_owned_write8(c, 0x1f801800u + reg, (uint8_t)v));
}
static unsigned r(MusashiCdOwned *c, unsigned reg) {
    uint8_t v = 0xaa; assert(musashi_cd_owned_read8(c, 0x1f801800u + reg, &v)); return v;
}
static void drain(MusashiCdOwned *c, Hardware *h, unsigned expected_if) {
    assert(r(c, 0) == 0x38);
    w(c, 0, 1);
    assert(r(c, 3) == (0xe0u | expected_if));
    assert(r(c, 0) == 0x39);
    assert(r(c, 1) == 2);
    assert(r(c, 0) == 0x19);
    w(c, 3, 7); w(c, 2, 7);
    assert(h->istat == 4); /* Internal acknowledge never clears CPU I_STAT. */
    h->istat = 0; /* Explicit fixture CPU acknowledgement. */
    w(c, 0, 0);
}
static void sequence(const MusashiDiscMedia *m) {
    Hardware h = {1,1,1,0,0,0};
    MusashiCdOwned *c = open_cd(m, &h, 100);
    MusashiCdOwnedState s;
    assert(c); s = state(c);
    assert(s.enable == 0x1f && s.lid_closed && s.rotating && s.standby);
    assert(s.volume[0] == 0x80 && s.volume[3] == 0x80);
    assert(s.volume_pending[0] == 0 && s.volume_pending[3] == 0);
    assert(r(c, 0) == 0x18); w(c, 3, 0);
    w(c, 1, 1); assert(r(c, 0) == 0x98);
    assert(musashi_cd_owned_advance(c, 100 + 0x7ff)); assert(!h.raises);
    assert(musashi_cd_owned_advance(c, 100 + 0x800)); assert(h.raises == 1);
    drain(c, &h, 3);
    w(c, 1, 0x0a);
    assert(musashi_cd_owned_advance(c, 100 + 0x1000));
    s = state(c); assert(s.phase == 2 && s.due == 100 + 0x1000 + 4100000);
    assert(!s.muted && s.mode == 0x20); drain(c, &h, 3);
    assert(musashi_cd_owned_advance(c, s.due - 1)); assert(h.raises == 2);
    assert(musashi_cd_owned_advance(c, s.due)); drain(c, &h, 2);
    w(c, 1, 0x0c);
    assert(musashi_cd_owned_advance(c, s.due + 0x800)); drain(c, &h, 3);
    assert(h.raises == 4 && !state(c).phase);
    assert(musashi_cd_owned_close(c));
}
static void holds(const MusashiDiscMedia *m) {
    Hardware h = {1,1,1,0,0,0}; MusashiCdOwned *c = open_cd(m,&h,0);
    uint64_t second = 0x800 + 4100000;
    w(c,0,1); w(c,2,0); w(c,0,0); w(c,1,0x0a);
    assert(musashi_cd_owned_advance(c,0x800)); assert(h.raises == 0);
    w(c,0,1); w(c,2,7); assert(h.raises == 1); w(c,0,0);
    assert(musashi_cd_owned_advance(c,second + 600));
    assert(state(c).due == second + 768 && state(c).interrupt == 3);
    assert(r(c,1) == 2); /* IF alone continues to hold. */
    assert(musashi_cd_owned_advance(c,second + 768));
    assert(state(c).due == second + 1024);
    w(c,0,1); w(c,3,7); w(c,0,0);
    assert(musashi_cd_owned_advance(c,second + 1023)); assert(h.raises == 1);
    assert(musashi_cd_owned_advance(c,second + 1024)); drain(c,&h,2);
    assert(musashi_cd_owned_close(c));
    c = open_cd(m,&h,0); w(c,1,0x0a);
    assert(musashi_cd_owned_advance(c,0x800)); w(c,0,1); w(c,3,7); w(c,0,0);
    assert(musashi_cd_owned_advance(c,second)); /* unread response also holds */
    assert(state(c).due == second + 256 && state(c).interrupt == 0);
    assert(r(c,1) == 2);
    assert(musashi_cd_owned_advance(c,second + 256)); assert(state(c).interrupt == 2);
    assert(musashi_cd_owned_close(c));
}
static void matrix(const MusashiDiscMedia *m) {
    Hardware h = {1,1,1,0,0,0}; MusashiCdOwned *c = open_cd(m,&h,0);
    int16_t in[4] = {1000,-2000,-32768,32767}, out[4];
    assert(musashi_cd_owned_mix_pcm(c,in,out,2)); assert(!memcmp(in,out,sizeof in));
    w(c,0,2); w(c,2,0); w(c,3,128); w(c,0,3); w(c,1,0); w(c,2,128);
    assert(musashi_cd_owned_mix_pcm(c,in,out,2)); assert(!memcmp(in,out,sizeof in));
    w(c,3,0x20); assert(musashi_cd_owned_mix_pcm(c,in,out,2));
    assert(out[0] == -2000 && out[1] == 1000 && out[2] == 32767 && out[3] == -32768);
    w(c,0,2); w(c,2,255); w(c,3,255); w(c,0,3); w(c,1,255); w(c,2,255); w(c,3,0x20);
    in[0]=30000; in[1]=30000; in[2]=-30000; in[3]=-30000;
    assert(musashi_cd_owned_mix_pcm(c,in,in,2));
    assert(in[0]==32767 && in[1]==32767 && in[2]==-32768 && in[3]==-32768);
    w(c,0,2); w(c,2,1); w(c,3,0); w(c,0,3); w(c,1,1); w(c,2,0); w(c,3,0x20);
    in[0]=-1; in[1]=1;
    assert(musashi_cd_owned_mix_pcm(c,in,out,1)); assert(out[0]==-1 && out[1]==0);
    assert(musashi_cd_owned_close(c));
}
static void faults(const MusashiDiscMedia *m) {
    Hardware h = {1,1,1,0,0,0}; MusashiCdOwned *c;
    uint8_t out; unsigned i;
    assert(!open_cd(NULL,&h,0));
    h.thread=0; assert(!open_cd(m,&h,0)); h.thread=1;
    h.healthy=0; assert(!open_cd(m,&h,0)); h.healthy=1;
    for (i=0;i<8;++i) {
        c=open_cd(m,&h,10); assert(c); out=0xaa;
        switch(i) {
        case 0: assert(!musashi_cd_owned_read8(c,0x1f801802,&out)); break;
        case 1: w(c,2,1); /* A status command cannot consume a track parameter. */
                assert(!musashi_cd_owned_write8(c,0x1f801801,1)); break;
        case 2: assert(!musashi_cd_owned_write8(c,0x1f801803,0x80)); break;
        case 3: assert(!musashi_cd_owned_write8(c,0x1f801801,3)); break;
        case 4: assert(!musashi_cd_owned_advance(c,9)); break;
        case 5: h.thread=2; assert(!musashi_cd_owned_read8(c,0x1f801800,&out));
                assert(!musashi_cd_owned_close(c)); h.thread=1; break;
        case 6: h.healthy=0; assert(!musashi_cd_owned_advance(c,11)); h.healthy=1; break;
        case 7: assert(!musashi_cd_owned_read8(c,0x1f801801,&out)); break;
        }
        assert(out==0xaa && state(c).fault != MUSASHI_CD_OWNED_OK);
        assert(!musashi_cd_owned_read8(c,0x1f801800,&out) && out==0xaa);
        assert(musashi_cd_owned_close(c));
    }
    c=open_cd(m,&h,UINT64_MAX-0x7ff); assert(c);
    assert(!musashi_cd_owned_write8(c,0x1f801801,1));
    assert(state(c).fault==MUSASHI_CD_OWNED_OVERFLOW); assert(musashi_cd_owned_close(c));
    c=open_cd(m,&h,UINT64_MAX-0x800); assert(c); w(c,1,0x0a);
    assert(!musashi_cd_owned_advance(c,UINT64_MAX));
    assert(state(c).fault==MUSASHI_CD_OWNED_OVERFLOW); assert(musashi_cd_owned_close(c));
    c=open_cd(m,&h,0); w(c,1,1); h.irq_ok=0;
    assert(!musashi_cd_owned_advance(c,0x800));
    assert(state(c).fault==MUSASHI_CD_OWNED_BACKEND_LOST); h.irq_ok=1;
    assert(musashi_cd_owned_close(c));
}
static void edge_cases(const MusashiDiscMedia *m) {
    Hardware h = {1,1,1,0,0,0};
    MusashiCdOwned *c;
    MusashiCdOwnedState before, after;
    int16_t pcm[8] = {1,2,3,4,5,6,7,8}, copy[8];
    MusashiCdController legacy;
    uint8_t byte;
    uint64_t late = UINT64_MAX - 1024;
    /* Legacy refusal does not poison the diagnostic, and legacy late Reset
     * remains relative to the service time, independently of the owned API. */
    musashi_cd_controller_init(&legacy,2);
    assert(!musashi_cd_controller_write8(&legacy,0x1f801803,0));
    assert(musashi_cd_controller_write8(&legacy,0x1f801801,0x0a));
    assert(musashi_cd_controller_advance(&legacy,0x1000));
    assert(legacy.due==0x1000+4100000);
    assert(musashi_cd_controller_read8(&legacy,0x1f801801,&byte) && byte==2);
    c=open_cd(m,&h,0); w(c,1,0x0a);
    assert(musashi_cd_owned_advance(c,late));
    after=state(c);
    assert(after.interrupt==3 && after.response_count==1 && after.phase==2);
    assert(after.due>late && after.due-late<=256);
    assert(after.cycle==late);
    assert(!musashi_cd_owned_advance(c,UINT64_MAX));
    assert(state(c).fault==MUSASHI_CD_OWNED_OVERFLOW);
    assert(musashi_cd_owned_close(c));
    c=open_cd(m,&h,0); w(c,1,1); reenter_irq=c;
    assert(!musashi_cd_owned_advance(c,0x800)); reenter_irq=NULL;
    assert(state(c).fault==MUSASHI_CD_OWNED_REENTRY);
    assert(musashi_cd_owned_close(c));
    c=open_cd(m,&h,100); before=state(c);
    assert(musashi_cd_owned_advance(c,100));
    assert(r(c,0)==0x18); after=state(c);
    assert(after.cycle==before.cycle && after.due==before.due);
    memcpy(copy,pcm,sizeof pcm);
    assert(!musashi_cd_owned_mix_pcm(c,pcm,pcm+1,2));
    assert(!memcmp(copy,pcm,sizeof pcm)); assert(musashi_cd_owned_close(c));
    c=open_cd(m,&h,0);
    assert(!musashi_cd_owned_mix_pcm(c,pcm,copy,SIZE_MAX));
    assert(!memcmp(copy,pcm,sizeof pcm)); assert(musashi_cd_owned_close(c));
    c=open_cd(m,&h,0); w(c,1,1); before=state(c);
    assert(!musashi_cd_owned_write8(c,0x1f801801,0x0a));
    after=state(c); assert(after.command==before.command && after.due==before.due);
    assert(musashi_cd_owned_close(c));
    assert(musashi_cd_owned_close(NULL));
    c=open_cd(m,&h,0); reenter_identity=c;
    memset(&after,0xa5,sizeof after); before=after;
    assert(!musashi_cd_owned_get_state(c,&after));
    assert(!memcmp(&before,&after,sizeof after));
    assert(!musashi_cd_owned_close(c)); /* faulted but reentered close refuses */
    reenter_identity=NULL;
    assert(state(c).fault==MUSASHI_CD_OWNED_REENTRY);
    assert(musashi_cd_owned_close(c));
}
int main(int argc, char **argv) {
    MusashiDiscMedia *m;
    assert(argc==3);
    m=musashi_disc_media_open_pinned(argv[1],argv[2]);
    assert(m); /* Present wrong media FAILS; it never becomes a skip. */
    sequence(m); holds(m); matrix(m); faults(m); edge_cases(m);
    musashi_disc_media_close(m);
    puts("owned CD: pinned mount, phase/IRQ/FIFO/matrix/refusal PASS");
    return 0;
}
