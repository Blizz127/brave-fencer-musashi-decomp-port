/* Exact retail word export for [80011C10,80011C8C); EXE and assembly verified. */
/* Retail [80011C10,80011C8C): increment substate and clear four halfword fields. */
extern unsigned char D_800AF630[];
void func_80011C10(void)
{
    register unsigned char *state = D_800AF630;
    ++*(short *)(state + 0xA3C6);
    *(short *)(state + 0xA3CC) = 0;
    *(short *)(state + 0xA3D0) = 0;
    *(short *)(state + 0xA3C8) = 0;
    *(short *)(state + 0xA3CE) = 0;
}

