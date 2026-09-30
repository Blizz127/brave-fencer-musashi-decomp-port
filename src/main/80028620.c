/* Exact retail word export for [80028620,80028694); EXE and assembly verified. */
typedef struct { int x,y,z; unsigned char r,g,b,pad; } Light;
extern void func_80053328(int,Light *);
void func_80028620(int index, Light *source)
{
    Light copy = *source;
    if (!(copy.x | copy.y | copy.z)) {
        copy.x = 0x1000;
        copy.r = copy.g = copy.b = 0;
    }
    func_80053328(index,&copy);
}
