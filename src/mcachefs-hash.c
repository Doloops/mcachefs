#include "mcachefs-hash.h"

#if 1
hash_t
continueHashPartial(hash_t h, const char *str, int sz)
{
    int cur = 0;
    const char *c;
    unsigned char d;
    for (c = str; *c; c++)
    {
        d = (unsigned char) (*c);

        h = d + (h << 6) + (h << 16) - h;

        cur++;
        if (cur == sz)
            break;
    }
    return h;
}
#else
hash_t
continueHashPartial(hash_t h, const char *str, int sz)
{
    int cur = 0;
    const char *c;
    unsigned char d;
    for (c = str; *c; c++)
    {
        d = (unsigned char) (*c);

        h += d + d % 2;
        cur++;
        if (cur == sz)
            break;
    }
    return h % 16;
}
#endif

hash_t
continueHash(hash_t h, const char *str)
{
    return continueHashPartial(h, str, ~((int) 0));
}

hash_t
doHashPartial(const char *str, int sz)
{
    hash_t h = 0x245;
    return continueHashPartial(h, str, sz);
}

hash_t
doHash(const char *str)
{
    return doHashPartial(str, ~((int) 0));
}
