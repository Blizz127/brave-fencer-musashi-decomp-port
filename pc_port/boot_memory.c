#include "musashi_boot_memory.h"

#include <openssl/evp.h>
#include <string.h>

uint8_t *musashi_boot_ram_span(MusashiBootMemory *memory,
                              uint32_t address, size_t size) {
    uint32_t offset;
    if (!memory || address < 0x80000000u || address >= 0x80200000u || !size)
        return NULL;
    offset = address - 0x80000000u;
    if (size > MUSASHI_RAM_SIZE - offset)
        return NULL;
    return memory->bytes + offset;
}

int musashi_boot_read16(MusashiBootMemory *memory, uint32_t address,
                        uint16_t *value) {
    const uint8_t *p = musashi_boot_ram_span(memory, address, 2);
    if (!p || !value || (address & 1u))
        return 0;
    *value = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    return 1;
}

int musashi_boot_write16(MusashiBootMemory *memory, uint32_t address,
                         uint16_t value) {
    uint8_t *p = musashi_boot_ram_span(memory, address, 2);
    if (!p || (address & 1u))
        return 0;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    return 1;
}

int musashi_boot_read32(MusashiBootMemory *memory, uint32_t address,
                        uint32_t *value) {
    const uint8_t *p = musashi_boot_ram_span(memory, address, 4);
    if (!p || !value || (address & 3u))
        return 0;
    *value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
             ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return 1;
}

int musashi_boot_write32(MusashiBootMemory *memory, uint32_t address,
                         uint32_t value) {
    uint8_t *p = musashi_boot_ram_span(memory, address, 4);
    if (!p || (address & 3u))
        return 0;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
    return 1;
}

int musashi_boot_call_800426d4(MusashiBootMemory *memory, uint16_t *result) {
    return result && musashi_boot_read16(memory, 0x8006bafeu, result);
}

int musashi_boot_map_exe(MusashiBootMemory *memory, const uint8_t *exe,
                         size_t exe_size) {
    /* provenance/exe_identity.json; hashing includes the header and padding.
     * The fixed identity pins all layout constants and entry instructions.
     * No untrusted header value controls a copy or address calculation. */
    static const uint8_t expected[32] = {
        0x66, 0x37, 0x1c, 0x3a, 0x75, 0x17, 0xe9, 0xea,
        0xbd, 0x7c, 0xb6, 0xcf, 0x0c, 0x5a, 0xbf, 0xfe,
        0x72, 0x96, 0xbd, 0x4b, 0xac, 0x29, 0xc8, 0x5b,
        0x8b, 0xf7, 0xbb, 0x9d, 0xb3, 0x49, 0x71, 0x4a
    };
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    if (!memory || !exe || exe_size != 0x65000u)
        return 0;
    if (EVP_Digest(exe, exe_size, digest, &digest_size, EVP_sha256(), NULL) != 1 ||
        digest_size != sizeof(expected) || memcmp(digest, expected, sizeof(expected)))
        return 0;

    memmove(memory->bytes + 0x10000u, exe + 0x800u, 0x64800u);
    return 1;
}

int musashi_boot_load_entry(MusashiBootMemory *memory, const uint8_t *exe,
                            size_t exe_size, uint32_t incoming_ra,
                            MusashiEntryState *state) {
    uint32_t stack_top, stack_reserve;
    MusashiEntryState result;
    if (!state || !musashi_boot_map_exe(memory, exe, exe_size)) return 0;

    /* Platform CRT effects at 80010000..80010094. Not a replacement for game
     * startup at 80010178, and no guest register value is used as a host pointer.
     * See docs/NATIVE-BOOT-ENTRY.md for the byte-checked entry contract. */
    memset(memory->bytes + 0x74750u, 0, 0x537b8u);
    musashi_boot_read32(memory, 0x800629bcu, &stack_top);
    musashi_boot_read32(memory, 0x800629c0u, &stack_reserve);
    stack_top -= 8u;
    result.stack_address = stack_top | 0x80000000u;
    result.heap_address = 0x800c7f08u;
    result.heap_size = stack_top - stack_reserve - 0xc7f08u;
    musashi_boot_write32(memory, 0x8006299cu, result.heap_address);
    musashi_boot_write32(memory, 0x800629a0u, result.heap_size);
    musashi_boot_write32(memory, 0x80074750u, incoming_ra);
    *state = result;
    return 1;
}
