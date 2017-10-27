#ifndef __FUSE_MCACHEFS_H
#define __FUSE_MCACHEFS_H

#define __MCACHEFS_VERSION__ "0.6.0"

#define FUSE_USE_VERSION 29

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 600
#ifndef _ATFILE_SOURCE
#define _ATFILE_SOURCE
#endif
#define __USE_GNU
#endif

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/statfs.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/statvfs.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <linux/limits.h>

#include <sys/time.h>
#include <time.h>

#define FUNCTIONCALL 1

#define PARANOID 0

#include "mcachefs-log.h"

#include "mcachefs-config.h"
#include "mcachefs-mutex.h"
#include "mcachefs-file.h"
#include "mcachefs-metadata.h"
#include "mcachefs-util.h"

extern struct fuse_operations mcachefs_oper;

extern struct stat mcachefs_target_stat;

extern time_t __mcachefs_jiffy_sec;

static inline time_t
mcachefs_get_jiffy_sec()
{
    return __mcachefs_jiffy_sec;
}

#endif // __FUSE_MCACHEFS_H
