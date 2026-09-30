/* Exact retail word export [80011E24,80011E84); no C recovery claim. */
/* Retail [80011E24,80011E84): reset counters and conditionally clear state flag.
 * GCC 2.7.2 / ASPSX 2.56 -O0 -G0: exact 24-word match.
 */
extern unsigned char D_800AF630[];
extern int D_80074788, D_800629D0;
extern short D_80074794, D_80074798;
void func_80011E24(void)
{
    register unsigned char *state = D_800AF630;
    /* Preserve the observed unused stack allocation at -O0. */
    int unused;
    D_80074788 = 0;
    if (D_800629D0 == 0)
        state[0xA434] = 0;
    D_80074794 = 0;
    D_80074798 = 0;
}

