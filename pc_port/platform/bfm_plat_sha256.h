#ifndef BFM_PLAT_SHA256_H
#define BFM_PLAT_SHA256_H

/* SHA-256 (FIPS 180-4), in-house so the core needs no crypto library. */

#include <stddef.h>
#include <stdint.h>

typedef struct BfmPlatSha256 {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t buf[64];
    size_t used;
} BfmPlatSha256;

void bfm_plat_sha256_init(BfmPlatSha256 *s);
void bfm_plat_sha256_update(BfmPlatSha256 *s, const void *data, size_t n);
void bfm_plat_sha256_final(BfmPlatSha256 *s, uint8_t out[32]);
/* One shot, lowercase hex into out[65]. */
void bfm_plat_sha256_hex(const void *data, size_t n, char out[65]);
void bfm_plat_sha256_to_hex(const uint8_t digest[32], char out[65]);

#endif
