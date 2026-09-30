#include "../pc_port/include/musashi_boot_memory.h"
#include "../pc_port/include/musashi_cd_controller.h"
#include "../pc_port/include/musashi_disc_media.h"
#include "../pc_port/include/musashi_spu_cd_audio.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Sink {
    uint64_t token;
    int ready;
    int playing;
    size_t queued;
    size_t calls;
    size_t frames;
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
    *queued = sink->queued;
    *playing = sink->playing;
    return 1;
}

static int sink_queue(void *userdata, const int16_t *pcm, size_t frames) {
    Sink *sink = userdata;
    if (!pcm || frames > MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT - sink->queued)
        return 0;
    sink->queued += frames;
    sink->frames += frames;
    ++sink->calls;
    /* This deterministic fixture consumes copied PCM immediately. It is not
     * the live SDL owner and makes no audibility claim. */
    sink->queued = 0;
    return 1;
}

typedef struct CdHardware {
    uint64_t token;
    int healthy;
    unsigned irq4;
} CdHardware;

static uint64_t cd_thread(void *userdata) {
    return ((CdHardware *)userdata)->token;
}

static int cd_healthy(void *userdata) {
    return ((CdHardware *)userdata)->healthy;
}

static int cd_irq4(void *userdata) {
    ++((CdHardware *)userdata)->irq4;
    return 1;
}

typedef struct RefusingSpu {
    MusashiSpuCdAudio *audio;
} RefusingSpu;

static int refusing_read16(void *userdata, uint32_t address, uint16_t *value) {
    RefusingSpu *spu = userdata;
    return musashi_spu_cd_audio_read16(spu->audio, address, value);
}

static int refusing_write16(void *userdata, uint32_t address, uint16_t value) {
    (void)userdata;
    (void)address;
    (void)value;
    return 0;
}

static void set_source_pointers(MusashiBootMemory *memory) {
    /* 80044BF4 reads the live SPU base from D_8006CF60 and the four live CD
     * register addresses from D_8006CF4C..D_8006CF58. */
    assert(musashi_boot_write32(memory, 0x8006cf60u, 0x1f801c00u));
    assert(musashi_boot_write32(memory, 0x8006cf4cu, 0x1f801800u));
    assert(musashi_boot_write32(memory, 0x8006cf50u, 0x1f801801u));
    assert(musashi_boot_write32(memory, 0x8006cf54u, 0x1f801802u));
    assert(musashi_boot_write32(memory, 0x8006cf58u, 0x1f801803u));
}

static MusashiSpuCdAudio *new_audio(Sink *sink) {
    MusashiSpuCdAudioBackend backend = {
        sink, sink_ready, sink_thread, sink_status, sink_queue
    };
    MusashiSpuCdAudio *audio = musashi_spu_cd_audio_create();
    assert(audio != NULL);
    assert(musashi_spu_cd_audio_init_bios_muted(audio, &backend, 0));
    return audio;
}

static MusashiCdOwned *new_cd(const MusashiDiscMedia *media,
                              CdHardware *hardware) {
    MusashiCdOwnedHardware owner = {
        .userdata=hardware, .current_thread=cd_thread,
        .healthy=cd_healthy, .raise_irq4=cd_irq4
    };
    MusashiCdOwned *cd = musashi_cd_owned_open_bios_idle(media, &owner, 0);
    assert(cd != NULL);
    /* Explicitly service the source-device clock before the source call. The
     * setup leaf itself has no timer completion or ready-register shortcut. */
    assert(musashi_cd_owned_advance(cd, 0));
    return cd;
}

static MusashiResetGraphPrefixStatus run_source(
    MusashiBootMemory *memory, MusashiCdOwned *cd,
    MusashiSpuCdAudio *audio, MusashiResetGraphPrefixStop *stop) {
    MusashiCdRegisterDevice registers = {
        .userdata=cd, .read8=musashi_cd_owned_read8, .write8=musashi_cd_owned_write8
    };
    MusashiCdSpuDevice spu = {
        audio, musashi_spu_cd_audio_read16, musashi_spu_cd_audio_write16
    };
    memset(stop, 0, sizeof(*stop));
    return musashi_boot_run_80044bf4_with_device(memory, &registers, &spu,
                                                 stop);
}

static void assert_identity_matrix(const MusashiCdOwnedState *state) {
    assert(state->volume_pending[0] == 0x80 &&
           state->volume_pending[1] == 0 && state->volume_pending[2] == 0 &&
           state->volume_pending[3] == 0x80);
    assert(state->volume[0] == 0x80 && state->volume[1] == 0 &&
           state->volume[2] == 0 && state->volume[3] == 0x80);
}

static void source_success_and_second_call(const MusashiDiscMedia *media) {
    MusashiBootMemory memory;
    Sink sink = {1, 1, 1, 0, 0, 0};
    CdHardware hardware = {1, 1, 0};
    MusashiSpuCdAudio *audio = new_audio(&sink);
    MusashiCdOwned *cd;
    MusashiSpuCdAudioSnapshot before, after, second;
    MusashiCdOwnedState cd_before, cd_after;
    MusashiResetGraphPrefixStop stop;

    memset(&memory, 0, sizeof(memory));
    set_source_pointers(&memory);
    cd = new_cd(media, &hardware);

    /* Make the source's two LH reads observe already settled, nonzero main
     * gains. This proves the retail conditional stores are not a reset path. */
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d80u, 0x2000u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d82u, 0x1000u));
    assert(musashi_spu_cd_audio_advance(audio,
                                        MUSASHI_SPU_CD_AUDIO_SAMPLE_CYCLES));
    assert(musashi_spu_cd_audio_snapshot(audio, &before));
    assert(before.main_left_current == 0x4000 &&
           before.main_right_current == 0x2000);
    assert(before.main_left_configured == before.main_left_current &&
           before.main_right_configured == before.main_right_current);
    assert(before.submitted_frames == 1 && sink.calls == 1 && sink.frames == 1);
    assert(musashi_cd_owned_get_state(cd, &cd_before));

    assert(run_source(&memory, cd, audio, &stop) ==
           MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
    assert(musashi_spu_cd_audio_snapshot(audio, &after));
    assert(after.main_left_current == before.main_left_current &&
           after.main_right_current == before.main_right_current &&
           after.main_left_configured == before.main_left_configured &&
           after.main_right_configured == before.main_right_configured);
    assert(after.cd_left_gain == (int16_t)0x3fff &&
           after.cd_right_gain == (int16_t)0x3fff &&
           after.control == 0xc001u && !after.faulted);
    assert(after.submitted_frames == before.submitted_frames);
    assert(musashi_cd_owned_get_state(cd, &cd_after));
    assert(cd_after.fault == MUSASHI_CD_OWNED_OK);
    assert_identity_matrix(&cd_after);
    assert(cd_after.index == 3 && hardware.irq4 == 0);

    /* A second execution starts a fresh formatter CPU but uses the same live
     * owners. It must repeat the source writes without resetting settled SPU
     * state or inventing a new CD ready/clock transition. */
    assert(run_source(&memory, cd, audio, &stop) ==
           MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
    assert(musashi_spu_cd_audio_snapshot(audio, &second));
    assert(second.main_left_current == after.main_left_current &&
           second.main_right_current == after.main_right_current &&
           second.cd_left_gain == after.cd_left_gain &&
           second.cd_right_gain == after.cd_right_gain &&
           second.control == after.control && !second.faulted);
    assert(musashi_cd_owned_get_state(cd, &cd_after));
    assert_identity_matrix(&cd_after);

    assert(musashi_cd_owned_close(cd));
    assert(musashi_spu_cd_audio_destroy(audio));
}

static void source_refusal_preserves_effects(const MusashiDiscMedia *media) {
    MusashiBootMemory memory;
    Sink sink = {1, 1, 1, 0, 0, 0};
    CdHardware hardware = {1, 1, 0};
    MusashiSpuCdAudio *audio = new_audio(&sink);
    MusashiCdOwned *cd = new_cd(media, &hardware);
    MusashiCdOwnedState cd_before, cd_after;
    MusashiSpuCdAudioSnapshot before, after;
    MusashiResetGraphPrefixStop stop;
    RefusingSpu refusing = {audio};
    MusashiCdRegisterDevice registers = {
        .userdata=cd, .read8=musashi_cd_owned_read8, .write8=musashi_cd_owned_write8
    };
    MusashiCdSpuDevice refused_spu = {
        &refusing, refusing_read16, refusing_write16
    };

    memset(&memory, 0, sizeof(memory));
    set_source_pointers(&memory);
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d80u, 0x2000u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801d82u, 0x1000u));
    assert(musashi_spu_cd_audio_advance(audio,
                                        MUSASHI_SPU_CD_AUDIO_SAMPLE_CYCLES));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801db0u, 0x1234u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801db2u, 0x2345u));
    assert(musashi_spu_cd_audio_write16(audio, 0x1f801daau, 0x8001u));
    assert(musashi_spu_cd_audio_snapshot(audio, &before));
    assert(musashi_cd_owned_get_state(cd, &cd_before));

    /* The exact source reaches the first SPU write after its two reads. A
     * refused MMIO callback must stop execution and leave prior owner effects
     * and the not-yet-reached CD writes intact. */
    memset(&stop, 0, sizeof(stop));
    assert(musashi_boot_run_80044bf4_with_device(&memory, &registers,
                                                  &refused_spu, &stop) ==
           MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    assert(musashi_spu_cd_audio_snapshot(audio, &after));
    assert(after.main_left_current == before.main_left_current &&
           after.main_right_current == before.main_right_current &&
           after.cd_left_gain == before.cd_left_gain &&
           after.cd_right_gain == before.cd_right_gain &&
           after.control == before.control && !after.faulted);
    assert(musashi_cd_owned_get_state(cd, &cd_after));
    assert(cd_after.index == cd_before.index &&
           cd_after.volume[0] == cd_before.volume[0] &&
           cd_after.volume[3] == cd_before.volume[3] &&
           cd_after.fault == MUSASHI_CD_OWNED_OK);

    /* The real SPU owner now refuses at its readiness gate. This tests the
     * unavailable backend path through the actual callback, rather than a
     * synthetic source completion flag. */
    sink.ready = 0;
    assert(run_source(&memory, cd, audio, &stop) ==
           MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    assert(musashi_spu_cd_audio_snapshot(audio, &after));
    assert(after.main_left_current == before.main_left_current &&
           after.main_right_current == before.main_right_current &&
           after.cd_left_gain == before.cd_left_gain &&
           after.cd_right_gain == before.cd_right_gain &&
           after.control == before.control && after.faulted);
    assert(musashi_cd_owned_get_state(cd, &cd_after));
    assert(cd_after.index == cd_before.index &&
           cd_after.volume[0] == cd_before.volume[0] &&
           cd_after.volume[3] == cd_before.volume[3]);

    assert(musashi_cd_owned_close(cd));
    assert(musashi_spu_cd_audio_destroy(audio));
}

int main(int argc, char **argv) {
    MusashiDiscMedia *media;
    assert(argc == 3);
    media = musashi_disc_media_open_pinned(argv[1], argv[2]);
    assert(media != NULL);
    source_success_and_second_call(media);
    source_refusal_preserves_effects(media);
    musashi_disc_media_close(media);
    puts("CD/SPU 80044BF4 source-owner fixture: PASS");
    return 0;
}

/* Keep this source-word executor in the same translation unit as the probe so
 * its generated includes are audited by the Python test, exactly like the
 * existing formatter source probes. */
#include "../pc_port/mips_formatter.c"
