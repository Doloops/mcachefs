/*
 * mcachefs-journal.h
 *
 *  Created on: 21 oct. 2009
 *      Author: francois
 */

#ifndef MCACHEFSJOURNAL_H_
#define MCACHEFSJOURNAL_H_

/**
 * Journal operations
 */
enum __mcachefs_journal_op
{
    mcachefs_journal_op_none = 0x0,
    mcachefs_journal_op_mknod = 0x1,
    mcachefs_journal_op_mkdir = 0x2,
    mcachefs_journal_op_unlink = 0x3,
    mcachefs_journal_op_rmdir = 0x4,
    mcachefs_journal_op_symlink = 0x5,
    mcachefs_journal_op_rename = 0x6,
    mcachefs_journal_op_link = 0x7,
    mcachefs_journal_op_chmod = 0x8,
    mcachefs_journal_op_chown = 0x9,
    mcachefs_journal_op_truncate = 0xa,
    mcachefs_journal_op_utime = 0xb,
    mcachefs_journal_op_fsync = 0xc,
    mcachefs_journal_op_MAX = 0xd
};

typedef enum __mcachefs_journal_op mcachefs_journal_op;

struct mcachefs_journal_entry_t
{
    int op;
    int path_sz;
    int to_sz;                  // for symlink, link and rename
    mode_t mode;                // for mknod, mkdir, chmod
    dev_t rdev;                 // for mknod
    uid_t uid;
    gid_t gid;                  // for chown
    off_t size;                 // for truncate
    struct utimbuf utimbuf;     // for utime
};


/**
 * ********************** JOURNAL *****************************
 */
/**
 * Init journal values, check for pending journals
 */
void mcachefs_journal_init();

/**
 * Append an entry in the update journal
 */
void mcachefs_journal_append(mcachefs_journal_op op, const char *path,
                             const char *to, mode_t mode, dev_t rdev,
                             uid_t uid, gid_t gid, off_t size,
                             struct utimbuf *utimbuf);

/**
 * Check if a new path was renamed in the journal, ie the provided path is a target in the path rename
 */
int mcachefs_journal_was_renamed(const char *path);

/**
 * Apply current journal
 */
void mcachefs_journal_apply();

/**
 * Drop current journal
 */
void mcachefs_journal_drop();

/**
 * Notify end of sync
 */
void mcachefs_notify_sync_end(const char *path, int success);


/**
 * Count actual number of entries in journal
 */
int mcachefs_journal_count_entries();

/**
 * Dump current journal contents
 */
void mcachefs_journal_dump(struct mcachefs_file_t *mvops);

#endif /* MCACHEFSJOURNAL_H_ */

