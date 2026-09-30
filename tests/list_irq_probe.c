/* LIST IRQ fixture: test-owned SDK request metadata and scheduling, actual
 * pinned CD announcements/FIFO, DMA3 copies and source callback execution.
 * No mainline loader completion or native cold-boot proof is claimed. */
#define main pvd_fixture_main
#include "pvd_irq_probe.c"
#undef main

#define DEST 0x80180000u
static int reject_400, corrupt_list_ra;
static int list_clock(void *p,uint32_t cost) {
    Fixture *f=p; MusashiCdDma3State d=dmastate(f);
    if(reject_400 && (d.chcr&0x01000000u) && (d.bcr&0xffff)==400) f->reject_dma=1;
    if(corrupt_list_ra && f->dma_count==2 && !f->injected) {
        f->injected=1; put(f,0x8006cabc,0x80043398); /* LIST SP=CA8C, savedRA+30 */
    }
    return clock_step(p,cost);
}
static void list_init(Fixture *f,const MusashiDiscMedia *media,const uint8_t *exe,size_t size) {
    const uint8_t mode=0xa0, location[]={0,5,2}; unsigned before;
    init(f,media,exe,size); reject_400=corrupt_list_ra=0; f->clock.advance=list_clock;
    command(f,0x0e,&mode,1); command(f,2,location,3);
    before=f->service20; command(f,0x15,NULL,0);
    assert(cdstate(f).seeking && cdstate(f).phase==2 && f->service20==before);
    assert(advance(f,cdstate(f).due)); assert(dispatch(f));
    assert(!cdstate(f).seeking && cdstate(f).seek_completions==1 &&
           cdstate(f).seek_sector_reads==1 && cdstate(f).position_lba==227);
    assert(f->service20==before+1);
    /* Corresponds to source state5's request publication, not captured RAM. */
    put(f,0x800ae6f8,3); put(f,0x800ae748,912); put(f,0x800ae728,DEST);
    put(f,0x800ae798,1); put(f,0x800ae74c,0); put(f,0x800ae7ac,227);
    put(f,0x8006cc88,0x8001a338);
    memset(f->ram->bytes+0x180000,0xa5,4096);
    put(f,DEST-4,0x13572468); put(f,DEST+4096,0x24681357);
    command(f,6,NULL,0); assert(advance(f,cdstate(f).sector_due));
    assert(cdstate(f).announced_valid && cdstate(f).announced_lba==227 &&
           !cdstate(f).data_requested && !dmastate(f).bytes);
}
static void next(Fixture *f,unsigned lba) {
    MusashiCdOwnedState before=cdstate(f),after;
    assert(advance(f,before.sector_due)); after=cdstate(f);
    assert(after.announced_lba==lba && after.announced_valid);
    /* New INT1 announcement must not destroy the still-requested old view. */
    assert(after.fifo_lba==before.fifo_lba && after.fifo_cursor==before.fifo_cursor);
    assert(after.requested_sequence==before.requested_sequence);
    assert(after.data_bytes_read==before.data_bytes_read);
}
static void compare_payload(Fixture *f,const MusashiDiscMedia *media) {
    uint8_t a[2352],b[2352];
    assert(musashi_disc_media_read_sector(media,227,a,sizeof(a)));
    assert(musashi_disc_media_read_sector(media,228,b,sizeof(b)));
    assert(!memcmp(f->ram->bytes+0x180000,a+24,2048));
    assert(!memcmp(f->ram->bytes+0x180800,b+24,1600));
    for(unsigned i=3648;i<4096;++i) assert(f->ram->bytes[0x180000+i]==0xa5);
    assert(get(f,DEST-4)==0x13572468 && get(f,DEST+4096)==0x24681357);
}
static int foreign_dispatch(void *p) {
    Fixture *f=p; return musashi_psycross_irq_scheduler_dispatch_pending(&f->runtime.scheduler,&f->continuation);
}
static void success_list(const MusashiDiscMedia *media,const uint8_t *exe,size_t size) {
    Fixture f; MusashiCdOwnedState s;
    list_init(&f,media,exe,size);
    { int result=-1; SDL_Thread *thread=SDL_CreateThread(foreign_dispatch,"foreign-list",&f);
      assert(thread); SDL_WaitThread(thread,&result); assert(result==0);
      assert(!f.runtime.scheduler.faulted && !f.runtime.cd_frame.faulted && !dmastate(&f).bytes); }
    assert(dispatch(&f));
    assert(get(&f,0x800ae728)==DEST+2048 && get(&f,0x800ae748)==400);
    assert(get(&f,0x800ae74c)==0 && get(&f,0x800ae6f8)==3 && get(&f,0x800ae7ac)==228);
    assert(f.dma_count==2 && f.dma_words[0]==3 && f.dma_words[1]==512);
    assert(dmastate(&f).bytes==2060 && cdstate(&f).fifo_cursor==2060);
    next(&f,228); assert(dispatch(&f)); compare_payload(&f,media);
    assert(f.dma_count==4 && f.dma_words[2]==3 && f.dma_words[3]==400);
    assert(dmastate(&f).bytes==3672 && cdstate(&f).fifo_cursor==1612);
    assert(get(&f,0x800ae74c)==1 && get(&f,0x800ae6f8)==2 && get(&f,0x800ae7ac)==229);
    /* Final short-copy path does not decrement remaining or advance dest. */
    assert(get(&f,0x800ae748)==400 && get(&f,0x800ae728)==DEST+2048);
    assert(f.service40==0 && !consume(&f,f.event40));
    next(&f,229); assert(dispatch(&f)); compare_payload(&f,media);
    assert(f.dma_count==5 && f.dma_words[4]==3 && dmastate(&f).bytes==3684);
    assert(cdstate(&f).fifo_cursor==12 && get(&f,0x800ae7ac)==230 && get(&f,0x800ae74c)==1);
    assert(f.service40==0); /* trailing mode2 is source header-only, not completion event */
    put(&f,0x8006cc88,0x800433c0); /* explicit mainline callback-restoration boundary */
    /* Source44828 publishes the intended Pause command before its MMIO
     * write. A trailing ReadN INT1 may arrive in that mainline window. */
    *musashi_boot_ram_span(f.ram,0x8006cca5,1)=9;
    next(&f,230); assert(dispatch(&f));
    assert(f.service40==1 && consume(&f,f.event40)==1 && f.dma_count==5);
    command(&f,9,NULL,0); s=cdstate(&f); assert(s.phase==2);
    assert(advance(&f,s.due)); assert(dispatch(&f));
    assert(!cdstate(&f).reading && !cdstate(&f).interrupt && !cdstate(&f).fault);
    compare_payload(&f,media);
    assert(get(&f,0x8006c9b8)==0xa5a5a5a5 && get(&f,0x8006caec)==0x5a5a5a5a);
    finish(&f);
}
static void wrong_header(const MusashiDiscMedia *media,const uint8_t *exe,size_t size) {
    Fixture f; list_init(&f,media,exe,size); put(&f,0x800ae7ac,999);
    assert(dispatch(&f)); assert(get(&f,0x800ae74c)==2 && get(&f,0x800ae7ac)==999);
    assert(f.dma_count==1 && f.dma_words[0]==3 && dmastate(&f).bytes==12);
    assert(get(&f,DEST)==0xa5a5a5a5 && get(&f,0x800ae748)==912 && !f.service40);
    finish(&f);
}
static void no_final_flag(const MusashiDiscMedia *media,const uint8_t *exe,size_t size) {
    Fixture f; list_init(&f,media,exe,size); assert(dispatch(&f)); next(&f,228);
    put(&f,0x800ae798,0); assert(dispatch(&f)); compare_payload(&f,media);
    assert(get(&f,0x800ae6f8)==0 && !get(&f,0x800ae74c)); /* source did not complete */
    finish(&f);
}
static void failures(const MusashiDiscMedia *media,const uint8_t *exe,size_t size) {
    for(unsigned kind=0;kind<7;++kind) {
        Fixture f; list_init(&f,media,exe,size);
        switch(kind) {
        case 0: put(&f,0x80072aa0,0x8001a958); break; /* mode3 aliases another mapped body */
        case 1: put(&f,0x800ae728,0x801ffffc); break;
        case 2: put(&f,0x800ae748,0); break;
        case 3: put(&f,0x800ae6f8,8); break; /* mode0 PAC is now supported; 8 is outside the table */
        case 4: corrupt_list_ra=1; break;
        case 5: f.reenter=1; break;
        case 6: assert(dispatch(&f)); next(&f,228); reject_400=1; break;
        }
        assert(!dispatch(&f));
        assert(f.runtime.cd_frame.faulted && f.runtime.scheduler.faulted && !f.runtime.cd_frame.busy);
        assert(f.runtime.scheduler.active_continuation == &f.continuation && !f.runtime.scheduler.delivering);
        if(kind==0) {
            assert(f.runtime.cd_stop.call_address==0x8001a3fc && get(&f,0x800ae7ac)==228);
            assert(dmastate(&f).bytes==12 && !get(&f,0x800ae74c));
        }
        if(kind==1 || kind==2) {
            assert(dmastate(&f).bytes==12 && get(&f,DEST)==0xa5a5a5a5 && !dmastate(&f).fault);
            assert(f.runtime.cd_stop.call_address==(kind==1?0x8001a820u:0x8001a80cu));
        }
        if(kind==3) assert(dmastate(&f).bytes==0 && f.runtime.cd_stop.call_address==0x80045300);
        if(kind==4) assert(f.runtime.cd_stop.call_address==0x8001a9d0 && dmastate(&f).bytes==2060);
        if(kind==5) assert(f.injected && !dmastate(&f).bytes);
        if(kind==6) {
            assert(dmastate(&f).fault && dmastate(&f).bytes==2072 && cdstate(&f).fifo_cursor==12);
            assert(get(&f,DEST+2048)==0xa5a5a5a5 && !get(&f,0x800ae74c));
            assert(get(&f,0x800ae6f8)==3 && get(&f,0x800ae748)==400);
        }
        finish(&f);
    }
}
static void seek_policy(const MusashiDiscMedia *media,const uint8_t *exe,size_t size) {
    for(unsigned phase=0;phase<2;++phase) {
        Fixture f; const uint8_t mode=0xa0,location[]={0,5,2};
        init(&f,media,exe,size); command(&f,0x0e,&mode,1); command(&f,2,location,3);
        *musashi_boot_ram_span(f.ram,0x8006cca5,1)=0x15;
        assert(write_byte(&f,0x1f801800,0) && write_byte(&f,0x1f801801,0x15));
        assert(advance(&f,cdstate(&f).due));
        if(phase==0) put(&f,0x8006cea0,0); /* live CE4C[15] before ACK */
        else {
            assert(dispatch(&f)); put(&f,0x8006cda0,0); /* live CD4C[15] before INT2 */
            assert(advance(&f,cdstate(&f).due));
        }
        assert(!dispatch(&f)); assert(f.runtime.cd_stop.call_address==0x80043e00);
        assert(cdstate(&f).seek_starts==1 && cdstate(&f).seek_completions==phase);
        assert(!dmastate(&f).bytes && !cdstate(&f).fault); finish(&f);
    }
}
static void title_pac_header(const MusashiDiscMedia *media,const uint8_t *exe,size_t size,
                             const char *reference) {
    Fixture f;
    const uint8_t mode=0xa0, location[]={0x01,0x24,0x12}; /* LBA 6162 */
    init(&f,media,exe,size);
    command(&f,0x0e,&mode,1); command(&f,2,location,3);
    command(&f,0x15,NULL,0);
    assert(advance(&f,cdstate(&f).due)); assert(dispatch(&f));
    assert(cdstate(&f).position_lba==6162);
    /* Test-owned request metadata; the PAC header itself comes from disc. */
    put(&f,0x800ae6f8,0); put(&f,0x800ae728,DEST); put(&f,0x800ae72c,DEST);
    put(&f,0x800ae798,1); put(&f,0x800ae7ac,6162);
    put(&f,0x8006cc88,0x8001a338);
    command(&f,6,NULL,0); assert(advance(&f,cdstate(&f).sector_due));
    assert(dispatch(&f));
    assert(get(&f,0x80079a70)==0x00434150); /* actual PAC magic */
    assert(get(&f,0x800ae6f8)==5);
    assert(get(&f,0x800c7d24)==1 && get(&f,0x800747ac)==DEST);
    assert(get(&f,0x800ae728)==DEST);
    assert(f.runtime.cd_frame.faulted==0);
    {
        FILE *file=fopen(reference,"rb"); uint32_t length; uint8_t *expected;
        assert(file && fread(&length,4,1,file)==1 && length>0 && length<0x20000);
        expected=malloc(length); assert(expected);
        assert(fread(expected,1,length,file)==length && fgetc(file)==EOF);
        assert(!fclose(file));
        next(&f,6163); assert(dispatch(&f));
        assert(get(&f,0x800747ac)==DEST+length);
        assert(!memcmp(f.ram->bytes+0x180000,expected,length));
        free(expected);
    }
    finish(&f);
}

int main(int argc,char **argv) {
    MusashiDiscMedia *media; FILE *file; uint8_t *exe; size_t size=0x65000;
    assert(argc==5); assert(SDL_Init(0)==0); g_intrMutex=SDL_CreateMutex(); assert(g_intrMutex);
    media=musashi_disc_media_open_pinned(argv[1],argv[2]); assert(media);
    exe=malloc(size); assert(exe); file=fopen(argv[3],"rb"); assert(file);
    assert(fread(exe,1,size,file)==size && fgetc(file)==EOF); assert(!fclose(file));
    success_list(media,exe,size); wrong_header(media,exe,size); no_final_flag(media,exe,size);
    failures(media,exe,size); seek_policy(media,exe,size);
    title_pac_header(media,exe,size,argv[4]);
    free(exe); musashi_disc_media_close(media); SDL_DestroyMutex(g_intrMutex); SDL_Quit();
    puts("LIST_IRQ_PASS fixture_only=1"); return 0;
}
