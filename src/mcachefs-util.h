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
hash_t continueHashPartial ( hash_t h, const char* str, int sz );
hash_t continueHash ( hash_t h, const char* str );
hash_t doHash ( const char* str );
hash_t doHashPartial ( const char* str, int sz );

char *mcachefs_makepath(const char *path, const char *prefix);
char *mcachefs_makerealpath(const char *path);
char *mcachefs_makebackingpath(const char *path);

char *mcachefs_makemetadatapath(const char* path);
int mcachefs_createpath(const char* prefix, const char* path, int lastIsDir);

/**
 * Create the backing path
 */
int mcachefs_createbackingpath(const char* path, int lastIsDir);

/**
 * Create the backing file
 */
int mcachefs_backing_createbackingfile ( const char* path, mode_t mode );

int mcachefs_fileinbacking(const char *path);

char* mcachefs_split_path ( const char* path, const char** lname );

/**
 * General status and configuration retrival and setting
 */
void mcachefs_setstate ( int status );
int mcachefs_getstate ( );

void mcachefs_setwrstate ( int wrstate );
int mcachefs_getwrstate ( );

int mcachefs_get_file_thread_interval();

int mcachefs_get_file_ttl();
int mcachefs_get_metadata_ttl();

off_t mcachefs_get_transfer_max_rate ();

/**
 * Cleanup Backing configuration
 */
int mcachefs_get_cleanup_backing_age();
const char* mcachefs_get_cleanup_backing_prefix();


#endif /* MCACHEFSUTIL_H_ */
