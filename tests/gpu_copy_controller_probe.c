/* Reuse the existing checked backend fixture and its regression cases. */
#define main fill_regressions
#include "gpu_fill_controller_probe.c"
#undef main
static int copy_read(void *p, uint16_t x, uint16_t y, uint16_t *v) {
    FillFixture *f=p; *v=f->vram[(unsigned)y*1024u+x]; return 1;
}
static int fail_after=-1, reenter=0;
static int copy_write(void *p, uint16_t x, uint16_t y, uint16_t v) {
    FillFixture *f=p;
    if (fail_after==0) return 0;
    if (fail_after>0) --fail_after;
    if (reenter) (void)write_gp0(f,0);
    f->vram[(unsigned)y*1024u+x]=v; return 1;
}
static int copy_init(FillFixture *f) {
    if (!fixture_init(f,1)) return 0;
    f->controller.backend.read_vram=copy_read;
    f->controller.backend.store_vram=copy_write;
    return 1;
}
static int packet(FillFixture *f,uint32_t op,uint32_t src,uint32_t dst,uint32_t size) {
    return write_gp0(f,op)&&write_gp0(f,src)&&write_gp0(f,dst)&&write_gp0(f,size);
}
static int copies(void) {
    static FillFixture f;
    unsigned alias;
    for(alias=0x80;alias<=0x9f;alias++) {
        CHECK(copy_init(&f)); f.vram[0]=0x1234;
        CHECK(packet(&f,(alias<<24)|0xabcdefu,0,10,0x10001));
        CHECK(f.vram[10]==0x1234); CHECK(f.controller.accepted_gp0_words==4);
    }
    CHECK(copy_init(&f)); f.vram[511u*1024u+1023u]=1;f.vram[511u*1024u]=2;f.vram[1023]=3;f.vram[0]=4;
    CHECK(packet(&f,0x80000000,0xffff03ff,0x000a0014,0x00020002));
    CHECK(f.vram[10u*1024u+20u]==1&&f.vram[10u*1024u+21u]==2);
    CHECK(f.vram[11u*1024u+20u]==3&&f.vram[11u*1024u+21u]==4);
    CHECK(copy_init(&f));f.vram[0]=0x123;f.vram[10]=0x8abc;
    CHECK(write_gp0(&f,0xe6000003));
    CHECK(packet(&f,0x80000000,0,10,0x10002));
    CHECK(f.vram[10]==0x8abc&&f.vram[11]==0x8000);
    CHECK(copy_init(&f));f.vram[0]=0x321;
    CHECK(write_gp0(&f,0x80000000));CHECK(write_gp0(&f,0));CHECK(write_gp0(&f,10));
    CHECK(f.vram[10]==0);CHECK(write_gp1(&f,0x01000000));CHECK(write_gp0(&f,0));CHECK(f.vram[10]==0);
    CHECK(copy_init(&f));f.controller.backend.read_vram=NULL;
    CHECK(!write_gp0(&f,0x80000000));CHECK(f.controller.accepted_gp0_words==0);
    CHECK(copy_init(&f));f.vram[0]=1;f.vram[1]=2;f.vram[2]=3;
    CHECK(packet(&f,0x80000000,0,1,0x10003));
    CHECK(f.vram[1]==1&&f.vram[2]==2&&f.vram[3]==3);
    CHECK(copy_init(&f));f.vram[0]=7;f.vram[1024]=8;f.vram[2048]=9;
    CHECK(packet(&f,0x80000000,0,0x10000,0x30001));
    CHECK(f.vram[1024]==7&&f.vram[2048]==7&&f.vram[3072]==7);
    CHECK(copy_init(&f));f.vram[0]=0x1234;f.vram[1023]=0x4567;
    CHECK(packet(&f,0x80000000,0,0x10000,0x10000));
    CHECK(f.vram[1024]==0x1234&&f.vram[2047]==0x4567);
    CHECK(copy_init(&f));f.vram[0]=1;f.vram[1]=2;fail_after=1;
    CHECK(!packet(&f,0x80000000,0,10,0x10002));
    CHECK(f.controller.faulted&&f.controller.copied_pixels==1);
    CHECK(f.controller.completed_copies==0&&f.controller.accepted_gp0_words==3);
    CHECK(!write_gp0(&f,0x10002));fail_after=-1;
    CHECK(copy_init(&f));reenter=1;
    CHECK(!packet(&f,0x80000000,0,10,0x10001));
    CHECK(f.controller.faulted&&f.controller.copied_pixels==1);reenter=0;
    CHECK(copy_init(&f));f.vram[0]=0x456;
    CHECK(packet(&f,0x80000000,0,10,1));
    CHECK(f.vram[511u*1024u+10u]==0&&f.controller.copied_pixels==512);
    CHECK(copy_init(&f));f.controller.backend.store_vram=NULL;
    CHECK(!write_gp0(&f,0x80000000));
    CHECK(copy_init(&f));f.vram[0]=5;
    CHECK(write_gp0(&f,0x80000000)&&write_gp0(&f,0)&&write_gp0(&f,10));
    f.ready_value=0;CHECK(!write_gp0(&f,0x10001));CHECK(f.vram[10]==0);
    f.ready_value=1;CHECK(write_gp0(&f,0x10001));CHECK(f.vram[10]==5);
    return 1;
}
int main(void) { if(fill_regressions()!=0||!copies())return 1;puts("GPU_COPY_CONTROLLER_PASS");return 0; }
