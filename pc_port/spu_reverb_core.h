/* Independently authored bounded SPU reverb model from PSX-SPX hardware
 * equations and measured FIR data, pinned at a37f2fec163e149f4e3e39599eb43c65c21181a8:
 * https://psx-spx.consoledev.net/soundprocessingunitspu/
 * No DuckStation implementation (CC-BY-NC-ND) is copied here. Factored IIR,
 * stage saturation and initial L/R phase are explicit native policies;
 * exact silicon rounding/subphase parity is not proven. */
#ifndef MUSASHI_PRIVATE_SPU_REVERB_CORE_H
#define MUSASHI_PRIVATE_SPU_REVERB_CORE_H

#define REVERB_TAPS 39u
#define REVERB_HISTORY_MASK ((UINT64_C(1)<<REVERB_TAPS)-1)
static const int16_t reverb_fir[REVERB_TAPS]={
    -1,0,2,0,-10,0,35,0,-103,0,266,0,-616,0,1332,0,-2960,0,10246,
    16384,10246,0,-2960,0,1332,0,-616,0,266,0,-103,0,35,0,-10,0,2,0,-1
};
typedef struct ReverbValue { int32_t sample; int known; } ReverbValue;
typedef struct SpuReverb {
    uint16_t reg[32], base;
    int16_t gain[2];
    uint32_t cursor,eon;
    int16_t down[2][REVERB_TAPS],up[2][REVERB_TAPS];
    uint64_t up_valid[2];
    uint64_t frames,processed[2],reads,writes,unknown,unsupported;
    uint32_t first_unknown;
    MusashiSpuReverbAccess accesses[32];
    unsigned access_count;
} SpuReverb;
static int64_t reverb_floor(int64_t v) {
    return v>=0?v/32768:-((-v+32767)/32768);
}
static int16_t reverb_clamp(int64_t v) {
    return (int16_t)(v>32767?32767:v< -32768?-32768:v);
}
static ReverbValue reverb_known(int64_t v) {
    ReverbValue x={reverb_clamp(v),1}; return x;
}
static ReverbValue reverb_unknown(void) {
    /* sample is never an observed RAM value when known==0. */
    ReverbValue x={0,0}; return x;
}
static void reverb_init(SpuReverb *r) {
    memset(r,0,sizeof(*r));
    r->up_valid[0]=r->up_valid[1]=REVERB_HISTORY_MASK;
    r->first_unknown=UINT32_MAX;
}
/* Restrict active addressing to a single work-area displacement. This covers
 * mode4 and permits independently tested mutations. Oversized passive partial
 * configurations produce unknown wet history, not guessed RAM addresses. */
static int reverb_supported(const SpuReverb *r) {
    uint32_t length=0x80000u-(uint32_t)r->base*8u; unsigned i;
    if(r->reg[2]==0x8000u || (uint32_t)r->reg[0]*8u>=length ||
       (uint32_t)r->reg[1]*8u>=length) return 0;
    for(i=10;i<30;++i) if((uint32_t)r->reg[i]*8u>=length) return 0;
    return 1;
}
static uint32_t reverb_address(const SpuReverb *r,int64_t displacement) {
    uint32_t base=(uint32_t)r->base*8u;
    int64_t length=0x80000u-base;
    int64_t offset=((int64_t)r->cursor-base+displacement)%length;
    if(offset<0) offset+=length;
    return base+(uint32_t)offset;
}
static void reverb_access(SpuReverb *r,uint32_t address,int write,int known,int16_t value) {
    if(r->access_count<32) {
        MusashiSpuReverbAccess *a=&r->accesses[r->access_count++];
        a->address=address;a->write=write;a->known=known;
        a->value=known?value:0; /* explicitly invalid diagnostic field */
    }
}
static ReverbValue reverb_read(SpuReverb *r,void *data,
        int (*read)(void *,uint32_t,int16_t *),int64_t displacement) {
    uint32_t at=reverb_address(r,displacement); int16_t sample;
    if(!read(data,at,&sample)) {
        if(r->unknown==UINT64_MAX) return reverb_unknown();
        ++r->unknown;if(r->first_unknown==UINT32_MAX) r->first_unknown=at;
        reverb_access(r,at,0,0,0);return reverb_unknown();
    }
    ++r->reads;reverb_access(r,at,0,1,sample);return reverb_known(sample);
}
static ReverbValue reverb_product(SpuReverb *r,void *data,
        int (*read)(void *,uint32_t,int16_t *),int64_t displacement,int32_t coefficient) {
    ReverbValue x;
    if(!coefficient) return reverb_known(0); /* no IRQ source is supported */
    x=reverb_read(r,data,read,displacement);
    if(x.known) x.sample=(int32_t)reverb_floor((int64_t)x.sample*coefficient);
    return x;
}
static int reverb_store(SpuReverb *r,void *data,
        int (*write)(void *,uint32_t,uint16_t),unsigned reg,ReverbValue x) {
    uint32_t at=reverb_address(r,(int64_t)r->reg[reg]*8);
    if(!x.known || !write(data,at,(uint16_t)reverb_clamp(x.sample))) return 0;
    ++r->writes;reverb_access(r,at,1,1,(int16_t)x.sample);return 1;
}
/* For vAPF2==0 with writes disabled, only its delayed tap contributes.
 * Eliminating multiplied-by-zero dependencies is exact, not zero RAM. */
static int reverb_channel(SpuReverb *r,void *data,
        int (*read)(void *,uint32_t,int16_t *),int (*write)(void *,uint32_t,uint16_t),
        unsigned channel,int16_t input,int enabled,ReverbValue *result) {
    ReverbValue value,delayed,part; unsigned i;
    if(!enabled && r->reg[9]==0) {
        *result=reverb_read(r,data,read,((int64_t)r->reg[28+channel]-r->reg[1])*8);
        return 1;
    }
    if(enabled) {
        for(i=0;i<2;++i) {
            unsigned destination=(i?18:10)+channel;
            unsigned source=i?24+(channel^1u):16+channel;
            int32_t alpha=(int16_t)r->reg[2];
            ReverbValue wall=reverb_product(r,data,read,(int64_t)r->reg[source]*8,(int16_t)r->reg[7]);
            ReverbValue previous=reverb_product(r,data,read,(int64_t)r->reg[destination]*8-2,32768-alpha);
            if(!wall.known || !previous.known) return 0;
            value=reverb_known(reverb_floor((int64_t)input*(int16_t)r->reg[30+channel])+wall.sample);
            value=reverb_known(reverb_floor((int64_t)value.sample*alpha)+previous.sample);
            if(!reverb_store(r,data,write,destination,value)) return 0;
        }
    }
    value=reverb_known(0);
    for(i=0;i<4;++i) {
        static const unsigned tap[4]={12,14,20,22};
        part=reverb_product(r,data,read,(int64_t)r->reg[tap[i]+channel]*8,(int16_t)r->reg[3+i]);
        if(!part.known) value.known=0;
        if(value.known) value.sample+=part.sample;
    }
    if(value.known) value.sample=reverb_clamp(value.sample);
    for(i=0;i<2;++i) {
        unsigned destination=26+2*i+channel;
        int32_t coefficient=(int16_t)r->reg[8+i];
        delayed=reverb_read(r,data,read,((int64_t)r->reg[destination]-r->reg[i])*8);
        /* Destination is required for feedback whenever writes are enabled. */
        if(value.known && (delayed.known || !coefficient))
            part=reverb_known(value.sample-(coefficient?reverb_floor((int64_t)delayed.sample*coefficient):0));
        else part=reverb_unknown();
        if(enabled && !reverb_store(r,data,write,destination,part)) return 0;
        if(delayed.known && (part.known || !coefficient))
            value=reverb_known(delayed.sample+(coefficient?reverb_floor((int64_t)part.sample*coefficient):0));
        else value=reverb_unknown();
    }
    *result=value;return 1;
}
static int reverb_render(SpuReverb *r,void *data,
        int (*read)(void *,uint32_t,int16_t *),int (*write)(void *,uint32_t,uint16_t),
        int16_t input[2],int enabled,int16_t output[2]) {
    unsigned pos=(unsigned)(r->frames%REVERB_TAPS),c,k;
    if(r->frames==UINT64_MAX || r->reads>UINT64_MAX-32 ||
       r->writes>UINT64_MAX-4 || r->unknown>UINT64_MAX-32 || r->unsupported==UINT64_MAX) return 0;
    r->down[0][pos]=input[0];r->down[1][pos]=input[1];
    /* Sparse upsampling inserts a real zero only on the other channel's
     * phase. An unknown computed sample leaves its storage uninterpreted. */
    for(c=0;c<2;++c) if(!r->frames || c!=((r->frames-1)&1u)) {
        r->up[c][pos]=0;r->up_valid[c]|=UINT64_C(1)<<pos;
    }
    if(r->frames) {
        unsigned channel=(unsigned)((r->frames-1)&1u);
        ReverbValue wet; int64_t sum=0;
        r->access_count=0;
        if(r->processed[channel]==UINT64_MAX) return 0;
        if(!reverb_supported(r)) {
            ++r->unsupported;if(enabled) return 0;wet=reverb_unknown();
        } else {
            for(k=0;k<REVERB_TAPS;++k)
                sum+=(int64_t)reverb_fir[k]*r->down[channel][(pos+REVERB_TAPS-k)%REVERB_TAPS];
            if(!reverb_channel(r,data,read,write,channel,reverb_clamp(reverb_floor(sum)),enabled,&wet)) return 0;
        }
        if(wet.known) {
            r->up[channel][pos]=(int16_t)wet.sample;
            r->up_valid[channel]|=UINT64_C(1)<<pos;
        }
        else r->up_valid[channel]&=~(UINT64_C(1)<<pos);
        ++r->processed[channel];
        if(channel) {
            r->cursor+=2;
            if(r->cursor==0x80000u) r->cursor=(uint32_t)r->base*8u;
        }
    }
    for(c=0;c<2;++c) {
        int64_t sum=0;
        if(!r->gain[c]) { output[c]=0;continue; }
        for(k=0;k<REVERB_TAPS;++k) if(reverb_fir[k]) {
            unsigned at=(pos+REVERB_TAPS-k)%REVERB_TAPS;
            if(!(r->up_valid[c]&(UINT64_C(1)<<at))) return 0;
            sum+=(int64_t)r->up[c][at]*reverb_fir[k]*2;
        }
        output[c]=reverb_clamp(reverb_floor((int64_t)reverb_clamp(reverb_floor(sum))*r->gain[c]));
    }
    ++r->frames;return 1;
}
#endif
