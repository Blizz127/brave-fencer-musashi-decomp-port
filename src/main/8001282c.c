/* PSY-Q library routine [8001282C,800128B4) from the SLUS executable.
 * SHA256(span)=cc4d6b1325599fe604328d90fedbdf2d4878525f79804f08351b5292e6f5eeb4.
 * Word export only: the native seam needs the retail instruction stream so the
 * guest can execute the library routine it calls. No C match claim yet.
 */
#include "psx_types.h"

typedef struct {
    s32 a, b;
} S8_8001282c;

void func_800128B4(void *);
void func_80013ED0(void *, void *, void *, void *);
void func_80013F98(void *, void *);
void func_80013FBC(void *, void *);
void func_80013FE0(void *, void *);

void func_8001282C(void *arg0) {
    S8_8001282c sp10;
    S8_8001282c sp18;
    S8_8001282c sp20;

    func_80013FE0(arg0, &sp20);
    func_800128B4(&sp20);
    func_80013FBC(arg0, &sp18);
    func_800128B4(&sp18);
    func_80013F98(arg0, &sp10);
    func_800128B4(&sp10);
    func_80013ED0(arg0, &sp10, &sp18, &sp20);
}
