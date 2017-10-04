#ifndef __MCACHEFS_HASH_H
#define __MCACHEFS_HASH_H

#include "mcachefs-types.h"

/**
 * Reentrant Hashing algorithm
 */
hash_t continueHashPartial(hash_t h, const char *str, int sz);
hash_t continueHash(hash_t h, const char *str);
hash_t doHash(const char *str);
hash_t doHashPartial(const char *str, int sz);

#endif // __MCACHEFS_HASH_H
