/* Private bounded SPU synthesis. Hardware arithmetic/data: PSX-SPX
 * https://psx-spx.consoledev.net/soundprocessingunitspu/
 * Gaussian coefficients are measured hardware constants, transcribed from its
 * 512-entry table, not the different SNES table in the local PCSX gauss.h.
 * Synchronous manual port and post-frame key ordering are declared native
 * policies; see musashi_spu_cd_audio.h. Physical subphase parity is unproven. */
/*
 * ADSR adaptation notice:
 *
 * The integer envelope step/counter machine below is adapted from
 * PsyX_SPUAL.cpp, AdsrAdvanceCycle, in PsyCross (REDRIVER2 Project),
 * source SHA-256:
 * 6dd07dfa97a409e30af3f09eeecfa19be9ce8e1fc2c6276f744d67621e743a1d.
 * PsyCross is distributed under the MIT License, Copyright (c) 2020
 * REDRIVER2 Project. The applicable notice is included here because this
 * header is the source carrying the adaptation:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * This file changes the state representation to SpuVoice and uses
 * voice_floor() for defined signed arithmetic.
 */
#ifndef MUSASHI_PRIVATE_SPU_VOICE_CORE_H
#define MUSASHI_PRIVATE_SPU_VOICE_CORE_H

#include <stdint.h>
#include <string.h>

static const int16_t spu_gauss[512] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 2, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 7, 7,
    8, 9, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 21, 22, 24,
    25, 27, 28, 30, 32, 33, 35, 37,
    39, 41, 44, 46, 48, 51, 53, 56,
    58, 61, 64, 67, 70, 73, 77, 80,
    84, 87, 91, 95, 99, 103, 107, 111,
    116, 120, 125, 130, 135, 140, 145, 150,
    156, 161, 167, 173, 179, 186, 192, 199,
    205, 212, 219, 227, 234, 242, 250, 257,
    266, 274, 283, 291, 300, 309, 319, 328,
    338, 348, 358, 369, 379, 390, 401, 412,
    424, 436, 448, 460, 473, 485, 498, 512,
    525, 539, 553, 567, 582, 597, 612, 627,
    643, 659, 675, 692, 708, 726, 743, 761,
    779, 797, 816, 835, 854, 874, 894, 914,
    935, 956, 977, 999, 1020, 1043, 1066, 1089,
    1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
    1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508,
    1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
    1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031,
    2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
    2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668,
    2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
    3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427,
    3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
    3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315,
    4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
    4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332,
    5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
    5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475,
    6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
    7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735,
    7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
    8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097,
    9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
    9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542,
    10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
    11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045,
    12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
    12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577,
    13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
    14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104,
    15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
    15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592,
    16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
    17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001,
    18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
    18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295,
    19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
    19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439,
    20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
    21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399,
    21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
    21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148,
    22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
    22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662,
    22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
    22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927,
    22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
};

typedef struct SpuVoice {
    uint16_t reg[8];
    int16_t gain[2];
    unsigned phase;
    int32_t envelope;
    uint32_t envelope_counter;
    uint32_t cursor, repeat;
    uint16_t fraction;
    int16_t decoded[28], history[4];
    unsigned position, flags;
    int loaded;
    uint64_t blocks, samples;
    int16_t last;
} SpuVoice;
typedef struct SpuVoices {
    SpuVoice voice[24];
    uint32_t key_on, key_off, endx;
} SpuVoices;

static int64_t voice_floor(int64_t n, int64_t d) {
    return n >= 0 ? n/d : -((-n+d-1)/d);
}
static int16_t voice_clamp(int64_t n) {
    return (int16_t)(n > 32767 ? 32767 : n < -32768 ? -32768 : n);
}
static void voice_off(SpuVoice *v) {
    v->phase=MUSASHI_SPU_VOICE_OFF; v->envelope=0;
    v->envelope_counter=0; v->last=0;
}
/* All 16-bit ADSR register encodings are structurally valid. */
static int voice_supported(const SpuVoice *v) {
    (void)v;
    return 1;
}
static void voice_key_on(SpuVoice *v) {
    v->phase=MUSASHI_SPU_VOICE_ATTACK; v->envelope=0;
    v->envelope_counter=0;
    v->cursor=((uint32_t)v->reg[3]*8u)&0x7fff0u;
    v->repeat=((uint32_t)v->reg[7]*8u)&0x7fff0u;
    v->fraction=0; v->position=0; v->loaded=0; v->flags=0; v->last=0;
    memset(v->history,0,sizeof(v->history));
}
/* Adapted from PsyX_SPUAL.cpp's AdsrAdvanceCycle under the MIT terms noted
 * above. Keep all shifts on nonnegative values: the original's negative
 * right-shift was replaced with voice_floor(), and decrement magnitudes are
 * negated after shifting a positive value. One call is one 44100 Hz envelope
 * cycle, matching the current 44.1 kHz output sample clock. */
static void voice_envelope_source(SpuVoice *v) {
    int shift, step2, exp, dec, target, sh_l;
    int32_t step;
    uint32_t counter_inc, never;

    switch (v->phase) {
    case MUSASHI_SPU_VOICE_ATTACK:
        shift=(v->reg[4]>>10)&0x1f; step2=(v->reg[4]>>8)&3;
        exp=(v->reg[4]>>15)&1; dec=0; target=0x7fff; break;
    case MUSASHI_SPU_VOICE_DECAY:
        shift=(v->reg[4]>>4)&0xf; step2=0; exp=1; dec=1;
        target=((v->reg[4]&0xf)+1)*0x800; break;
    case MUSASHI_SPU_VOICE_SUSTAIN:
        shift=(v->reg[5]>>8)&0x1f; step2=(v->reg[5]>>6)&3;
        exp=(v->reg[5]>>15)&1; dec=(v->reg[5]>>14)&1; target=-1; break;
    case MUSASHI_SPU_VOICE_RELEASE:
        shift=v->reg[5]&0x1f; step2=0; exp=(v->reg[5]>>5)&1;
        dec=1; target=0; break;
    default: return;
    }

    sh_l=11-shift;
    if (sh_l<0) sh_l=0;
    step=(dec ? -(8-step2) : (7-step2)) * (int32_t)(1u<<sh_l);
    counter_inc=shift>11 ? 0x8000u>>(shift-11) : 0x8000u;
    if (exp && !dec && v->envelope>0x6000) {
        if (shift<10) step/=4;
        else if (shift>=11) counter_inc/=4;
        else { step/=2; counter_inc/=2; }
    } else if (exp && dec) {
        step=(int32_t)voice_floor((int64_t)step*v->envelope,32768);
    }
    never=(v->phase==MUSASHI_SPU_VOICE_ATTACK ||
           v->phase==MUSASHI_SPU_VOICE_SUSTAIN) ?
          (shift==0x1f && step2==3) : (shift==0x1f);
    if (!never && counter_inc<1) counter_inc=1;
    v->envelope_counter+=counter_inc;
    if ((v->envelope_counter&0x8000u)==0) return;
    v->envelope_counter=0;
    v->envelope+=step;
    if (!dec) {
        if (v->envelope>0x7fff) v->envelope=0x7fff;
    } else if (v->envelope<0) v->envelope=0;
    if (v->phase==MUSASHI_SPU_VOICE_ATTACK && v->envelope>=0x7fff) {
        v->envelope=0x7fff; v->phase=MUSASHI_SPU_VOICE_DECAY;
        v->envelope_counter=0;
    } else if (v->phase==MUSASHI_SPU_VOICE_DECAY && v->envelope<=target) {
        v->phase=MUSASHI_SPU_VOICE_SUSTAIN; v->envelope_counter=0;
    } else if (v->phase==MUSASHI_SPU_VOICE_RELEASE && v->envelope<=0) {
        voice_off(v);
    }
}
static void voice_envelope(SpuVoice *v) {
    voice_envelope_source(v);
}
/* Only startup filter0 is admitted. Nonzero predictors need independent SPU
 * rounding validation (XA rounding must not be silently substituted). */
static int voice_decode(SpuVoice *v, const uint8_t b[16]) {
    unsigned filter=b[0]>>4, shift=b[0]&15u, i;
    if(filter>4 || shift>12 || (b[1]&0xf8u) || v->blocks==UINT64_MAX) return 0;
    static const int k0[5] = { 0, 60, 115, 98, 122 };
    static const int k1[5] = { 0, 0, -52, -55, -60 };
    int64_t f0 = k0[filter];
    int64_t f1 = k1[filter];
    for(i=0;i<28;++i) {
        int nibble=(b[2+i/2]>>((i&1u)*4))&15;
        int64_t raw, s;
        if(nibble&8) nibble-=16;
        raw=voice_floor((int64_t)nibble*4096,(int64_t)1<<shift);
        s = raw + (v->history[3] * f0 + v->history[2] * f1 + 32) / 64;
        v->decoded[i]=voice_clamp(s);
    }
    v->flags=b[1];
    if(v->flags&4u) { v->repeat=v->cursor; v->reg[7]=(uint16_t)(v->repeat/8u); }
    v->position=0; v->loaded=1; ++v->blocks;
    return 1;
}
static void voice_shift_sample(SpuVoice *v, int16_t sample) {
    v->history[0]=v->history[1]; v->history[1]=v->history[2];
    v->history[2]=v->history[3]; v->history[3]=sample;
}
static int voice_next(SpuVoice *v, void *data,
                      int (*read_block)(void *,uint32_t,uint8_t *),
                      uint32_t mask, uint32_t *endx) {
    uint8_t bytes[16];
    if(v->loaded && v->position==28) {
        if(v->flags&1u) {
            *endx|=mask; v->cursor=v->repeat;
            if(!(v->flags&2u)) { voice_off(v); return 1; }
        } else v->cursor=(v->cursor+16u)&0x7ffffu;
        v->loaded=0;
    }
    if(!v->loaded && (!read_block(data,v->cursor,bytes) || !voice_decode(v,bytes))) {
        voice_off(v);
        return 1;
    }
    if(v->samples==UINT64_MAX) return 0;
    voice_shift_sample(v,v->decoded[v->position++]); ++v->samples;
    return 1;
}
static int voices_render(SpuVoices *s, void *data,
                         int (*read_block)(void *,uint32_t,uint8_t *),
                         int64_t out[2]) {
    unsigned i;
    out[0]=out[1]=0;
    for(i=0;i<24;++i) {
        SpuVoice *v=&s->voice[i]; unsigned p;
        int16_t rendered;
        int64_t interpolated;
        uint32_t phase;
        v->last=0;
        if(v->phase==MUSASHI_SPU_VOICE_OFF) continue;
        if(!voice_supported(v)) { voice_off(v); continue; }
        if(!v->loaded && !voice_next(v,data,read_block,1u<<i,&s->endx)) return 0;
        p=v->fraction>>4;
        /* PSX-SPX specifies four separately shifted products; exact measured
         * rounding parity is unproven (DuckStation shifts the sum once). */
        interpolated=voice_floor((int64_t)spu_gauss[255-p]*v->history[0],32768)+
                     voice_floor((int64_t)spu_gauss[511-p]*v->history[1],32768)+
                     voice_floor((int64_t)spu_gauss[256+p]*v->history[2],32768)+
                     voice_floor((int64_t)spu_gauss[p]*v->history[3],32768);
        rendered=voice_clamp(voice_floor((int64_t)voice_clamp(interpolated)*v->envelope,32768));
        v->last=rendered;
        out[0]+=voice_floor((int64_t)v->last*v->gain[0],32768);
        out[1]+=voice_floor((int64_t)v->last*v->gain[1],32768);
        voice_envelope(v);
        phase=(uint32_t)v->fraction+v->reg[2];
        while(phase>=4096 && v->phase!=MUSASHI_SPU_VOICE_OFF) {
            if(!voice_next(v,data,read_block,1u<<i,&s->endx)) return 0;
            phase-=4096;
        }
        v->fraction=(uint16_t)(phase&4095u);
        /* voice_envelope() and the ADPCM end transition may call voice_off(),
         * but the sample just mixed remains the caller's post-envelope tap.
         * The next frame clears an off voice at loop entry. */
        v->last=rendered;
    }
    return 1;
}
/* DuckStation 0d33bd17776cfec46a2237a46f57081459ed0b13:
 * Execute renders the first due frame, then applies KOFF before KON.
 * This ordering is applied per logical frame, independent of host chunking. */
static void voices_apply_keys(SpuVoices *s) {
    unsigned i;
    for(i=0;i<24;++i) {
        SpuVoice *v=&s->voice[i]; uint32_t mask=1u<<i;
        if((s->key_off&mask) && v->phase!=MUSASHI_SPU_VOICE_OFF) {
            v->phase=MUSASHI_SPU_VOICE_RELEASE;
            v->envelope_counter=0;
        }
        if(s->key_on&mask) { voice_key_on(v); s->endx&=~mask; }
    }
    s->key_on=s->key_off=0;
}
#endif
