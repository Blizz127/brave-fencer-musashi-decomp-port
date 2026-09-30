/* Exact retail [8001C9D0,8001CA1C). */
/* Retail [8001C9D0,8001CA1C): initialize object transform and control fields. */
void func_8001C9D0(unsigned char *object)
{
    *(short *)(object+0x1C) = 0x1000;
    *(short *)(object+0x1A) = 0x1000;
    *(short *)(object+0x18) = 0x1000;
    *(short *)(object+0x14) = 0;
    *(short *)(object+0x12) = 0;
    *(short *)(object+0x10) = 0;
    *(short *)(object+0x0C) = 0;
    *(short *)(object+0x0A) = 0;
    *(short *)(object+0x08) = 0;
    *(int *)(object+0x04) = 0;
    *(int *)(object+0x20) = 0;
    *(short *)(object+0x28) = 200;
    *(short *)(object+0x2A) = 200;
    *(short *)(object+0x2C) = 0;
    *(short *)(object+0x2E) = 0;
    *(int *)(object+0x80) = 0;
}

