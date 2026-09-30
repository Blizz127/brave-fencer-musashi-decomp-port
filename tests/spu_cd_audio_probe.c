#include "../pc_port/include/musashi_spu_cd_audio.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Sink {
    uint64_t token;
    int ready;
    int playing;
    int fail_queue;
    size_t fail_queue_call;
    size_t queue_calls;
    size_t queued;
    size_t frames;
    int16_t pcm[2048 * 2];
    MusashiSpuCdAudio *reenter_audio;
    int reenter_status;
    int reenter_queue;
} Sink;

static int sink_ready(void *userdata) {
    return ((Sink *)userdata)->ready;
}

static uint64_t sink_thread(void *userdata) {
    return ((Sink *)userdata)->token;
}

static int sink_status(void *userdata, size_t *queued, int *playing) {
    Sink *sink = userdata;
    if (!queued || !playing) return 0;
    if (sink->reenter_status && sink->reenter_audio) {
        uint16_t value = 0;
        sink->reenter_status = 0;
        assert(!musashi_spu_cd_audio_read16(sink->reenter_audio,
                                            0x1f801db8u, &value));
    }
    *queued = sink->queued;
    *playing = sink->playing;
    return 1;
}

static int sink_queue(void *userdata, const int16_t *pcm, size_t frames) {
    Sink *sink = userdata;
    ++sink->queue_calls;
    if (sink->fail_queue || sink->queue_calls == sink->fail_queue_call ||
        !pcm || frames > 2048u - sink->frames) return 0;
    memcpy(&sink->pcm[sink->frames * 2u], pcm, frames * 2u * sizeof(*pcm));
    sink->frames += frames;
    sink->queued = 0; /* The checked recording sink drains immediately. */
    if (sink->reenter_queue && sink->reenter_audio) {
        uint16_t value = 0xa55au;
        sink->reenter_queue = 0;
        assert(!musashi_spu_cd_audio_read16(sink->reenter_audio,
                                           0x1f801db8u, &value));
        assert(value == 0xa55au);
    }
    return 1;
}

static MusashiSpuCdAudioBackend backend_for(Sink *sink) {
    MusashiSpuCdAudioBackend backend = {
        sink, sink_ready, sink_thread, sink_status, sink_queue
    };
    return backend;
}

static MusashiSpuCdAudio *new_audio(Sink *sink) {
    MusashiSpuCdAudio *audio = musashi_spu_cd_audio_create();
    MusashiSpuCdAudioBackend backend = backend_for(sink);
    assert(audio != NULL);
    assert(musashi_spu_cd_audio_init_bios_muted(audio, &backend, 0));
    return audio;
}

static MusashiSpuCdAudioSnapshot snapshot(MusashiSpuCdAudio *audio) {
    MusashiSpuCdAudioSnapshot state;
    assert(musashi_spu_cd_audio_snapshot(audio, &state));
    return state;
}

static void test_mute_phase_and_mixer(void) {
    Sink sink = {.token = 1, .ready = 1, .playing = 1};
    MusashiSpuCdAudio *audio = new_audio(&sink);
    MusashiSpuCdAudioBackend backend = backend_for(&sink);
    MusashiSpuCdAudioSnapshot before, after;
    int16_t sample[2] = {16384, -16384};
    uint16_t value = 0xffffu;

    before = snapshot(audio);
    assert(before.main_left_current == 0 && before.main_right_current == 0);
    assert(!musashi_spu_cd_audio_read16(audio, 0x1f801dbcu, &value));
    assert(value == 0xffffu);
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d80u, 0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d82u, 0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801db0u, 0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801db2u, 0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801daau, 0xc001u));
    assert(musashi_spu_cd_audio_read16(audio, 0x1f801db8u, &value));
    assert(value == 0u); /* fixed gains publish at the sample boundary */
    assert(!musashi_spu_cd_audio_init_bios_muted(audio, &backend, 0));
    before = snapshot(audio);
    assert(musashi_spu_cd_audio_read16(audio, 0x1f801dbau, &value));
    after = snapshot(audio);
    assert(after.cycle == before.cycle && after.sample_index == before.sample_index);
    assert(musashi_spu_cd_audio_submit_cd_pcm(audio, 0, sample, 1));
    assert(musashi_spu_cd_audio_advance(audio, 768));
    assert(sink.frames == 1u);
    assert(sink.pcm[0] == 8190 && sink.pcm[1] == -8192);
    assert(musashi_spu_cd_audio_read16(audio, 0x1f801db8u, &value));
    assert(value == 0x7ffeu);
    after = snapshot(audio);
    assert(after.submitted_frames == 1u && after.sample_index == 1u);

    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d80u, 0));
    assert(musashi_spu_cd_audio_read16(audio, 0x1f801db8u, &value));
    assert(value == 0x7ffeu);
    assert(musashi_spu_cd_audio_advance(audio, 1536));
    assert(musashi_spu_cd_audio_read16(audio, 0x1f801db8u, &value));
    assert(value == 0u);
    musashi_spu_cd_audio_destroy(audio);
}

static void test_gate_signed_channels_and_refusal(void) {
    Sink sink = {.token = 1, .ready = 1, .playing = 1};
    MusashiSpuCdAudio *audio = new_audio(&sink);
    MusashiSpuCdAudioSnapshot before, after;
    int16_t sample[4] = {10000, -10000, -10000, 10000};
    uint16_t value = 0xa55au;

    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d80u, 0x2000u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d82u, 0x1000u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801db0u, 0x7fffu));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801db2u, 0x8001u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801daau, 0xc000u));
    assert(musashi_spu_cd_audio_submit_cd_pcm(audio, 0, sample, 2));
    assert(musashi_spu_cd_audio_advance(audio, 768u * 2u));
    assert(sink.pcm[0] == 0 && sink.pcm[1] == 0);
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801daau, 0x8001u));
    before = snapshot(audio);
    assert(!musashi_spu_cd_audio_write16(audio, 0x1f801d80u, 0x8000u));
    assert(!musashi_spu_cd_audio_write16(audio, 0x1f801daau, 0x0002u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d84u, 0x1234u));
    assert(!musashi_spu_cd_audio_write16(audio, 0x1f801db4u, 0x1234u));
    after = snapshot(audio);
    assert(after.main_left_configured == before.main_left_configured);
    assert(after.control == before.control);
    assert(!musashi_spu_cd_audio_read16(audio, 0x1f801dbcu, &value));
    assert(value == 0xa55au);
    assert(musashi_spu_cd_audio_submit_cd_pcm(audio, 2, sample, 2));
    assert(musashi_spu_cd_audio_advance(audio, 768u * 4u));
    assert(sink.pcm[4] != 0 || sink.pcm[5] != 0);
    musashi_spu_cd_audio_destroy(audio);

    sink = (Sink){.token = 1, .ready = 1, .playing = 1};
    audio = new_audio(&sink);
    sample[0] = -32768;
    sample[1] = -32768;
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d80u, 0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d82u, 0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801db0u, 0x8000u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801db2u, 0x8000u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801daau, 0xc001u));
    assert(musashi_spu_cd_audio_submit_cd_pcm(audio, 0, sample, 1));
    assert(musashi_spu_cd_audio_advance(audio, 768));
    assert(sink.pcm[0] == 32765 && sink.pcm[1] == 32765);
    musashi_spu_cd_audio_destroy(audio);
}

static void test_clock_queue_thread_and_overflow_refusals(void) {
    Sink sink = {.token = 1, .ready = 1, .playing = 1};
    MusashiSpuCdAudio *audio = new_audio(&sink);
    MusashiSpuCdAudioSnapshot before, after;
    int16_t sample[2] = {1, 1};

    assert(!musashi_spu_cd_audio_advance(audio, UINT64_MAX));
    assert(snapshot(audio).faulted);
    musashi_spu_cd_audio_destroy(audio);

    sink = (Sink){.token = 1, .ready = 1, .playing = 1};
    audio = new_audio(&sink);
    assert(musashi_spu_cd_audio_advance(audio, 768));
    assert(!musashi_spu_cd_audio_advance(audio, 767));
    assert(snapshot(audio).faulted);
    musashi_spu_cd_audio_destroy(audio);

    /* Live boot STOP: SDL queue already full on a 1-sample paced cut.
     * Drop the surplus and keep the epoch so CD/GPU keep moving. */
    sink = (Sink){.token = 1, .ready = 1, .playing = 1, .queued = 8192u};
    audio = new_audio(&sink);
    {
        MusashiSpuCdAudioSnapshot state;
        assert(musashi_spu_cd_audio_advance(audio, 768));
        state = snapshot(audio);
        assert(!state.faulted && state.sample_index == 1 && state.cycle == 768);
        assert(sink.queue_calls == 0);
    }
    musashi_spu_cd_audio_destroy(audio);

    /* A host-time cut can request more samples than the SDL queue holds.
     * Catch up the sample index without faulting so CD/GPU keep moving. */
    sink = (Sink){.token = 1, .ready = 1, .playing = 1, .queued = 8000u};
    audio = new_audio(&sink);
    {
        uint64_t samples = (uint64_t)MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT + 100u;
        MusashiSpuCdAudioSnapshot state;
        assert(musashi_spu_cd_audio_advance(audio, samples * 768u));
        state = snapshot(audio);
        assert(!state.faulted && state.sample_index == samples);
        assert(state.cycle == samples * 768u);
    }
    musashi_spu_cd_audio_destroy(audio);

    sink = (Sink){.token = 1, .ready = 1, .playing = 1};
    audio = new_audio(&sink);
    assert(musashi_spu_cd_audio_submit_cd_pcm(audio, 0, sample, 1));
    before = snapshot(audio);
    assert(!musashi_spu_cd_audio_submit_cd_pcm(audio, 0, sample, 1));
    assert(!musashi_spu_cd_audio_submit_cd_pcm(audio, UINT64_MAX, sample, 2));
    after = snapshot(audio);
    assert(after.pending_frames == before.pending_frames);
    sink.token = 2;
    assert(!musashi_spu_cd_audio_advance(audio, 0));
    assert(!musashi_spu_cd_audio_snapshot(audio, &after));
    sink.token = 1;
    assert(snapshot(audio).faulted);
    assert(musashi_spu_cd_audio_destroy(audio));

    sink = (Sink){.token = 1, .ready = 1, .playing = 1};
    audio = new_audio(&sink);
    sink.token = 2;
    assert(!musashi_spu_cd_audio_destroy(audio));
    sink.token = 1;
    assert(musashi_spu_cd_audio_destroy(audio));

    sink = (Sink){.token = 1, .ready = 1, .playing = 1, .fail_queue = 1};
    audio = new_audio(&sink);
    assert(!musashi_spu_cd_audio_advance(audio, 768));
    assert(snapshot(audio).faulted);
    musashi_spu_cd_audio_destroy(audio);
}

static void test_backend_init_rejections(void) {
    Sink sink = {.token = 0, .ready = 0, .playing = 1};
    MusashiSpuCdAudioBackend backend = backend_for(&sink);
    MusashiSpuCdAudio *audio = musashi_spu_cd_audio_create();
    assert(audio != NULL);
    assert(!musashi_spu_cd_audio_init_bios_muted(audio, &backend, 0));
    sink.ready = 1;
    sink.playing = 0;
    assert(!musashi_spu_cd_audio_init_bios_muted(audio, &backend, 0));
    musashi_spu_cd_audio_destroy(audio);
}

static void test_callback_reentry_is_refused(void) {
    Sink sink = {.token = 1, .ready = 1, .playing = 1};
    MusashiSpuCdAudio *audio = new_audio(&sink);
    sink.reenter_audio = audio;
    sink.reenter_status = 1;
    assert(!musashi_spu_cd_audio_advance(audio, 768));
    assert(snapshot(audio).faulted);
    assert(musashi_spu_cd_audio_destroy(audio));
}

static void test_partial_queue_progress(void) {
    Sink sink = {.token = 1, .ready = 1, .playing = 1, .fail_queue_call = 2};
    MusashiSpuCdAudio *audio = new_audio(&sink);
    MusashiSpuCdAudioSnapshot state;
    int16_t pcm[2048 * 2];
    size_t index;
    for (index = 0; index < 2048u; ++index) {
        pcm[index * 2u] = (int16_t)index;
        pcm[index * 2u + 1u] = (int16_t)-(int)index;
    }
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d80u, 0x3fffu));
    assert(musashi_spu_cd_audio_submit_cd_pcm(audio, 0, pcm, 2048));
    assert(!musashi_spu_cd_audio_advance(audio, 768u * 2048u + 31u));
    state = snapshot(audio);
    assert(state.faulted && sink.queue_calls == 2 && sink.frames == 1024);
    assert(state.sample_index == 1024 && state.submitted_frames == 1024);
    assert(state.cycle == 768u * 1024u && state.pending_frames == 1024);
    assert(state.main_left_current == 0x7ffe);
    assert(!musashi_spu_cd_audio_advance(audio, 768u * 2048u + 31u));
    assert(sink.queue_calls == 2);
    assert(musashi_spu_cd_audio_destroy(audio));

    sink = (Sink){.token = 1, .ready = 1, .playing = 1, .fail_queue = 1};
    audio = new_audio(&sink);
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d80u, 0x3fffu));
    assert(musashi_spu_cd_audio_submit_cd_pcm(audio, 0, pcm, 2));
    assert(!musashi_spu_cd_audio_advance(audio, 768u * 2u));
    state = snapshot(audio);
    assert(state.faulted && state.pending_frames == 2);
    assert(state.sample_index == 0 && state.submitted_frames == 0 && state.cycle == 0);
    assert(state.main_left_current == 0 && state.main_left_configured == 0x7ffe);
    assert(musashi_spu_cd_audio_destroy(audio));

    sink = (Sink){.token = 1, .ready = 1, .playing = 1, .reenter_queue = 1};
    audio = new_audio(&sink);
    sink.reenter_audio = audio;
    assert(musashi_spu_cd_audio_submit_cd_pcm(audio, 0, pcm, 2));
    assert(!musashi_spu_cd_audio_advance(audio, 768u * 2u + 31u));
    state = snapshot(audio);
    assert(state.faulted && sink.frames == 2 && sink.queue_calls == 1);
    assert(state.sample_index == 2 && state.submitted_frames == 2);
    assert(state.cycle == 768u * 2u && state.pending_frames == 0);
    assert(musashi_spu_cd_audio_destroy(audio));
}

/* Queue-pressure drops must retire only samples in the skipped interval.
 * Compare retained audio with a no-drop run, including a future frame. */
static void test_pending_pcm_across_capacity_drop(void) {
    const int16_t pcm[10] = {1000,-1000,2000,-2000,3000,-3000,
                              4000,-4000,5000,-5000};
    Sink reference = {.token=1,.ready=1,.playing=1};
    MusashiSpuCdAudio *baseline = new_audio(&reference);
    unsigned space;
    assert(musashi_spu_cd_audio_write16(baseline,0x1f801d80u,0x3fffu));
    assert(musashi_spu_cd_audio_write16(baseline,0x1f801d82u,0x3fffu));
    assert(musashi_spu_cd_audio_write16(baseline,0x1f801db0u,0x3fffu));
    assert(musashi_spu_cd_audio_write16(baseline,0x1f801db2u,0x3fffu));
    assert(musashi_spu_cd_audio_write16(baseline,0x1f801daau,0xc001u));
    assert(musashi_spu_cd_audio_submit_cd_pcm(baseline,0,pcm,5));
    assert(musashi_spu_cd_audio_advance(baseline,5u*768u));
    assert(reference.frames==5 && reference.pcm[0]!=0);
    musashi_spu_cd_audio_destroy(baseline);
    /* Four slots: no drop. Two slots: skip two, keep the remaining two.
     * Zero slots: retire all four, then resume with the future fifth. */
    for(space=4;;space-=2) {
        Sink sink={.token=1,.ready=1,.playing=1};
        MusashiSpuCdAudio *audio;
        MusashiSpuCdAudioSnapshot state;
        sink.queued=MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT-space;
        audio=new_audio(&sink);
        assert(musashi_spu_cd_audio_write16(audio,0x1f801d80u,0x3fffu));
        assert(musashi_spu_cd_audio_write16(audio,0x1f801d82u,0x3fffu));
        assert(musashi_spu_cd_audio_write16(audio,0x1f801db0u,0x3fffu));
        assert(musashi_spu_cd_audio_write16(audio,0x1f801db2u,0x3fffu));
        assert(musashi_spu_cd_audio_write16(audio,0x1f801daau,0xc001u));
        assert(musashi_spu_cd_audio_submit_cd_pcm(audio,0,pcm,5));
        assert(musashi_spu_cd_audio_advance(audio,4u*768u));
        state=snapshot(audio);
        assert(!state.faulted && state.sample_index==4 && state.pending_frames==1);
        assert(state.submitted_frames==space && sink.frames==space);
        assert(!memcmp(sink.pcm,reference.pcm+(4-space)*2,space*2*sizeof(int16_t)));
        sink.queued=0;
        assert(musashi_spu_cd_audio_advance(audio,5u*768u));
        state=snapshot(audio);
        assert(!state.faulted && state.pending_frames==0 && state.sample_index==5);
        assert(sink.frames==space+1);
        assert(!memcmp(sink.pcm+space*2,reference.pcm+8,2*sizeof(int16_t)));
        musashi_spu_cd_audio_destroy(audio);
        if(!space) break;
    }
}

static void test_pending_pcm_across_large_skip(void) {
    Sink sink={.token=1,.ready=1,.playing=1};
    MusashiSpuCdAudio *audio=new_audio(&sink);
    MusashiSpuCdAudioSnapshot state;
    const uint64_t target=MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT+1u;
    const int16_t pcm[6]={1000,-1000,2000,-2000,3000,-3000};
    assert(musashi_spu_cd_audio_write16(audio,0x1f801d80u,0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio,0x1f801d82u,0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio,0x1f801db0u,0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio,0x1f801db2u,0x3fffu));
    assert(musashi_spu_cd_audio_write16(audio,0x1f801daau,0xc001u));
    assert(musashi_spu_cd_audio_submit_cd_pcm(audio,target-1,pcm,3));
    assert(musashi_spu_cd_audio_advance(audio,target*768u));
    state=snapshot(audio);
    assert(!state.faulted && state.sample_index==target);
    assert(state.pending_frames==2 && state.submitted_frames==0 && sink.frames==0);
    /* The exact-boundary frame survives; future frame remains pending. */
    assert(musashi_spu_cd_audio_advance(audio,(target+1)*768u));
    state=snapshot(audio);
    assert(!state.faulted && state.pending_frames==1 && sink.frames==1);
    assert(sink.pcm[0]==998 && sink.pcm[1]==-1000);
    assert(musashi_spu_cd_audio_advance(audio,(target+2)*768u));
    state=snapshot(audio);
    assert(!state.faulted && state.pending_frames==0 && sink.frames==2);
    assert(sink.pcm[2]==1498 && sink.pcm[3]==-1500);
    /* Explicit skip retirement does not permit late input submissions. */
    assert(!musashi_spu_cd_audio_submit_cd_pcm(audio,target,pcm,1));
    assert(!snapshot(audio).faulted);
    musashi_spu_cd_audio_destroy(audio);
}

int main(void) {
    test_mute_phase_and_mixer();
    test_gate_signed_channels_and_refusal();
    test_clock_queue_thread_and_overflow_refusals();
    test_backend_init_rejections();
    test_callback_reentry_is_refused();
    test_partial_queue_progress();
    test_pending_pcm_across_capacity_drop();
    test_pending_pcm_across_large_skip();
    puts("spu cd audio probe: PASS");
    return 0;
}
