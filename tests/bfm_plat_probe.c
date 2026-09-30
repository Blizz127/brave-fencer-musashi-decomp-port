/* Probe for the bfm_plat platform layer (no SDL). Driven by
 * tests/test_bfm_plat.py: `bfm_plat_probe GROUP [ARGS...]` prints
 * "ok GROUP" on success, or "FAIL file:line: expr" and exits 1. */
#include "bfm_plat.h"
#include "bfm_plat_font.h"
#include "psyq/bfm_psyq_compat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

static void reset_all(void) {
    bfm_plat_shutdown();
    bfm_plat_mods_reset();
    bfm_plat_console_reset();
    bfm_plat_renderer_reset();
    bfm_plat_audio_reset();
    bfm_plat_input_reset();
    bfm_plat_storage_reset();
    bfm_plat_timing_set_clock(NULL);
    bfm_plat_hooks_reset();
    bfm_plat_console_ui_reset();
    bfm_plat_projection_reset();
    bfm_plat_watch_reset();
}

/* ------------------------------------------------------------ guest RAM */
static uint8_t ram[2u * 1024u * 1024u];
static int ram_read(void *u, uint32_t a, void *out, uint32_t n) {
    (void)u;
    a &= 0x1FFFFFu;
    if (a + n > sizeof ram) return BFM_PLAT_INVALID;
    memcpy(out, ram + a, n);
    return BFM_PLAT_OK;
}
static int ram_write(void *u, uint32_t a, const void *d, uint32_t n) {
    (void)u;
    a &= 0x1FFFFFu;
    if (a + n > sizeof ram) return BFM_PLAT_INVALID;
    memcpy(ram + a, d, n);
    return BFM_PLAT_OK;
}
static void bind_ram(void) {
    BfmPlatGuestMemory m;
    m.read = ram_read;
    m.write = ram_write;
    m.user = NULL;
    bfm_plat_guest_memory_bind(&m);
}

/* ------------------------------------------------------------- config */
static int group_config(int argc, char **argv) {
    BfmPlatConfig c;
    char path[600];
    const char *text =
        "; comment\n[video]\nwidescreen = yes\ninternal_scale = 3\n"
        "frame_rate = 60\n[timing]\nfast_forward_speed = 8\n"
        "[backends]\nrenderer = null\n[mod.hello]\ngreeting = hi there\n"
        "[video]\ninternal_scale = 99\nbroken line\n";
    CHECK(argc >= 1);
    bfm_plat_config_defaults(&c);
    CHECK(c.widescreen == 0 && c.internal_scale == 1 && c.frame_rate == 30);
    CHECK(strcmp(c.renderer, "psycross") == 0 && c.mods_enabled == 1);
    CHECK(bfm_plat_config_load_string(&c, text) == BFM_PLAT_OK);
    CHECK(c.widescreen == 1 && c.internal_scale == 3 && c.frame_rate == 60);
    CHECK(c.fast_forward_speed == 8.0);
    CHECK(strcmp(c.renderer, "null") == 0);
    CHECK(c.rejected_values == 1);           /* scale 99 kept 3 */
    CHECK(c.bad_line == 14);
    CHECK(strcmp(bfm_plat_config_get(&c, "mod.hello.greeting"), "hi there") == 0);
    {
        int n, d;
        bfm_plat_config_aspect(&c, &n, &d);
        CHECK(n == 16 && d == 9);
    }
    CHECK(bfm_plat_config_set(&c, "video.frame_rate", "45") == BFM_PLAT_INVALID);
    CHECK(c.frame_rate == 60);
    CHECK(bfm_plat_config_set(&c, "backends.audio", "Bad Name") == BFM_PLAT_INVALID);
    /* gl renderer dither: auto (default) | on | off */
    CHECK(c.dither == BFM_DITHER_AUTO);
    CHECK(bfm_plat_config_set(&c, "video.dither", "sometimes") == BFM_PLAT_INVALID);
    CHECK(bfm_plat_config_set(&c, "video.dither", "off") == BFM_PLAT_OK && c.dither == BFM_DITHER_OFF);
    CHECK(bfm_plat_config_set(&c, "video.dither", "on") == BFM_PLAT_OK && c.dither == BFM_DITHER_ON);
    /* GPU path: cpu (default) | bfm_plat */
    CHECK(c.gpu_path == BFM_GPU_CPU);
    CHECK(bfm_plat_config_set(&c, "video.gpu", "gl") == BFM_PLAT_INVALID);
    CHECK(bfm_plat_config_set(&c, "video.gpu", "bfm_plat") == BFM_PLAT_OK && c.gpu_path == BFM_GPU_BFM_PLAT);
    {
        char *args[] = {"prog", "--fps", "30", "--keep-me", "--set",
                        "debug.log_level=4", "--no-mods", "--renderer", "hle"};
        unsigned char used[9] = {0};
        CHECK(bfm_plat_config_apply_args(&c, 9, args, used) == BFM_PLAT_OK);
        CHECK(c.frame_rate == 30 && c.log_level == 4 && c.mods_enabled == 0);
        CHECK(strcmp(c.renderer, "hle") == 0);
        CHECK(used[1] && used[2] && !used[3] && used[4] && used[5] && used[6]);
    }
    {
        char *bad[] = {"prog", "--scale"};
        CHECK(bfm_plat_config_apply_args(&c, 2, bad, NULL) == BFM_PLAT_INVALID);
    }
    /* save + reload round trip, including the plugin section */
    snprintf(path, sizeof path, "%s/saved.ini", argv[0]);
    CHECK(bfm_plat_config_save_file(&c, path) == BFM_PLAT_OK);
    {
        BfmPlatConfig r;
        bfm_plat_config_defaults(&r);
        CHECK(bfm_plat_config_load_file(&r, path) == BFM_PLAT_OK);
        CHECK(r.widescreen == 1 && r.internal_scale == 3 && r.frame_rate == 30);
        CHECK(r.log_level == 4 && r.mods_enabled == 0 && r.bad_line == 0);
        CHECK(r.dither == BFM_DITHER_ON && r.gpu_path == BFM_GPU_BFM_PLAT);
        CHECK(strcmp(r.renderer, "hle") == 0);
        CHECK(strcmp(bfm_plat_config_get(&r, "mod.hello.greeting"), "hi there") == 0);
        /* --config is applied before other overrides */
        {
            char *args[] = {"prog", "--scale", "2", "--config", path};
            bfm_plat_config_defaults(&r);
            CHECK(bfm_plat_config_apply_args(&r, 5, args, NULL) == BFM_PLAT_OK);
            CHECK(r.internal_scale == 2 && r.widescreen == 1);
        }
    }
    {
        BfmPlatConfig r;
        bfm_plat_config_defaults(&r);
        CHECK(bfm_plat_config_load_file(&r, "/nonexistent/x.ini") == BFM_PLAT_NOT_FOUND);
    }
    CHECK(bfm_plat_config_default_path(path, sizeof path) == BFM_PLAT_OK);
    printf("path %s\n", path);
    return 0;
}

/* ----------------------------------------------------------- renderer */
static int group_renderer(void) {
    BfmPlatOutput o;
    BfmPlatRect r = {100, 50, 4, 2};
    uint16_t px[8] = {1, 2, 3, 4, 5, 6, 7, 8}, back[8];
    BfmPlatRendererStats st;
    BfmPlatPrim p[2];
    const char *sel = NULL;
    reset_all();
    memset(&o, 0, sizeof o);
    CHECK(bfm_plat_renderer_upload_vram(&r, px) == BFM_PLAT_NOT_READY);
    CHECK(bfm_plat_renderer_open("psycross", &o, &sel) == BFM_PLAT_OK);
    CHECK(strcmp(sel, "null") == 0);          /* not built in: falls back */
    CHECK(bfm_plat_renderer_upload_vram(&r, px) == BFM_PLAT_OK);
    CHECK(bfm_plat_renderer_download_vram(&r, back) == BFM_PLAT_OK);
    CHECK(memcmp(px, back, sizeof px) == 0);
    {
        BfmPlatRect bad = {1020, 0, 8, 1}, empty = {0, 0, 0, 1};
        CHECK(bfm_plat_renderer_upload_vram(&bad, px) == BFM_PLAT_INVALID);
        CHECK(bfm_plat_renderer_upload_vram(&empty, px) == BFM_PLAT_INVALID);
    }
    memset(p, 0, sizeof p);
    p[0].kind = BFM_PRIM_POLY_FT4;
    p[1].kind = BFM_PRIM_FILL;
    p[1].v[0].x = 100; p[1].v[0].y = 50; p[1].w = 2; p[1].h = 1;
    p[1].v[0].r = 0xF8; p[1].v[0].g = 0; p[1].v[0].b = 0;
    CHECK(bfm_plat_renderer_submit(p, 2) == BFM_PLAT_OK);
    CHECK(bfm_plat_renderer_download_vram(&r, back) == BFM_PLAT_OK);
    CHECK(back[0] == 0x1F && back[1] == 0x1F && back[2] == 3);
    p[0].kind = 99;
    CHECK(bfm_plat_renderer_submit(p, 1) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_renderer_present() == BFM_PLAT_OK);
    bfm_plat_renderer_stats(&st);
    CHECK(st.uploads == 1 && st.downloads == 2 && st.prims == 2 && st.frames == 1);
    CHECK(st.prims_by_kind[BFM_PRIM_POLY_FT4] == 1);
    return 0;
}

/* Upload a known texture; the mods dir holds a replacement for it. */
static int group_texture_keep(int argc, char **argv) {
    BfmPlatModsOptions mo;
    BfmPlatOutput o;
    BfmPlatRect r = {0, 0, 2, 2};
    uint16_t px[4] = {0x1234, 0x0001, 0x7FFF, 0x0000}, back[4];
    BfmPlatRendererStats st;
    char hex[17];
    CHECK(argc >= 1);
    bfm_plat_hash_hex(bfm_plat_texture_hash(&r, px), hex);
    printf("hash %s\n", hex);
    memset(&mo, 0, sizeof mo);
    mo.dirs = argv[0];
    if (argc >= 2) { mo.dump_textures = 1; mo.dump_dir = argv[1]; }
    CHECK(bfm_plat_mods_init(&mo) >= 0);
    memset(&o, 0, sizeof o);
    CHECK(bfm_plat_renderer_open("null", &o, NULL) == BFM_PLAT_OK);
    CHECK(bfm_plat_renderer_upload_vram(&r, px) == BFM_PLAT_OK);
    CHECK(bfm_plat_renderer_download_vram(&r, back) == BFM_PLAT_OK);
    bfm_plat_renderer_stats(&st);
    printf("vram %04x %04x %04x %04x replaced %u\n", back[0], back[1], back[2],
           back[3], (unsigned)st.replaced_uploads);
    return 0;
}

static int group_texture(int argc, char **argv) {
    reset_all();
    return group_texture_keep(argc, argv);
}

/* -------------------------------------------------------------- input */
static int group_input(void) {
    BfmPlatPad pad, out;
    uint8_t wire[8];
    BfmPlatConfig c;
    reset_all();
    CHECK(bfm_plat_input_open("null", NULL) == BFM_PLAT_OK);
    memset(&pad, 0, sizeof pad);
    pad.connected = 1;
    pad.buttons = BFM_PAD_CROSS | BFM_PAD_START;
    bfm_plat_input_scripted_set(0, &pad);
    CHECK(bfm_plat_input_poll() == BFM_PLAT_OK);
    CHECK(bfm_plat_input_pad(0, &out) == BFM_PLAT_OK && out.buttons == pad.buttons);
    CHECK(bfm_plat_input_pad(2, &out) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_pad_to_wire(&out, wire) == 4);
    CHECK(wire[0] == 0x41 && wire[1] == 0x5A && wire[2] == 0xF7 && wire[3] == 0xBF);
    pad.analog = 1; pad.lx = 1; pad.ly = 2; pad.rx = 3; pad.ry = 4;
    CHECK(bfm_plat_pad_to_wire(&pad, wire) == 8);
    CHECK(wire[0] == 0x73 && wire[4] == 3 && wire[5] == 4 && wire[6] == 1 && wire[7] == 2);
    pad.connected = 0;
    CHECK(bfm_plat_pad_to_wire(&pad, wire) == 0);
    bfm_plat_input_scripted_hotkeys(BFM_HOTKEY_CONSOLE);
    CHECK(bfm_plat_input_poll() == BFM_PLAT_OK);
    CHECK(bfm_plat_input_hotkeys_pressed() == BFM_HOTKEY_CONSOLE);
    CHECK(bfm_plat_input_poll() == BFM_PLAT_OK);
    CHECK(bfm_plat_input_hotkeys_pressed() == 0 && bfm_plat_input_hotkeys() == BFM_HOTKEY_CONSOLE);

    /* Hotkeys through the frame loop: fast-forward hold, then toggle mode,
     * cheat menu toggle. */
    reset_all();
    bfm_plat_config_defaults(&c);
    c.mods_enabled = 0;
    strcpy(c.input, "null");
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    {
        BfmPlatClock fake;
        extern uint64_t probe_now(void *);
        extern void probe_sleep(void *, uint64_t);
        fake.now_ns = probe_now; fake.sleep_ns = probe_sleep; fake.user = NULL;
        bfm_plat_timing_set_clock(&fake);
    }
    bfm_plat_input_scripted_hotkeys(BFM_HOTKEY_FAST_FORWARD);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(bfm_plat_timing_fast_forward() == 1);
    bfm_plat_input_scripted_hotkeys(0);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(bfm_plat_timing_fast_forward() == 0);
    bfm_plat_config()->fast_forward_toggle = 1;
    bfm_plat_input_scripted_hotkeys(BFM_HOTKEY_FAST_FORWARD | BFM_HOTKEY_CHEAT_MENU);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    bfm_plat_input_scripted_hotkeys(0);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(bfm_plat_timing_fast_forward() == 1 && bfm_plat_cheat_menu_is_open());
    CHECK(bfm_plat_frame_end(1) == BFM_PLAT_OK);
    return 0;
}

/* ------------------------------------------------------------- timing */
static uint64_t fake_ns;
static uint64_t slept_total;
uint64_t probe_now(void *u) { (void)u; return fake_ns; }
void probe_sleep(void *u, uint64_t ns) { (void)u; fake_ns += ns; slept_total += ns; }

static int group_timing(void) {
    BfmPlatClock c;
    BfmPlatTimingStats st;
    uint64_t start;
    reset_all();
    c.now_ns = probe_now; c.sleep_ns = probe_sleep; c.user = NULL;
    fake_ns = 1000000000ull;
    bfm_plat_timing_set_clock(&c);
    bfm_plat_timing_init();
    start = fake_ns;
    CHECK(bfm_plat_timing_vsync(2) == 2);
    CHECK(fake_ns - start == 2 * BFM_PLAT_NTSC_FIELD_NS);
    /* Game work that takes half a field shortens the next wait. */
    fake_ns += BFM_PLAT_NTSC_FIELD_NS / 2;
    CHECK(bfm_plat_timing_vsync(1) == 1);
    CHECK(fake_ns - start == 3 * BFM_PLAT_NTSC_FIELD_NS);
    /* Far behind: resync rather than race. */
    fake_ns += 10 * BFM_PLAT_NTSC_FIELD_NS;
    slept_total = 0;
    bfm_plat_timing_vsync(1);
    CHECK(slept_total == 0);
    bfm_plat_timing_vsync(1);
    CHECK(slept_total == BFM_PLAT_NTSC_FIELD_NS);
    /* Fast-forward 4x: a field costs a quarter. */
    bfm_plat_timing_set_fast_forward_speed(4.0);
    bfm_plat_timing_set_fast_forward(1);
    slept_total = 0;
    bfm_plat_timing_vsync(1);
    bfm_plat_timing_vsync(1);
    CHECK(slept_total == 2 * (BFM_PLAT_NTSC_FIELD_NS / 4));
    /* Uncapped. */
    bfm_plat_timing_set_fast_forward_speed(0.0);
    slept_total = 0;
    bfm_plat_timing_vsync(3);
    CHECK(slept_total == 0);
    bfm_plat_timing_stats(&st);
    CHECK(st.fast_forward == 1 && st.resyncs == 1 && st.vsyncs == 10);
    CHECK(bfm_plat_mods_event_count(BFM_EVENT_VSYNC) == 10);
    return 0;
}

/* -------------------------------------------------------------- audio */
static int group_audio(void) {
    int16_t pcm[2 * 256];
    BfmPlatAudioStatus st;
    const char *sel;
    reset_all();
    memset(pcm, 0, sizeof pcm);
    CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, pcm, 256) == BFM_PLAT_NOT_READY);
    CHECK(bfm_plat_audio_open("sdl", &sel) == BFM_PLAT_OK && strcmp(sel, "null") == 0);
    CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, pcm, 256) == BFM_PLAT_OK);
    CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_CD, pcm, 100) == BFM_PLAT_OK);
    CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_COUNT, pcm, 1) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, NULL, 1) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, pcm, BFM_PLAT_AUDIO_RATE) == BFM_PLAT_NO_SPACE);
    bfm_plat_audio_set_fast_forward(1);
    CHECK(bfm_plat_audio_queue(BFM_AUDIO_STREAM_MAIN, pcm, 256) == BFM_PLAT_OK);
    bfm_plat_audio_set_fast_forward(0);
    CHECK(bfm_plat_audio_set_volume(0.5f) == BFM_PLAT_OK);
    CHECK(bfm_plat_audio_set_volume(2.0f) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_audio_pause(1) == BFM_PLAT_OK);
    CHECK(bfm_plat_audio_status(&st) == BFM_PLAT_OK);
    CHECK(st.rate == 44100 && st.channels == 2 && st.playing == 0);
    CHECK(st.submitted_frames[0] == 256 && st.submitted_frames[1] == 100);
    CHECK(st.master_volume == 0.5f);
    return 0;
}

/* ------------------------------------------------------------ storage */
static int group_storage(int argc, char **argv) {
    uint32_t lba, size;
    uint8_t sec[2352];
    char buf[64];
    size_t got;
    BfmPlatDiscInfo info;
    CHECK(argc >= 3);           /* image path, expected file text, save dir */
    reset_all();
    CHECK(bfm_plat_disc_read(0, 1, BFM_DISC_READ_DATA, sec, sizeof sec) == BFM_PLAT_NOT_READY);
    CHECK(bfm_plat_disc_open("nope", argv[0]) == BFM_PLAT_NOT_FOUND);
    CHECK(bfm_plat_disc_open("image", "/nonexistent.bin") == BFM_PLAT_NOT_FOUND);
    CHECK(bfm_plat_disc_open("image", argv[0]) == BFM_PLAT_OK);
    CHECK(bfm_plat_disc_info(&info) == BFM_PLAT_OK);
    printf("sectors %u size %u tracks %u\n", info.sector_count, info.sector_size,
           info.track_count);
    CHECK(bfm_plat_disc_find_file("\\DATA\\HELLO.TXT;1", &lba, &size) == BFM_PLAT_OK);
    CHECK(size == strlen(argv[1]));
    CHECK(bfm_plat_disc_find_file("data/hello.txt", &lba, &size) == BFM_PLAT_OK);
    CHECK(bfm_plat_disc_find_file("DATA/MISSING.TXT", NULL, NULL) == BFM_PLAT_NOT_FOUND);
    CHECK(bfm_plat_disc_find_file("DATA", NULL, NULL) == BFM_PLAT_NOT_FOUND);
    CHECK(bfm_plat_disc_read_file("DATA/HELLO.TXT", buf, sizeof buf, &got) == BFM_PLAT_OK);
    CHECK(got == strlen(argv[1]) && memcmp(buf, argv[1], got) == 0);
    CHECK(bfm_plat_disc_read_file("DATA/HELLO.TXT", buf, 2, &got) == BFM_PLAT_NO_SPACE);
    if (info.sector_size == 2352) {
        CHECK(bfm_plat_disc_read(lba, 1, BFM_DISC_READ_RAW, sec, sizeof sec) == BFM_PLAT_OK);
        CHECK(sec[0] == 0 && sec[1] == 0xFF && sec[11] == 0);
        CHECK(memcmp(sec + 24, argv[1], strlen(argv[1])) == 0);
        CHECK(bfm_plat_disc_read(lba, 1, BFM_DISC_READ_NO_SYNC, sec, sizeof sec) == BFM_PLAT_OK);
        CHECK(memcmp(sec + 12, argv[1], strlen(argv[1])) == 0);
    } else {
        CHECK(bfm_plat_disc_read(lba, 1, BFM_DISC_READ_RAW, sec, sizeof sec) == BFM_PLAT_UNSUPPORTED);
    }
    CHECK(bfm_plat_disc_read(info.sector_count, 1, BFM_DISC_READ_DATA, sec, sizeof sec) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_disc_read(0, 2, BFM_DISC_READ_DATA, sec, sizeof sec) == BFM_PLAT_INVALID);
    {
        uint8_t m, s, f;
        bfm_plat_disc_lba_to_msf(0, &m, &s, &f);
        CHECK(m == 0 && s == 2 && f == 0);
        bfm_plat_disc_lba_to_msf(4500 - 150 + 1, &m, &s, &f);
        CHECK(m == 1 && s == 0 && f == 1);
        CHECK(bfm_plat_disc_msf_to_lba(1, 0, 1) == 4351);
    }
    /* memory cards */
    {
        static uint8_t card[BFM_MEMCARD_SIZE], back[BFM_MEMCARD_SIZE];
        unsigned i;
        CHECK(bfm_plat_memcard_set_dir(argv[2]) == BFM_PLAT_OK);
        CHECK(bfm_plat_memcard_load(0, back, sizeof back) == BFM_PLAT_NOT_FOUND);
        for (i = 0; i < sizeof card; i++) card[i] = (uint8_t)(i * 7u);
        CHECK(bfm_plat_memcard_save(1, card, sizeof card) == BFM_PLAT_OK);
        CHECK(bfm_plat_memcard_load(1, back, sizeof back) == BFM_PLAT_OK);
        CHECK(memcmp(card, back, sizeof card) == 0);
        CHECK(bfm_plat_memcard_save(2, card, sizeof card) == BFM_PLAT_INVALID);
        CHECK(bfm_plat_memcard_save(0, card, 100) == BFM_PLAT_INVALID);
    }
    return 0;
}

/* Disc file replacement: argv = image, mods dir; prints the bytes read. */
static int group_file_replace(int argc, char **argv) {
    BfmPlatModsOptions mo;
    char buf[128];
    size_t got;
    CHECK(argc >= 2);
    reset_all();
    CHECK(bfm_plat_disc_open("image", argv[0]) == BFM_PLAT_OK);
    memset(&mo, 0, sizeof mo);
    mo.dirs = argv[1];
    CHECK(bfm_plat_mods_init(&mo) >= 0);
    CHECK(bfm_plat_disc_read_file("DATA/HELLO.TXT", buf, sizeof buf - 1, &got) == BFM_PLAT_OK);
    buf[got] = '\0';
    printf("file %s\n", buf);
    return 0;
}

/* --------------------------------------------------------------- mods */
static unsigned boot_seen, rooms_seen;
static void on_boot(void *u, BfmEvent e, void *p) { (void)u; (void)e; (void)p; boot_seen++; }
static void on_room(void *u, BfmEvent e, void *p) {
    (void)u; (void)e;
    CHECK(((BfmEventRoom *)p)->room == 7);
    rooms_seen++;
}

static int static_inits;
static const BfmPluginHost *static_host;
static int static_init(const BfmPluginHost *h, BfmPluginInfo *info) {
    static_inits++;
    static_host = h;
    info->abi_version = BFM_PLUGIN_ABI_VERSION;
    return 0;
}

static int script_loads;
static char script_path[700];
static int fake_lua_load(const BfmPluginHost *h, const char *path) {
    (void)h;
    script_loads++;
    snprintf(script_path, sizeof script_path, "%s", path);
    return BFM_PLAT_OK;
}
static const BfmPlatScriptRuntime fake_lua = {"lua-test", ".lua", fake_lua_load, NULL};

static char log_buf[4096];
static void capture_log(const char *mod, int level, const char *msg) {
    size_t n = strlen(log_buf);
    (void)level;
    snprintf(log_buf + n, sizeof log_buf - n, "[%s] %s\n", mod, msg);
}

/* argv[0] = mods dir(s) prepared by pytest (hello plugin built as .so,
 * a static-plugin mod, a script mod, a disabled mod, a priority mod). */
static int group_mods(int argc, char **argv) {
    BfmPlatConfig c;
    char out[1024];
    unsigned i;
    BfmPlatPad pad, got;
    int sub;
    CHECK(argc >= 1);
    reset_all();
    bind_ram();
    CHECK(bfm_plat_mods_register_static_plugin("builtin_static", static_init, NULL) == BFM_PLAT_OK);
    CHECK(bfm_plat_mods_register_static_plugin("builtin_static", static_init, NULL) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_mods_register_script_runtime(&fake_lua) == BFM_PLAT_OK);
    sub = bfm_plat_mods_subscribe(BFM_EVENT_BOOT, on_boot, NULL);
    CHECK(sub > 0);
    CHECK(bfm_plat_mods_subscribe(BFM_EVENT_COUNT, on_boot, NULL) == BFM_PLAT_INVALID);
    bfm_plat_mods_subscribe(BFM_EVENT_ROOM_ENTER, on_room, NULL);

    bfm_plat_config_defaults(&c);
    strcpy(c.mod_dirs, argv[0]);
    strcpy(c.renderer, "null");
    strcpy(c.audio, "null");
    strcpy(c.input, "null");
    CHECK(bfm_plat_config_load_string(&c, "[mod.hello]\ngreeting = howdy\n") == BFM_PLAT_OK);
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    CHECK(boot_seen == 1);

    for (i = 0; i < bfm_plat_mods_count(); i++) {
        const BfmPlatModInfo *m = bfm_plat_mods_get(i);
        printf("mod %u %s prio=%d enabled=%d plugin=%d script=%d cheats=%u\n", i,
               m->name, m->priority, m->enabled, m->plugin_loaded,
               m->script_loaded, m->cheats);
    }
    CHECK(static_inits == 1 && static_host && strcmp(static_host->mod_name, "static") == 0);
    CHECK(static_host->abi_version == BFM_PLUGIN_ABI_VERSION &&
          static_host->size == sizeof(BfmPluginHost));
    CHECK(script_loads == 1 && strstr(script_path, "main.lua") != NULL);

    /* plugin console command + config section */
    CHECK(bfm_plat_console_exec("hello", out, sizeof out) == 0);
    printf("hello-reply %s\n", out);
    CHECK(bfm_plat_console_exec("nosuch", out, sizeof out) != 0);
    CHECK(bfm_plat_console_exec("help", out, sizeof out) == 0 && strstr(out, "hello -"));
    CHECK(bfm_plat_console_exec("cheats", out, sizeof out) == 0);
    CHECK(strstr(out, "turbo_cross") && strstr(out, "hp_lock"));

    /* frames + room events reach the plugin */
    bfm_plat_event_room_enter(3, 7);
    CHECK(rooms_seen == 1);
    for (i = 0; i < 5; i++) {
        CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
        CHECK(bfm_plat_frame_end(0) == BFM_PLAT_OK);
    }
    CHECK(bfm_plat_console_exec("hello", out, sizeof out) == 0);
    CHECK(strcmp(out, "howdy: 5 frames, 1 rooms") == 0);

    /* poke cheat from cheats.ini: enabled=1 in the manifest */
    ram[0x1234] = 0;
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(bfm_plat_frame_end(0) == BFM_PLAT_OK);
    CHECK(ram[0x1234] == 0x63 && ram[0x1235] == 0x00);
    CHECK(bfm_plat_console_exec("cheat hp_lock off", out, sizeof out) == 0);
    ram[0x1234] = 1;
    bfm_plat_frame_end(0);
    CHECK(ram[0x1234] == 1);

    /* peek / poke / get / set / ff */
    CHECK(bfm_plat_console_exec("poke 0x80001000 0xdeadbeef", out, sizeof out) == 0);
    CHECK(ram[0x1000] == 0xef && ram[0x1003] == 0xde);
    CHECK(bfm_plat_console_exec("peek 0x80001000 4", out, sizeof out) == 0);
    CHECK(strcmp(out, "80001000: ef be ad de") == 0);
    CHECK(bfm_plat_console_exec("poke 0x80001000 1 3", out, sizeof out) != 0);
    CHECK(bfm_plat_console_exec("get mod.hello.greeting", out, sizeof out) == 0);
    CHECK(strcmp(out, "mod.hello.greeting = howdy") == 0);
    CHECK(bfm_plat_console_exec("set video.frame_rate 60", out, sizeof out) == 0);
    CHECK(bfm_plat_config()->frame_rate == 60);
    CHECK(bfm_plat_console_exec("set video.frame_rate 50", out, sizeof out) != 0);
    CHECK(bfm_plat_console_exec("ff 2", out, sizeof out) == 0 && bfm_plat_timing_fast_forward());
    CHECK(bfm_plat_console_exec("ff", out, sizeof out) == 0 && !bfm_plat_timing_fast_forward());
    CHECK(bfm_plat_console_exec("mods", out, sizeof out) == 0 && strstr(out, "hello 1.0.0"));

    /* turbo cheat from the plugin edits input through BFM_EVENT_INPUT */
    memset(&pad, 0, sizeof pad);
    pad.connected = 1;
    pad.buttons = BFM_PAD_CROSS;
    bfm_plat_input_scripted_set(0, &pad);
    CHECK(bfm_plat_cheat_set("turbo_cross", 1) == BFM_PLAT_OK);
    {
        unsigned pressed = 0;
        for (i = 0; i < 4; i++) {
            bfm_plat_frame_begin();
            bfm_plat_input_pad(0, &got);
            pressed += (got.buttons & BFM_PAD_CROSS) != 0;
            bfm_plat_frame_end(0);
        }
        CHECK(pressed == 2);
    }

    /* cheat menu model */
    CHECK(bfm_plat_cheat_count() >= 2);
    bfm_plat_cheat_menu_move(-1);
    CHECK(bfm_plat_cheat_menu_selection() == bfm_plat_cheat_count() - 1);
    bfm_plat_cheat_menu_move(1);
    CHECK(bfm_plat_cheat_menu_selection() == 0);
    {
        int before = bfm_plat_cheat_get(0)->enabled;
        CHECK(bfm_plat_cheat_menu_toggle_selected() == BFM_PLAT_OK);
        CHECK(bfm_plat_cheat_get(0)->enabled == !before);
    }

    /* shutdown drops mod hooks + cheats but keeps ours; log captured */
    CHECK(bfm_plat_mods_unsubscribe(sub) == BFM_PLAT_OK);
    CHECK(bfm_plat_mods_unsubscribe(sub) == BFM_PLAT_NOT_FOUND);
    bfm_plat_shutdown();
    CHECK(bfm_plat_cheat_count() == 0);
    bfm_plat_mods_emit(BFM_EVENT_FRAME_END, NULL);   /* no dangling plugin hook */
    bfm_plat_event_room_enter(0, 7);
    CHECK(rooms_seen == 2);
    printf("ok-shutdown\n");

    /* no plugins allowed: native plugin refused, static still fine */
    {
        BfmPlatModsOptions mo;
        memset(&mo, 0, sizeof mo);
        mo.dirs = argv[0];
        mo.log = capture_log;
        mo.allow_plugins = 0;
        log_buf[0] = '\0';
        CHECK(bfm_plat_mods_init(&mo) >= 1);
        CHECK(strstr(log_buf, "hello_plugin' not found") != NULL);
        bfm_plat_mods_shutdown();
    }
    return 0;
}

static int group_hash(void) {
    char hex[17];
    bfm_plat_hash_hex(bfm_plat_hash64("", 0), hex);
    CHECK(strcmp(hex, "cbf29ce484222325") == 0);
    bfm_plat_hash_hex(bfm_plat_hash64("a", 1), hex);
    CHECK(strcmp(hex, "af63dc4c8601ec8c") == 0);
    bfm_plat_hash_hex(bfm_plat_hash64("foobar", 6), hex);
    CHECK(strcmp(hex, "85944171f73967e8") == 0);
    return 0;
}


/* ----------------------------------------------------------- bindings */
static int32_t fake_resolve(void *u, const char *dev, const char *name) {
    (void)u;
    if (strcmp(dev, "key") == 0 && strlen(name) == 1) return 0x100 + name[0];
    if (strcmp(dev, "key") == 0 && strcmp(name, "tab") == 0) return 9;
    if (strcmp(dev, "pad") == 0 && strcmp(name, "a") == 0) return 0x10000;
    return -1;
}

static int group_bindings(void) {
    BfmPlatConfig c;
    BfmPlatBinding b[128];
    size_t n = 0, bad = 0, i, targets;
    const BfmPlatBindingTarget *t;
    int found_cross = 0, found_ff = 0, found_p2 = 0;
    targets = bfm_plat_input_binding_targets(&t);
    CHECK(targets == 38);
    bfm_plat_config_defaults(&c);
    CHECK(bfm_plat_input_bindings_from_config(&c, fake_resolve, NULL, b, 128, &n, &bad) == BFM_PLAT_OK);
    /* p1 single-letter keys + tab + pad:a twice (p1, p2) resolve */
    for (i = 0; i < n; i++) {
        if (!b[i].is_hotkey && b[i].port == 0 && b[i].mask == BFM_PAD_CROSS && b[i].host_code == 0x100 + 'z') found_cross = 1;
        if (b[i].is_hotkey && b[i].mask == BFM_HOTKEY_FAST_FORWARD && b[i].host_code == 9) found_ff = 1;
        if (b[i].port == 1 && b[i].mask == BFM_PAD_CROSS && b[i].host_code == 0x10000) found_p2 = 1;
    }
    CHECK(found_cross && found_ff && found_p2);
    CHECK(bad > 0);
    bfm_plat_config_load_string(&c,
        "[input]\np1.cross = key:k, pad:a, bogus\np1.start =\nhotkey.console = key:c\n");
    CHECK(bfm_plat_input_bindings_from_config(&c, fake_resolve, NULL, b, 128, &n, &bad) == BFM_PLAT_OK);
    found_cross = 0;
    for (i = 0; i < n; i++) {
        CHECK(!(b[i].port == 0 && !b[i].is_hotkey && b[i].mask == BFM_PAD_START));
        if (!b[i].is_hotkey && b[i].port == 0 && b[i].mask == BFM_PAD_CROSS)
            found_cross += b[i].host_code == 0x100 + 'k' ? 1 : b[i].host_code == 0x10000 ? 2 : 100;
        if (b[i].is_hotkey && b[i].mask == BFM_HOTKEY_CONSOLE) CHECK(b[i].host_code == 0x100 + 'c');
    }
    CHECK(found_cross == 3);
    CHECK(bfm_plat_input_bindings_from_config(&c, fake_resolve, NULL, b, 2, &n, NULL) == BFM_PLAT_NO_SPACE);
    CHECK(n == 2);
    CHECK(bfm_plat_input_bindings_from_config(&c, NULL, NULL, b, 2, &n, NULL) == BFM_PLAT_INVALID);
    return 0;
}

/* --------------------------------------------------------- console UI */
static char overlay[32][160];
static int overlay_n;
static int cap_overlay(void *self, int x, int y, const char *text) {
    (void)self; (void)x; (void)y;
    if (overlay_n < 32) snprintf(overlay[overlay_n++], sizeof overlay[0], "%s", text);
    return BFM_PLAT_OK;
}
static int cap_ok(void *self) { (void)self; return BFM_PLAT_OK; }
static const BfmPlatRendererBackend capture_backend = {
    "capture", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    cap_ok, cap_overlay, NULL
};

static void type_text(const char *s) {
    char one[2] = {0, 0};
    for (; *s; s++) { one[0] = *s; bfm_plat_input_push_text(one); }
}

static int group_console_ui(void) {
    BfmPlatConfig c;
    BfmPlatPad pad, got;
    BfmPlatClock fake;
    int i;
    reset_all();
    CHECK(bfm_plat_renderer_register(&capture_backend) == BFM_PLAT_OK);
    bfm_plat_config_defaults(&c);
    c.mods_enabled = 0;
    strcpy(c.renderer, "capture");
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    fake.now_ns = probe_now; fake.sleep_ns = probe_sleep; fake.user = NULL;
    bfm_plat_timing_set_clock(&fake);
    memset(&pad, 0, sizeof pad);
    pad.connected = 1;
    pad.buttons = BFM_PAD_CROSS;
    bfm_plat_input_scripted_set(0, &pad);

    /* Console hotkey opens it; text typed before opening is dropped. */
    type_text("junk");
    bfm_plat_input_scripted_hotkeys(BFM_HOTKEY_CONSOLE);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(bfm_plat_console_ui_is_open());
    CHECK(bfm_plat_input_pad(0, &got) == BFM_PLAT_OK && got.buttons == 0);
    bfm_plat_input_scripted_hotkeys(0);

    type_text("`get video.frame_rat");
    bfm_plat_input_push_key(BFM_KEY_BACKSPACE);
    type_text("te");
    bfm_plat_input_push_key(BFM_KEY_ENTER);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(strcmp(bfm_plat_console_ui_line(1), "> get video.frame_rate") == 0);
    CHECK(strcmp(bfm_plat_console_ui_line(0), "video.frame_rate = (unset)") == 0);

    /* history, cursor editing, completion */
    bfm_plat_input_push_key(BFM_KEY_UP);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(strcmp(bfm_plat_console_ui_input(), "get video.frame_rate") == 0);
    bfm_plat_input_push_key(BFM_KEY_DOWN);
    type_text("hel");
    bfm_plat_input_push_key(BFM_KEY_TAB);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(strcmp(bfm_plat_console_ui_input(), "help ") == 0);
    bfm_plat_input_push_key(BFM_KEY_LEFT);
    bfm_plat_input_push_key(BFM_KEY_LEFT);
    type_text("X");
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(strcmp(bfm_plat_console_ui_input(), "helXp ") == 0 && bfm_plat_console_ui_cursor() == 4);
    bfm_plat_input_push_key(BFM_KEY_RIGHT);
    bfm_plat_input_push_key(BFM_KEY_RIGHT);
    bfm_plat_input_push_key(BFM_KEY_RIGHT);   /* already at end: no-op */
    for (i = 0; i < 6; i++) bfm_plat_input_push_key(BFM_KEY_BACKSPACE);
    type_text("events");
    bfm_plat_input_push_key(BFM_KEY_ENTER);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(bfm_plat_console_ui_line_count() > 10);

    /* draw goes through overlay_text: scrollback then prompt */
    overlay_n = 0;
    CHECK(bfm_plat_frame_end(1) == BFM_PLAT_OK);
    CHECK(overlay_n == BFM_CONSOLE_UI_VISIBLE + 1);
    CHECK(strcmp(overlay[overlay_n - 1], "> _") == 0);

    /* long output wraps; Esc closes and releases the pads */
    {
        char longline[300];
        memset(longline, 'a', 250);
        longline[250] = '\0';
        i = (int)bfm_plat_console_ui_line_count();
        bfm_plat_console_ui_print(longline);
        CHECK(strlen(bfm_plat_console_ui_line(0)) == 250 - 2 * BFM_CONSOLE_UI_COLS);
    }
    bfm_plat_input_push_key(BFM_KEY_ESCAPE);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(!bfm_plat_console_ui_is_open());
    CHECK(bfm_plat_input_pad(0, &got) == BFM_PLAT_OK && got.buttons == BFM_PAD_CROSS);
    overlay_n = 0;
    bfm_plat_frame_end(1);
    CHECK(overlay_n == 0);

    /* [debug] console = 0 disables the hotkey */
    bfm_plat_config()->console = 0;
    bfm_plat_input_scripted_hotkeys(0);
    bfm_plat_frame_begin();
    bfm_plat_input_scripted_hotkeys(BFM_HOTKEY_CONSOLE);
    bfm_plat_frame_begin();
    CHECK(!bfm_plat_console_ui_is_open());
    return 0;
}

/* -------------------------------------------------------------- hooks */
static unsigned room_hits, item_hits;
static uint32_t last_room;
static void on_room_hook(void *u, BfmEvent e, void *p) {
    (void)u; (void)e;
    room_hits++;
    last_room = ((BfmEventRoom *)p)->room;
}
static void on_item_hook(void *u, BfmEvent e, void *p) {
    (void)u; (void)e;
    item_hits++;
    CHECK(((BfmEventItem *)p)->item == 0);
}
static int fill_room(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    uint8_t v = 0;
    (void)u; (void)e;
    if (bfm_plat_guest_read(0x80002000, &v, 1) != BFM_PLAT_OK) return 1;
    if (v == 0xFF) return 1;       /* not a real room change */
    p->room.area = 2;
    p->room.room = v;
    return 0;
}

static int group_hooks(void) {
    BfmPlatHookSite s[3];
    char out[4096];
    reset_all();
    bind_ram();
    CHECK(strcmp(bfm_plat_event_name(BFM_EVENT_ROOM_ENTER), "room_enter") == 0);
    CHECK(bfm_plat_event_from_name("battle_end") == BFM_EVENT_BATTLE_END);
    CHECK(bfm_plat_event_from_name("nope") == BFM_EVENT_COUNT);
    {
        unsigned i;
        for (i = 0; i < BFM_EVENT_COUNT; i++)
            CHECK(bfm_plat_event_info((BfmEvent)i)->event == (BfmEvent)i);
        CHECK(bfm_plat_event_info(BFM_EVENT_COUNT) == NULL);
    }
    memset(s, 0, sizeof s);
    s[0].guest_pc = 0x80030000; s[0].event = BFM_EVENT_ITEM_GET; s[0].label = "item";
    s[1].guest_pc = 0x80010000; s[1].event = BFM_EVENT_ROOM_ENTER; s[1].label = "room"; s[1].fill = fill_room;
    s[2].guest_pc = 0x80030000; s[2].event = BFM_EVENT_ROOM_EXIT; s[2].label = "exit";
    CHECK(bfm_plat_hooks_register(s, 3) == BFM_PLAT_OK);
    {
        BfmPlatHookSite bad = s[0];
        bad.guest_pc = 0x80030002;
        CHECK(bfm_plat_hooks_register(&bad, 1) == BFM_PLAT_INVALID);
        bad.guest_pc = 0x80030004;
        bad.event = BFM_EVENT_COUNT;
        CHECK(bfm_plat_hooks_register(&bad, 1) == BFM_PLAT_INVALID);
    }
    CHECK(bfm_plat_hooks_count() == 3);
    bfm_plat_mods_subscribe(BFM_EVENT_ROOM_ENTER, on_room_hook, NULL);
    bfm_plat_mods_subscribe(BFM_EVENT_ITEM_GET, on_item_hook, NULL);
    CHECK(bfm_plat_hooks_has(0x80010000) && bfm_plat_hooks_has(0x80030000));
    CHECK(!bfm_plat_hooks_has(0x80020000));
    CHECK(bfm_plat_hooks_at(0x80020000) == 0);
    ram[0x2000] = 9;
    CHECK(bfm_plat_hooks_at(0x80010000) == 1 && room_hits == 1 && last_room == 9);
    ram[0x2000] = 0xFF;
    CHECK(bfm_plat_hooks_at(0x80010000) == 0 && room_hits == 1);
    CHECK(bfm_plat_hooks_at(0x80030000) == 2 && item_hits == 1);
    CHECK(bfm_plat_mods_event_count(BFM_EVENT_ROOM_EXIT) == 1);
    bfm_plat_console_init(NULL);
    bfm_plat_events_register_console();
    CHECK(bfm_plat_console_exec("events", out, sizeof out) == 0);
    CHECK(strstr(out, "room_enter") && strstr(out, "site 80010000 room_enter room hits=1"));
    CHECK(strstr(out, "site 80030000"));
    /* direct emitters used by hand-ported code */
    bfm_plat_event_room_enter(1, 4);
    CHECK(room_hits == 2 && last_room == 4);
    bfm_plat_event_save(0, 1);
    bfm_plat_event_save(1, 1);
    CHECK(bfm_plat_mods_event_count(BFM_EVENT_SAVE) == 1 && bfm_plat_mods_event_count(BFM_EVENT_LOAD) == 1);
    return 0;
}


/* ------------------------------------------------------ known hook sites */
static uint32_t regs[32];
static int reg_read(void *u, unsigned r, uint32_t *out) { (void)u; *out = regs[r]; return BFM_PLAT_OK; }
static void bind_regs(void) {
    BfmPlatGuestRegs g;
    g.read = reg_read;
    g.user = NULL;
    bfm_plat_guest_regs_bind(&g);
}
static void put16(uint32_t a, uint16_t v) { ram[a & 0x1FFFFF] = (uint8_t)v; ram[(a & 0x1FFFFF) + 1] = (uint8_t)(v >> 8); }
static void put32g(uint32_t a, uint32_t v) { put16(a, (uint16_t)v); put16(a + 2, (uint16_t)(v >> 16)); }

static BfmEventRoom seen_room;
static BfmEventSave seen_save;
static unsigned n_enter, n_exit, n_save, n_load;
static void ev_rec(void *u, BfmEvent e, void *p) {
    (void)u;
    if (e == BFM_EVENT_ROOM_ENTER) { n_enter++; seen_room = *(BfmEventRoom *)p; }
    if (e == BFM_EVENT_ROOM_EXIT) { n_exit++; seen_room = *(BfmEventRoom *)p; }
    if (e == BFM_EVENT_SAVE) { n_save++; seen_save = *(BfmEventSave *)p; }
    if (e == BFM_EVENT_LOAD) { n_load++; seen_save = *(BfmEventSave *)p; }
}

static int group_sites(void) {
    const BfmPlatKnownSite *k;
    const BfmEvent *u;
    size_t n = bfm_plat_known_sites(&k), i, m = bfm_plat_unsited_events(&u);
    for (i = 0; i < n; i++)
        printf("site %08x %s %s %s %.*s\n", (unsigned)k[i].guest_pc,
               !k[i].overlay ? "-" : strchr(k[i].overlay, ',') ? "list" : k[i].overlay, bfm_plat_event_name(k[i].event),
               k[i].symbol, (int)strcspn(k[i].confidence, ":"), k[i].confidence);
    for (i = 0; i < m; i++) printf("unsited %s\n", bfm_plat_event_name(u[i]));
    for (i = 0; i < n; i++) printf("evidence %08x %s\n", (unsigned)k[i].guest_pc, k[i].evidence);
    return 0;
}

static unsigned polls_until_unpause;
static void unpause_later(void *u, BfmEvent e, void *p) {
    BfmEventInput *in = (BfmEventInput *)p;
    (void)u; (void)e;
    if (polls_until_unpause && --polls_until_unpause == 0) *in->hotkeys |= BFM_HOTKEY_PAUSE;
}

static void stat_rec(void *u, BfmEvent e, void *p);
static BfmEventStat last_stat[BFM_EVENT_COUNT];
static BfmEventItem last_item;
static unsigned stat_n[BFM_EVENT_COUNT], item_n;
static uint32_t item_use_seen;
static void ev_item_use(void *u, BfmEvent e, void *p) {
    (void)u; (void)e;
    item_use_seen = ((BfmEventItem *)p)->item;
}

static int group_hook_fills(void) {
    BfmPlatConfig c;
    BfmPlatClock fake;
    BfmPlatHookSite bad;
    BfmPlatRendererStats st;
    reset_all();
    bind_ram();
    bind_regs();
    memset(ram, 0, sizeof ram);
    memset(&bad, 0, sizeof bad);
    bad.guest_pc = 0x80128158;
    bad.event = BFM_EVENT_ROOM_ENTER;
    CHECK(bfm_plat_hooks_register(&bad, 1) == BFM_PLAT_INVALID);  /* overlay pc, no overlay */
    CHECK(bfm_plat_overlay_matches("SC*", "sc02_031") && !bfm_plat_overlay_matches("SC*", "MAIN_012"));
    CHECK(bfm_plat_overlay_matches("SC02_031", "sc02_031") && !bfm_plat_overlay_matches("SC02", "SC02_031"));
    CHECK(!bfm_plat_overlay_matches("SC*", NULL) && bfm_plat_overlay_matches(NULL, NULL));
    CHECK(bfm_plat_overlay_matches("SC02_031", "sc02_0031") && bfm_plat_overlay_matches("ov_SC02_031", "SC02_031"));
    CHECK(!bfm_plat_overlay_matches("SC02_031", "SC02_013") && !bfm_plat_overlay_matches("SC02_031", "SC20_031"));
    CHECK(bfm_plat_overlay_matches("SC01_000,SC02_031,MAIN_012", "sc02_0031"));
    CHECK(bfm_plat_overlay_matches("SC01_000,SC02_031,MAIN_012", "MAIN_0012"));
    CHECK(!bfm_plat_overlay_matches("SC01_000,SC02_031", "SC03_053"));

    bfm_plat_config_defaults(&c);
    c.mods_enabled = 0;
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    fake.now_ns = probe_now; fake.sleep_ns = probe_sleep; fake.user = NULL;
    bfm_plat_timing_set_clock(&fake);
    CHECK(bfm_plat_hooks_register_known() == BFM_PLAT_OK);
    CHECK(bfm_plat_hooks_count() == bfm_plat_known_sites(NULL));
    bfm_plat_mods_subscribe(BFM_EVENT_ROOM_ENTER, ev_rec, NULL);
    bfm_plat_mods_subscribe(BFM_EVENT_ROOM_EXIT, ev_rec, NULL);
    bfm_plat_mods_subscribe(BFM_EVENT_SAVE, ev_rec, NULL);
    bfm_plat_mods_subscribe(BFM_EVENT_LOAD, ev_rec, NULL);

    /* room enter: overlay-qualified, edge into phase 0 */
    put16(BFM_GUEST_SCENE_PHASE, 0);
    CHECK(bfm_plat_hooks_at(0x80128158) == 0 && n_enter == 0);   /* no overlay selected */
    bfm_plat_hooks_set_overlay("MAIN_012");
    CHECK(bfm_plat_hooks_at(0x80128158) == 0);
    bfm_plat_hooks_set_overlay("SC02_031");
    CHECK(bfm_plat_hooks_at(0x80128158) == 1 && n_enter == 1);
    CHECK(seen_room.area == 2 && seen_room.room == 31 && seen_room.location == 0);
    CHECK(bfm_plat_hooks_at(0x80128158) == 0);                  /* still phase 0 */
    put16(BFM_GUEST_SCENE_PHASE, 1);
    CHECK(bfm_plat_hooks_at(0x80128158) == 0);
    put16(BFM_GUEST_SCENE_PHASE, 0);
    CHECK(bfm_plat_hooks_at(0x80128158) == 1 && n_enter == 2);
    bfm_plat_hooks_set_overlay("sc03_0053");
    put16(BFM_GUEST_LOCATION, 0x3075);
    CHECK(bfm_plat_hooks_at(0x80128158) == 1 && seen_room.area == 3 && seen_room.room == 53);
    CHECK(seen_room.location == 0x3075);

    /* room exit: func_80011B7C(8) with a location overlay */
    regs[4] = 0;
    CHECK(bfm_plat_hooks_at(0x80011B7C) == 0);
    regs[4] = 8;
    CHECK(bfm_plat_hooks_at(0x80011B7C) == 1 && n_exit == 1 && seen_room.room == 53);
    bfm_plat_hooks_set_overlay(NULL);
    CHECK(bfm_plat_hooks_at(0x80011B7C) == 0);

    /* save / load: edges on the card state machine */
    regs[5] = 2;
    put32g(BFM_GUEST_CARD_STATE, 12);
    CHECK(bfm_plat_hooks_at(0x8002B0B4) == 0);
    put32g(BFM_GUEST_CARD_STATE, 13);
    CHECK(bfm_plat_hooks_at(0x8002B0B4) == 1 && n_save == 1 && seen_save.slot == 2);
    CHECK(bfm_plat_hooks_at(0x8002B0B4) == 0);
    put32g(BFM_GUEST_CARD_STATE, 17);
    regs[5] = 1;
    CHECK(bfm_plat_hooks_at(0x8002B0B4) == 1 && n_load == 1 && seen_save.slot == 1);
    put32g(BFM_GUEST_CARD_STATE, 32);
    CHECK(bfm_plat_hooks_at(0x8002B0B4) == 1 && n_load == 2);
    put32g(BFM_GUEST_CARD_STATE, 13);
    CHECK(bfm_plat_hooks_at(0x8002B0B4) == 1 && n_save == 2);

    /* frame sites are actions */
    CHECK(bfm_plat_hooks_at(0x800189A8) == 1);
    CHECK(bfm_plat_mods_event_count(BFM_EVENT_FRAME_BEGIN) == 1);
    CHECK(bfm_plat_hooks_at(0x800184F0) == 1);
    CHECK(bfm_plat_mods_event_count(BFM_EVENT_FRAME_END) == 1);
    bfm_plat_renderer_stats(&st);
    CHECK(st.frames == 0);        /* hook end does not present */

    /* pause at the FRAME_BEGIN site blocks until unpaused (3 polls later) */
    bfm_plat_mods_subscribe(BFM_EVENT_INPUT, unpause_later, NULL);
    bfm_plat_pause(1);
    polls_until_unpause = 4;
    bfm_plat_input_scripted_hotkeys(0);
    CHECK(bfm_plat_hooks_at(0x800189A8) == 1);
    CHECK(!bfm_plat_is_paused());
    bfm_plat_renderer_stats(&st);
    CHECK(st.frames == 3);        /* three paused frames presented */
    CHECK(bfm_plat_mods_event_count(BFM_EVENT_FRAME_END) == 1);
    CHECK(bfm_plat_mods_event_count(BFM_EVENT_FRAME_BEGIN) == 2);

    /* damage code site: overlay membership, payload, derived suppression */
    bfm_plat_mods_subscribe(BFM_EVENT_DAMAGE, stat_rec, NULL);
    bfm_plat_mods_subscribe(BFM_EVENT_ITEM_GET, stat_rec, NULL);
    bfm_plat_mods_subscribe(BFM_EVENT_ITEM_USE, ev_item_use, NULL);
    put16(BFM_GUEST_HP_MAX, 250); put16(BFM_GUEST_HP, 100);
    bfm_plat_watch_resync();
    regs[5] = 30;
    bfm_plat_hooks_set_overlay("SC03_053");               /* not a member */
    CHECK(bfm_plat_hooks_at(0x8014BC80) == 0);
    bfm_plat_hooks_set_overlay("sc02_0031");
    CHECK(bfm_plat_hooks_at(0x8014BC80) == 1);
    CHECK(stat_n[BFM_EVENT_DAMAGE] == 1 && last_stat[BFM_EVENT_DAMAGE].previous == 100 &&
          last_stat[BFM_EVENT_DAMAGE].value == 70 && last_stat[BFM_EVENT_DAMAGE].max == 250 &&
          last_stat[BFM_EVENT_DAMAGE].flags == 0);
    put16(BFM_GUEST_HP, 70);                              /* the helper's store */
    bfm_plat_frame_hook_end();                            /* poll: derived suppressed */
    CHECK(stat_n[BFM_EVENT_DAMAGE] == 1);
    put16(BFM_GUEST_HP, 60);                              /* a drop with no code site */
    bfm_plat_frame_hook_end();
    CHECK(stat_n[BFM_EVENT_DAMAGE] == 2 && last_stat[BFM_EVENT_DAMAGE].flags == BFM_EVENT_FLAG_DERIVED);
    regs[5] = 500;                                        /* lethal: clamps at 0 */
    CHECK(bfm_plat_hooks_at(0x8014BC80) == 1 && last_stat[BFM_EVENT_DAMAGE].value == 0);
    bfm_plat_frame_hook_end();

    /* inventory: code-site add, derived add, use */
    regs[4] = BFM_GUEST_INV_VAR0 + 3; regs[5] = 106;
    CHECK(bfm_plat_hooks_at(0x800D0F0C) == 1);
    CHECK(item_n == 1 && last_item.item == 106 && last_item.kind == BFM_ITEM_KIND_INVENTORY && last_item.flags == 0);
    ram[(BFM_GUEST_SCRIPT_VARS & 0x1FFFFF) + BFM_GUEST_INV_VAR0 + 3] = 106;
    bfm_plat_frame_hook_end();
    CHECK(item_n == 1);                                   /* suppressed duplicate */
    ram[(BFM_GUEST_SCRIPT_VARS & 0x1FFFFF) + BFM_GUEST_INV_VAR0 + 7] = 12;
    bfm_plat_frame_hook_end();
    CHECK(item_n == 2 && last_item.item == 12 && last_item.flags == BFM_EVENT_FLAG_DERIVED);
    regs[5] = 0;
    CHECK(bfm_plat_hooks_at(0x800D0F0C) == 0);            /* clearing a slot is not a get */
    regs[4] = 119;
    CHECK(bfm_plat_hooks_at(0x800D128C) == 1 && item_use_seen == 119);
    return 0;
}

/* ------------------------------------------------- pause + screenshot */
static int group_pause_screenshot(int argc, char **argv) {
    BfmPlatConfig c;
    BfmPlatClock fake;
    BfmPlatDispEnv d;
    BfmPlatRect r = {0, 0, 4, 2};
    uint16_t px[8] = {0x001F, 0x03E0, 0x7C00, 0x7FFF, 0x0000, 0x4210, 0x0421, 0x8000};
    BfmPlatAudioStatus as;
    char out[256];
    CHECK(argc >= 1);
    reset_all();
    CHECK(bfm_plat_renderer_register(&capture_backend) == BFM_PLAT_OK);
    bfm_plat_config_defaults(&c);
    c.mods_enabled = 0;
    strcpy(c.renderer, "null");
    snprintf(c.screenshot_dir, sizeof c.screenshot_dir, "%s", argv[0]);
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    fake.now_ns = probe_now; fake.sleep_ns = probe_sleep; fake.user = NULL;
    bfm_plat_timing_set_clock(&fake);
    CHECK(bfm_plat_renderer_upload_vram(&r, px) == BFM_PLAT_OK);
    memset(&d, 0, sizeof d);
    d.disp = r;
    CHECK(bfm_plat_renderer_set_disp_env(&d) == BFM_PLAT_OK);

    /* screenshot hotkey -> PNG at next frame end */
    bfm_plat_input_scripted_hotkeys(BFM_HOTKEY_SCREENSHOT);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(bfm_plat_frame_end(1) == BFM_PLAT_OK);
    CHECK(bfm_plat_last_screenshot() != NULL);
    printf("shot %s\n", bfm_plat_last_screenshot());
    bfm_plat_input_scripted_hotkeys(0);
    bfm_plat_frame_begin();
    bfm_plat_frame_end(1);
    /* console command with explicit path; 24-bit mode */
    d.rgb24 = 1;
    d.disp.w = 2;          /* 2 pixels * 3 bytes = 3 halfwords per row */
    CHECK(bfm_plat_renderer_set_disp_env(&d) == BFM_PLAT_OK);
    {
        char line[256];
        snprintf(line, sizeof line, "screenshot %s/rgb24.png", argv[0]);
        CHECK(bfm_plat_console_exec(line, out, sizeof out) == 0);
        CHECK(strstr(out, "rgb24.png") != NULL);
    }
    printf("shot24 %s\n", bfm_plat_last_screenshot());
    CHECK(bfm_plat_console_exec("screenshot /nonexistent-dir/x.png", out, sizeof out) != 0);

    /* pause hotkey: frame_begin reports PAUSED, no FRAME_BEGIN/END, audio paused */
    bfm_plat_input_scripted_hotkeys(BFM_HOTKEY_PAUSE);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_PAUSED);
    CHECK(bfm_plat_is_paused());
    CHECK(bfm_plat_audio_status(&as) == BFM_PLAT_OK && as.playing == 0);
    {
        uint64_t b = bfm_plat_mods_event_count(BFM_EVENT_FRAME_BEGIN);
        uint64_t e = bfm_plat_mods_event_count(BFM_EVENT_FRAME_END);
        bfm_plat_input_scripted_hotkeys(0);
        CHECK(bfm_plat_frame_end(1) == BFM_PLAT_OK);
        CHECK(bfm_plat_frame_begin() == BFM_PLAT_PAUSED);
        CHECK(bfm_plat_frame_end(1) == BFM_PLAT_OK);
        CHECK(bfm_plat_mods_event_count(BFM_EVENT_FRAME_BEGIN) == b);
        CHECK(bfm_plat_mods_event_count(BFM_EVENT_FRAME_END) == e);
    }
    bfm_plat_input_scripted_hotkeys(BFM_HOTKEY_PAUSE);
    CHECK(bfm_plat_frame_begin() == BFM_PLAT_OK);
    CHECK(bfm_plat_audio_status(&as) == BFM_PLAT_OK && as.playing == 1);
    CHECK(bfm_plat_console_exec("pause", out, sizeof out) == 0 && strcmp(out, "paused") == 0);
    CHECK(bfm_plat_console_exec("pause", out, sizeof out) == 0 && strcmp(out, "running") == 0);

    /* "PAUSED" overlay reaches overlay_text */
    bfm_plat_shutdown();
    strcpy(c.renderer, "capture");
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    bfm_plat_timing_set_clock(&fake);
    bfm_plat_pause(1);
    overlay_n = 0;
    bfm_plat_frame_end(1);
    CHECK(overlay_n == 1 && strcmp(overlay[0], "PAUSED") == 0);
    bfm_plat_pause(0);

    /* quit */
    CHECK(!bfm_plat_quit_requested());
    bfm_plat_input_scripted_hotkeys(BFM_HOTKEY_QUIT);
    bfm_plat_frame_begin();
    CHECK(bfm_plat_quit_requested());
    return 0;
}

/* --------------------------------------------------------------- font */
static int group_font(void) {
    BfmPlatPrim p[64];
    const uint8_t *g = bfm_plat_font_glyph('I');
    size_t n;
    unsigned c;
    CHECK(g[0] == 0x0E && g[3] == 0x04 && g[6] == 0x0E);
    CHECK(bfm_plat_font_glyph('\x01') == bfm_plat_font_glyph('?'));
    for (c = 33; c < 127; c++) {
        const uint8_t *q = bfm_plat_font_glyph((char)c);
        unsigned any = 0, r;
        for (r = 0; r < 7; r++) { any |= q[r]; CHECK(q[r] < 0x20); }
        CHECK(any != 0);          /* every printable glyph draws something */
    }
    /* 'I': rows 0 and 6 are one 3-px run, rows 1-5 one 1-px run = 7 runs */
    n = bfm_plat_font_text_prims(10, 20, "I", 1, 255, 255, 255, 0, p, 64);
    CHECK(n == 7);
    CHECK(p[0].kind == BFM_PRIM_TILE && p[0].v[0].x == 11 && p[0].v[0].y == 20 && p[0].w == 3 && p[0].h == 1);
    CHECK(p[3].v[0].x == 12 && p[3].w == 1);
    /* scale 2 + backing: backing first, runs doubled */
    n = bfm_plat_font_text_prims(0, 0, "II", 2, 1, 2, 3, 1, p, 64);
    CHECK(n == 15);
    CHECK(p[0].flags == BFM_PRIM_FLAG_SEMI_TRANS && p[0].w == 2 * 6 * 2 + 2 && p[0].h == 18);
    CHECK(p[1].w == 6 && p[1].h == 2 && p[1].v[0].x == 2 && p[8].v[0].x == 12 + 2);
    CHECK(bfm_plat_font_text_prims(0, 0, "Hello, world!", 1, 0, 0, 0, 1, NULL, 0) > 20);
    CHECK(bfm_plat_font_text_prims(0, 0, "abc", 1, 0, 0, 0, 0, p, 2) > 2);  /* sizing */
    return 0;
}


/* --------------------------------------------------------- widescreen */
static int group_widescreen(void) {
    static const int aspects[][2] = {{4, 3}, {16, 9}, {16, 10}};
    static const int widths[] = {256, 320, 368, 512, 640};
    static const int32_t sxs[] = {-1024, -1, 0, 1, 40, 159, 160, 161, 319, 320, 1023};
    BfmPlatWidescreen ws;
    unsigned a, w, i;
    int win;
    for (a = 0; a < 3; a++) {
        for (w = 0; w < 5; w++) {
            CHECK(bfm_plat_widescreen_compute(aspects[a][0], aspects[a][1], BFM_WIDESCREEN_HOR_PLUS,
                                              widths[w], 240, &ws) == BFM_PLAT_OK);
            printf("horplus %d:%d %d visible %d x0 %d scale %u active %d\n", aspects[a][0],
                   aspects[a][1], widths[w], ws.visible_w, ws.visible_x0,
                   (unsigned)ws.sx_scale_q16, ws.active);
            CHECK(bfm_plat_widescreen_sx(&ws, 300, 160) == 300);
        }
        CHECK(bfm_plat_widescreen_compute(aspects[a][0], aspects[a][1], BFM_WIDESCREEN_ANAMORPHIC,
                                          320, 240, &ws) == BFM_PLAT_OK);
        printf("anamorphic %d:%d scale %u", aspects[a][0], aspects[a][1], (unsigned)ws.sx_scale_q16);
        for (i = 0; i < sizeof sxs / sizeof sxs[0]; i++)
            printf(" %d", (int)bfm_plat_widescreen_sx(&ws, sxs[i], 160));
        printf("\n");
        for (i = 0; i < 3; i++) {
            static const int hs[] = {240, 720, 1081};
            bfm_plat_widescreen_window(&ws, hs[i], &win);
            printf("window %d:%d %d %d\n", aspects[a][0], aspects[a][1], hs[i], win);
        }
    }
    CHECK(bfm_plat_widescreen_compute(5, 4, BFM_WIDESCREEN_HOR_PLUS, 320, 240, &ws) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_widescreen_compute(16, 9, 7, 320, 240, &ws) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_widescreen_compute(16, 9, 0, 0, 240, &ws) == BFM_PLAT_INVALID);
    printf("hfov %d %d\n", (int)bfm_plat_projection_hfov_mdeg(1000, 320),
           (int)bfm_plat_projection_hfov_mdeg(1000, 428));

    /* config + current state + projection hook sites */
    {
        BfmPlatConfig c;
        char *args[] = {"prog", "--aspect", "16:10"};
        BfmPlatProjection pr;
        reset_all();
        bind_ram();
        bind_regs();
        bfm_plat_config_defaults(&c);
        CHECK(c.aspect_num == 4 && c.aspect_den == 3 && !c.widescreen);
        CHECK(bfm_plat_config_set(&c, "video.aspect", "5:4") == BFM_PLAT_INVALID);
        CHECK(bfm_plat_config_set(&c, "video.widescreen", "1") == BFM_PLAT_OK && c.aspect_num == 16 && c.aspect_den == 9);
        CHECK(bfm_plat_config_apply_args(&c, 3, args, NULL) == BFM_PLAT_OK && c.aspect_den == 10 && c.widescreen);
        CHECK(bfm_plat_config_set(&c, "video.widescreen_mode", "anamorphic") == BFM_PLAT_OK);
        CHECK(bfm_plat_config_set(&c, "video.widescreen_mode", "stretch") == BFM_PLAT_INVALID);
        c.mods_enabled = 0;
        strcpy(c.renderer, "null");
        CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
        CHECK(bfm_plat_widescreen_current()->mode == BFM_WIDESCREEN_ANAMORPHIC);
        CHECK(bfm_plat_widescreen_current()->sx_scale_q16 == 54613);
        CHECK(bfm_plat_hooks_register_known() == BFM_PLAT_OK);
        regs[4] = 0x3E8;
        CHECK(bfm_plat_hooks_at(0x8004923C) == 1);
        regs[4] = 160; regs[5] = 120;
        CHECK(bfm_plat_hooks_at(0x8004921C) == 1);
        bfm_plat_projection_get(&pr);
        CHECK(pr.h == 1000 && pr.ofx == 160 && pr.ofy == 120 && pr.updates == 2);
        CHECK(bfm_plat_mods_event_count(BFM_EVENT_PROJECTION) == 2);
        CHECK(bfm_plat_widescreen_sx_current(320) == 293 && bfm_plat_widescreen_sx_current(0) == 27);
        regs[4] = 0xFFFFFFF0u; regs[5] = 0x10078u;   /* -16, 120 (upper bits ignored) */
        bfm_plat_hooks_at(0x8004921C);
        bfm_plat_projection_get(&pr);
        CHECK(pr.ofx == -16 && pr.ofy == 120);
        /* hor+ follows the display width */
        bfm_plat_config()->widescreen_mode = BFM_WIDESCREEN_HOR_PLUS;
        {
            BfmPlatDispEnv d;
            memset(&d, 0, sizeof d);
            d.disp.w = 512; d.disp.h = 240;
            bfm_plat_renderer_set_disp_env(&d);
            CHECK(bfm_plat_widescreen_current()->visible_w == 614);
            CHECK(bfm_plat_widescreen_sx_current(500) == 500);
        }
    }
    return 0;
}



/* ------------------------------------------------------------- watches */
static unsigned w_hits;
static uint8_t w_old[8], w_new[8];
static uint32_t w_addr;
static void w_rec(void *u, uint32_t a, uint32_t len, const uint8_t *o, const uint8_t *n) {
    (void)u;
    w_hits++;
    w_addr = a;
    memcpy(w_old, o, len);
    memcpy(w_new, n, len);
}
static void stat_rec(void *u, BfmEvent e, void *p) {
    (void)u;
    if (e == BFM_EVENT_ITEM_GET) { item_n++; last_item = *(BfmEventItem *)p; return; }
    stat_n[e]++;
    last_stat[e] = *(BfmEventStat *)p;
}
static int watch_plugin_id;
static int watch_plugin_init(const BfmPluginHost *h, BfmPluginInfo *info) {
    info->abi_version = BFM_PLUGIN_ABI_VERSION;
    CHECK(h->abi_version >= 2 && h->watch_add && h->watch_remove);
    watch_plugin_id = h->watch_add(0x80005000, 4, w_rec, NULL);
    return watch_plugin_id > 0 ? 0 : -1;
}

static int group_watch(int argc, char **argv) {
    uint8_t o[4] = {1, 2, 3, 4}, n[4] = {1, 9, 3, 4};
    BfmPlatConfig c;
    BfmPlatClock fake;
    int id;
    CHECK(argc >= 1);
    reset_all();
    bind_ram();
    memset(ram, 0, sizeof ram);
    CHECK(bfm_plat_watch_add(0x80001000, 0, w_rec, NULL) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_watch_add(0x80001000, 65, w_rec, NULL) == BFM_PLAT_INVALID);
    id = bfm_plat_watch_add(0x80001000, 4, w_rec, NULL);
    CHECK(id > 0);
    CHECK(bfm_plat_watch_hit(0x80001002, 1) && bfm_plat_watch_hit(0xA0001000, 4));   /* KSEG1 mirror */
    CHECK(bfm_plat_watch_hit(0x00000FFE, 4));           /* straddles the start */
    CHECK(!bfm_plat_watch_hit(0x80000FFC, 4) && !bfm_plat_watch_hit(0x80001004, 2));
    CHECK(!bfm_plat_watch_hit(0x80101000, 4));
    /* write hook: a 1-byte store in the middle */
    ram[0x1001] = 2;
    bfm_plat_watch_resync();
    bfm_plat_watch_on_write(0x80001001, 1, &o[1], &n[1]);
    CHECK(w_hits == 1 && w_addr == 0x80001000 && w_old[1] == 2 && w_new[1] == 9 && w_new[0] == 0);
    bfm_plat_watch_on_write(0x80001001, 1, &n[1], &n[1]);      /* no change: no fire */
    CHECK(w_hits == 1);
    bfm_plat_watch_on_write(0x80002000, 4, o, n);               /* elsewhere */
    CHECK(w_hits == 1);
    /* poll: sees RAM changes made without the hook */
    ram[0x1003] = 0x77;
    CHECK(bfm_plat_watch_poll() == 1 && w_hits == 2 && w_new[3] == 0x77);
    CHECK(bfm_plat_watch_poll() == 0);
    CHECK(bfm_plat_watch_remove(id) == BFM_PLAT_OK && bfm_plat_watch_remove(id) == BFM_PLAT_NOT_FOUND);
    CHECK(!bfm_plat_watch_hit(0x80001000, 4));

    /* derived events through the platform, poll mode */
    put16(BFM_GUEST_HP_MAX, 250); put16(BFM_GUEST_HP, 250);
    put16(BFM_GUEST_BP_MAX, 300); put16(BFM_GUEST_BP, 120);
    put32g(BFM_GUEST_MONEY, 100);
    bfm_plat_config_defaults(&c);
    snprintf(c.mod_dirs, sizeof c.mod_dirs, "%s", argv[0]);
    strcpy(c.renderer, "null");
    CHECK(bfm_plat_mods_register_static_plugin("watcher", watch_plugin_init, NULL) == BFM_PLAT_OK);
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    fake.now_ns = probe_now; fake.sleep_ns = probe_sleep; fake.user = NULL;
    bfm_plat_timing_set_clock(&fake);
    CHECK(bfm_plat_watch_count() == 6);                 /* 5 derived + the plugin's */
    bfm_plat_mods_subscribe(BFM_EVENT_DAMAGE, stat_rec, NULL);
    bfm_plat_mods_subscribe(BFM_EVENT_BP_USE, stat_rec, NULL);
    bfm_plat_mods_subscribe(BFM_EVENT_MONEY, stat_rec, NULL);
    bfm_plat_mods_subscribe(BFM_EVENT_ITEM_GET, stat_rec, NULL);
    bfm_plat_frame_end(1);
    CHECK(stat_n[BFM_EVENT_DAMAGE] == 0 && stat_n[BFM_EVENT_MONEY] == 0);
    put16(BFM_GUEST_HP, 200);
    put16(BFM_GUEST_BP, 100);
    put32g(BFM_GUEST_MONEY, 40);
    ram[(BFM_GUEST_SCRIPT_VARS & 0x1FFFFF) + BFM_GUEST_FIGURE_VAR0 + 5] = BFM_GUEST_FIGURE_UNLOCK | BFM_GUEST_FIGURE_OWNED;
    bfm_plat_frame_end(1);
    CHECK(stat_n[BFM_EVENT_DAMAGE] == 1 && last_stat[BFM_EVENT_DAMAGE].value == 200 &&
          last_stat[BFM_EVENT_DAMAGE].previous == 250 && last_stat[BFM_EVENT_DAMAGE].max == 250 &&
          last_stat[BFM_EVENT_DAMAGE].flags == BFM_EVENT_FLAG_DERIVED);
    CHECK(stat_n[BFM_EVENT_BP_USE] == 1 && last_stat[BFM_EVENT_BP_USE].max == 300);
    CHECK(stat_n[BFM_EVENT_MONEY] == 1 && last_stat[BFM_EVENT_MONEY].value == 40 &&
          last_stat[BFM_EVENT_MONEY].previous == 100);
    CHECK(item_n == 1 && last_item.item == 5 && last_item.kind == BFM_ITEM_KIND_FIGURE &&
          last_item.count == 1 && last_item.flags == BFM_EVENT_FLAG_DERIVED);
    /* heal: no damage event; write-hook path fires immediately */
    put16(BFM_GUEST_HP, 250);
    bfm_plat_frame_end(1);
    CHECK(stat_n[BFM_EVENT_DAMAGE] == 1);
    {
        uint8_t ov[2] = {250, 0}, nv[2] = {10, 0};
        put16(BFM_GUEST_HP, 10);
        bfm_plat_watch_on_write(BFM_GUEST_HP, 2, ov, nv);
        CHECK(stat_n[BFM_EVENT_DAMAGE] == 2 && last_stat[BFM_EVENT_DAMAGE].value == 10);
        bfm_plat_frame_end(1);                          /* poll sees no further change */
        CHECK(stat_n[BFM_EVENT_DAMAGE] == 2);
    }
    /* paused frames don't poll */
    bfm_plat_pause(1);
    put32g(BFM_GUEST_MONEY, 41);
    bfm_plat_frame_end(1);
    CHECK(stat_n[BFM_EVENT_MONEY] == 1);
    bfm_plat_pause(0);
    bfm_plat_frame_end(1);
    CHECK(stat_n[BFM_EVENT_MONEY] == 2);
    /* the plugin's watch is dropped with the mods, the derived ones stay */
    bfm_plat_mods_shutdown();
    CHECK(bfm_plat_watch_count() == 5);
    bfm_plat_shutdown();
    CHECK(bfm_plat_watch_count() == 0);
    /* derived_events = 0 */
    c.derived_events = 0;
    c.mods_enabled = 0;
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK && bfm_plat_watch_count() == 0);
    return 0;
}

/* cheat_pack DIR: DIR holds a copy of pc_port/mods/examples/cheat_pack. */
static int group_cheat_pack(int argc, char **argv) {
    BfmPlatConfig c;
    BfmPlatClock fake;
    char out[2048];
    unsigned i;
    CHECK(argc >= 1);
    reset_all();
    bind_ram();
    memset(ram, 0, sizeof ram);
    bfm_plat_config_defaults(&c);
    snprintf(c.mod_dirs, sizeof c.mod_dirs, "%s", argv[0]);
    strcpy(c.renderer, "null");
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    fake.now_ns = probe_now; fake.sleep_ns = probe_sleep; fake.user = NULL;
    bfm_plat_timing_set_clock(&fake);
    CHECK(bfm_plat_cheat_count() == 4);
    for (i = 0; i < 4; i++) CHECK(!bfm_plat_cheat_get(i)->enabled);   /* off by default */
    put16(BFM_GUEST_HP_MAX, 300); put16(BFM_GUEST_HP, 10);
    put16(BFM_GUEST_BP_MAX, 400); put16(BFM_GUEST_BP, 5);
    put32g(BFM_GUEST_MONEY, 12);
    bfm_plat_frame_end(1);
    CHECK(ram[0x78EB4] == 10 && ram[0x78E8C] == 12);            /* nothing applied */
    CHECK(bfm_plat_console_exec("cheat infinite_hp on", out, sizeof out) == 0);
    CHECK(bfm_plat_console_exec("cheat infinite_bp on", out, sizeof out) == 0);
    CHECK(bfm_plat_console_exec("cheat max_money on", out, sizeof out) == 0);
    bfm_plat_frame_end(1);
    CHECK(ram[0x78EB4] == 44 && ram[0x78EB5] == 1);             /* 300 */
    CHECK(ram[0x78EB8] == 0x90 && ram[0x78EB9] == 1);           /* 400 */
    CHECK(ram[0x78E8C] == 0x9F && ram[0x78E8D] == 0x86 && ram[0x78E8E] == 1);   /* 99999 */
    put16(BFM_GUEST_HP, 1);                                     /* take damage */
    bfm_plat_frame_end(1);
    CHECK(ram[0x78EB4] == 44 && ram[0x78EB5] == 1);
    CHECK(bfm_plat_console_exec("cheat max_hp_gauge on", out, sizeof out) == 0);
    bfm_plat_frame_end(1);
    bfm_plat_frame_end(1);
    CHECK(ram[0x78EB2] == 0xF4 && ram[0x78EB3] == 1 && ram[0x78EB4] == 0xF4);  /* 500 */
    CHECK(bfm_plat_console_exec("cheats", out, sizeof out) == 0);
    printf("%s", out);
    return 0;
}


/* ------------------------------------------------------------- disc check */
static int group_sha256(void) {
    char hex[65];
    static char million[1000000];
    bfm_plat_sha256_hex("", 0, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    bfm_plat_sha256_hex("abc", 3, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    bfm_plat_sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    CHECK(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    memset(million, 'a', sizeof million);
    bfm_plat_sha256_hex(million, sizeof million, hex);
    CHECK(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    return 0;
}

/* disc_check PATH [BOOT SHA]: prints status, boot name, sha, detail. */
static int group_disc_check(int argc, char **argv) {
    BfmPlatDiscCheck c;
    char msg[800];
    int r;
    CHECK(argc >= 1);
    reset_all();
    if (argc >= 3) r = bfm_plat_disc_validate_ex(argv[0], argv[1], argv[2], &c);
    else r = bfm_plat_disc_validate(argc && strcmp(argv[0], "-") ? argv[0] : NULL, &c);
    CHECK((r == BFM_PLAT_OK) == (c.status == BFM_DISC_OK));
    CHECK((bfm_plat_disc_backend() != NULL) == (c.status == BFM_DISC_OK));   /* open only on OK */
    bfm_plat_disc_check_describe(&c, msg, sizeof msg);
    printf("status %s boot=%s sha=%s tracks=%u sector=%u\n", bfm_plat_disc_status_name(c.status),
           c.boot_name, c.boot_sha256, (unsigned)c.track_count, (unsigned)c.sector_size);
    printf("message %s\n", msg);
    return 0;
}

/* disc_init DISCDIR BADPATH: first-run autodetect + refusal through init. */
static int group_disc_init(int argc, char **argv) {
    BfmPlatConfig c;
    CHECK(argc >= 2);
    reset_all();
    bfm_plat_config_defaults(&c);
    c.mods_enabled = 0;
    strcpy(c.renderer, "null");
    snprintf(c.disc_search, sizeof c.disc_search, "/nonexistent;%s", argv[0]);
    c.disc_validate = 1;
    /* autodetect finds the .cue; retail validation refuses the fake EXE */
    CHECK(bfm_plat_init(&c) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_disc_last_check()->status == BFM_DISC_BAD_DUMP);
    printf("autodetected %s\n", bfm_plat_config()->disc_path);
    /* validation off: boots with the image backend */
    c.disc_validate = 0;
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK && bfm_plat_disc_backend() != NULL);
    bfm_plat_shutdown();
    /* explicit bad path */
    c.disc_validate = 1;
    snprintf(c.disc_path, sizeof c.disc_path, "%s", argv[1]);
    CHECK(bfm_plat_init(&c) == BFM_PLAT_INVALID);
    CHECK(bfm_plat_disc_last_check()->status == BFM_DISC_UNREADABLE);
    /* nothing configured, nothing found: fine (headless) */
    c.disc_path[0] = '\0';
    strcpy(c.disc_search, "/nonexistent");
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    CHECK(bfm_plat_disc_last_check()->status == BFM_DISC_NO_PATH);
    return 0;
}


/* ------------------------------------------------------------ psyq compat */
static uint8_t spad[0x400];
static uint32_t R[32];
static void g32(uint32_t a, uint32_t v) { put32g(a, v); }
static uint32_t r32(uint32_t a) {
    a &= 0x1FFFFF;
    return (uint32_t)ram[a] | ((uint32_t)ram[a + 1] << 8) | ((uint32_t)ram[a + 2] << 16) | ((uint32_t)ram[a + 3] << 24);
}
static int call(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    const BfmPsyqEntry *t;
    size_t n = bfm_psyq_table(&t), i;
    R[4] = a0; R[5] = a1; R[6] = a2; R[7] = a3;
    for (i = 0; i < n; i++)
        if (strcmp(t[i].name, name) == 0) return bfm_psyq_call(t[i].pc, R);
    printf("FAIL no entry %s\n", name);
    exit(1);
}

int bfm_psyq_strcmp(uint32_t *r);   /* not a table entry (no game call sites) */

static int group_psyq(int argc, char **argv) {
    const BfmPsyqEntry *t;
    size_t n = bfm_psyq_table(&t), i, hle = 0;
    BfmPlatOutput o;
    BfmPlatRendererStats st;
    BfmPsyqStats ps;
    BfmPlatClock fake;
    reset_all();
    memset(ram, 0, sizeof ram);
    bfm_psyq_reset();
    bfm_psyq_set_ram(ram, sizeof ram);
    bfm_psyq_set_scratchpad(spad);
    memset(&o, 0, sizeof o);
    CHECK(bfm_plat_renderer_open("null", &o, NULL) == BFM_PLAT_OK);
    fake.now_ns = probe_now; fake.sleep_ns = probe_sleep; fake.user = NULL;
    bfm_plat_timing_set_clock(&fake);
    bfm_plat_timing_init();
    /* table: sorted, unique, stubs refuse */
    for (i = 1; i < n; i++) CHECK(t[i - 1].pc < t[i].pc);
    for (i = 0; i < n; i++) {
        if (t[i].status == BFM_PSYQ_HLE) { CHECK(t[i].available); hle++; }
        if (t[i].status == BFM_PSYQ_STUB) CHECK(!t[i].available && t[i].fn(R) == 0);
    }
    printf("table %u hle %u\n", (unsigned)n, (unsigned)hle);
    CHECK(bfm_psyq_find(0x80010000) == NULL && bfm_psyq_call(0x80010000, R) == 0);

    /* GetTPage / GetClut */
    CHECK(call("GetTPage", 2, 1, 640, 256) && R[2] == ((2u << 7) | (1u << 5) | (1u << 4) | (640u >> 6)));
    CHECK(call("GetTPage", 0, 0, 0, 512) && R[2] == (1u << 11));
    CHECK(call("GetClut", 320, 480, 0, 0) && R[2] == ((480u << 6) | (320u >> 4)));

    /* ClearOTagR + AddPrim + SetPolyFT4 + CatPrim */
    CHECK(call("ClearOTagR", 0x80010000, 4, 0, 0) && R[2] == 0x80010000);
    /* retail relinks ot[0] to the static terminator prim at 0x80072844 */
    CHECK(r32(0x80010000) == 0x00072844 && r32(0x8001000C) == 0x00010008);
    CHECK(call("SetPolyFT4", 0x80020000, 0, 0, 0));
    CHECK(ram[0x20003] == 9 && ram[0x20007] == 0x2C);
    CHECK(call("SetSemiTrans", 0x80020000, 1, 0, 0) && ram[0x20007] == 0x2E);
    CHECK(call("SetSemiTrans", 0x80020000, 0, 0, 0) && ram[0x20007] == 0x2C);
    CHECK(call("AddPrim", 0x8001000C, 0x80020000, 0, 0));
    CHECK(r32(0x8001000C) == 0x00020000 && r32(0x80020000) == 0x09010008);
    CHECK(call("SetLineG3", 0x80021000, 0, 0, 0) && r32(0x80021000 + 28) == 0x55555555);
    CHECK(ram[0x21003] == 7 && ram[0x21007] == 0x58);
    /* LineG4: len 9, code 0x5C, terminator at +0x24 */
    CHECK(call("SetLineG4", 0x80021100, 0, 0, 0) && ram[0x21103] == 9 && ram[0x21107] == 0x5C &&
          r32(0x80021100 + 0x24) == 0x55555555);
    CHECK(call("CatPrim", 0x80021000, 0x80022000, 0, 0) && (r32(0x80021000) & 0xFFFFFF) == 0x022000);

    /* SetDefDispEnv: 5th argument on the guest stack */
    R[29] = 0x801FFF00;
    g32(0x801FFF10, 240);
    CHECK(call("SetDefDispEnv", 0x80030000, 0, 256, 320) && R[2] == 0x80030000);
    CHECK(ram[0x30002] == 0 && ram[0x30003] == 1 && ram[0x30004] == 0x40 && ram[0x30006] == 240);

    /* LoadImage from a scratchpad RECT, StoreImage, MoveImage */
    {
        uint16_t back[4];
        BfmPlatRect rc = {512, 0, 2, 2};
        spad[0] = 0; spad[1] = 2; spad[2] = 0; spad[3] = 0; spad[4] = 2; spad[5] = 0; spad[6] = 2; spad[7] = 0;
        g32(0x80040000, 0x22221111); g32(0x80040004, 0x44443333);
        CHECK(call("LoadImage", 0x1F800000, 0x80040000, 0, 0));
        CHECK(bfm_plat_renderer_download_vram(&rc, back) == BFM_PLAT_OK && back[0] == 0x1111 && back[3] == 0x4444);
        CHECK(call("StoreImage", 0x1F800000, 0x80041000, 0, 0) && r32(0x80041004) == 0x44443333);
        CHECK(call("MoveImage", 0x1F800000, 600, 10, 0));
        rc.x = 600; rc.y = 10;
        CHECK(bfm_plat_renderer_download_vram(&rc, back) == BFM_PLAT_OK && back[1] == 0x2222);
        spad[4] = 0; spad[5] = 8;                          /* w = 2048: invalid rect refuses */
        CHECK(!call("LoadImage", 0x1F800000, 0x80040000, 0, 0));
    }

    /* PutDispEnv / PutDrawEnv reach the renderer */
    CHECK(call("PutDispEnv", 0x80030000, 0, 0, 0) && R[2] == 0x80030000);
    {
        BfmPlatDispEnv d;
        bfm_plat_renderer_last_disp_env(&d);
        CHECK(d.disp.y == 256 && d.disp.w == 320 && d.disp.h == 240);
    }
    CHECK(call("PutDrawEnv", 0x80031000, 0, 0, 0));
    bfm_plat_renderer_stats(&st);
    CHECK(st.draw_envs == 1 && st.disp_envs == 1);

    /* DrawOTag: an OT with every packet family */
    {
        uint32_t a = 0x80050000;
        /* OT: 2 entries (reverse), entry1 -> p0 -> p1 -> ... -> entry0 -> end */
        g32(0x80050000, 0x00FFFFFF);
        /* p0 at 0x80051000: FT4 (9 words) */
        g32(0x80051000, 0x09051100);
        g32(0x80051004, 0x2C808080); g32(0x80051008, 0x00100010); g32(0x8005100C, 0x7FC00000);
        g32(0x80051010, 0x00100020); g32(0x80051014, 0x000C0010); g32(0x80051018, 0x00200010);
        g32(0x8005101C, 0x00001000); g32(0x80051020, 0x00200020); g32(0x80051024, 0x00001010);
        /* p1 at 0x80051100: G3 (6), then E1 + SPRT16 (1 + 3) */
        g32(0x80051100, 0x0A051200);
        g32(0x80051104, 0x30FF0000); g32(0x80051108, 0x00000000);
        g32(0x8005110C, 0x0000FF00); g32(0x80051110, 0x00000010);
        g32(0x80051114, 0x00FF0000); g32(0x80051118, 0x00100000);
        g32(0x8005111C, 0xE1000205);
        g32(0x80051120, 0x7C808080); g32(0x80051124, 0x00400040); g32(0x80051128, 0x7FC00000);
        /* p2 at 0x80051200: variable TILE (3), polyline G with 3 points (6+term), FILL (3), unknown 0x90 */
        g32(0x80051200, 0x11050000);
        g32(0x80051204, 0x60102030); g32(0x80051208, 0x00050005); g32(0x8005120C, 0x00030004);
        g32(0x80051210, 0x58FF0000); g32(0x80051214, 0x00000000);
        g32(0x80051218, 0x0000FF00); g32(0x8005121C, 0x00000010);
        g32(0x80051220, 0x000000FF); g32(0x80051224, 0x00100010);
        g32(0x80051228, 0x55555555);
        g32(0x8005122C, 0x020000F8); g32(0x80051230, 0x01000100); g32(0x80051234, 0x00010002);
        g32(0x80051238, 0x90000000);
        g32(0x8005123C, 0x00000000); g32(0x80051240, 0x00000000); g32(0x80051244, 0x00000000);
        g32(0x80050004, 0x00051000);
        bfm_psyq_reset();
        CHECK(call("DrawOTag", 0x80050004, 0, 0, 0));
        (void)a;
        bfm_psyq_stats(&ps);
        bfm_plat_renderer_stats(&st);
        printf("ot packets %u prims %u unknown %u (0x%02x)\n", (unsigned)ps.ot_packets, (unsigned)ps.prims,
               (unsigned)ps.unknown_commands, ps.last_unknown_code);
        CHECK(ps.ot_packets == 3 && ps.prims == 7 && ps.unknown_commands == 1 && ps.last_unknown_code == 0x90);
        CHECK(st.prims_by_kind[BFM_PRIM_POLY_FT4] == 1 && st.prims_by_kind[BFM_PRIM_POLY_G3] == 1 &&
              st.prims_by_kind[BFM_PRIM_SPRITE] == 1 && st.prims_by_kind[BFM_PRIM_TILE] == 1 &&
              st.prims_by_kind[BFM_PRIM_LINE_G] == 2 && st.prims_by_kind[BFM_PRIM_FILL] == 1);
        {   /* FILL landed in VRAM (null renderer), colour 0xF8 red -> 0x001F */
            uint16_t px;
            BfmPlatRect rc = {256, 256, 1, 1};
            CHECK(bfm_plat_renderer_download_vram(&rc, &px) == BFM_PLAT_OK && px == 0x001F);
        }
    }
    /* direct decoder: FT4 fields, sprite picks up the E1 tpage */
    {
        uint32_t w[] = {0x2C808080, 0x00100010, 0x7FC00000, 0x00100020, 0x000C0010,
                        0x00200010, 0x00001000, 0x00200020, 0x00001010,
                        0xE1000205, 0x7D112233, 0x00400040, 0x7FC00808};
        BfmPlatPrim p[4];
        uint16_t tp = 0;
        CHECK(bfm_psyq_decode_packet(w, 13, &tp, p, 4) == 2);
        CHECK(p[0].kind == BFM_PRIM_POLY_FT4 && p[0].clut == 0x7FC0 && p[0].tpage == 0x000C &&
              p[0].v[1].x == 32 && p[0].v[3].u == 0x10 && p[0].v[3].v == 0x10);
        CHECK(p[1].kind == BFM_PRIM_SPRITE && p[1].w == 16 && p[1].tpage == 0x0205 &&
              p[1].flags == BFM_PRIM_FLAG_RAW_TEXTURE && p[1].v[0].u == 8 && p[1].v[0].r == 0x33);
        CHECK(bfm_psyq_decode_packet(w, 5, &tp, p, 4) == 0);   /* truncated FT4 */
    }
    /* draw state: E6 mask bits and the texpage reach untextured prims; a
     * textured polygon's texpage becomes current */
    {
        uint32_t w[] = {0xE6000003, 0x22102030, 0x00000000, 0x00000010, 0x00100000,
                        0xE6000001, 0x2C808080, 0x00000000, 0x7FC00000, 0x00000010,
                        0x00400010, 0x00100000, 0x00001000, 0x00100010, 0x00001010,
                        0x62102030, 0x00000000, 0x00040004};
        BfmPlatPrim p[4];
        uint16_t tp = 0x0020;   /* abr 1 from an earlier E1 */
        CHECK(bfm_psyq_decode_packet(w, 18, &tp, p, 4) == 3);
        CHECK(p[0].kind == BFM_PRIM_POLY_F3 && p[0].tpage == 0x0020 &&
              p[0].flags == (BFM_PRIM_FLAG_SEMI_TRANS | BFM_PRIM_FLAG_MASK_SET | BFM_PRIM_FLAG_MASK_CHECK));
        CHECK(p[1].kind == BFM_PRIM_POLY_FT4 && p[1].tpage == 0x0040 &&
              p[1].flags == BFM_PRIM_FLAG_MASK_SET);
        CHECK(p[2].kind == BFM_PRIM_TILE && p[2].tpage == 0x0040 &&
              p[2].flags == (BFM_PRIM_FLAG_SEMI_TRANS | BFM_PRIM_FLAG_MASK_SET));
        CHECK(tp == (0x4000 | 0x0040));
    }

    /* libcd */
    CHECK(call("CdIntToPos", 16, 0x80060000, 0, 0) && ram[0x60000] == 0x00 && ram[0x60001] == 0x02 && ram[0x60002] == 0x16);
    CHECK(call("CdPosToInt", 0x80060000, 0, 0, 0) && R[2] == 16);
    CHECK(call("CdIntToPos", 4500 * 3 + 75 * 7 + 3 - 150, 0x80060000, 0, 0) && ram[0x60000] == 0x03 && ram[0x60001] == 0x07 && ram[0x60002] == 0x03);
    /* signed like the retail code: i + 150 = -50 -> s 0, frame -50 -> ((-5) << 4) + 0 */
    CHECK(call("CdIntToPos", (uint32_t)-200, 0x80060000, 0, 0) && ram[0x60000] == 0x00 &&
          ram[0x60001] == 0x00 && ram[0x60002] == 0xB0);
    /* i + 150 = -4577: s = -61 (trunc), frame -2 -> 0xFE; minute -1 -> 0xFF; second -1 -> 0xFF */
    CHECK(call("CdIntToPos", (uint32_t)-4727, 0x80060000, 0, 0) && ram[0x60000] == 0xFF &&
          ram[0x60001] == 0xFF && ram[0x60002] == 0xFE);
    if (argc >= 1) {
        CHECK(bfm_plat_disc_open("image", argv[0]) == BFM_PLAT_OK);
        memcpy(ram + 0x61000, "\\DATA\\HELLO.TXT;1", 18);
        CHECK(call("CdSearchFile", 0x80062000, 0x80061000, 0, 0) && R[2] == 0x80062000);
        CHECK(ram[0x62000] == 0x00 && ram[0x62001] == 0x02 && ram[0x62002] == 0x20 && r32(0x80062004) == 27 && strcmp((char *)ram + 0x62008, "HELLO.TXT;1") == 0);
        memcpy(ram + 0x61000, "\\NOPE.BIN;1", 12);
        CHECK(call("CdSearchFile", 0x80062000, 0x80061000, 0, 0) && R[2] == 0);
    }

    /* libc2: rand on the guest seed, string/memory helpers */
    g32(0x80078980, 1);
    CHECK(call("rand", 0, 0, 0, 0) && R[2] == ((1u * 0x41C64E6Du + 0x3039u) >> 16 & 0x7FFF));
    CHECK(r32(0x80078980) == 1u * 0x41C64E6Du + 0x3039u);
    memcpy(ram + 0x63000, "musashi", 8);
    CHECK(call("strcpy", 0x80063100, 0x80063000, 0, 0) && strcmp((char *)ram + 0x63100, "musashi") == 0);
    /* retail strcpy: NULL dst or src returns 0 and copies nothing */
    ram[0x63400] = 'q';
    CHECK(call("strcpy", 0x80063400, 0, 0, 0) && R[2] == 0 && ram[0x63400] == 'q');
    CHECK(call("strcpy", 0, 0x80063000, 0, 0) && R[2] == 0);
    /* srand stores the seed (the entry at 8005C4CC, once misnamed strcmp) */
    CHECK(call("srand", 0x1234, 0, 0, 0) && r32(0x80078980) == 0x1234);
    /* strcmp (8005C4DC) is not called by game code, so not in the table:
     * the wrapper directly, with the retail NULL rules */
    {
        R[4] = 0x80063100; R[5] = 0x80063000;
        CHECK(bfm_psyq_strcmp(R) && R[2] == 0);
        ram[0x63105] = 'z';
        CHECK(bfm_psyq_strcmp(R) && R[2] == (uint32_t)('z' - 'h'));
        R[4] = 0; R[5] = 0;
        CHECK(bfm_psyq_strcmp(R) && R[2] == 0);
        R[4] = 0; R[5] = 0x80063000;
        CHECK(bfm_psyq_strcmp(R) && R[2] == (uint32_t)-1);
        R[4] = 0x80063000; R[5] = 0;
        CHECK(bfm_psyq_strcmp(R) && R[2] == 1);
    }
    CHECK(call("memset", 0x80063200, 0xAB, 16, 0) && ram[0x6320F] == 0xAB && ram[0x63210] == 0);
    CHECK(call("memcpy", 0x80063300, 0x80063200, 16, 0) && ram[0x6330F] == 0xAB);
    CHECK(call("bzero", 0x80063300, 8, 0, 0) && ram[0x63307] == 0 && ram[0x63308] == 0xAB);
    CHECK(!call("memcpy", 0x80063300, 0x801FFFF0, 64, 0));    /* past the end of RAM */
    CHECK(call("putchar", 'h', 0, 0, 0) && call("putchar", 'i', 0, 0, 0) && call("putchar", '\n', 0, 0, 0));

    /* VSync */
    CHECK(call("VSync", 0xFFFFFFFF, 0, 0, 0) && R[2] == 0);
    CHECK(call("VSync", 2, 0, 0, 0) && call("VSync", 0, 0, 0, 0));
    CHECK(call("VSync", 0xFFFFFFFF, 0, 0, 0) && R[2] == 3);
    CHECK(call("VSync", 1, 0, 0, 0) && R[2] == 0);

    /* no RAM bound: HLE refuses without touching anything */
    bfm_psyq_set_ram(NULL, 0);
    CHECK(!call("AddPrim", 0x8001000C, 0x80020000, 0, 0));
    bfm_psyq_stats(&ps);
    CHECK(ps.refused >= 2);
    return 0;
}


/* ------------------------------------------------------------------ libgs */
static int group_libgs(void) {
    BfmPlatOutput o;
    BfmPlatRendererStats st;
    const uint32_t PKT = 0x80100000, OT = 0x80090000, ORG = 0x80091000;
    uint32_t w;
    reset_all();
    memset(ram, 0, sizeof ram);
    bfm_psyq_reset();
    bfm_psyq_set_ram(ram, sizeof ram);
    memset(&o, 0, sizeof o);
    CHECK(bfm_plat_renderer_open("null", &o, NULL) == BFM_PLAT_OK);
    g32(0x800A5E60, PKT);                                  /* GsOUT_PACKET_P */
    put16(0x800A6548, 10); put16(0x800A654A, 20);          /* draw offset */
    g32(OT + 0, 16); g32(OT + 4, ORG); g32(OT + 8, 0);     /* GsOT: length, org, offset/point */
    CHECK(call("ClearOTagR", ORG, 16, 0, 0));

    /* GsSortLine: attribute bit 30 = semi-transparent, bits 28-29 = rate 1 */
    g32(0x80092000, 0x50000000);
    put16(0x80092004, 1); put16(0x80092006, 2); put16(0x80092008, 30); put16(0x8009200A, 40);
    ram[0x9200C] = 0x11; ram[0x9200D] = 0x22; ram[0x9200E] = 0x33;
    CHECK(call("GsSortLine", 0x80092000, OT, 5, 0));
    CHECK(r32(PKT + 4) == (0xE1000200u | 0x20u));
    CHECK(r32(PKT + 8) == 0x42332211u);
    CHECK(r32(PKT + 12) == ((22u << 16) | 11u) && r32(PKT + 16) == ((60u << 16) | 40u));
    CHECK(r32(PKT) == (0x04000000u | ((ORG + 4 * 4) & 0xFFFFFF)));   /* old slot 5 -> slot 4 */
    CHECK(r32(ORG + 5 * 4) == (PKT & 0xFFFFFF));
    CHECK(r32(0x800A5E60) == PKT + 20);

    /* GsSortFastSprite: brightness-off (bit 6) + semi (bit 30), tpage 5, clut (320, 480) */
    {
        uint32_t sp = 0x80092100, p2 = PKT + 20;
        g32(sp, 0x40000040);
        put16(sp + 4, 100); put16(sp + 6, 50); put16(sp + 8, 16); put16(sp + 10, 32);
        put16(sp + 12, 5); ram[(sp & 0x1FFFFF) + 14] = 8; ram[(sp & 0x1FFFFF) + 15] = 16;
        put16(sp + 16, 320); put16(sp + 18, 480);
        ram[(sp & 0x1FFFFF) + 20] = 0x80; ram[(sp & 0x1FFFFF) + 21] = 0x81; ram[(sp & 0x1FFFFF) + 22] = 0x82;
        CHECK(call("GsSortFastSprite", sp, OT, 5, 0));
        CHECK(r32(p2 + 4) == (0xE1000200u | 5u));
        CHECK(r32(p2 + 8) == (0x64000000u | 0x02000000u | 0x01000000u | 0x828180u));
        CHECK(r32(p2 + 12) == ((70u << 16) | 110u));
        CHECK(r32(p2 + 16) == (8u | (16u << 8) | (20u << 16) | (480u << 22)));
        CHECK(r32(p2 + 20) == ((32u << 16) | 16u));
        CHECK(r32(p2) == (0x05000000u | (PKT & 0xFFFFFF)));        /* chained in front of the line */
        CHECK(r32(ORG + 5 * 4) == (p2 & 0xFFFFFF) && r32(0x800A5E60) == p2 + 24);
        /* zero width or the display-off bit: nothing emitted */
        put16(sp + 8, 0);
        CHECK(call("GsSortFastSprite", sp, OT, 6, 0) && r32(0x800A5E60) == p2 + 24);
        put16(sp + 8, 16); g32(sp, 0x80000000);
        CHECK(call("GsSortFastSprite", sp, OT, 6, 0) && r32(0x800A5E60) == p2 + 24);
    }

    /* DrawOTag over the GsOT: the sprite then the line, one packet each */
    bfm_psyq_reset();
    CHECK(call("DrawOTag", ORG + 15 * 4, 0, 0, 0));
    bfm_plat_renderer_stats(&st);
    CHECK(st.prims_by_kind[BFM_PRIM_SPRITE] == 1 && st.prims_by_kind[BFM_PRIM_LINE_F] == 1);
    {
        BfmPsyqStats ps;
        bfm_psyq_stats(&ps);
        CHECK(ps.ot_packets == 2 && ps.unknown_commands == 0);
    }

    /* GsInitCoordinate2 */
    for (w = 0; w < 8; w++) g32(0x800AE620 + 4 * w, 0x1000 * (w + 1));
    g32(0x80093100, 0xFFFFFFFF);
    CHECK(call("GsInitCoordinate2", 0x80093000, 0x80093100, 0, 0));
    CHECK(r32(0x80093100) == 0 && r32(0x80093104) == 0x1000 && r32(0x80093120) == 0x8000);
    CHECK(r32(0x80093148) == 0x80093000 && r32(0x8009304C) == 0x80093100);
    CHECK(call("GsInitCoordinate2", 0, 0x80093200, 0, 0) && r32(0x80093248) == 0);

    /* GsMapModelingData: 2 objects, offsets relative to the object table */
    g32(0x80094000, 0x41); g32(0x80094004, 0); g32(0x80094008, 2);
    g32(0x8009400C, 0x40); g32(0x80094014, 0x80); g32(0x8009401C, 0xC0);
    g32(0x80094028, 0x100); g32(0x80094030, 0x140); g32(0x80094038, 0x180);
    CHECK(call("GsMapModelingData", 0x80094004, 0, 0, 0));
    CHECK(r32(0x80094004) == 1 && r32(0x8009400C) == 0x8009404C && r32(0x8009401C) == 0x800940CC);
    CHECK(r32(0x80094028) == 0x8009410C && r32(0x80094038) == 0x8009418C);
    CHECK(call("GsMapModelingData", 0x80094004, 0, 0, 0) && r32(0x8009400C) == 0x8009404C);   /* once */

    /* GsSetLightMode */
    CHECK(call("GsSetLightMode", 2, 0, 0, 0) && r32(0x800C6DC8) == 2);
    CHECK(call("GsSetLightMode", 7, 0, 0, 0) && r32(0x800C6DC8) == 2);
    return 0;
}


/* ------------------------------------------------------------ libgte HLE */
static void fill_sincos_table(void) {
    /* synthetic table (not the retail data): round(4096 * sin/cos) */
    unsigned i;
    for (i = 0; i < 4096; i++) {
        double a = 6.283185307179586 * (double)i / 4096.0;
        int16_t sn = (int16_t)lround(4096.0 * sin(a)), cs = (int16_t)lround(4096.0 * cos(a));
        put32g(0x8006DF1C + 4 * i, (uint32_t)(uint16_t)sn | ((uint32_t)(uint16_t)cs << 16));
    }
}

static void fill_atan_table(void) {
    /* synthetic (not the retail data): round(atan(i / 1024) * 2048 / pi) */
    unsigned i;
    for (i = 0; i < 1026; i++)
        put16(0x80071F1C + 2 * i, (uint16_t)(int16_t)lround(atan((double)i / 1024.0) * 2048.0 / 3.141592653589793));
}

static int group_libgte(void) {
    static const int16_t angles[][3] = {{0, 0, 0}, {0, 0, 1024}, {512, -300, 77}, {-4000, 4095, -1},
                                        {1234, 2345, 3456}, {-2048, -1024, 2048}};
    unsigned k, i;
    reset_all();
    memset(ram, 0, sizeof ram);
    bfm_psyq_set_ram(ram, sizeof ram);
    fill_sincos_table();
    for (k = 0; k < sizeof angles / sizeof angles[0]; k++) {
        put16(0x80001000, (uint16_t)angles[k][0]); put16(0x80001002, (uint16_t)angles[k][1]);
        put16(0x80001004, (uint16_t)angles[k][2]);
        for (i = 0; i < 32; i++) ram[0x1100 + i] = 0xEE;          /* t and pad must survive */
        CHECK(call("RotMatrix", 0x80001000, 0x80001100, 0, 0) && R[2] == 0x80001100);
        printf("rot %d %d %d:", angles[k][0], angles[k][1], angles[k][2]);
        for (i = 0; i < 9; i++) printf(" %d", (int)(int16_t)(ram[0x1100 + 2 * i] | (ram[0x1101 + 2 * i] << 8)));
        printf("\n");
        CHECK(ram[0x1112] == 0xEE && r32(0x80001114) == 0xEEEEEEEE);
        CHECK(call("RotMatrixYXZ", 0x80001000, 0x80001100, 0, 0) && R[2] == 0x80001100);
        printf("rotyxz %d %d %d:", angles[k][0], angles[k][1], angles[k][2]);
        for (i = 0; i < 9; i++) printf(" %d", (int)(int16_t)(ram[0x1100 + 2 * i] | (ram[0x1101 + 2 * i] << 8)));
        printf("\n");
        CHECK(ram[0x1112] == 0xEE && r32(0x80001114) == 0xEEEEEEEE);
    }
    /* RotMatrixX/Y/Z multiply m in place (rows combined from the originals) */
    fill_atan_table();
    {
        static const int32_t axis_angles[] = {0, 1024, -300, 4095, -4097, 77};
        unsigned a, ax;
        for (ax = 0; ax < 3; ax++)
            for (a = 0; a < sizeof axis_angles / sizeof axis_angles[0]; a++) {
                static const char *names[3] = {"RotMatrixX", "RotMatrixY", "RotMatrixZ"};
                for (i = 0; i < 9; i++) put16(0x80002200 + 2 * i, (uint16_t)(int16_t)(1100 * (int)i - 4000));
                CHECK(call(names[ax], (uint32_t)axis_angles[a], 0x80002200, 0, 0) && R[2] == 0x80002200);
                printf("%s %d:", names[ax], axis_angles[a]);
                for (i = 0; i < 9; i++) printf(" %d", (int)(int16_t)(ram[0x2200 + 2 * i] | (ram[0x2201 + 2 * i] << 8)));
                printf("\n");
            }
    }
    {   /* ratan2(y, x) */
        static const int32_t yx[][2] = {{1, 1}, {0, 5}, {5, 0}, {0, 0}, {-3, 7}, {3, -7}, {-3, -7},
                                        {100, 37}, {37, 100}, {0x3000000, 0x2000001},
                                        {0x2000001, 0x3000000}, {-1, -1000000}, {1000000, 1}};
        for (k = 0; k < sizeof yx / sizeof yx[0]; k++) {
            CHECK(call("ratan2", (uint32_t)yx[k][0], (uint32_t)yx[k][1], 0, 0));
            printf("ratan2 %d %d: %d\n", yx[k][0], yx[k][1], (int)(int32_t)R[2]);
        }
        CHECK(!call("ratan2", 0x80000000u, 1, 0, 0));   /* INT_MIN: retail code decides */
    }
    /* ScaleMatrix: column scaling; m[2][2] writes its whole word */
    for (i = 0; i < 9; i++) put16(0x80002000 + 2 * i, (uint16_t)(int16_t)(1000 * (int)i - 3000));
    put16(0x80002012, 0x7777);
    g32(0x80002100, 2048); g32(0x80002104, 8192); g32(0x80002108, (uint32_t)-4096);
    CHECK(call("ScaleMatrix", 0x80002000, 0x80002100, 0, 0) && R[2] == 0x80002000);
    printf("scale:");
    for (i = 0; i < 9; i++) printf(" %d", (int)(int16_t)(ram[0x2000 + 2 * i] | (ram[0x2001 + 2 * i] << 8)));
    printf(" word %08x\n", (unsigned)r32(0x80002010));
    /* TransMatrix */
    {   /* TransMatrix is only called inside libgs: test the helper */
        static const int32_t v[3] = {5, -6, 7};
        bfm_psyq_transmatrix(ram + 0x2000, v);
        CHECK(r32(0x80002014) == 5 && (int32_t)r32(0x80002018) == -6 && r32(0x8000201C) == 7);
    }
    /* no table mapped: refuse */
    bfm_psyq_set_ram(NULL, 0);
    CHECK(!call("RotMatrix", 0x80001000, 0x80001100, 0, 0));
    return 0;
}

#ifdef BFM_PLAT_WITH_LUA
/* lua MODSDIR: the pytest writes lua_hello + test mods there. */
static int group_lua(int argc, char **argv) {
    BfmPlatConfig c;
    BfmPlatClock fake;
    BfmPlatPad pad, got;
    char out[1024];
    unsigned i, pressed = 0;
    CHECK(argc >= 1);
    reset_all();
    bind_ram();
    bind_regs();
    memset(ram, 0, sizeof ram);
    ram[0x3000] = 0x11;
    ram[0x3001] = 0x22;
    bfm_plat_config_defaults(&c);
    snprintf(c.mod_dirs, sizeof c.mod_dirs, "%s", argv[0]);
    strcpy(c.renderer, "null");
    CHECK(bfm_plat_config_load_string(&c, "[mod.lua_hello]\ngreeting = hola\n") == BFM_PLAT_OK);
    CHECK(bfm_plat_init(&c) == BFM_PLAT_OK);
    fake.now_ns = probe_now; fake.sleep_ns = probe_sleep; fake.user = NULL;
    bfm_plat_timing_set_clock(&fake);
    for (i = 0; i < bfm_plat_mods_count(); i++) {
        const BfmPlatModInfo *m = bfm_plat_mods_get(i);
        printf("mod %s script=%d\n", m->name, m->script_loaded);
    }
    CHECK(bfm_plat_console_exec("lua_hello a b", out, sizeof out) == 0);
    printf("reply %s\n", out);
    bfm_plat_event_room_enter(4, 12);
    for (i = 0; i < 3; i++) { bfm_plat_frame_begin(); bfm_plat_frame_end(1); }
    CHECK(bfm_plat_console_exec("lua_hello", out, sizeof out) == 0);
    printf("reply %s\n", out);
    /* guest memory, payload tables, poke cheat from Lua */
    bfm_plat_event_item_get(7, 3);
    bfm_plat_event_save(0, 2);
    bfm_plat_projection_set_offset(160, 120);
    bfm_plat_frame_end(1);
    CHECK(bfm_plat_console_exec("luatest", out, sizeof out) == 0);
    printf("luatest %s\n", out);
    printf("ram %02x%02x%02x%02x poke %02x\n", ram[0x3010], ram[0x3011], ram[0x3012], ram[0x3013], ram[0x3020]);
    /* callback cheat edits input (turbo) */
    memset(&pad, 0, sizeof pad);
    pad.connected = 1;
    pad.buttons = BFM_PAD_CROSS;
    bfm_plat_input_scripted_set(0, &pad);
    CHECK(bfm_plat_cheat_set("lua_turbo", 1) == BFM_PLAT_OK);
    for (i = 0; i < 4; i++) {
        bfm_plat_frame_begin();
        bfm_plat_input_pad(0, &got);
        pressed += (got.buttons & BFM_PAD_CROSS) != 0;
        bfm_plat_frame_end(1);
    }
    printf("turbo %u\n", pressed);
    CHECK(bfm_plat_console_exec("luaboom", out, sizeof out) != 0);   /* error in command */
    printf("boom %s\n", out);
    bfm_plat_shutdown();
    CHECK(bfm_plat_console_exec("lua_hello", out, sizeof out) != 0);  /* dropped with the mod */
    CHECK(bfm_plat_cheat_count() == 0);
    bfm_plat_mods_emit(BFM_EVENT_FRAME_END, NULL);                    /* no dangling hooks */
    return 0;
}
#endif

#ifdef BFM_PLAT_WITH_PNG
int bfm_plat_texture_png_decode_memory(const uint8_t *, size_t, uint32_t *,
                                       uint32_t *, uint8_t **);
int bfm_plat_texture_png_register(void);
/* png FILE: prints "png W H <rgba hex>" or "png error N". */
static int group_png(int argc, char **argv) {
    FILE *f;
    static uint8_t buf[1 << 20];
    size_t n;
    uint32_t w, h, i;
    uint8_t *rgba;
    int r;
    CHECK(argc >= 1 && (f = fopen(argv[0], "rb")) != NULL);
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    r = bfm_plat_texture_png_decode_memory(buf, n, &w, &h, &rgba);
    if (r != BFM_PLAT_OK) {
        printf("png error %d\n", r);
        return 0;
    }
    printf("png %u %u ", w, h);
    for (i = 0; i < w * h * 4u; i++) printf("%02x", rgba[i]);
    printf("\n");
    free(rgba);
    return 0;
}
#endif

int main(int argc, char **argv) {
    const char *g;
    int r;
    if (argc < 2) {
        printf("usage: %s GROUP [ARGS]\n", argv[0]);
        return 2;
    }
    g = argv[1];
    if (strcmp(g, "config") == 0) r = group_config(argc - 2, argv + 2);
    else if (strcmp(g, "renderer") == 0) r = group_renderer();
    else if (strcmp(g, "texture") == 0) r = group_texture(argc - 2, argv + 2);
    else if (strcmp(g, "input") == 0) r = group_input();
    else if (strcmp(g, "timing") == 0) r = group_timing();
    else if (strcmp(g, "audio") == 0) r = group_audio();
    else if (strcmp(g, "storage") == 0) r = group_storage(argc - 2, argv + 2);
    else if (strcmp(g, "file_replace") == 0) r = group_file_replace(argc - 2, argv + 2);
    else if (strcmp(g, "mods") == 0) r = group_mods(argc - 2, argv + 2);
    else if (strcmp(g, "hash") == 0) r = group_hash();
    else if (strcmp(g, "bindings") == 0) r = group_bindings();
    else if (strcmp(g, "console_ui") == 0) r = group_console_ui();
    else if (strcmp(g, "hooks") == 0) r = group_hooks();
    else if (strcmp(g, "sites") == 0) r = group_sites();
    else if (strcmp(g, "hook_fills") == 0) r = group_hook_fills();
    else if (strcmp(g, "pause_screenshot") == 0) r = group_pause_screenshot(argc - 2, argv + 2);
    else if (strcmp(g, "font") == 0) r = group_font();
    else if (strcmp(g, "widescreen") == 0) r = group_widescreen();
    else if (strcmp(g, "sha256") == 0) r = group_sha256();
    else if (strcmp(g, "disc_check") == 0) r = group_disc_check(argc - 2, argv + 2);
    else if (strcmp(g, "disc_init") == 0) r = group_disc_init(argc - 2, argv + 2);
    else if (strcmp(g, "libgs") == 0) r = group_libgs();
    else if (strcmp(g, "libgte") == 0) r = group_libgte();
    else if (strcmp(g, "psyq") == 0) r = group_psyq(argc - 2, argv + 2);
    else if (strcmp(g, "watch") == 0) r = group_watch(argc - 2, argv + 2);
    else if (strcmp(g, "cheat_pack") == 0) r = group_cheat_pack(argc - 2, argv + 2);
#ifdef BFM_PLAT_WITH_LUA
    else if (strcmp(g, "lua") == 0) r = group_lua(argc - 2, argv + 2);
#endif
#ifdef BFM_PLAT_WITH_PNG
    else if (strcmp(g, "png") == 0) r = group_png(argc - 2, argv + 2);
    else if (strcmp(g, "texture_png") == 0) {
        reset_all();
        bfm_plat_texture_png_register();
        r = group_texture_keep(argc - 2, argv + 2);
    }
#endif
    else { printf("unknown group %s\n", g); return 2; }
    reset_all();
    if (r == 0) printf("ok %s\n", g);
    return r;
}
