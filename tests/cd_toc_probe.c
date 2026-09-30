/* Real pinned media, synthetic hardware owner. No native lifecycle claim. */
#define main startup_probe_main
#include "cd_owned_probe.c"
#undef main

static void toc(const MusashiDiscMedia *m) {
    Hardware h = {1,1,1,0,0,0};
    MusashiCdOwned *c = open_cd(m,&h,0);
    /* Independent literal physical MSF: lead-out, INDEX01 tracks1..4.
     * Frames30/0/22/62/28 are intentionally absent from GetTD packets. */
    const unsigned mm[5] = {0x39,0x00,0x34,0x35,0x36};
    const unsigned ss[5] = {0x20,0x02,0x32,0x32,0x14};
    unsigned i, j;
    uint64_t now = 0;
    assert(c);
    for (i=0;i<6;++i) {
        if (i) { w(c,2,i-1); assert(r(c,0)==0x10); }
        w(c,1,i ? 0x14 : 0x13);
        assert(state(c).parameter_count==0 && r(c,0)==0x98);
        assert(musashi_cd_owned_advance(c,now+0x7ff));
        assert(state(c).response_count==0 && h.raises==i);
        now+=0x800;
        assert(musashi_cd_owned_advance(c,now));
        assert(h.raises==i+1 && state(c).response_count==3);
        /* Internal ACK while FIFO remains populated neither consumes bytes
         * nor clears the separate CPU latch. Repeated reads don't add time. */
        w(c,0,1); assert(r(c,3)==0xe3); w(c,3,7);
        assert(h.istat==4 && state(c).response_count==3);
        for (j=0;j<3;++j) {
            unsigned expected = j==0 ? 2 : i ? (j==1 ? mm[i-1] : ss[i-1]) : (j==1 ? 1 : 4);
            assert(r(c,0)==0x39 && r(c,1)==expected);
            assert(state(c).cycle==now && state(c).response_count==2-j);
        }
        assert(r(c,0)==0x19 && !state(c).phase && !state(c).busy);
        h.istat=0; w(c,0,0);
    }
    /* Exactly three bytes; no stale fourth result byte is exposed. */
    { uint8_t out=0xa5; assert(!musashi_cd_owned_read8(c,0x1f801801,&out)); assert(out==0xa5); }
    assert(musashi_cd_owned_close(c));
}

static void parameters(const MusashiDiscMedia *m) {
    Hardware h = {1,1,1,0,0,0};
    MusashiCdOwned *c = open_cd(m,&h,0);
    unsigned i;
    for(i=0;i<16;++i) w(c,2,i);
    assert(r(c,0)==0 && state(c).parameter_count==16);
    w(c,0,1); w(c,3,0x40); assert(r(c,0)==0x19 && !state(c).parameter_count);
    w(c,0,0); w(c,2,4); w(c,1,0x14);
    w(c,0,1); w(c,2,0); /* masked event retains three real bytes */
    assert(musashi_cd_owned_advance(c,0x800)); assert(!h.raises);
    assert(state(c).response_count==3 && state(c).interrupt==3);
    w(c,2,7); assert(h.raises==1);
    assert(musashi_cd_owned_close(c));
    for(i=0;i<7;++i) {
        MusashiCdOwnedState before, after;
        c=open_cd(m,&h,10); assert(c);
        if(i==0) w(c,2,0x0a); /* invalid BCD */
        if(i==1) w(c,2,5); /* out of range */
        if(i==2) { w(c,2,0); w(c,2,1); } /* excess parameters */
        if(i==4) w(c,2,1); /* GetTN takes none */
        if(i==5) for(unsigned j=0;j<16;++j) w(c,2,j);
        if(i==6) { w(c,1,0x13); assert(musashi_cd_owned_advance(c,10+0x800)); }
        before=state(c);
        if(i==5 || i==6) assert(!musashi_cd_owned_write8(c,0x1f801802,0));
        else assert(!musashi_cd_owned_write8(c,0x1f801801,i==4 ? 0x13 : 0x14));
        after=state(c);
        assert(after.fault==MUSASHI_CD_OWNED_BAD_ACCESS);
        assert(after.command==before.command && after.due==before.due && after.cycle==before.cycle);
        assert(after.parameter_count==before.parameter_count && after.response_count==before.response_count);
        assert(musashi_cd_owned_close(c));
    }
}

int main(int argc, char **argv) {
    MusashiDiscMedia *m;
    assert(argc==3);
    m=musashi_disc_media_open_pinned(argv[1],argv[2]); assert(m);
    toc(m); parameters(m);
    musashi_disc_media_close(m);
    puts("owned CD TOC: pinned INDEX01/lead-out, exact packets, FIFO/IRQ/deadlines/refusals PASS");
    return 0;
}
