#ifndef MUSASHI_MDEC_DECODE_H
#define MUSASHI_MDEC_DECODE_H
/* Integer reference model of the documented PSX MDEC pipeline.
 * https://psx-spx.consoledev.net/macroblockdecodermdec/
 * Uses the uploaded quantization and scale tables. Numeric rounding follows
 * the documented low-level model; hardware-exact IDCT rounding is not claimed.
 */
static int mdec_floor(int64_t n, int divisor) {
    return (int)(n >= 0 ? n/divisor : -((-n+divisor-1)/divisor));
}
static int mdec_clip(int n,int lo,int hi) { return n<lo?lo:n>hi?hi:n; }
static int mdec_signed10(unsigned n) { return (int)(n&1023u)-((n&512u)?1024:0); }
static void mdec_idct(int *block,const int16_t *scale) {
    int temp[64]; unsigned pass,x,y,z;
    for(pass=0;pass<2;pass++) {
        for(y=0;y<8;y++)for(x=0;x<8;x++) {
            int64_t sum=0;
            for(z=0;z<8;z++)sum+=(int64_t)block[y+z*8]*mdec_floor(scale[x+z*8],8);
            temp[x+y*8]=mdec_floor(sum+4095,8192);
        }
        memcpy(block,temp,sizeof(temp));
    }
}
static int mdec_block(MusashiMdecController *d,unsigned *cursor,int *b,const uint8_t *q) {
    static const uint8_t order[64]={
        0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,
        12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
        35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
        58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
    unsigned n,k=0,scale,limit=d->received*2u; int val;
    memset(b,0,64*sizeof(*b));
    do { if(*cursor>=limit)return 0; n=d->input[(*cursor)++]; } while(n==0xfe00u);
    scale=n>>10; val=mdec_signed10(n)*q[0];
    for(;;) {
        if(!scale)val=mdec_signed10(n)*2;
        b[scale?order[k]:k]=mdec_clip(val,-1024,1023);
        if(*cursor>=limit)return 0;
        n=d->input[(*cursor)++]; k+=(n>>10)+1u;
        if(k>=64)break;
        val=mdec_floor((int64_t)mdec_signed10(n)*q[k]*scale+4,8);
    }
    mdec_idct(b,d->scale); return 1;
}
static int mdec_decode_frame(MusashiMdecController *d) {
    unsigned cursor=0,depth=(d->command>>27)&3u,bytes=depth==3?512:768;
    int blocks[6][64]; unsigned i,x,y;
    d->output_size=0; d->output_cursor=0;
    if(depth<2)return 0;
    while(cursor<d->received*2u) {
        while(cursor<d->received*2u && d->input[cursor]==0xfe00u)++cursor;
        if(cursor==d->received*2u)break;
        if(d->output_size>sizeof(d->output)-bytes)return 0;
        for(i=0;i<6;i++)if(!mdec_block(d,&cursor,blocks[i],d->quant+(i<2?64:0)))return 0;
        for(y=0;y<16;y++)for(x=0;x<16;x++) {
            unsigned chroma=x/2+(y/2)*8,yi=2+(x/8)+(y/8)*2;
            int cr=blocks[0][chroma],cb=blocks[1][chroma],lum=blocks[yi][x%8+(y%8)*8];
            int rgb[3]={lum+mdec_floor(1436*(int64_t)cr,1024),
                        lum+mdec_floor(-352*(int64_t)cb-731*(int64_t)cr,1024),
                        lum+mdec_floor(1815*(int64_t)cb,1024)};
            uint8_t *out=d->output+d->output_size+(y*16+x)*(depth==3?2:3);
            for(i=0;i<3;i++) {
                rgb[i]=mdec_clip(rgb[i],-128,127)&255;
                if(!(d->command&0x04000000u))rgb[i]^=128;
            }
            if(depth==3) {
                unsigned pixel=(rgb[0]>>3)|((rgb[1]>>3)<<5)|((rgb[2]>>3)<<10)|
                    ((d->command&0x02000000u)?0x8000u:0u);
                out[0]=(uint8_t)pixel;out[1]=(uint8_t)(pixel>>8);
            } else for(i=0;i<3;i++)out[i]=(uint8_t)rgb[i];
        }
        d->output_size+=bytes;
    }
    return d->output_size!=0;
}
#endif
