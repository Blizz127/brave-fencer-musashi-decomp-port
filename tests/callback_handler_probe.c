#include <assert.h>
#include "../src/main/80042ce8.c"

/* Isolated host semantics: these host pointers are not guest RAM bindings. */
Callback D_8006CB98[9];
volatile u32 D_8006CBB8;
static unsigned seen;
static void later(void) {
    assert(D_8006CBB8 == 0);
    assert(seen == 1);
    seen++;
}
static void first(void) {
    assert(D_8006CBB8 == 0);
    assert(seen == 0);
    seen++;
    D_8006CB98[0] = 0;
    D_8006CB98[7] = later;
}
static void forbidden(void) { assert(0 && "called stale or out-of-range slot"); }
int main(void) {
    D_8006CBB8 = 0xffffffffu;
    D_8006CB98[0] = first;
    D_8006CB98[7] = forbidden;
    D_8006CB98[8] = forbidden;
    func_80042CE8();
    assert(seen == 2 && D_8006CBB8 == 0);
    D_8006CB98[7] = 0;
    func_80042CE8();
    assert(seen == 2 && D_8006CBB8 == 1);
    return 0;
}
