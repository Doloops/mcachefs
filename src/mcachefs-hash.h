#ifndef __MCACHEFS_HASH_H
#define __MCACHEFS_HASH_H



/**
 * Hash values, used for hashing paths
 */
typedef unsigned long long int hash_t;

/**
 * Reentrant Hashing algorithm
 */
hash_t continueHashPartial (hash_t h, const char *str, int sz);
hash_t continueHash (hash_t h, const char *str);
hash_t doHash (const char *str);
hash_t doHashPartial (const char *str, int sz);

#endif // __MCACHEFS_HASH_H

