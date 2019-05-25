/*
 * mcachefs-transfer.h
 *
 *  Created on: 21 oct. 2009
 *      Author: francois
 */

#ifndef MCACHEFSTRANSFER_H_
#define MCACHEFSTRANSFER_H_


/**
 * ********************* TRANSFER *****************************
 */

/**
 * Backing sem
 */
extern sem_t mcachefs_backing_sem;

/**
 * Maximum number of concurrent threads
 */
#define MCACHEFS_TRANSFER_THREADS_MAX_NUMBER 64

/**
 * Transfer backing frontend
 * Call for file backup. If the backup exists and is fresh enough, do nothing, otherwise wakeup the backingthread.
 */
int mcachefs_transfer_backfile(struct mcachefs_file_t *mfile,
                               struct stat *metadata_st);

/**
 * Writeback frontend
 */
int mcachefs_transfer_writeback(const char *path);

/**
 * Queue a file for transfer
 * Transfer direction depends on mfile->backing value :
 * - MCACHEFS_FILE_BACKING_ASKED means file is aimed at being backed up
 * - MCACHEFS_FILE_BACKING_DONE means file is aimed at being written back
 * returns 0 if queued, -EEXIST if already in queue.
 */
int mcachefs_transfer_queue_file(struct mcachefs_file_t *mfile, int type);

/**
 * Generic transfer function :
 * - when tobacking=1, copies from target to backing
 * - when tobacking=0, writes back from backing to target
 */
// int mcachefs_transfer_file ( struct mcachefs_file_t* mfile, int tobacking, off_t size );

/**
 * Run the backing thread, which will be woken up by mcachefs_backing_backfile() (with backing_sem)
 */
void mcachefs_transfer_start_threads();

/**
 * Stop backing threads
 */
void mcachefs_transfer_stop_threads();


/**
 * Dump the files currently being transfered
 */
void mcachefs_transfer_dump(struct mcachefs_file_t *mvops);

/**
 * Cleanup Backing
 */
void mcachefs_cleanup_backing(struct mcachefs_file_t *mvops, int simulate);

#endif /* MCACHEFSTRANSFER_H_ */
