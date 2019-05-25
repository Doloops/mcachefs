/*
 * mcachefs-util.h
 *
 *  Created on: 21 oct. 2009
 *      Author: francois
 */

#ifndef MCACHEFSUTIL_H_
#define MCACHEFSUTIL_H_

#define __IS_WRITE(__flag) ( (__flag) & ( O_RDWR | O_WRONLY ) )


/**
 * Utility parts : make paths (prefix them with the real- or backing-part), create backing paths on purpose, ..
 */
char *mcachefs_makepath(const char *path, const char *prefix);
char *mcachefs_makepath_source(const char *path);
char *mcachefs_makepath_cache(const char *path);

int mcachefs_createpath(const char *prefix, const char *path, int lastIsDir);

/**
 * Create the backing path
 */
int mcachefs_createpath_cache(const char *path, int lastIsDir);

/**
 * Create the file in cache (touch)
 */
int mcachefs_createfile_cache(const char *path, mode_t mode);

/**
 * Checks if the file is in cache
 * Returns 0 if not in cache, non-zero if in cache
 */
int mcachefs_fileincache(const char *path);

/**
 * the same as mcachefs_fileincache, but with some extra logic
 * to flag broken cached files to be re-downloaded
 */
int mcachefs_check_fileincache(struct mcachefs_file_t *mfile,
                               struct stat *metadata_st);

char *mcachefs_split_path(const char *path, const char **lname);

#endif /* MCACHEFSUTIL_H_ */
