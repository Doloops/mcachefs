/*
 * mcachefs-mutex.h
 *
 *  Created on: 21 oct. 2009
 *      Author: francois
 */

#ifndef MCACHEFSMUTEX_H_
#define MCACHEFSMUTEX_H_

#include "mcachefs-types.h"

#include <pthread.h>
#include <sys/timeb.h>
#include <string.h>

/**
 * MCachefs mutex interface, based on pthread_mutex
 */
void mcachefs_mutex_init (struct mcachefs_mutex_t *mutex);
void mcachefs_mutex_destroy (struct mcachefs_mutex_t *mutex,
                             const char *name);

#ifdef  __MCACHEFS_MUTEX_DEBUG

void mcachefs_mutex_lock (struct mcachefs_mutex_t *mutex, const char *name,
                          const char *context);
void mcachefs_mutex_unlock (struct mcachefs_mutex_t *mutex, const char *name,
                            const char *context);
void mcachefs_mutex_check_locked (struct mcachefs_mutex_t *mutex,
                                  const char *name, const char *context);
void mcachefs_mutex_check_unlocked (struct mcachefs_mutex_t *mutex,
                                    const char *name, const char *context);

#else

#define mcachefs_mutex_lock(__mutex,name,context) pthread_mutex_lock(&((__mutex)->mutex))
#define mcachefs_mutex_unlock(__mutex,name,context) pthread_mutex_unlock(&((__mutex)->mutex))
#define mcachefs_mutex_check_locked(__mutex,name,context) do{} while(0)
#define mcachefs_mutex_check_unlocked(__mutex,name,context) do{} while(0)

#endif

extern struct mcachefs_mutex_t mcachefs_metadata_mutex;
extern struct mcachefs_mutex_t mcachefs_file_mutex;
extern struct mcachefs_mutex_t mcachefs_journal_mutex;
extern struct mcachefs_mutex_t mcachefs_transfer_mutex;

#define __CONTEXT __FUNCTION__
#define mcachefs_metadata_lock() do { \
    mcachefs_file_check_unlocked(); \
    mcachefs_mutex_lock ( &mcachefs_metadata_mutex, "metadata", __CONTEXT ); } while (0)
#define mcachefs_metadata_unlock() mcachefs_mutex_unlock ( &mcachefs_metadata_mutex, "metadata", __CONTEXT )
#define mcachefs_metadata_check_locked() mcachefs_mutex_check_locked ( &mcachefs_metadata_mutex, "metadata", __CONTEXT )
#define mcachefs_metadata_check_unlocked() mcachefs_mutex_check_unlocked ( &mcachefs_metadata_mutex, "metadata", __CONTEXT )

#define mcachefs_file_lock() do { \
    mcachefs_mutex_lock ( &mcachefs_file_mutex, "file", __CONTEXT ); } while (0)
#define mcachefs_file_unlock() mcachefs_mutex_unlock ( &mcachefs_file_mutex, "file", __CONTEXT )
#define mcachefs_file_check_locked() mcachefs_mutex_check_locked ( &mcachefs_file_mutex, "file", __CONTEXT )
#define mcachefs_file_check_unlocked() mcachefs_mutex_check_unlocked ( &mcachefs_file_mutex, "file", __CONTEXT )

#define mcachefs_journal_lock() mcachefs_mutex_lock ( &mcachefs_journal_mutex, "journal", __CONTEXT )
#define mcachefs_journal_unlock() mcachefs_mutex_unlock ( &mcachefs_journal_mutex, "journal", __CONTEXT )
#define mcachefs_journal_check_locked() mcachefs_mutex_check_locked ( &mcachefs_journal_mutex, "journal", __CONTEXT )
#define mcachefs_journal_check_unlocked() mcachefs_mutex_check_unlocked ( &mcachefs_journal_mutex, "journal", __CONTEXT )

#define mcachefs_transfer_lock() mcachefs_mutex_lock ( &mcachefs_transfer_mutex, "backing", __CONTEXT )
#define mcachefs_transfer_unlock() mcachefs_mutex_unlock ( &mcachefs_transfer_mutex, "backing", __CONTEXT )

#define mcachefs_file_lock_file(__mfile) do { \
    mcachefs_mutex_lock ( &(__mfile->mutex), __mfile->path, __CONTEXT ); } while (0)
#define mcachefs_file_unlock_file(__mfile) mcachefs_mutex_unlock ( &(__mfile->mutex), __mfile->path, __CONTEXT )
#define mcachefs_file_check_locked_file(__mfile) mcachefs_mutex_check_locked ( &(__mfile->mutex), __mfile->path, __CONTEXT )
#define mcachefs_file_check_unlocked_file(__mfile) mcachefs_mutex_check_unlocked ( &(__mfile->mutex), __mfile->path, __CONTEXT )

#endif /* MCACHEFSMUTEX_H_ */
