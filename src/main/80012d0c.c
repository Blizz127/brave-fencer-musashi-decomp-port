/* Complete retail export [80012D0C,80012DBC). */
short func_80012D0C(short from, short to, short divisor, short *attempts)
{
    short delta = to - from;
    short result;
    if (!*attempts || !divisor) return delta;
    result = delta / divisor;
    if (result) return result;
    --*attempts;
    return func_80012D0C(from,to,divisor/2,attempts);
}
