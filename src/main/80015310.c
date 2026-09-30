/* Exact retail word export for [80015310,8001534C); EXE and assembly verified. */
/* Retail [80015310,8001534C): clear first halfwords in 16-byte entries. */
typedef struct Entry { short active; unsigned char remaining[14]; } Entry;
extern Entry D_800B93D8[], D_800B97D8[];
void func_80015310(void)
{
    Entry *entry;
    /* Preserve the observed otherwise-unused 8-byte stack allocation. */
    volatile int unused;
    for (entry = D_800B93D8; entry < D_800B97D8; ++entry)
        entry->active = 0;
}

