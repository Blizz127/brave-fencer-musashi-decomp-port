/* Contract unit only: no device calls or runtime completion are claimed. */
#define main inherited_gte_main
#include "gte_init_source_probe.c"
#undef main
static int unread(void *p,uint32_t a,uint32_t *v){(void)p;(void)a;(void)v;abort();}
static int unwritten(void *p,uint32_t a,uint32_t v){(void)p;(void)a;(void)v;abort();}
static int undelivered(void *p,uint32_t a,uint32_t v){(void)p;(void)a;(void)v;abort();}
int main(int argc,char **argv) {
    static MusashiBootMemory memory;
    FormatterCpu cpu={0}; CdIrqReturn ret[3]={{0}};
    MusashiCdRegisterDevice cd={0}; MusashiCdIrqServices svc={0};
    const uint32_t saved=0x801fff00;
    uint32_t slots[]={0x8006cf5c,0x8006cf80,0x8006cf84,0x8006cf88,0x8006cf8c,0x8006cf90};
    uint32_t values[]={0x1f801020,0x1f801018,0x1f8010f0,0x1f8010b0,0x1f8010b4,0x1f8010b8};
    unsigned i; (void)argc;(void)argv;
    cd.read32=unread;cd.write32=unwritten;cpu.cd_register_device=&cd;svc.deliver_event=undelivered;
    for(i=0;i<6;i++) assert(musashi_boot_write32(&memory,slots[i],values[i]));
    assert(musashi_boot_write32(&memory,0x8006cc88,0x800377d8));
    memory.bytes[0x6cf65]=1;
    cpu.pc=0x800377d8;cpu.r[29]=saved-0x50;cpu.r[4]=1;cpu.r[5]=0x800763e0;
#define CHECK(d,c,w) cd_irq_call(&memory,&cpu,&svc,ret,d,saved,c,w)
    assert(CHECK(0,0x80045300,0x0040f809));
    cpu.r[29]-=4;assert(!CHECK(0,0x80045300,0x0040f809));cpu.r[29]+=4;
    cpu.r[4]=2;assert(!CHECK(0,0x80045300,0x0040f809));cpu.r[4]=1;
    memory.bytes[0x6cf65]=2;assert(!CHECK(0,0x80045300,0x0040f809));memory.bytes[0x6cf65]=1;
    assert(musashi_boot_write32(&memory,0x8006cc88,0x800377dc));assert(!CHECK(0,0x80045300,0x0040f809));
    assert(musashi_boot_write32(&memory,0x8006cc88,0x800377d8));
    ret[0].target=0x800377d8;ret[0].pc=0x80045308;ret[0].sp=saved-0x50;
    cpu.pc=0x80043994;cpu.r[29]=saved-0x70;cpu.r[4]=0x800762a0;cpu.r[5]=3;
    assert(CHECK(1,0x80037800,0x0c010e65));
    assert(!CHECK(2,0x80037800,0x0c010e65));
    ret[0].pc+=4;assert(!CHECK(1,0x80037800,0x0c010e65));ret[0].pc-=4;
    cpu.r[4]+=4;assert(!CHECK(1,0x80037800,0x0c010e65));cpu.r[4]-=4;
    cpu.r[5]=4;assert(!CHECK(1,0x80037800,0x0c010e65));cpu.r[5]=3;
    cpu.pc+=4;assert(!CHECK(1,0x80037800,0x0c010e66));cpu.pc-=4;
    assert(musashi_boot_write32(&memory,0x80076228,0x80100000));
    cpu.r[4]=0x80100000;cpu.r[5]=0x200;
    assert(CHECK(1,0x8003787c,0x0c010e65));
    cpu.r[4]+=4;assert(!CHECK(1,0x8003787c,0x0c010e65));cpu.r[4]-=4;
    cpu.r[5]=0x1ff;assert(!CHECK(1,0x8003787c,0x0c010e65));cpu.r[5]=0x200;
    ret[1].pc=0x80037884;ret[1].a0=cpu.r[4];ret[1].a1=cpu.r[5];
    cpu.pc=0x80045090;cpu.r[29]=saved-0x88;
    assert(CHECK(2,0x8004399c,0x0c011424));
    ret[1].pc+=4;assert(!CHECK(2,0x8004399c,0x0c011424));ret[1].pc-=4;
    cpu.r[5]--;assert(!CHECK(2,0x8004399c,0x0c011424));cpu.r[5]++;
    assert(musashi_boot_write32(&memory,slots[0],values[0]+4));assert(!CHECK(2,0x8004399c,0x0c011424));
    puts("CD_IRQ_BANK_CONTRACT_PASS");return 0;
}
