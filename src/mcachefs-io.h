/*
 * mcachefs-io.h
 *
 *  Created on: 9 oct. 2010
 *      Author: francois
 */

#ifndef MCACHEFSIO_H_
#define MCACHEFSIO_H_

int
mcachefs_open_mfile(struct mcachefs_file_t* mfile, struct fuse_file_info* info, mcachefs_file_type_t type);

int
mcachefs_read_mfile(struct mcachefs_file_t* mfile, char* buf, size_t size, off_t offset);

int
mcachefs_write_mfile(struct mcachefs_file_t* mfile, const char* buf, size_t size, off_t offset);

int
mcachefs_fsync_mfile(struct mcachefs_file_t* mfile);

int
mcachefs_release_mfile(struct mcachefs_file_t* mfile, struct fuse_file_info* info);

#endif /* MCACHEFSIO_H_ */
