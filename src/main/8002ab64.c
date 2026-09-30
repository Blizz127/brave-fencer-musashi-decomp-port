/* Complete native export and matched C [8002AB64,8002AC00). */
extern unsigned char func_800291B4(int);
extern void func_800291A0(int, int);
extern void func_8002AC00(unsigned char);
void func_8002AB64(void)
{
    short i;
    unsigned char count = func_800291B4(0x2E) + 1;
    func_800291A0(0x2E, count);
    if (count == 1) {
        for (i = 1; i < 7; ++i) func_8002AC00(i);
        for (i = 0x3B; i < 0x40; ++i) func_800291A0(i, 3);
    }
}
