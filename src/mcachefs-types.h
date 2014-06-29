/*
 * mcachefs-types.h
 *
 *  Created on: 26 juin 2014
 *      Author: francois
 */

#ifndef __MCACHEFS_TYPES_H_
#define __MCACHEFS_TYPES_H_

#include <sys/types.h>

/**
 * Fuse File Handlers, provided by mcachefs at open() and used in read(), write() and release()
 */
typedef unsigned long long mcachefs_fh_t;

/**
 * Unique identifier for metadata Id
 */
typedef unsigned long long mcachefs_metadata_id;

/**
 * Hash values, used for hashing paths
 */
typedef unsigned long long int hash_t;


/**
 * Mcachefs MUTEX type
 */
struct mcachefs_mutex_t
{
    pthread_mutex_t mutex;
    pthread_t owner;
    const char *context;
};

struct mcachefs_metadata_t;

/**
 * Types of files handled by mcachefs
 * file : regular file (can be backuped, back-written, ...)
 * dir : can have metadata associated with it, for readdir() and getattr()
 * vops : virtual-file operation (ala sysfs), contents generated in mem by mcachefs and ability to update mcachefs config at runtime
 */
enum __mcachefs_file_type_t
{
    mcachefs_file_type_file = 0x1,
    mcachefs_file_type_dir = 0x2,
    mcachefs_file_type_vops = 0x3,
};
typedef enum __mcachefs_file_type_t mcachefs_file_type_t;

/**
 * Transfer action file
 */
struct mcachefs_file_transfer_t
{
    int tobacking;
    off_t total_size;
    off_t transfered_size;
    off_t rate;
    time_t total_time;
};

/**
 * Structure for source access (real or backing)
 */
struct mcachefs_file_source_t
{
    int fd;                     //< The file descriptor to use
    int use;                    //< Number of concurrent accesses to this fd
    int wr;                    //< Set to TRUE to indicate that fd is openned wr
    size_t bytesrd;             // Number of bytes read
    size_t nbrd;                // Number of read accesses
    size_t byteswr;             // Number of bytes written
    size_t nbwr;                // Number of write accesses
};

#define MCACHEFS_FILE_SOURCE_BACKING 0
#define MCACHEFS_FILE_SOURCE_REAL    1

/**
 * Openned file structure, which can be a regular file, a dir, or a vops file
 */
struct mcachefs_file_t
{
    /**
     * General file header
     */
    hash_t hash;                //< The hash value of the file
    char *path;          //< The strdup()ed value of the path provided at open()
    mcachefs_file_type_t type;  //< The type of the file openned
    mcachefs_metadata_id metadata_id;   //< The corresponding metadata id

    /**
     * Global usage :
     * use : may not be destroyed if use is non-zero
     * ttl : explicitly indicated the time-to-live of the file (unused)
     * mutex : an internal protection lock for fds, metadata, ...
     * mcachefs_file_lock has a lock precedence over each fd_lock :
     *    if a thread has locked a fd_lock, it shall not try to lock mcachefs_file_lock at all
     */
    int use;
    time_t ttl;
    struct mcachefs_mutex_t mutex;

    /**
     * File descriptor accessors
     */
    struct mcachefs_file_source_t sources[2];

    /**
     * Backing part
     */
    int cache_status; //< Indicate the state of the backing : asked, in progress, done
    int dirty; //< If the file has been written, this flag indicates the backing file is fresher than the real one
    // off_t backed_size; //< How many bytes have been backed now, only available when backing == IN_PROGRESS

    /**
     * Transfer information
     */
    struct mcachefs_file_transfer_t transfer;

    /**
     * VOPS stuff
     * vops mean being able to have in-mem contents for a file from open() till release()
     */
    char *contents; // Pointer to the contents, which must be NULL or malloc()ed (ie not mmap()ed)
    off_t contents_size;        // Size of the valuable part of the contents
    off_t contents_alloced; // Size of the last allocation of contents (how much we can write to it).

    /*
     * Timeslice double-linked-list
     */
    int timeslice;          //< only valid for head timeslice (previous == NULL)
    struct mcachefs_file_t *timeslice_previous;
    struct mcachefs_file_t *timeslice_next;

};

/**
 * Various states and enums
 */

#define MCACHEFS_FILE_BACKING_ASKED       0
#define MCACHEFS_FILE_BACKING_IN_PROGRESS 1
#define MCACHEFS_FILE_BACKING_DONE        2
#define MCACHEFS_FILE_BACKING_ERROR       3

/**
 * States of the backing mechanism
 */
#define MCACHEFS_STATE_NORMAL   0
#define MCACHEFS_STATE_FULL     1
#define MCACHEFS_STATE_HANDSUP  2
#define MCACHEFS_STATE_NOCACHE  3
#define MCACHEFS_STATE_QUITTING 4

/**
 * States of the writing mechanism
 */
#define MCACHEFS_WRSTATE_CACHE 0
#define MCACHEFS_WRSTATE_FLUSH 1
#define MCACHEFS_WRSTATE_FORCE 2



#endif /* __MCACHEFS_TYPES_H_ */
