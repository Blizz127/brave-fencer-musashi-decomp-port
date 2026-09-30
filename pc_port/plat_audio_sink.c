/* SPU core -> bfm_plat_audio adapter (musashi_plat_audio_sink.h). */
#include "musashi_plat_audio_sink.h"

#include <string.h>

#include "bfm_plat_audio.h"

static uint64_t token(MusashiPlatAudioSink *a) {
    return a->current_thread ? a->current_thread(a->thread_user) : 0;
}

static int sink_ready(void *user) {
    MusashiPlatAudioSink *a = user;
    return a && !a->faulted && a->owner && token(a) == a->owner &&
           bfm_plat_audio_active() != NULL;
}

static uint64_t sink_thread(void *user) { return token(user); }

static int sink_status(void *user, size_t *queued, int *playing) {
    MusashiPlatAudioSink *a = user;
    BfmPlatAudioStatus st;
    if (!sink_ready(a) || !queued || !playing || bfm_plat_audio_status(&st) != BFM_PLAT_OK ||
        st.faulted) {
        if (a) a->faulted = 1;
        return 0;
    }
    *queued = st.queued_frames[BFM_AUDIO_STREAM_MAIN];
    *playing = 1;   /* a paused bfm_plat device still accepts PCM */
    return 1;
}

static int sink_queue(void *user, const int16_t *pcm, size_t frames) {
    MusashiPlatAudioSink *a = user;
    size_t i;
    if (!sink_ready(a) || !pcm) return 0;
    for (i = 0; i < frames; i++) {
        int l = pcm[2 * i], r = pcm[2 * i + 1];
        int m = l < 0 ? -l : l, n = r < 0 ? -r : r;
        if (l || r) a->nonsilent++;
        if (m > a->peak) a->peak = m;
        if (n > a->peak) a->peak = n;
    }
    if (bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, pcm, frames) != BFM_PLAT_OK) {
        a->faulted = 1;
        return 0;
    }
    a->frames += frames;
    return 1;
}

const MusashiSpuCdAudioBackend *musashi_plat_audio_sink_init(
    MusashiPlatAudioSink *sink, uint64_t (*current_thread)(void *user), void *user) {
    if (!sink || !current_thread) return NULL;
    memset(sink, 0, sizeof *sink);
    sink->current_thread = current_thread;
    sink->thread_user = user;
    sink->owner = current_thread(user);
    sink->backend = (MusashiSpuCdAudioBackend){sink, sink_ready, sink_thread,
                                               sink_status, sink_queue};
    return sink->owner ? &sink->backend : NULL;
}
