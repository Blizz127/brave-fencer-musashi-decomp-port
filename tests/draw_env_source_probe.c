/* Synthetic dimensions/CPU/RAM only. Actual source children run to the caller
 * return; no GPU submission, presentation, fake service or native proof. */
#include "../pc_port/mips_formatter.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static MusashiBootMemory memory,expected,saved;
static FormatterCpu cpu;
static unsigned cycles,calls;
static uint32_t refuse_pc;
static int advance(void *p,uint32_t cost){uint32_t i;(void)p;assert(formatter_fetch(NULL,&cpu,&i));assert(cost==2u+((i>>26)>=32u&&(i>>26)<=43u));calls++;if(cpu.pc==refuse_pc)return 0;cycles+=cost;return 1;}
static MusashiExecutionClock clock_device={NULL,advance};
static void p16(MusashiBootMemory *m,unsigned a,unsigned v){m->bytes[a]=(uint8_t)v;m->bytes[a+1]=(uint8_t)(v>>8);}
static void p32(MusashiBootMemory *m,unsigned a,uint32_t v){for(unsigned n=0;n<4;n++)m->bytes[a+n]=(uint8_t)(v>>(n*8));}
static void setup(unsigned width,unsigned height,unsigned pal){
 memset(&memory,0xa5,sizeof(memory));memset(&cpu,0,sizeof(cpu));
 p16(&memory,0xaf7bc,width);p16(&memory,0xaf7be,height);p32(&memory,0x6cbe8,pal);
 cpu.pc=0x800147b8;cpu.npc=cpu.pc+4;cpu.r[29]=0x801ff000;cpu.r[30]=0x801ff080;cpu.r[31]=0x800146a0;
 for(unsigned n=16;n<=23;n++)cpu.r[n]=0x11220000+n;
 cpu.hi=0x12345678;cpu.lo=0x87654321;cpu.clock=&clock_device;
 expected=memory;cycles=calls=refuse_pc=0;
}
static int until(uint32_t target){for(unsigned n=0;n<700;n++){if(cpu.pc==target)return 1;if(!formatter_step(&memory,&cpu)){fprintf(stderr,"DRAW_ENV_STOP pc=%08x\n",cpu.pc);return 0;}}return 0;}
static void draw(unsigned base,unsigned width,unsigned height,unsigned y,unsigned offset_y,unsigned pal){
 p16(&expected,base,0);p16(&expected,base+2,y);p16(&expected,base+4,width);p16(&expected,base+6,height-20);
 p16(&expected,base+8,width/2);p16(&expected,base+10,offset_y);
 p16(&expected,base+12,0);p16(&expected,base+14,0);
 /* Texture-window w/h are256 at+10/+12; tpage+14 remains10. */
 p16(&expected,base+16,256);p16(&expected,base+18,256);p16(&expected,base+20,10);
 expected.bytes[base+22]=0; /* Parent disables dither, not the dfe byte. */
 expected.bytes[base+23]=(height-20)<(pal?289u:257u);
 expected.bytes[base+24]=1;
 memset(expected.bytes+base+25,0,3);
 /* +1c..+5b are intentionally untouched, including command packet storage. */
}
static void disp(unsigned base,unsigned width,unsigned height,unsigned y){
 p16(&expected,base,0);p16(&expected,base+2,y);p16(&expected,base+4,width);p16(&expected,base+6,height);
 memset(expected.bytes+base+8,0,12);
}
static void oracle(unsigned width,unsigned height,unsigned pal){
 unsigned y=height==480?0:height;
 draw(0xaf668,width,height,10,height/2,pal);draw(0xaf6c4,width,height,y+10,height==240?360:240,pal);
 disp(0xaf77c,width,height,0);disp(0xaf790,width,height,y);
 memset(expected.bytes+0xaf7c2,0,3);
 /* Exact retained source stack writes, including reuse by14960. */
 p32(&expected,0x1fefc0,y);p32(&expected,0x1fefc4,0x800af630);
 p32(&expected,0x1fefc8,0x11220012);p32(&expected,0x1fefcc,0x11220013);
 p32(&expected,0x1fefd0,0x80014910);p32(&expected,0x1fefd4,0x80014854);
 p32(&expected,0x1fefe8,height);p32(&expected,0x1feff0,0x11220010);
 p32(&expected,0x1feff4,0x11220011);p32(&expected,0x1feff8,0x800146a0);
}
static void all_environments(void){
 const unsigned heights[]={240,480,277,309};
 for(unsigned pal=0;pal<2;pal++)for(unsigned k=0;k<4;k++){
  unsigned height=heights[k];setup(320,height,pal);oracle(320,height,pal);
  assert(until(0x80058a4c));assert(cpu.r[29]==0x801fefd8&&cpu.r[31]==0x80014808);
  assert(cpu.r[4]==0x800af668&&cpu.r[5]==0&&cpu.r[6]==10&&cpu.r[7]==320);
  assert(until(0x80043078));assert(cpu.r[29]==0x801fefb0&&cpu.r[31]==0x80058a80);
  assert(until(0x80058a80));assert(cpu.r[2]==pal);
  assert(until(0x800146a0));
  if(memcmp(&memory,&expected,sizeof(memory))){for(unsigned n=0;n<sizeof(memory.bytes);n++)if(memory.bytes[n]!=expected.bytes[n]){fprintf(stderr,"RAM_DIFF %06x actual=%02x expected=%02x\n",n,memory.bytes[n],expected.bytes[n]);break;}}
  assert(!memcmp(&memory,&expected,sizeof(memory)));
  assert(cpu.r[29]==0x801ff000&&cpu.r[30]==0x801ff080&&cpu.r[31]==0x800146a0);
  for(unsigned n=16;n<=23;n++)assert(cpu.r[n]==0x11220000+n);
  assert(cpu.hi==0x12345678&&cpu.lo==0x87654321&&cpu.r[2]==0&&cpu.r[4]==2);
  assert(!cpu.delay_slot&&!cpu.branch_pc&&cpu.npc==0x800146a4&&cycles>calls);
 }
}
static void refused(unsigned charge){FormatterCpu old=cpu;saved=memory;unsigned c=cycles,n=calls;assert(!formatter_step(&memory,&cpu));assert(!memcmp(&old,&cpu,sizeof(cpu))&&!memcmp(&memory,&saved,sizeof(memory)));assert(cycles==c+charge&&calls==n+1);}
static void negatives(void){
 setup(320,240,0);assert(until(0x80058a84));refuse_pc=cpu.pc;refused(0);
 setup(320,240,0);assert(until(0x80058a84));cpu.r[17]=0x801fffff;refused(3);
 setup(320,240,0);cpu.r[29]=0x1f801028;assert(formatter_step(&memory,&cpu));assert(cpu.pc==0x800147bc&&cpu.r[29]==0x1f801000);refused(3);
 /* Refused fifth-argument delay retains the link/branch prefix already
  * executed by this interpreter; no invented rollback or completed call. */
 setup(320,240,0);assert(until(0x80014804));refuse_pc=cpu.pc;refused(0);
 setup(320,240,0);assert(until(0x80058b3c));refuse_pc=cpu.pc;refused(0);
}
int main(void){all_environments();negatives();puts("DRAW_ENV_SOURCE_PASS fixture_only=1 gpu_submission=NOT_RUN");return 0;}
