#include "musashi_bios_exception.h"
#include <openssl/evp.h>
#include <string.h>

static const uint8_t rom_hash[32] = {
    0x11,0x05,0x2b,0x64,0x99,0xe4,0x66,0xbb,0xf0,0xa7,0x09,0xb1,0xf9,0xcb,0x68,0x34,
    0xa9,0x41,0x8e,0x66,0x68,0x03,0x87,0x91,0x24,0x51,0xe9,0x71,0xcf,0x8a,0x1f,0xef
};
static const uint32_t table_words[7] = {0x1508,0x1b20,0x1420,0x1444,0x1d00,0x1f88,0xc80};
/* No BIOS code is carried here. The handler (C80..CB8) and continuation
 * (CB8..CF0) come from the user's own ROM when one is supplied (its full
 * SHA-256 is pinned above); without a ROM they are zero stand-ins, which is
 * safe because exception entry is serviced natively and never executes
 * them. The game's own libgte PATCHGTE copies its 14-word handler from the
 * EXE over C80; that variant is recognised by its FNV-1a 64 digest. */
#define GTE_PATCH_HANDLER_DIGEST 0x7cecf5a173ee2e66ull
static uint64_t words_digest(const uint8_t *p, unsigned n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned i = 0; i < 4u * n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}
static uint32_t load32(const uint8_t *p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int equal_words(const uint8_t *p,const uint32_t *words,unsigned n) {
    for(unsigned i=0;i<n;i++)if(load32(p+4*i)!=words[i])return 0;
    return 1;
}
static int normalize(uint32_t address,uint32_t *physical) {
    if(address<0x00200000u)*physical=address;
    else if(address>=0x80000000u && address<0x80200000u)*physical=address-0x80000000u;
    else if(address>=0xa0000000u && address<0xa0200000u)*physical=address-0xa0000000u;
    else return 0;
    return 1;
}
int musashi_bios_exception_intersects(uint32_t address,size_t size) {
    uint32_t p;
    if(!size || !normalize(address,&p))return 0;
    /* Saturate instead of wrapping oversized span arithmetic. */
    uint64_t end=size>UINT32_MAX?UINT64_MAX:(uint64_t)p+size;
    return (p<0x690 && end>0x674) || (p<0xcf0 && end>0xc80);
}
static int binding_valid(const MusashiBiosExceptionImage *image) {
    return image->kernel && image->binding.owner==image->kernel &&
        image->kernel->exception_binding==&image->binding && image->kernel->exception_required &&
        image->binding.enter==musashi_bios_exception_enter && image->binding.userdata==image;
}
static int immutable_valid(const MusashiBiosExceptionImage *image) {
    return image->memory && equal_words(image->memory->bytes+0x674,table_words,7) &&
        equal_words(image->memory->bytes+0xcb8,image->seed_continuation,14);
}
static int published_valid(const MusashiBiosExceptionImage *image) {
    if(!image->generation)return 0;
    if(image->variant==MUSASHI_BIOS_EXCEPTION_ORIGINAL)
        return equal_words(image->memory->bytes+0xc80,image->seed_handler,14);
    return image->variant==MUSASHI_BIOS_EXCEPTION_GTE_PATCH &&
        words_digest(image->memory->bytes+0xc80,14)==GTE_PATCH_HANDLER_DIGEST;
}
static int end(MusashiBiosExceptionImage *image,int accepted,int fault) {
    if(fault)image->faulted=1;
    image->busy=0;
    return accepted;
}
static int reserve(MusashiBiosExceptionImage *image,int allow_fault) {
    if(!image)return 0;
    if(image->busy) {image->faulted=1;return 0;}
    if(!image->initialized || (!allow_fault && image->faulted))return 0;
    image->busy=1;
    if(!binding_valid(image))return end(image,0,1);
    return 1;
}
static int ordinary(MusashiBiosExceptionImage *image,MusashiCpuStatusSnapshot *s) {
    if(!reserve(image,0))return 0;
    if(!musashi_cpu_status_snapshot(image->cpu,s) || image->faulted || s->faulted || s->active ||
        image->kernel->dispatching || !immutable_valid(image) || (!image->dirty && !published_valid(image)))
        return end(image,0,1);
    return 1;
}
static int zeroed(const MusashiBiosExceptionImage *image) {
    const uint8_t *p=(const uint8_t *)image;
    for(size_t i=0;i<sizeof(*image);i++)if(p[i])return 0;
    return 1;
}
static void store_words(uint8_t *p,const uint32_t *words,unsigned count) {
    for(unsigned i=0;i<count;i++)for(unsigned j=0;j<4;j++)p[4*i+j]=(uint8_t)(words[i]>>(8*j));
}
static int init_with(MusashiBiosExceptionImage *image,MusashiBootMemory *memory,
    MusashiBiosKernel *kernel,MusashiCpuStatus *cpu,const uint8_t *rom) {
    MusashiCpuStatusSnapshot s;
    if(!image)return 0;
    if(image->busy) {image->faulted=1;return 0;}
    if(!memory || !kernel || !cpu || !zeroed(image) || kernel->dispatching || kernel->exception_binding)return 0;
    image->busy=1;
    if(!musashi_cpu_status_snapshot(cpu,&s) || s.active || s.faulted || image->faulted)
        return end(image,0,image->faulted);
    /* No callback occurs after lease publication or the first RAM write.
     * These small role views are independently checked against selected ROM;
     * they are source-derived bootstrap state, never a captured RAM image. */
    if(!musashi_bios_kernel_install_exception_binding(kernel,&image->binding,musashi_bios_exception_enter,image))
        return end(image,0,0);
    image->memory=memory;image->kernel=kernel;image->cpu=cpu;
    for(unsigned i=0;i<14;i++) {
        image->seed_handler[i]=rom?load32(rom+0x10780+4*i):0;
        image->seed_continuation[i]=rom?load32(rom+0x107b8+4*i):0;
    }
    store_words(memory->bytes+0x674,table_words,7);
    store_words(memory->bytes+0xc80,image->seed_handler,14);
    store_words(memory->bytes+0xcb8,image->seed_continuation,14);
    image->variant=MUSASHI_BIOS_EXCEPTION_ORIGINAL;image->generation=1;image->initialized=1;
    return end(image,1,0);
}
int musashi_bios_exception_init(MusashiBiosExceptionImage *image,MusashiBootMemory *memory,
    MusashiBiosKernel *kernel,MusashiCpuStatus *cpu,const uint8_t *rom,size_t size) {
    unsigned length=0;uint8_t digest[32];
    if(image && image->busy) {image->faulted=1;return 0;}
    if(!rom || size!=524288 || !EVP_Digest(rom,size,digest,&length,EVP_sha256(),NULL) ||
        length!=32 || memcmp(digest,rom_hash,32) || !equal_words(rom+0x10174,table_words,7))return 0;
    return init_with(image,memory,kernel,cpu,rom);
}
int musashi_bios_exception_init_selected(MusashiBiosExceptionImage *image,MusashiBootMemory *memory,
    MusashiBiosKernel *kernel,MusashiCpuStatus *cpu) {
    return init_with(image,memory,kernel,cpu,NULL);
}
int musashi_bios_exception_table(MusashiBiosExceptionImage *image,uint32_t *out) {
    MusashiCpuStatusSnapshot s;
    if(!out || !ordinary(image,&s))return 0;
    *out=0x674;return end(image,1,0);
}
int musashi_bios_exception_read32(MusashiBiosExceptionImage *image,uint32_t address,uint32_t *out) {
    uint32_t p;MusashiCpuStatusSnapshot s;
    if(!out || !ordinary(image,&s))return 0;
    if((address&3u) || !normalize(address,&p) ||
        !((p>=0x674 && p<0x690)||(p>=0xc80 && p<0xcf0)))return end(image,0,1);
    *out=load32(image->memory->bytes+p);return end(image,1,0);
}
int musashi_bios_exception_write32(MusashiBiosExceptionImage *image,uint32_t address,uint32_t value) {
    uint32_t p;MusashiCpuStatusSnapshot s;
    if(!ordinary(image,&s))return 0;
    if((s.sr&0x401u) || (address&3u) || !normalize(address,&p) || p<0xc80 || p>=0xcb8 || image->writes==UINT64_MAX)
        return end(image,0,1);
    uint8_t *bytes=image->memory->bytes+p;
    bytes[0]=(uint8_t)value;bytes[1]=(uint8_t)(value>>8);bytes[2]=(uint8_t)(value>>16);bytes[3]=(uint8_t)(value>>24);
    image->dirty=1;image->written_mask|=1u<<((p-0xc80)/4);image->writes++;
    return end(image,1,0);
}
int musashi_bios_exception_publish(MusashiBiosExceptionImage *image) {
    MusashiCpuStatusSnapshot s;MusashiBiosExceptionVariant variant;
    if(!ordinary(image,&s))return 0;
    if((s.sr&0x401u) || !image->dirty || image->written_mask!=0x3fff || image->generation==UINT64_MAX)
        return end(image,0,1);
    if(words_digest(image->memory->bytes+0xc80,14)==GTE_PATCH_HANDLER_DIGEST)variant=MUSASHI_BIOS_EXCEPTION_GTE_PATCH;
    else if(equal_words(image->memory->bytes+0xc80,image->seed_handler,14))variant=MUSASHI_BIOS_EXCEPTION_ORIGINAL;
    else return end(image,0,1);
    image->variant=variant;image->generation++;image->dirty=0;image->written_mask=0;
    return end(image,1,0);
}
int musashi_bios_exception_enter(void *userdata,const MusashiCpuExceptionToken *token) {
    MusashiBiosExceptionImage *image=userdata;MusashiCpuStatusSnapshot s;
    if(!reserve(image,0))return 0;
    if(!token || !image->kernel->dispatching || image->dirty || !immutable_valid(image) ||
        !published_valid(image) || image->entries==UINT64_MAX ||
        !musashi_cpu_status_inspect(image->cpu,token,&s) || image->faulted || s.faulted ||
        !s.active || s.kernel_entered || !s.identity || s.gpr[0] || s.sequence<=image->last_sequence)
        return end(image,0,1);
    /* begin already saved the real at/v0/v1/ra in this SAME token's GPR frame.
     * Both selected prefixes reach EA0's Cause/EPC read before CF0. SOURCE
     * has real physical provenance; compiled HOST services deliberately do not. */
    if(s.provenance==MUSASHI_CPU_CONTEXT_SOURCE) {
        uint32_t code=s.kind==MUSASHI_CPU_EXCEPTION_IRQ?0:0x20;
        if(!s.cause_epc_valid || !s.instruction_valid || (s.cause&0x7cu)!=code ||
            (s.epc!=(s.delay_slot?s.branch_pc:s.pc)) ||
            (s.kind==MUSASHI_CPU_EXCEPTION_IRQ && ((s.instruction>>24)&0xfeu)==0x4au))
            return end(image,0,1);
    } else if(s.provenance!=MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE || s.cause_epc_valid ||
        s.pc || s.npc || s.instruction || s.branch_pc || s.instruction_valid || s.delay_slot)
        return end(image,0,1);
    if(!musashi_cpu_status_accept_kernel_entry(image->cpu,token) || image->faulted)return end(image,0,1);
    image->last_sequence=s.sequence;image->entries++;
    return end(image,1,0);
}
int musashi_bios_exception_destroy(MusashiBiosExceptionImage *image) {
    MusashiCpuStatusSnapshot s;int old_fault;
    if(!reserve(image,1))return 0;
    old_fault=image->faulted;image->faulted=0;
    if(!musashi_cpu_status_snapshot(image->cpu,&s) || image->faulted || image->kernel->dispatching ||
        (s.active && !s.faulted))return end(image,0,old_fault || image->faulted);
    image->faulted=old_fault;
    if(!musashi_bios_kernel_remove_exception_binding(image->kernel,&image->binding))return end(image,0,1);
    image->initialized=0;return end(image,1,0);
}
