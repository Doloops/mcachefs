/*
 * mcachefs-file.h
 *
 *  Created on: 21 oct. 2009
 *      Author: francois
 */

#ifndef MCACHEFSFILE_H_
#define MCACHEFSFILE_H_

#include "mcachefs-types.h"

/**
 * ***************************** FILE ********************************
 * File Handling stuff
 */

/**
 * Find an openned file incrementing its use, or create one if not found (with use=1) - locks mcachefs_file_lock
 * @param path the path of the file to find
 * @type the type (for coherency check)
 * @return the found or created fh
 */
mcachefs_fh_t
mcachefs_fileid_get(struct mcachefs_metadata_t *mdata, const char *path,
                    mcachefs_file_type_t type);

/**
 * Decrement use of the fh - locks mcachefs_file_lock
 * This will call mcachefs_file_release()
 * @param fh the fh to release
 */
void mcachefs_fileid_put(mcachefs_fh_t fh);

/**
 * Get an openned file from its fh - does not lock mcachefs_file_lock
 * @param fh the fh
 * @return the corresponding file (shall NOT be NULL)
 */
struct mcachefs_file_t *mcachefs_file_get(mcachefs_fh_t fh);

/**
 * Decrement use of a file - locks mcachefs_file_lock
 * If use falls down to zero, then we may release some ressources
 * @param mfile the corresponding mfile
 */
void mcachefs_file_release(struct mcachefs_file_t *mfile);

/**
 * Remove a file (putting it in the freed list) - must be called with mcachefs_file_lock HELD, and with a use=0
 * @param mfile the mfile to remove
 */
void mcachefs_file_remove(struct mcachefs_file_t *mfile);

/**
 * Release ressources of an openned file (real and backing fds, if we can)
 * @param mfile the mfile to cleanup
 */
void mcachefs_file_cleanup_file(struct mcachefs_file_t *mfile);

/**
 * Get the real or backing fd of a given file, incrementing its use_real_fd or use_backing_fd
 * @param mfile the mfile
 * @real 1 for the real fd, 0 for the backing
 * @return the corresponding fd
 */
int mcachefs_file_getfd(struct mcachefs_file_t *mfile, int real, int flags);

/**
 * Get the real or backing fd of a given file, with a given mode, incrementing its use_real_fd or use_backing_fd
 * @param mfile the mfile
 * @real 1 for the real fd, 0 for the backing
 * @return the corresponding fd
 */
int
mcachefs_file_getfd_mode(struct mcachefs_file_t *mfile, int real, int flags,
                         mode_t mode);

/**
 * Decrement use_real_fd or use_backing_fd
 * @param mfile the mfile
 * @real 1 for the real fd, 0 for the backing
 */
void mcachefs_file_putfd(struct mcachefs_file_t *, int real);

/**
 * Extend size to (at least) offset
 */
void
mcachefs_file_update_metadata(struct mcachefs_file_t *mfile, off_t size,
                              int modified);

/**
 * Get metadata from an openned file (locks mcachefs_metadata_lock() )
 */
struct mcachefs_metadata_t *mcachefs_file_get_metadata(struct mcachefs_file_t
                                                       *mfile);

/**
 * Walk down the list of fds to find the next file to backup
 * @return the next file to backup, or NULL if none
 */
struct mcachefs_file_t *mcachefs_file_get_next_file_to_back();

/**
 * Run the file garbage thread (using timeslice mechanism to garbage collect)
 */
void mcachefs_file_start_thread();

/**
 * Stop the file thread
 */
void mcachefs_file_stop_thread();

/**
 * VOPS : dump the list of openned files to the mvops (file '.mcachefs/files')
 */
void mcachefs_file_dump(struct mcachefs_file_t *mvops);

/**
 * **************************** TIMESLICE *************************************
 * File timeslice (garbage collector) mechanisms
 */
/**
 * Init timeslice variables, to be called in main()
 */
void mcachefs_file_timeslice_init_variables();

/**
 * Freshen a file, setting it freshly used. Shall be called with mcachefs_file_lock HELD
 */
void mcachefs_file_timeslice_do_freshen(struct mcachefs_file_t *mfile);

/**
 * Freshen a file, setting it freshly used. Does lock mcachefs_file_lock
 */
void mcachefs_file_timeslice_freshen(struct mcachefs_file_t *mfile);

/**
 * Free a file, putting it in the freed fake-timeslice. Shall be called with mcachefs_file_lock HELD
 */
void mcachefs_file_timeslice_insert_in_freed(struct mcachefs_file_t *mfile);

/**
 * Get a freed file from the freed list. Shall be called with mcachefs_file_lock HELD.
 */
struct mcachefs_file_t *mcachefs_file_timeslice_get_freed();

/**
 * Insert a given file in the current timeslice. Shall be called with mcachefs_file_lock HELD.
 */
void mcachefs_file_timeslice_insert(struct mcachefs_file_t *mfile);

/**
 * Remove a given file from its timeslice. Shall be called with mcachefs_file_lock HELD.
 */
void mcachefs_file_timeslice_remove(struct mcachefs_file_t *mfile);

/**
 * Periodical garbage collecting using timeslices. Shall be called with mcachefs_file_lock HELD.
 * Actions performed are, with different intervals :
 * - cleanup file ressources
 * - cleanup dir metadata
 * - free the freed mcachefs_file_t*
 */
void mcachefs_file_timeslice_cleanup();

/**
 * Update the timeslice, putting the current to the next timeslice. Shall be called with mcachefs_file_lock HELD.
 */
void mcachefs_file_timeslice_update();

/**
 * VOPS : dump the timeslice (file .mcachefs/timeslices)
 */
void mcachefs_file_timeslices_dump(struct mcachefs_file_t *mvops);

/**
 * Count number of active files in timeslice
 */
int mcachefs_file_timeslices_count_open();

#endif /* MCACHEFSFILE_H_ */
