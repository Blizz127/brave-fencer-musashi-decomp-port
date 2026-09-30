/* Complete retail function; independently matched ordinary C. */
/* Complete retail main [8001CFDC,8001D050), 29 words. */
typedef struct Record { unsigned short state; unsigned char rest[0x82]; } Record;
typedef struct Pool { unsigned char header[0x2a8]; Record records[192]; } Pool;
extern Pool D_800AF630;
Record *func_8001CFDC(int first, int limit)
{
    Pool *pool=&D_800AF630;
    if ((unsigned int)first>=192 || (unsigned int)limit>192) return 0;
    for (;first<limit;first++) {
        if (!pool->records[first].state) return &pool->records[first];
    }
    return 0;
}
