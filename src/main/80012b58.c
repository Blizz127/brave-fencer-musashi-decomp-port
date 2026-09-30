/* Complete retail export [80012B58,80012C6C). */
short func_80012B58(short from, short to, short divisor, short *attempts)
{
    short delta = to - from;
    short result;
    if (!*attempts || !divisor) {
        if (delta > 0x800) delta -= 0x1000;
        if (delta < -0x800) delta += 0x1000;
        return delta;
    }
    if (delta > 0x800) delta -= 0x1000;
    if (delta < -0x800) delta += 0x1000;
    result = delta / divisor;
    if (result) return result;
    --*attempts;
    return func_80012B58(from,to,divisor/2,attempts);
}
