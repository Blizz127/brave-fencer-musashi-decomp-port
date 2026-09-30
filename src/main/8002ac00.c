/* Complete native export and matched C [8002AC00,8002AC98). */
extern unsigned char func_800291B4(int);
extern void func_800291A0(int,int);
extern unsigned short D_800A6588[];
extern unsigned char D_8010F46E[];
extern unsigned short D_8010F46C[];
void func_8002AC00(unsigned char id)
{
    unsigned char flag = id + 0x62;
    unsigned short *destination = D_800A6588;
    if (!(func_800291B4(flag) & 0x80)) {
        func_800291A0(flag, D_8010F46E[id * 8] | 0x80);
        destination[id-1] = D_8010F46C[id * 4];
    }
}
