#include "musashi_spu_cd_audio.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "spu_voice_core.h"
#include "spu_reverb_core.h"

enum {
    MAIN_LEFT = 0,
    MAIN_RIGHT = 1,
    CD_LEFT = 0,
    CD_RIGHT = 1,
    PCM_CHUNK = 1024
};

typedef struct PendingPcmFrame {
    uint64_t sample;
    int16_t left;
    int16_t right;
} PendingPcmFrame;

typedef struct RamUndo {
    uint32_t address;
    uint16_t value;
    uint8_t validity;
} RamUndo;

struct MusashiSpuCdAudio {
    MusashiSpuCdAudioBackend backend;
    uint64_t owner_thread;
    uint64_t epoch;
    uint64_t cycle;
    uint64_t sample_index;
    int16_t configured_main[2];
    int16_t pending_main[2];
    int pending_main_valid[2];
    int16_t current_main[2];
    int16_t cd_gain[2];
    uint16_t control;
    SpuVoices voices;
    SpuReverb reverb;
    RamUndo undo[PCM_CHUNK * 8];
    size_t undo_count;
    int staging_ram;
    uint8_t ram[512u*1024u];
    uint8_t ram_valid[512u*1024u/8u];
    uint32_t transfer_cursor;
    uint16_t transfer_address, transfer_control;
    int transfer_address_valid;
    uint64_t manual_halfwords, bootstrap_halfwords;
    PendingPcmFrame pending[ MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT ];
    size_t pending_head;
    size_t pending_count;
    size_t queued_frames;
    size_t submitted_frames;
    int initialized;
    int initializing;
    int callback_depth;
    int callback_violation;
    int faulted;
};

static int callback_enter(MusashiSpuCdAudio *audio) {
    if (!audio || audio->callback_depth != 0) {
        if (audio) {
            audio->callback_violation = 1;
            audio->faulted = 1;
        }
        return 0;
    }
    audio->callback_depth = 1;
    return 1;
}

static void callback_leave(MusashiSpuCdAudio *audio) {
    if (audio) audio->callback_depth = 0;
}

static int owner_thread_ok(MusashiSpuCdAudio *audio) {
    int ready;
    uint64_t token;
    if (!audio || !audio->initialized || audio->faulted || audio->owner_thread == 0 ||
        !audio->backend.ready || !audio->backend.thread_token ||
        !callback_enter(audio))
        return 0;
    ready = audio->backend.ready(audio->backend.userdata);
    token = audio->backend.thread_token(audio->backend.userdata);
    callback_leave(audio);
    if (audio->callback_violation || audio->faulted || !ready || token == 0 ||
        token != audio->owner_thread) {
        audio->faulted = 1;
        return 0;
    }
    return 1;
}

static int owner_thread_for_cleanup(const MusashiSpuCdAudio *const_audio) {
    MusashiSpuCdAudio *audio = (MusashiSpuCdAudio *)const_audio;
    uint64_t token;
    if (!audio || !audio->initialized || audio->owner_thread == 0 ||
        !audio->backend.thread_token || !callback_enter(audio))
        return 0;
    audio->callback_violation = 0;
    token = audio->backend.thread_token(audio->backend.userdata);
    callback_leave(audio);
    return !audio->callback_violation && token != 0 &&
           token == audio->owner_thread;
}

static int backend_status(MusashiSpuCdAudio *audio) {
    size_t queued = 0;
    int playing = 0;
    if (!owner_thread_ok(audio) || !audio->backend.status ||
        !callback_enter(audio)) {
        if (audio) audio->faulted = 1;
        return 0;
    }
    if (!audio->backend.status(audio->backend.userdata, &queued, &playing)) {
        callback_leave(audio);
        audio->faulted = 1;
        return 0;
    }
    callback_leave(audio);
    if (audio->faulted || queued > MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT || !playing) {
        if (audio) audio->faulted = 1;
        return 0;
    }
    audio->queued_frames = queued;
    return 1;
}

static int64_t floor_div_32768(int64_t value) {
    if (value >= 0) return value / 32768;
    return -(((-value) + 32767) / 32768);
}

static int16_t clamp16(int64_t value) {
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

static int16_t fixed_gain(uint16_t value) {
    int32_t signed15 = (value & 0x4000u) ?
        (int32_t)(value & 0x3fffu) - 0x4000 : (int32_t)(value & 0x3fffu);
    return clamp16((int64_t)signed15 * 2);
}

static void apply_pending_main(MusashiSpuCdAudio *audio) {
    int channel;
    for (channel = 0; channel < 2; ++channel) {
        if (audio->pending_main_valid[channel]) {
            audio->current_main[channel] = audio->pending_main[channel];
            audio->pending_main_valid[channel] = 0;
        }
    }
}

/* Retire only frames covered by an explicit output skip. Keep the ordinary
 * consumer strict: stale input outside this policy remains a fault. */
static void discard_pending_before(MusashiSpuCdAudio *audio, uint64_t sample) {
    while (audio->pending_count && audio->pending[audio->pending_head].sample < sample) {
        ++audio->pending_head;
        --audio->pending_count;
    }
    if (!audio->pending_count) audio->pending_head = 0;
}

static void consume_pending(MusashiSpuCdAudio *audio, uint64_t sample_index,
                            int16_t *left,
                            int16_t *right) {
    *left = 0;
    *right = 0;
    if (audio->pending_count == 0) return;
    if (audio->pending[audio->pending_head].sample < sample_index) {
        audio->faulted = 1;
        return;
    }
    if (audio->pending[audio->pending_head].sample == sample_index) {
        *left = audio->pending[audio->pending_head].left;
        *right = audio->pending[audio->pending_head].right;
        ++audio->pending_head;
        --audio->pending_count;
        if (audio->pending_count == 0) audio->pending_head = 0;
    }
}

static int ram_initialized(const MusashiSpuCdAudio *a, uint32_t offset, size_t n) {
    size_t i;
    if(offset>sizeof(a->ram) || n>sizeof(a->ram)-offset) return 0;
    for(i=0;i<n;++i) if(!(a->ram_valid[(offset+i)/8]&(1u<<((offset+i)&7)))) return 0;
    return 1;
}
static void ram_halfword(MusashiSpuCdAudio *a, uint32_t at, uint16_t value) {
    a->ram[at]=(uint8_t)value; a->ram[at+1]=(uint8_t)(value>>8);
    a->ram_valid[at/8]|=(uint8_t)(3u<<(at&7u));
}
static int ram_stage_halfword(void *data, uint32_t at, uint16_t value) {
    MusashiSpuCdAudio *a=data;
    if((at&1u) || at>=sizeof(a->ram)-1u) return 0;
    if(a->staging_ram) {
        RamUndo *u;
        if(a->undo_count>=sizeof(a->undo)/sizeof(a->undo[0])) return 0;
        u=&a->undo[a->undo_count++]; u->address=at;
        u->value=(uint16_t)(a->ram[at]|((uint16_t)a->ram[at+1]<<8));
        u->validity=(uint8_t)((a->ram_valid[at/8]>>(at&7u))&3u);
    }
    ram_halfword(a,at,value); return 1;
}
static void ram_rollback(MusashiSpuCdAudio *a) {
    while(a->undo_count) {
        const RamUndo *u=&a->undo[--a->undo_count]; uint32_t at=u->address;
        a->ram[at]=(uint8_t)u->value; a->ram[at+1]=(uint8_t)(u->value>>8);
        a->ram_valid[at/8]=(uint8_t)((a->ram_valid[at/8]&~(3u<<(at&7u)))|
                                   ((unsigned)u->validity<<(at&7u)));
    }
}
static int read_reverb_halfword(void *data, uint32_t at, int16_t *value) {
    MusashiSpuCdAudio *a=data; uint32_t bits;
    if((at&1u) || !ram_initialized(a,at,2)) return 0;
    bits=a->ram[at]|((uint32_t)a->ram[at+1]<<8);
    *value=(int16_t)(bits>=0x8000u?(int32_t)bits-65536:(int32_t)bits);
    return 1;
}
static int read_voice_block(void *data, uint32_t at, uint8_t *bytes) {
    MusashiSpuCdAudio *a=data;
    if((at&15u) || !ram_initialized(a,at,16)) return 0;
    memcpy(bytes,a->ram+at,16); return 1;
}
static void render_sample(MusashiSpuCdAudio *audio, uint64_t sample_index,
                          int16_t *output) {
    int16_t input_left, input_right, cd[2], wet_input[2], wet[2];
    int64_t voice[2], left, right, send[2]={0,0};
    unsigned i;
    uint32_t capture=(uint32_t)(sample_index&511u)*2u;
    apply_pending_main(audio);
    consume_pending(audio,sample_index,&input_left,&input_right);
    if(audio->faulted || !voices_render(&audio->voices,audio,read_voice_block,voice)) {
        audio->faulted=1; return;
    }
    for(i=0;i<24;++i) if(audio->reverb.eon&(1u<<i)) {
        const SpuVoice *v=&audio->voices.voice[i];
        send[0]+=floor_div_32768((int64_t)v->last*v->gain[0]);
        send[1]+=floor_div_32768((int64_t)v->last*v->gain[1]);
    }
    /* Mute gates the voice bus, including its reverb input. CD has its own
     * direct and wet routing gates and signed common gains. */
    left=(audio->control&0x4000u)?voice[0]:0;
    right=(audio->control&0x4000u)?voice[1]:0;
    if(!(audio->control&0x4000u)) send[0]=send[1]=0;
    cd[0]=clamp16(floor_div_32768((int64_t)input_left*audio->cd_gain[0]));
    cd[1]=clamp16(floor_div_32768((int64_t)input_right*audio->cd_gain[1]));
    if(audio->control&1u) {
        left+=cd[0]; right+=cd[1];
        if(audio->control&4u) { send[0]+=cd[0]; send[1]+=cd[1]; }
    }
    wet_input[0]=clamp16(send[0]);wet_input[1]=clamp16(send[1]);
    if(!reverb_render(&audio->reverb,audio,read_reverb_halfword,ram_stage_halfword,
                      wet_input,!!(audio->control&0x80u),wet)) {
        audio->faulted=1;return;
    }
    left+=wet[0]; right+=wet[1];
    output[0]=clamp16(floor_div_32768((int64_t)clamp16(left)*audio->current_main[0]));
    output[1]=clamp16(floor_div_32768((int64_t)clamp16(right)*audio->current_main[1]));
    if(!ram_stage_halfword(audio,capture,(uint16_t)input_left) ||
       !ram_stage_halfword(audio,0x400u+capture,(uint16_t)input_right) ||
       !ram_stage_halfword(audio,0x800u+capture,(uint16_t)audio->voices.voice[1].last) ||
       !ram_stage_halfword(audio,0xc00u+capture,(uint16_t)audio->voices.voice[3].last)) {
        audio->faulted=1;return;
    }
    voices_apply_keys(&audio->voices);
}

static int queue_chunk(MusashiSpuCdAudio *audio, const int16_t *pcm,
                       size_t frames) {
    int accepted;
    int violated;
    if (!backend_status(audio) || frames > MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT -
        audio->queued_frames || !audio->backend.queue ||
        !callback_enter(audio)) {
        audio->faulted = 1;
        return 0;
    }
    accepted = audio->backend.queue(audio->backend.userdata, pcm, frames);
    violated = audio->callback_violation;
    callback_leave(audio);
    if (!accepted) {
        audio->faulted = 1;
        return 0;
    }
    audio->submitted_frames += frames;
    audio->sample_index += frames;
    /* The accepted sample count is bounded by this advance's target, so this
     * boundary cannot exceed the checked absolute_cycles argument. Preserve
     * it even when the backend accepted PCM and then violated reentry. */
    audio->cycle = audio->epoch + audio->sample_index *
                   MUSASHI_SPU_CD_AUDIO_SAMPLE_CYCLES;
    if (violated || audio->faulted) return 0;
    return 1;
}

MusashiSpuCdAudio *musashi_spu_cd_audio_create(void) {
    return calloc(1, sizeof(MusashiSpuCdAudio));
}

int musashi_spu_cd_audio_init_bios_muted(
    MusashiSpuCdAudio *audio, const MusashiSpuCdAudioBackend *backend,
    uint64_t epoch) {
    size_t queued = 0;
    int playing = 0;
    int ready;
    uint64_t token;
    if (!audio || audio->initialized || audio->initializing || !backend ||
        !backend->ready || !backend->thread_token || !backend->status ||
        !backend->queue)
        return 0;
    audio->initializing = 1;
    if (!callback_enter(audio)) {
        audio->initializing = 0;
        return 0;
    }
    ready = backend->ready(backend->userdata);
    token = backend->thread_token(backend->userdata);
    if (ready && token != 0 &&
        !backend->status(backend->userdata, &queued, &playing))
        ready = 0;
    callback_leave(audio);
    if (!ready || token == 0 || !playing || queued > MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT) {
        audio->initializing = 0;
        return 0;
    }
    memset(audio, 0, sizeof(*audio));
    /* Selected BIOS shell clears [70000,80000) before the original epoch.
     * Its later jingle/mode6 is omitted by this explicit fresh muted policy. */
    {
        uint32_t at;
        for(at=0x70000u;at<0x80000u;at+=2u) {
            ram_halfword(audio,at,0); ++audio->bootstrap_halfwords;
        }
    }
    reverb_init(&audio->reverb);
    audio->backend = *backend;
    audio->owner_thread = token;
    audio->epoch = epoch;
    audio->cycle = epoch;
    audio->queued_frames = queued;
    audio->initialized = 1;
    audio->initializing = 0;
    return 1;
}

int musashi_spu_cd_audio_advance(MusashiSpuCdAudio *audio,
                                 uint64_t absolute_cycles) {
    uint64_t target_sample;
    uint64_t due;
    /* Fast path: no sample is due before the next MUSASHI_SPU_CD_AUDIO_SAMPLE_CYCLES
     * boundary, and the clock advances every instruction. owner_thread_ok
     * asks the host backend whether it is still playing (a locked SDL call);
     * doing that per instruction dominated native CPU time. The full check
     * still runs whenever a sample is due. */
    if (audio && audio->initialized && !audio->faulted && absolute_cycles >= audio->cycle &&
        absolute_cycles >= audio->epoch &&
        (absolute_cycles - audio->epoch) / MUSASHI_SPU_CD_AUDIO_SAMPLE_CYCLES == audio->sample_index &&
        audio->backend.thread_token &&
        audio->backend.thread_token(audio->backend.userdata) == audio->owner_thread) {
        audio->cycle = absolute_cycles;
        return 1;
    }
    if (!owner_thread_ok(audio) || absolute_cycles < audio->cycle) {
        if (audio && audio->initialized && absolute_cycles < audio->cycle)
            audio->faulted = 1;
        return 0;
    }
    target_sample = (absolute_cycles - audio->epoch) /
                    MUSASHI_SPU_CD_AUDIO_SAMPLE_CYCLES;
    if (target_sample < audio->sample_index) {
        audio->faulted = 1;
        return 0;
    }
    due = target_sample - audio->sample_index;
    if (due > MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT) {
        /* UINT64_MAX and other overflow-scale jumps still refuse. A host-time
         * cut within the bound below drops PCM and keeps the epoch. */
        if (due > 44100ull * 60ull) {
            audio->faulted = 1;
            return 0;
        }
        discard_pending_before(audio, target_sample);
        audio->sample_index = target_sample;
        audio->cycle = absolute_cycles;
        return 1;
    }
    /* A sample is due only every MUSASHI_SPU_CD_AUDIO_SAMPLE_CYCLES, but the
     * clock advances every instruction. Querying the host backend (a locked
     * SDL call) when nothing is due dominated native CPU time. */
    if (due == 0) {
        audio->cycle = absolute_cycles;
        return 1;
    }
    if (!backend_status(audio))
        return 0;
    {
        size_t space = MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT - audio->queued_frames;
        if (due > space) {
            audio->sample_index += due - space;
            discard_pending_before(audio, audio->sample_index);
            due = space;
        }
    }
    if (due == 0) {
        audio->cycle = absolute_cycles;
        return 1;
    }
    while (due != 0) {
        int16_t pcm[PCM_CHUNK * 2];
        SpuVoices saved_voices=audio->voices;
        SpuReverb saved_reverb=audio->reverb;
        int16_t saved_main[2];
        int saved_main_valid[2];
        size_t saved_head = audio->pending_head;
        size_t saved_count = audio->pending_count;
        uint64_t saved_sample = audio->sample_index;
        size_t frames = due > PCM_CHUNK ? PCM_CHUNK : (size_t)due;
        size_t frame;
        audio->undo_count=0; audio->staging_ram=1;
        memcpy(saved_main, audio->current_main, sizeof(saved_main));
        memcpy(saved_main_valid, audio->pending_main_valid,
               sizeof(saved_main_valid));
        for (frame = 0; frame < frames && !audio->faulted; ++frame)
            render_sample(audio, audio->sample_index + frame,
                          &pcm[frame * 2u]);
        if (audio->faulted || !queue_chunk(audio, pcm, frames)) {
            /* Stage voice/capture and input/gain evolution as one sample transaction. A refused queue owns no
             * PCM, so leave the chunk pending; accepted external PCM and its
             * committed sample boundary cannot be rolled back. */
            if (audio->sample_index == saved_sample) {
                audio->voices=saved_voices;
                audio->reverb=saved_reverb;
                ram_rollback(audio);
                audio->pending_head = saved_head;
                audio->pending_count = saved_count;
                memcpy(audio->current_main, saved_main, sizeof(saved_main));
                memcpy(audio->pending_main_valid, saved_main_valid,
                       sizeof(saved_main_valid));
            }
            audio->staging_ram=0; audio->undo_count=0;
            return 0;
        }
        audio->staging_ram=0; audio->undo_count=0;
        due -= frames;
    }
    audio->cycle = absolute_cycles;
    return 1;
}

int musashi_spu_cd_audio_read16(void *userdata,
                                uint32_t address, uint16_t *value) {
    MusashiSpuCdAudio *audio = userdata;
    if (!value || !owner_thread_ok(audio)) return 0;
    if (address == 0x1f801db8u) {
        *value = (uint16_t)audio->current_main[MAIN_LEFT];
        return 1;
    }
    if (address == 0x1f801dbau) {
        *value = (uint16_t)audio->current_main[MAIN_RIGHT];
        return 1;
    }
    if(address>=0x1f801c00u && address<0x1f801d80u && !(address&1u)) {
        unsigned index=(address-0x1f801c00u)/16u, reg=(address&15u)/2u;
        SpuVoice *v=&audio->voices.voice[index];
        *value=reg==6 ? (uint16_t)v->envelope : v->reg[reg]; return 1;
    }
    if(address>=0x1f801dc0u && address<=0x1f801dfeu && !(address&1u)) {
        *value=audio->reverb.reg[(address-0x1f801dc0u)/2u];return 1;
    }
    switch(address) {
    case 0x1f801d84u: *value=(uint16_t)audio->reverb.gain[0];return 1;
    case 0x1f801d86u: *value=(uint16_t)audio->reverb.gain[1];return 1;
    case 0x1f801da2u: *value=audio->reverb.base;return 1;
    case 0x1f801d98u: *value=(uint16_t)audio->reverb.eon;return 1;
    case 0x1f801d9au: *value=(uint16_t)(audio->reverb.eon>>16);return 1;
    case 0x1f801daau: *value=audio->control; return 1;
    case 0x1f801daeu:
        /* No DMA/IRQ or outstanding manual job is admitted. Capture position
         * is real sample progress; PSX-SPX documents bit11 gated by DAC bits2/3.
         * Capture starts at byte0 at the owned epoch. Mode commits immediately. */
        *value=(uint16_t)((audio->control&0x3fu)|
            ((audio->transfer_control&0xcu) && (audio->sample_index&256u)?0x800u:0));
        return 1;
    case 0x1f801da6u: *value=audio->transfer_address; return 1;
    case 0x1f801dacu: *value=audio->transfer_control; return 1;
    case 0x1f801d9cu: *value=(uint16_t)audio->voices.endx; return 1;
    case 0x1f801d9eu: *value=(uint16_t)(audio->voices.endx>>16); return 1;
    default: return 0;
    }
}

int musashi_spu_cd_audio_write16(void *userdata,
                                 uint32_t address, uint16_t value) {
    MusashiSpuCdAudio *audio = userdata;
    int channel;
    if (!owner_thread_ok(audio)) return 0;
    if (address == 0x1f801d80u || address == 0x1f801d82u) {
        if (value & 0x8000u) return 0;
        channel = address == 0x1f801d80u ? MAIN_LEFT : MAIN_RIGHT;
        audio->configured_main[channel] = fixed_gain(value);
        audio->pending_main[channel] = audio->configured_main[channel];
        audio->pending_main_valid[channel] = 1;
        return 1;
    }
    if (address == 0x1f801db0u || address == 0x1f801db2u) {
        channel = address == 0x1f801db0u ? CD_LEFT : CD_RIGHT;
        audio->cd_gain[channel] = (int16_t)value;
        return 1;
    }
    if(address>=0x1f801c00u && address<0x1f801d80u && !(address&1u)) {
        unsigned index=(address-0x1f801c00u)/16u, reg=(address&15u)/2u;
        SpuVoice *v=&audio->voices.voice[index];
        if(reg<2) { if(value&0x8000u) return 0; v->gain[reg]=fixed_gain(value); }
        if(reg==2 && value>0x3fff) return 0;
        if((reg==3 || reg==7) && (value&1u)) return 0;
        /* Live ADSR fields feed the existing phase/counter on its next sample.
         * Register stores do not manufacture a key transition or reset time. */
        if(reg==6) { if(value>0x7fff) return 0; v->envelope=value; }
        v->reg[reg]=value;
        if(reg==7) v->repeat=(uint32_t)value*8u;
        return 1;
    }
    if(address==0x1f801daau) {
        unsigned i;
        /* Bits 4-5 are SRAM transfer mode (0=stop,1=manual,2=DMA write,3=DMA
         * read). Retail 8003AB84 writes bit5 (0x20) for DMA-write. */
        if(value&(uint16_t)~0xc0b5u) return 0;
        if((value&0x80u) && !reverb_supported(&audio->reverb)) return 0;
        /* Hardware-test-informed enabled->disabled transition; preserve CD. */
        if((audio->control&0x8000u) && !(value&0x8000u))
            for(i=0;i<24;++i) voice_off(&audio->voices.voice[i]);
        audio->control=value; return 1;
    }
    if(address==0x1f801da6u) {
        audio->transfer_address=value; audio->transfer_cursor=(uint32_t)value*8u;
        audio->transfer_address_valid=1; return 1;
    }
    if(address==0x1f801dacu) {
        if(value!=4) return 0;
        audio->transfer_control=value; return 1;
    }
    if(address==0x1f801da8u) {
        if(!audio->transfer_address_valid || audio->transfer_control!=4 ||
           audio->manual_halfwords==UINT64_MAX) return 0;
        ram_halfword(audio,audio->transfer_cursor,value);
        audio->transfer_cursor=(audio->transfer_cursor+2u)&0x7ffffu;
        ++audio->manual_halfwords; return 1;
    }
    if(address==0x1f801d88u || address==0x1f801d8au ||
       address==0x1f801d8cu || address==0x1f801d8eu) {
        uint32_t mask=(uint32_t)(value&((address&2u)?0xffu:0xffffu))<<((address&2u)?16:0);
        if(address<0x1f801d8cu) {
            unsigned i; uint8_t block[16];
            for(i=0;i<24;++i) if(mask&(1u<<i)) {
                SpuVoice *v=&audio->voices.voice[i];
                if(!voice_supported(v) || !read_voice_block(audio,(uint32_t)v->reg[3]*8u,block) ||
                   (block[0]>>4)!=0 || (block[0]&15u)>12 || (block[1]&0xf8u)) return 0;
            }
            audio->voices.key_on|=mask;
        } else audio->voices.key_off|=mask;
        return 1;
    }
    if(address==0x1f801d84u || address==0x1f801d86u) {
        audio->reverb.gain[(address-0x1f801d84u)/2u]=(int16_t)value;return 1;
    }
    if(address==0x1f801d98u || address==0x1f801d9au) {
        unsigned shift=(address&2u)?16u:0u;
        uint32_t mask=(shift?0xffu:0xffffu)<<shift;
        audio->reverb.eon=(audio->reverb.eon&~mask)|(((uint32_t)value<<shift)&mask);
        return 1;
    }
    if(address==0x1f801da2u ||
       (address>=0x1f801dc0u && address<=0x1f801dfeu && !(address&1u))) {
        SpuReverb staged=audio->reverb;
        if(address==0x1f801da2u) { staged.base=value;staged.cursor=(uint32_t)value*8u; }
        else staged.reg[(address-0x1f801dc0u)/2u]=value;
        if((audio->control&0x80u) && !reverb_supported(&staged)) return 0;
        audio->reverb=staged;return 1;
    }
    switch(address) {
    case 0x1f801d90u: case 0x1f801d92u: case 0x1f801d94u: case 0x1f801d96u:
    case 0x1f801db4u: case 0x1f801db6u:
        return value==0; /* Only disabled processors are owned in this subset. */
    case 0x1f801d9cu: case 0x1f801d9eu:
        return value==0; /* Read-only ENDX: never substitute written status. */
    default: return 0;
    }
}

int musashi_spu_cd_audio_submit_cd_pcm(MusashiSpuCdAudio *audio,
                                       uint64_t first_sample,
                                       const int16_t *stereo_pcm,
                                       size_t frames) {
    size_t index;
    size_t bytes;
    uintptr_t pointer;
    uint64_t last;
    if (!owner_thread_ok(audio) || (!stereo_pcm && frames != 0) ||
        frames > MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT - audio->pending_count ||
        first_sample < audio->sample_index)
        return 0;
    if (frames == 0) return 1;
    if (first_sample > UINT64_MAX - (frames - 1u)) return 0;
    if (frames > SIZE_MAX / (sizeof(int16_t) * 2u)) return 0;
    pointer = (uintptr_t)stereo_pcm;
    bytes = frames * sizeof(int16_t) * 2u;
    if (pointer % sizeof(int16_t) != 0 || pointer > UINTPTR_MAX - bytes)
        return 0;
    if (audio->pending_count != 0) {
        last = audio->pending[audio->pending_head + audio->pending_count - 1u].sample;
        if (last == UINT64_MAX || first_sample <= last) return 0;
    }
    if (audio->pending_head + audio->pending_count + frames >
        MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT) {
        memmove(audio->pending, audio->pending + audio->pending_head,
                audio->pending_count * sizeof(audio->pending[0]));
        audio->pending_head = 0;
    }
    for (index = 0; index < frames; ++index) {
        PendingPcmFrame *pending = &audio->pending[audio->pending_head +
                                                     audio->pending_count + index];
        pending->sample = first_sample + index;
        pending->left = stereo_pcm[index * 2u];
        pending->right = stereo_pcm[index * 2u + 1u];
    }
    audio->pending_count += frames;
    return 1;
}

int musashi_spu_cd_audio_snapshot(const MusashiSpuCdAudio *audio,
                                  MusashiSpuCdAudioSnapshot *snapshot) {
    if (!audio || !snapshot || !audio->initialized ||
        !owner_thread_for_cleanup(audio)) return 0;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->main_left_configured = audio->configured_main[MAIN_LEFT];
    snapshot->main_right_configured = audio->configured_main[MAIN_RIGHT];
    snapshot->main_left_current = audio->current_main[MAIN_LEFT];
    snapshot->main_right_current = audio->current_main[MAIN_RIGHT];
    snapshot->cd_left_gain = audio->cd_gain[CD_LEFT];
    snapshot->cd_right_gain = audio->cd_gain[CD_RIGHT];
    snapshot->control = audio->control;
    snapshot->cycle = audio->cycle;
    snapshot->sample_index = audio->sample_index;
    snapshot->pending_frames = audio->pending_count;
    snapshot->queued_frames = audio->queued_frames;
    snapshot->submitted_frames = audio->submitted_frames;
    snapshot->faulted = audio->faulted;
    snapshot->transfer_cursor=audio->transfer_cursor;
    snapshot->transfer_address=audio->transfer_address;
    snapshot->transfer_control=audio->transfer_control;
    snapshot->transfer_address_valid=audio->transfer_address_valid;
    snapshot->manual_halfwords=audio->manual_halfwords;
    snapshot->bootstrap_halfwords=audio->bootstrap_halfwords;
    return 1;
}

int musashi_spu_cd_audio_copy_ram(const MusashiSpuCdAudio *a, uint32_t offset,
                                void *dst, size_t length) {
    if(!a || (!dst && length) || !a->initialized || !owner_thread_for_cleanup(a) ||
       !ram_initialized(a,offset,length)) return 0;
    if(length) memcpy(dst,a->ram+offset,length);
    return 1;
}
int musashi_spu_cd_audio_voice_snapshot(const MusashiSpuCdAudio *a, unsigned i,
                                      MusashiSpuVoiceSnapshot *out) {
    const SpuVoice *v;
    if(!a || !out || i>=24 || !a->initialized || !owner_thread_for_cleanup(a)) return 0;
    v=&a->voices.voice[i]; memset(out,0,sizeof(*out));
    memcpy(out->registers,v->reg,sizeof(out->registers)); out->registers[6]=(uint16_t)v->envelope;
    out->gain_left=v->gain[0]; out->gain_right=v->gain[1];
    out->phase=v->phase; out->envelope=v->envelope; out->cursor=v->cursor; out->repeat=v->repeat;
    out->pitch_fraction=v->fraction; out->decoded_blocks=v->blocks; out->decoded_samples=v->samples;
    out->pending_on=!!(a->voices.key_on&(1u<<i)); out->pending_off=!!(a->voices.key_off&(1u<<i));
    out->endx=!!(a->voices.endx&(1u<<i)); return 1;
}

int musashi_spu_cd_audio_reverb_snapshot(const MusashiSpuCdAudio *a,
                                        MusashiSpuReverbSnapshot *out) {
    const SpuReverb *r; unsigned i;
    if(!a || !out || !a->initialized || !owner_thread_for_cleanup(a)) return 0;
    r=&a->reverb;memset(out,0,sizeof(*out));
    memcpy(out->coefficients,r->reg,sizeof(r->reg));out->base=r->base;
    out->wet_left=r->gain[0];out->wet_right=r->gain[1];
    out->cursor=r->cursor;out->eon=r->eon;
    out->next_channel=r->frames?(unsigned)((r->frames-1)&1u):0;
    out->frames=r->frames;out->processed_left=r->processed[0];out->processed_right=r->processed[1];
    out->ram_reads=r->reads;out->ram_writes=r->writes;
    out->unknown_reads=r->unknown;out->unsupported_steps=r->unsupported;
    for(i=0;i<REVERB_TAPS;++i) {
        out->unknown_history_left+=!(r->up_valid[0]&(UINT64_C(1)<<i));
        out->unknown_history_right+=!(r->up_valid[1]&(UINT64_C(1)<<i));
    }
    out->enabled=!!(a->control&0x80u);out->first_unknown_address=r->first_unknown;
    out->access_count=r->access_count;
    memcpy(out->accesses,r->accesses,r->access_count*sizeof(r->accesses[0]));
    return 1;
}

int musashi_spu_cd_audio_destroy(MusashiSpuCdAudio *audio) {
    if (!audio) return 0;
    if (audio->initializing || (audio->initialized &&
        !owner_thread_for_cleanup(audio))) return 0;
    free(audio);
    return 1;
}
