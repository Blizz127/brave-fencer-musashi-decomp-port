/* Retail EXE [80011380,80011680), including shared epilogue at11664.
 * Every instruction verified against EXE and asm/main.s. No C match claim. */
/* Retail complete range [80011380,80011680), including shared epilogue.
 * GCC 2.7.2 / ASPSX 2.56 -O0 -G0: exact 192-word match.
 * The empty input constraint keeps both index values live to preserve the
 * observed register copy; it emits no instruction assembly.
 */
extern unsigned char D_800AF630[], D_800D3A68[];
typedef struct Other { unsigned char pad[0x16]; unsigned char value; } Other;
extern Other D_800AE6A8;
extern short D_800B99E8;
extern int D_80074784, D_8007478C;
extern unsigned int D_800629D4[];
extern unsigned short D_800629E8[];
extern unsigned short D_800629D6[];
extern void func_800295D4(void),func_80029664(void),func_80011DCC(void),func_80011E24(void),func_800118AC(void),func_80011EB4(void),func_800CEDFC(void),func_80011DA0(void);
extern void func_8005C4CC(int),func_8002D4C8(int,int),func_80011B7C(int),func_800D1724(void *);
extern short func_80014CAC(int,int);
void func_80011380(void)
{
    register unsigned char *state = D_800AF630;
    Other *other = &D_800AE6A8;
    switch (*(unsigned short *)(state + 0xA3B4)) {
    case 0:
        D_800B99E8 = 0;
        func_800295D4();
        if (D_80074784 >= 5) D_80074784 = 0;
        *(unsigned short *)(state + 0xA3D8) = *(unsigned short *)&D_800629D4[D_80074784];
        *(unsigned short *)(state + 0xA3DA) = *(unsigned short *)&D_800629D4[D_80074784] & 0xF000;
        other->value = *(unsigned char *)&D_800629D6[({
            register int index = D_80074784;
            register int copy = index;
            __asm__("" : : "r"(index), "r"(copy));
            copy;
        }) << 1];
        D_8007478C = D_800629E8[D_80074784];
        if (D_80074784 == 0) func_80029664();
        func_80011DCC();
        func_80011E24();
        func_8005C4CC(1);
        state[0xA434] = 1;
        state[0xA3E0] = 0;
        ++D_80074784;
        func_8002D4C8(40,0);
        func_80011B7C(0);
        func_800118AC();
        break;
    case 1:
        if (state[0xA3E0]) func_80011EB4();
        func_800CEDFC();
        if (++D_800B99E8 > D_8007478C ||
            (func_80014CAC(0,0x840) && state[0xA3E0])) {
            func_80011DA0();
            func_800D1724(D_800D3A68);
            func_800118AC();
        }
        break;
    case 2:
        func_800CEDFC();
        break;
    }
}

