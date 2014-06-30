/*
 * mcachefs-metadata.h
 *
 *  Created on: 21 oct. 2009
 *      Author: francois
 */

#ifndef MCACHEFSMETADATA_H_
#define MCACHEFSMETADATA_H_

static const mcachefs_fh_t mcachefs_fh_t_NULL = ~((mcachefs_fh_t) 0);
static const mcachefs_metadata_id mcachefs_metadata_id_EMPTY =
        ~((mcachefs_metadata_id) 0);

struct mcachefs_metadata_t
{
    hash_t hash;
    char d_name[NAME_MAX + 1];

    mcachefs_metadata_id id;    //< Myself
    mcachefs_metadata_id father; //< Pointer to my father dir
    mcachefs_metadata_id child; //< First child (head of the tree)
    mcachefs_metadata_id next;  //< Next child

    mcachefs_metadata_id up;    //< Hashtree father
    mcachefs_metadata_id left;  //< Hashtree left part, where all hashes are stricly inf
    mcachefs_metadata_id right; //< Hashtree right part, where hashes are equal or sup (collision mgmt)

    mcachefs_metadata_id collision_next; //< Collision : next in the linked-list of colliders
    mcachefs_metadata_id collision_previous; //< Collision : previous in the linked-list of colliders

    mcachefs_fh_t fh;
    struct stat st;
};

/**
 * *********************** METADATA *******************************
 */

void
mcachefs_metadata_open();
void
mcachefs_metadata_close();

struct mcachefs_metadata_t *
mcachefs_metadata_get(mcachefs_metadata_id id);

struct mcachefs_metadata_t *
mcachefs_metadata_get_child(struct mcachefs_metadata_t *father);

struct mcachefs_metadata_t *
mcachefs_metadata_find_locked(const char *path); // Assert that mcachefs_metadata_lock IS locked
struct mcachefs_metadata_t *
mcachefs_metadata_find(const char *path); // Locks mcachefs_metadata_lock, remains locked
void
mcachefs_metadata_release(struct mcachefs_metadata_t *mdata); // Releases mcachefs_metadata_lock

void
mcachefs_metadata_flush();
void
mcachefs_metadata_flush_entry(const char *path);
void
mcachefs_metadata_fill_entry(struct mcachefs_file_t *mfile);
void
mcachefs_metadata_fill(const char *path);

int
mcachefs_metadata_getattr(const char *path, struct stat *stbuf);

void
mcachefs_metadata_dump(struct mcachefs_file_t *mvops);

int
mcachefs_metadata_make_entry(const char *path, mode_t mode, dev_t rdev);
int
mcachefs_metadata_rmdir_unlink(const char *path, int isDir);
int
mcachefs_metadata_rename_entry(const char *path, const char *to);
int
mcachefs_metadata_unlink(const char *path);

void
mcachefs_metadata_clean_fh_locked(mcachefs_metadata_id id);

/**
 * Allcate and return the full path of a given mdata
 */
char *
mcachefs_metadata_get_path(struct mcachefs_metadata_t *mdata);

/**
 * When metadata is renamed we have to also rename the path of the openned file
 */
void
mcachefs_metadata_update_fh_path(struct mcachefs_metadata_t *mdata);

#endif /* MCACHEFSMETADATA_H_ */
