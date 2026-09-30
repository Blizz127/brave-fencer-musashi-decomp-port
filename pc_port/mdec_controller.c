#include "musashi_mdec_controller.h"
#include <string.h>
#include "musashi_mdec_decode.h"

/* Register/command authority:
 * https://psx-spx.consoledev.net/macroblockdecodermdec/
 * DMA0 request-mode transport uses shared DPCR enable and DICR completion.
 * Color decode consumes real RLE words and provides DMA1 pixel output.
 */
static int channel_register(uint32_t a, unsigned *ch, unsigned *reg) {
    if (a<0x1f801080u || a>0x1f801098u || (a&3u) || (a&15u)>8u) return 0;
    *ch=(a-0x1f801080u)/16u; *reg=(a&15u)/4u; return 1;
}
void musashi_mdec_init(MusashiMdecController *d, MusashiBootMemory *ram,
                        MusashiDmaController *dma) {
    memset(d,0,sizeof(*d)); d->ram=ram; d->dma=dma;
}
int musashi_mdec_read32(MusashiMdecController *d,uint32_t a,uint32_t *v) {
    unsigned ch,reg;
    if (!d || !v) return 0;
    if (a==0x1f801824u) {
        *v=0x80040000u | ((d->command>>2)&0x07800000u) | d->count;
        if(d->output_cursor<d->output_size) {
            *v&=~0x80000000u;
            if(d->control&0x20000000u)*v|=0x08000000u;
        }
        if(d->remaining) *v|=0x20000000u;
        if(d->control&0x40000000u) *v|=0x10000000u;
        return 1;
    }
    if(!channel_register(a,&ch,&reg))return 0;
    *v=reg==0?d->madr[ch]:reg==1?d->bcr[ch]:d->chcr[ch]; return 1;
}
static int parameter(MusashiMdecController *d,uint32_t v) {
    unsigned i;
    if(!d->remaining) {
        unsigned cmd=v>>29;
        if(cmd==1 && (((v>>27)&3u)<2u || !(v&0xffffu)))return 0;
        if(d->output_cursor<d->output_size)return 0;
        d->output_cursor=d->output_size=0;
        d->command=v; d->received=0;
        d->remaining=cmd==1?(v&0xffffu):cmd==2?((v&1u)?32u:16u):cmd==3?32u:0u;
        d->count=d->remaining?(d->remaining-1u):(v&0xffffu);
        return 1;
    }
    if((d->command>>29)==2) {
        for(i=0;i<4;i++)d->quant[d->received*4u+i]=(uint8_t)(v>>(i*8u));
    } else if((d->command>>29)==3) {
        d->scale[d->received*2u]=(int16_t)v;
        d->scale[d->received*2u+1u]=(int16_t)(v>>16);
    } else if((d->command>>29)==1) {
        d->input[d->received*2u]=(uint16_t)v;
        d->input[d->received*2u+1u]=(uint16_t)(v>>16);
    } else return 0;
    ++d->received; --d->remaining; d->count=(d->remaining-1u)&0xffffu;
    if(!d->remaining && (d->command>>29)==1)return mdec_decode_frame(d);
    return 1;
}
int musashi_mdec_write32(MusashiMdecController *d,uint32_t a,uint32_t v) {
    unsigned ch,reg; uint32_t words,addr,segment;
    if(!d || !d->ram || !d->dma)return 0;
    if(a==0x1f801824u) {
        if(v&0x80000000u) {
            d->command=0; d->remaining=0; d->received=0; d->count=0;
            d->output_size=d->output_cursor=0;
        }
        d->control=v&0x60000000u; return 1;
    }
    if(a==0x1f801820u) {
        if(d->chcr[0]&0x01000000u)return 0;
        return parameter(d,v);
    }
    if(!channel_register(a,&ch,&reg))return 0;
    if((d->chcr[ch]&0x01000000u) && !(reg==2 && v==0))return 0;
    if(reg==0){d->madr[ch]=v&0x00ffffffu;return 1;}
    if(reg==1){d->bcr[ch]=v;return 1;}
    if(!(v&0x01000000u)){d->chcr[ch]=v;return 1;}
    /* Request-mode input/output share the real DMA enable and IRQ owner. */
    if(v!=(ch?0x01000200u:0x01000201u) || !(d->dma->control&(8u<<(ch*4u))) ||
       !(d->control&(ch?0x20000000u:0x40000000u)) || !d->dma->irq)return 0;
    words=(d->bcr[ch]&0xffffu)*(d->bcr[ch]>>16);
    addr=d->madr[ch]&0x1fffffffu; segment=d->madr[ch]&0xe0000000u;
    if(!words || (!ch && words>d->remaining) ||
       (!ch && (d->command>>29)!=1 && words>32u) || (addr&3u) ||
       segment || addr>=MUSASHI_RAM_SIZE || words>(MUSASHI_RAM_SIZE-addr)/4u ||
       d->cycle>UINT64_MAX-1u-words)return 0;
    d->chcr[ch]=v;
    if(ch)d->output_due=d->cycle+1u+words;else d->due=d->cycle+1u+words;
    return 1;
}
int musashi_mdec_advance(MusashiMdecController *d,uint64_t cycle) {
    uint32_t words,i,a;
    if(!d || cycle<d->cycle)return 0;
    d->cycle=cycle;
    if((d->chcr[0]&0x01000000u) && cycle>=d->due) {
        words=(d->bcr[0]&0xffffu)*(d->bcr[0]>>16); a=d->madr[0];
        if(words>d->remaining)return 0; /* Reset during DMA cannot fake success. */
        for(i=0;i<words;i++) {
            const uint8_t *p=d->ram->bytes+a+i*4u;
            uint32_t v=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
            if(!parameter(d,v))return 0;
        }
        d->madr[0]+=words*4u; d->bcr[0]&=0xffffu;
        d->chcr[0]&=~0x01000000u; ++d->transfers;
        if(!musashi_dma_controller_complete(d->dma,0))return 0;
    }
    if((d->chcr[1]&0x01000000u) && cycle>=d->output_due) {
        words=(d->bcr[1]&0xffffu)*(d->bcr[1]>>16);
        if(words>(d->output_size-d->output_cursor)/4u) {
            if(d->remaining)return 1; /* Output request waits for real input. */
            return 0;
        }
        memcpy(d->ram->bytes+d->madr[1],d->output+d->output_cursor,words*4u);
        d->output_cursor+=words*4u; d->madr[1]+=words*4u;
        d->bcr[1]&=0xffffu; d->chcr[1]&=~0x01000000u; ++d->transfers;
        if(!musashi_dma_controller_complete(d->dma,1))return 0;
    }
    return 1;
}
