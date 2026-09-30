/* Exact retail word export for [80011B7C,80011C10); EXE and assembly verified. */
/* Retail [80011B7C,80011C10): initialize halfword state and clear related fields. */
extern unsigned char D_800AF630[];
void func_80011B7C(short value)
{
    register unsigned char *state = D_800AF630;
    *(short *)(state + 0xA3C0) = value;
    *(short *)(state + 0xA3C6) = 0;
    *(short *)(state + 0xA3CC) = 0;
    *(short *)(state + 0xA3CA) = 0;
    *(short *)(state + 0xA3D0) = 0;
    *(short *)(state + 0xA3C2) = 0;
    *(short *)(state + 0xA3C8) = 0;
    *(short *)(state + 0xA3CE) = 0;
}

