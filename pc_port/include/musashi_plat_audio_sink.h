#ifndef MUSASHI_PLAT_AUDIO_SINK_H
#define MUSASHI_PLAT_AUDIO_SINK_H

/* MUSASHI_AUDIO=bfm_plat: a MusashiSpuCdAudioBackend over bfm_plat_audio
 * (pc_port/platform), so the in-house SPU core's 44.1 kHz stereo output goes
 * to whichever bfm_plat audio backend is open. It also counts what it
 * forwarded (frames, non-silent frames, peak). */

#include <stddef.h>
#include <stdint.h>

#include "musashi_spu_cd_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MusashiPlatAudioSink {
    MusashiSpuCdAudioBackend backend;
    uint64_t (*current_thread)(void *user);
    void *thread_user;
    uint64_t owner, frames, nonsilent;
    int peak, faulted;
} MusashiPlatAudioSink;

/* Binds the sink to the calling thread (current_thread's token); the
 * bfm_plat audio device must already be open. Returns sink->backend. */
const MusashiSpuCdAudioBackend *musashi_plat_audio_sink_init(
    MusashiPlatAudioSink *sink, uint64_t (*current_thread)(void *user), void *user);

#ifdef __cplusplus
}
#endif

#endif
