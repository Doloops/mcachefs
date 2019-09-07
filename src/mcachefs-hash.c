#include "mcachefs-hash.h"

#include <stdio.h>
#define Log(...) fprintf(stderr, __VA_ARGS__)

#ifdef __MCACHEFS_HASH_USE_CRC32

#include "Crc32.h"

typedef hash_t uint32_t;
typedef unsigned char uint8_t;

/// zlib's CRC32 polynomial
const uint32_t Polynomial = 0xEDB88320;

uint32_t crc32_bitwise(const void* data, size_t length, uint32_t previousCrc32)
{
  uint32_t crc = ~previousCrc32; // same as previousCrc32 ^ 0xFFFFFFFF
  const uint8_t* current = (const uint8_t*) data;

  while (*current && length-- != 0)
  {
    crc ^= *current++;
    for (int j = 0; j < 8; j++)
    {
      // branch-free
      crc = (crc >> 1) ^ (-((int32_t)(crc & 1)) & Polynomial);

      // branching, much slower:
      //if (crc & 1)
      //  crc = (crc >> 1) ^ Polynomial;
      //else
      //  crc =  crc >> 1;
    }
  }
  //  Log("crc32(%s, %d, %lx) = %lx\n", (const char*)data, length, previousCrc32, ~crc);
  return ~crc; // same as crc ^ 0xFFFFFFFF
}
#endif

#ifdef __MCACHEFS_HASH_USE_CRC32_EXT
/// compute CRC32 (bitwise algorithm), taken from https://create.stephan-brumme.com/crc32/
hash_t
continueHashPartial(hash_t crc, const char *str, int sz)
{
    if ( sz == - 1 )
    {
        sz = 0;
        const char* s = str;
        while (*s++)
            sz++;
    }
    hash_t result = crc32_fast((void*)str, (size_t) sz, crc);
    Log("crc32(%s, %d, %lx) = %lx\n", str, sz, crc, result);
    return result;
}
#endif

#ifdef __MCACHEFS_HASH_USE_CRC64

#include "crc64table.h"
/**
 * crc64 algorithm, directly taken from Linux crc64.c
 */
hash_t
continueHashPartial(hash_t crc, const char *str, int sz)
{
    int cur, t;

    for (cur = 0; *str; cur++) {
        t = ((crc >> 56) ^ (*str++)) & 0xFF;
        crc = crc64table[t] ^ (crc << 8);
        if ( cur == sz )
        {
            break;
        }
    }
    return crc;
}
#endif




#if 0
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
#endif
#if 0
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
