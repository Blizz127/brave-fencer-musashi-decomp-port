#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../pc_port/include/musashi_spu_cd_audio.h"
#include "../pc_port/spu_voice_core.h"

static SpuVoice fresh(unsigned phase, int32_t level, uint16_t adsr1,
                      uint16_t adsr2) {
    SpuVoice v;
    memset(&v, 0, sizeof(v));
    v.phase = phase;
    v.envelope = level;
    v.reg[4] = adsr1;
    v.reg[5] = adsr2;
    return v;
}

static void test_linear_attack_decay_and_counter(void) {
    SpuVoice v = fresh(MUSASHI_SPU_VOICE_ATTACK, 0, 0x0010, 0);
    voice_envelope(&v);
    assert(v.envelope == 14336 && v.phase == MUSASHI_SPU_VOICE_ATTACK);
    v = fresh(MUSASHI_SPU_VOICE_ATTACK, 0, 0x3000, 0);
    voice_envelope(&v);
    assert(v.envelope == 0 && v.envelope_counter == 16384);
    voice_envelope(&v);
    assert(v.envelope == 7 && v.envelope_counter == 0);
    v = fresh(MUSASHI_SPU_VOICE_DECAY, 32767, 0, 0x0020);
    voice_envelope(&v);
    assert(v.envelope == 16383 && v.phase == MUSASHI_SPU_VOICE_DECAY);
}

static void test_exponential_attack_and_sustain(void) {
    SpuVoice v = fresh(MUSASHI_SPU_VOICE_ATTACK, 0, 0x8010, 0);
    voice_envelope(&v);
    assert(v.envelope == 14336);
    voice_envelope(&v);
    assert(v.envelope == 28672);
    voice_envelope(&v);
    assert(v.envelope == 32256);
    v = fresh(MUSASHI_SPU_VOICE_SUSTAIN, 1000, 0, 0x0000);
    voice_envelope(&v);
    assert(v.envelope == 15336);
    v = fresh(MUSASHI_SPU_VOICE_SUSTAIN, 32767, 0, 0xc000);
    voice_envelope(&v);
    assert(v.envelope == 16383);
}

static void test_release_and_all_bits_set_freeze(void) {
    SpuVoice v = fresh(MUSASHI_SPU_VOICE_RELEASE, 32767, 1, 0);
    voice_envelope(&v);
    assert(v.envelope == 16383 && v.phase == MUSASHI_SPU_VOICE_RELEASE);
    v = fresh(MUSASHI_SPU_VOICE_RELEASE, 3, 1, 0);
    voice_envelope(&v);
    assert(v.envelope == 0 && v.phase == MUSASHI_SPU_VOICE_OFF);
    v = fresh(MUSASHI_SPU_VOICE_ATTACK, 1234, 0xffff, 0);
    voice_envelope(&v);
    assert(v.envelope == 1234 && v.envelope_counter == 0);
    v = fresh(MUSASHI_SPU_VOICE_SUSTAIN, 1234, 0, 0xffff);
    voice_envelope(&v);
    assert(v.envelope == 1234 && v.envelope_counter == 0);
    v = fresh(MUSASHI_SPU_VOICE_RELEASE, 1234, 0, 0x003f);
    voice_envelope(&v);
    assert(v.envelope == 1234 && v.envelope_counter == 0);
}

static void test_live_rates_limits_and_key_reset(void) {
    SpuVoice v = fresh(MUSASHI_SPU_VOICE_ATTACK, 0, 0x3000, 0);
    voice_envelope(&v);
    v.reg[4] = 0x0010;
    voice_envelope(&v);
    assert(v.envelope == 14336);
    v = fresh(MUSASHI_SPU_VOICE_ATTACK, 32760, 0x0010, 0);
    voice_envelope(&v);
    assert(v.envelope == 32767 && v.phase == MUSASHI_SPU_VOICE_DECAY);
    v = fresh(MUSASHI_SPU_VOICE_DECAY, 1000, 0x0000, 0x0020);
    voice_envelope(&v);
    assert(v.phase == MUSASHI_SPU_VOICE_SUSTAIN);
    v = fresh(MUSASHI_SPU_VOICE_RELEASE, 1, 0, 0);
    voice_envelope(&v);
    assert(v.phase == MUSASHI_SPU_VOICE_OFF && v.envelope == 0);
    v.envelope_counter = 0x7fff;
    v.envelope = 100;
    voice_key_on(&v);
    assert(v.phase == MUSASHI_SPU_VOICE_ATTACK && v.envelope == 0 &&
           v.envelope_counter == 0);
    v.envelope_counter = 0x7fff;
    voice_off(&v);
    assert(v.phase == MUSASHI_SPU_VOICE_OFF && v.envelope == 0 &&
           v.envelope_counter == 0);
}

static void test_key_transition_counter_policy(void) {
    SpuVoices s;
    SpuVoice *v;
    memset(&s, 0, sizeof(s));
    v = &s.voice[0];
    v->phase = MUSASHI_SPU_VOICE_ATTACK;
    v->envelope = 1000;
    v->envelope_counter = 0x4000;
    v->reg[5] = 0x000c; /* release shift 12: one carry would step */
    s.key_off = 1u;
    voices_apply_keys(&s);
    assert(v->phase == MUSASHI_SPU_VOICE_RELEASE &&
           v->envelope_counter == 0);
    voice_envelope(v);
    assert(v->envelope == 1000 && v->envelope_counter == 0x4000);

    v->phase = MUSASHI_SPU_VOICE_SUSTAIN;
    v->envelope = 1234;
    v->envelope_counter = 0x4000;
    s.key_off = 1u;
    s.key_on = 1u;
    voices_apply_keys(&s);
    assert(v->phase == MUSASHI_SPU_VOICE_ATTACK && v->envelope == 0 &&
           v->envelope_counter == 0 && s.key_off == 0 && s.key_on == 0);

    v->phase = MUSASHI_SPU_VOICE_RELEASE;
    v->envelope = 900;
    v->envelope_counter = 0x4000;
    s.key_off = 1u;
    voices_apply_keys(&s);
    assert(v->phase == MUSASHI_SPU_VOICE_RELEASE &&
           v->envelope_counter == 0);
}

static void test_rendered_last_survives_terminal_transition(void) {
    SpuVoices s;
    int64_t out[2];
    SpuVoice *v;
    memset(&s, 0, sizeof(s));
    v = &s.voice[0];
    v->phase = MUSASHI_SPU_VOICE_RELEASE;
    v->envelope = 3;
    v->gain[0] = v->gain[1] = 32767;
    v->loaded = 1;
    v->history[1] = 32767;
    voices_render(&s, NULL, NULL, out);
    assert(v->phase == MUSASHI_SPU_VOICE_OFF && v->last == 2 &&
           out[0] == 1 && out[1] == 1);
    voices_render(&s, NULL, NULL, out);
    assert(v->last == 0 && out[0] == 0 && out[1] == 0);

    memset(&s, 0, sizeof(s));
    v = &s.voice[0];
    v->phase = MUSASHI_SPU_VOICE_ATTACK;
    v->envelope = 32767;
    v->gain[0] = v->gain[1] = 32767;
    v->reg[2] = 0x1000;
    v->loaded = 1;
    v->position = 28;
    v->flags = 1; /* terminal ADPCM block, no loop */
    v->history[1] = 32767;
    voices_render(&s, NULL, NULL, out);
    assert(v->phase == MUSASHI_SPU_VOICE_OFF && v->last == 22961 &&
           out[0] == 22960 && out[1] == 22960 && (s.endx & 1u));
    voices_render(&s, NULL, NULL, out);
    assert(v->last == 0 && out[0] == 0 && out[1] == 0);
}

static void test_startup_regression(void) {
    static const int32_t levels[] = {14336, 28672, 32767, 16383,
                                     8191, 4095, 2047};
    SpuVoice v = fresh(MUSASHI_SPU_VOICE_ATTACK, 0, 0, 0);
    unsigned i;
    for (i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        voice_envelope(&v);
        assert(v.envelope == levels[i]);
    }
    assert(v.phase == MUSASHI_SPU_VOICE_SUSTAIN);
}

int main(void) {
    test_linear_attack_decay_and_counter();
    test_exponential_attack_and_sustain();
    test_release_and_all_bits_set_freeze();
    test_live_rates_limits_and_key_reset();
    test_key_transition_counter_policy();
    test_rendered_last_survives_terminal_transition();
    test_startup_regression();
    puts("SPU_ADSR_PASS");
    return 0;
}
