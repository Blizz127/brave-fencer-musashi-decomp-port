/* Exact retail word export for [8003C438,8003C498); EXE and assembly verified. */
extern void func_8003AE14(void *, unsigned int);
extern int D_8006B584, D_8006B580;
unsigned int func_8003C438(void *data, unsigned int size)
{
    if (size > 0x7EFF0) size = 0x7EFF0;
    func_8003AE14(data, size);
    if (!D_8006B584) D_8006B580 = 0;
    return size;
}
