/* Native retail instruction export [80014774,800147B8).
 * Verified against asm/main.s and the extracted EXE. No C match claim. */
/* Retail [80014774,800147B8): clear the 640x480 rectangle to black. */
typedef struct Rect { short x, y, width, height; } Rect;
extern int func_80059888(Rect *, unsigned char, unsigned char, unsigned char);
void func_80014774(void)
{
    Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = 640;
    rect.height = 480;
    func_80059888(&rect, 0, 0, 0);
}

