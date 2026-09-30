/* Main-exec range [80011EB4,800120C4) from the SLUS executable.
 * SHA256(span)=8ce37486f205bce88e1e1fc8e905ae1fef2674455643c410173997a432390e23.
 * Word export for the native seam; the body below is an
 * UNVERIFIED draft, not an oracle match claim. */
#include "psx_types.h"

extern s32 D_800629D0;
extern s32 D_80074788;
extern u16 *D_80074790;
extern u16 D_80074794;
extern u16 D_80074798;
#define D_80700000 ((u16 *)0x80700000)

extern u16 func_80014B10(int);
extern u16 func_800149E0(int);

void func_80011EB4(void) {
    u16 sp10;

    switch (D_800629D0) {
    case 0:
        sp10 = func_80014B10(0);
        D_80700000[D_80074788] = sp10;
        D_80074788++;
        sp10 = func_800149E0(0);
        D_80700000[D_80074788] = sp10;
        D_80074788++;
        break;
    case 1:
        if (D_80074790 != 0) {
            D_80074794 = D_80074790[D_80074788++];
            D_80074798 = D_80074790[D_80074788++];
        } else {
            D_80074794 = 0;
            D_80074798 = 0;
        }
        break;
    case 2:
        D_80074794 = D_80700000[D_80074788++];
        D_80074798 = D_80700000[D_80074788++];
        break;
    }
}
