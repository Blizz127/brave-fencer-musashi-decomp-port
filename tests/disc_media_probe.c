#define _POSIX_C_SOURCE 200809L

#include "../pc_port/include/musashi_disc_media.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void make_file(char *path, const char *contents, size_t size) {
    int fd = mkstemp(path);
    FILE *file;
    assert(fd >= 0);
    file = fdopen(fd, "wb");
    assert(file != NULL);
    assert(fwrite(contents, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void check_sector_against_file(const char *bin_path,
                                      uint32_t frame,
                                      const unsigned char *sector) {
    FILE *file = fopen(bin_path, "rb");
    unsigned char expected[MUSASHI_DISC_RAW_SECTOR_SIZE];
    assert(file != NULL);
    assert((uint64_t)frame * MUSASHI_DISC_RAW_SECTOR_SIZE <= LONG_MAX);
    assert(fseek(file, (long)((uint64_t)frame *
                              MUSASHI_DISC_RAW_SECTOR_SIZE), SEEK_SET) == 0);
    assert(fread(expected, 1, sizeof(expected), file) == sizeof(expected));
    assert(fclose(file) == 0);
    assert(memcmp(expected, sector, sizeof(expected)) == 0);
}

static void test_rejections(const char *cue_path, const char *bin_path) {
    char wrong_cue[] = "/tmp/musashi-disc-wrong-cue-XXXXXX";
    char short_bin[] = "/tmp/musashi-disc-short-bin-XXXXXX";
    char wrong_bin[] = "/tmp/musashi-disc-wrong-bin-XXXXXX";
    unsigned char wrong_cue_bytes[250];
    MusashiDiscMedia *media;

    memset(wrong_cue_bytes, 'x', sizeof(wrong_cue_bytes));
    memcpy(wrong_cue_bytes, "wrong cue", strlen("wrong cue"));
    make_file(wrong_cue, (const char *)wrong_cue_bytes,
              sizeof(wrong_cue_bytes));
    media = musashi_disc_media_open_pinned(wrong_cue, bin_path);
    assert(media == NULL);
    assert(unlink(wrong_cue) == 0);

    make_file(short_bin, "x", 1);
    media = musashi_disc_media_open_pinned(cue_path, short_bin);
    assert(media == NULL);
    assert(unlink(short_bin) == 0);

    /* Sparse, same-size synthetic bytes prove the BIN digest is enforced.
     * This writes no licensed payload or large duplicate image. */
    {
        int fd = mkstemp(wrong_bin);
        assert(fd >= 0 && ftruncate(fd, 416021760) == 0 && close(fd) == 0);
        media = musashi_disc_media_open_pinned(cue_path, wrong_bin);
        assert(unlink(wrong_bin) == 0);
        assert(media == NULL);
    }

    assert(musashi_disc_media_open_pinned("/does/not/exist", bin_path) == NULL);
    assert(musashi_disc_media_open_pinned(cue_path, "/does/not/exist") == NULL);
}

int main(int argc, char **argv) {
    MusashiDiscMedia *media;
    MusashiDiscMediaInfo info;
    unsigned char first[MUSASHI_DISC_RAW_SECTOR_SIZE];
    unsigned char last[MUSASHI_DISC_RAW_SECTOR_SIZE];
    unsigned char preserved[MUSASHI_DISC_RAW_SECTOR_SIZE];
    unsigned char oversized[MUSASHI_DISC_RAW_SECTOR_SIZE + 16u];
    const char *cue_path;
    const char *bin_path;

    assert(argc == 3);
    cue_path = argv[1];
    bin_path = argv[2];
    media = musashi_disc_media_open_pinned(cue_path, bin_path);
    assert(media != NULL);

    memset(&info, 0, sizeof(info));
    assert(musashi_disc_media_get_info(media, &info));
    assert(info.sector_size == MUSASHI_DISC_RAW_SECTOR_SIZE);
    assert(info.total_frames == 176880u);
    assert(info.track_count == 4u);
    assert(info.tracks[0].number == 1u);
    assert(info.tracks[0].index00_frame == 0u);
    assert(info.tracks[0].index01_frame == 0u);
    assert(info.tracks[0].first_frame == 0u);
    assert(info.tracks[0].end_frame == 155122u);
    assert(info.tracks[0].mode == MUSASHI_DISC_TRACK_MODE2_2352);
    assert(info.tracks[1].index00_frame == 155122u);
    assert(info.tracks[1].index01_frame == 155272u);
    assert(info.tracks[1].first_frame == 155272u);
    assert(info.tracks[1].end_frame == 159662u);
    assert(info.tracks[1].mode == MUSASHI_DISC_TRACK_AUDIO);
    assert(info.tracks[2].index00_frame == 159662u);
    assert(info.tracks[2].index01_frame == 159812u);
    assert(info.tracks[2].first_frame == 159812u);
    assert(info.tracks[2].end_frame == 162778u);
    assert(info.tracks[2].mode == MUSASHI_DISC_TRACK_AUDIO);
    assert(info.tracks[3].index00_frame == 162778u);
    assert(info.tracks[3].index01_frame == 162928u);
    assert(info.tracks[3].first_frame == 162928u);
    assert(info.tracks[3].end_frame == 176880u);
    assert(info.tracks[3].mode == MUSASHI_DISC_TRACK_AUDIO);

    assert(musashi_disc_media_read_sector(media, 0, first, sizeof(first)));
    check_sector_against_file(bin_path, 0, first);
    assert(musashi_disc_media_read_sector(media, info.total_frames - 1u,
                                          last, sizeof(last)));
    check_sector_against_file(bin_path, info.total_frames - 1u, last);

    memset(oversized, 0x5a, sizeof(oversized));
    assert(musashi_disc_media_read_sector(media, 0, oversized,
                                          sizeof(oversized)));
    for (size_t i = MUSASHI_DISC_RAW_SECTOR_SIZE; i < sizeof(oversized); ++i)
        assert(oversized[i] == 0x5a);

    memset(preserved, 0xa5, sizeof(preserved));
    assert(!musashi_disc_media_read_sector(media, info.total_frames, preserved,
                                           sizeof(preserved)));
    for (size_t i = 0; i < sizeof(preserved); ++i) assert(preserved[i] == 0xa5);
    assert(!musashi_disc_media_read_sector(media, 0, preserved,
                                           sizeof(preserved) - 1u));
    for (size_t i = 0; i < sizeof(preserved); ++i) assert(preserved[i] == 0xa5);
    assert(!musashi_disc_media_read_sector(media, 0, NULL, sizeof(first)));
    assert(!musashi_disc_media_get_info(media, NULL));
    musashi_disc_media_close(media);

    test_rejections(cue_path, bin_path);
    puts("disc media probe: PASS");
    return 0;
}
