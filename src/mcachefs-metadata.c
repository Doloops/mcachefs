#include "mcachefs.h"
#include "mcachefs-hash.h"
#include "mcachefs-vops.h"
#include "mcachefs-transfer.h"

/**********************************************************************
 Metadata functions
 **********************************************************************/

#if 1
#define Log_M Log
#else
#define Log_M(...)
#undef Log
#define Log(...) do{} while(0)
#endif

#define MCACHEFS_METADATA_MAX_LEVELS 1024

#define __MCACHEFS_METADATA_HAS_FILLENTRY
// #define __MCACHEFS_METADATA_HAS_SYNC

/**
 * Number of entries to alloc in a single mmap()
 */
#define MCACHEFS_METADATA_BLOCK_BITS (10)
#define MCACHEFS_METADATA_BLOCK_SIZE ((1 << MCACHEFS_METADATA_BLOCK_BITS))
#define MCACHEFS_METADATA_BLOCK_MASK (MCACHEFS_METADATA_BLOCK_SIZE - 1)

/**
 * Size of each metadata entry. Shall be a power of 2 to align on mem blocks
 */
#define MCACHEFS_METADATA_ENTRY_SIZE ((unsigned long )(1 << 9))

static const char* MCACHEFS_METADATA_MAGIC = "mcachefs.metafile.1c";

DIR *
fdopendir(int __fd);

struct mcachefs_metadata_head_t
{
    char magic[64];
    mcachefs_metadata_id alloced_nb;
    mcachefs_metadata_id first_free;
};

static struct mcachefs_metadata_head_t* mcachefs_metadata_head = NULL;
static const mcachefs_metadata_id mcachefs_metadata_id_root = 1;

static int mcachefs_metadata_fd = -1;

struct mcachefs_metadata_map_t
{
    struct mcachefs_metadata_t* map;
    int use;
};

struct mcachefs_metadata_map_t* metadata_map = NULL;
size_t metadata_map_sz = 0;

/**
 * **************************************** VERY LOW LEVEL *******************************************
 * format, open and close the metafile
 */

inline static struct mcachefs_metadata_t *
mcachefs_metadata_do_get(mcachefs_metadata_id id)
{
    if (!id)
    {
        return NULL ;
    }
#if 1 // PARANOID
    mcachefs_metadata_check_locked();
    if (id == mcachefs_metadata_id_EMPTY)
    {
        Bug("Attempt to fetch an EMPTY child !\n");
    }
    if (id >= mcachefs_metadata_head->alloced_nb)
    {
        Bug(
                "Out of reach : id=%llu, alloced_nb=%llu\n", id, mcachefs_metadata_head->alloced_nb);
    }
#endif
    unsigned long long block = id >> MCACHEFS_METADATA_BLOCK_BITS;
    unsigned long block_idx = id & MCACHEFS_METADATA_BLOCK_MASK;

    if (metadata_map[block].map == NULL )
    {
        off_t block_size = MCACHEFS_METADATA_ENTRY_SIZE
                * MCACHEFS_METADATA_BLOCK_SIZE;
        off_t block_offset = block * block_size;
        struct mcachefs_metadata_t* rmap = mmap(NULL, block_size,
                PROT_READ | PROT_WRITE, MAP_SHARED, mcachefs_metadata_fd,
                block_offset);

        if (rmap == NULL || rmap == MAP_FAILED )
        {
            Err("Could not open metadata !\n");
            exit(-1);
        }
        Log(
                "Malloced block=%llu, size=%lu, offset=%lu, at %p (end at 0x%lx)\n", block, block_size, block_offset, rmap, ((long) rmap + (long) block_size));
        metadata_map[block].map = rmap;
    }

    struct mcachefs_metadata_t* meta =
            (struct mcachefs_metadata_t*) ((unsigned long) metadata_map[block].map
                    + (block_idx * MCACHEFS_METADATA_ENTRY_SIZE ));

    // Log("id=%llu, block=%llu, idx=%lu\n", id, block, block_idx);
    return meta;
}

void
mcachefs_metadata_extend_free_entries(mcachefs_metadata_id first,
        mcachefs_metadata_id last)
{
    Log("Formatting entries (first=%llu, last=%llu)\n", first, last);

    struct mcachefs_metadata_t mdata;
    memset(&mdata, 0, sizeof(struct mcachefs_metadata_t));

    mcachefs_metadata_id nfree;
    for (nfree = first; nfree < last; nfree++)
    {
        mdata.id = nfree;
        mdata.next = (nfree < last - 1) ? (nfree + 1) : 0;

        int res = pwrite(mcachefs_metadata_fd, &mdata,
                sizeof(struct mcachefs_metadata_t),
                MCACHEFS_METADATA_ENTRY_SIZE * nfree);
        if (res != sizeof(struct mcachefs_metadata_t))
        {
            Err("Could not format metafile !");
            exit(-1);
        }
    }
}

void
mcachefs_metadata_format()
{
    struct mcachefs_metadata_head_t mhead;
    memset(&mhead, 0, sizeof(mhead));

    strcpy(mhead.magic, MCACHEFS_METADATA_MAGIC);
    mhead.alloced_nb = MCACHEFS_METADATA_BLOCK_SIZE;
    mhead.first_free = 2;

    Info("Formatting metafile with magic=%s\n", mhead.magic);

    int res = pwrite(mcachefs_metadata_fd, &mhead,
            sizeof(struct mcachefs_metadata_head_t), 0);
    if (res != sizeof(struct mcachefs_metadata_head_t))
    {
        Err("Could not format metafile !");
        exit(-1);
    }

    struct mcachefs_metadata_t mdata;
    memset(&mdata, 0, sizeof(struct mcachefs_metadata_t));
    mdata.id = 1;
    strcpy(mdata.d_name, "/");
    mdata.hash = doHash("/");

    if (stat(mcachefs_config_get_source(), &(mdata.st)))
    {
        Err("Could not stat source : '%s'\n", mcachefs_config_get_source());
        exit(-1);
    }
    mdata.st.st_ino = 0;

    res = pwrite(mcachefs_metadata_fd, &mdata,
            sizeof(struct mcachefs_metadata_t), MCACHEFS_METADATA_ENTRY_SIZE );
    if (res != sizeof(struct mcachefs_metadata_t))
    {
        Err("Could not format metafile !");
        exit(-1);
    }

    mcachefs_metadata_extend_free_entries(2, MCACHEFS_METADATA_BLOCK_SIZE);
}

void
mcachefs_metadata_reset_fh()
{
    mcachefs_metadata_lock();

    mcachefs_metadata_id id;
    for (id = 1; id < mcachefs_metadata_head->alloced_nb; id++)
    {
        struct mcachefs_metadata_t *mdata = mcachefs_metadata_do_get(id);
        mdata->fh = 0;
    }

    mcachefs_metadata_unlock();
}

void
mcachefs_metadata_populate_vops();

void
mcachefs_resize_metadata_map()
{
    unsigned long long nbblocks = mcachefs_metadata_head->alloced_nb
            >> MCACHEFS_METADATA_BLOCK_BITS;
    Log(
            "Number of existing blocks : %llu (alloced_nb=%llu), current size=%lu\n", nbblocks, mcachefs_metadata_head->alloced_nb, metadata_map_sz);
    size_t new_map_sz = sizeof(struct mcachefs_metadata_map_t) * nbblocks;
    Log("Realloc from %lu to %lu in size\n", metadata_map_sz, new_map_sz);
    metadata_map = (struct mcachefs_metadata_map_t*) realloc(metadata_map, new_map_sz);
    void* metadata_map_fresh = (void*) ((long) metadata_map
            + (long) metadata_map_sz);
    Log("Realloced to metadata_map=%p, metadata_map_fresh=%p\n", metadata_map, metadata_map_fresh);
    memset(metadata_map_fresh, 0, new_map_sz - metadata_map_sz);
    metadata_map_sz = new_map_sz;
}

void
mcachefs_metadata_release_all()
{
    mcachefs_metadata_id nbblocks = mcachefs_metadata_head->alloced_nb
                >> MCACHEFS_METADATA_BLOCK_BITS;
    mcachefs_metadata_id block;
    for ( block = 1 ; block < nbblocks ; block++ )
    {
        off_t block_size = MCACHEFS_METADATA_ENTRY_SIZE
                * MCACHEFS_METADATA_BLOCK_SIZE;
        int res = munmap(metadata_map[block].map, block_size);
        if( res )
        {
            Bug("Could not munmap !");
        }
        metadata_map[block].map = NULL;
    }
}


void
mcachefs_metadata_open()
{
    if (MCACHEFS_METADATA_ENTRY_SIZE < (sizeof(struct mcachefs_metadata_t)))
    {
        Bug(
                "Invalid size for MCACHEFS_METADATA_SIZE (%lu), metadata size is (%lu)\n", MCACHEFS_METADATA_ENTRY_SIZE, sizeof(struct mcachefs_metadata_t));
    }

    Info(
            "Opening metadata file '%s' (struct metadata=%lu)\n", mcachefs_config_get_metafile(), sizeof(struct mcachefs_metadata_t));

    struct stat st;

    int is_valid = 0;

    if (stat(mcachefs_config_get_metafile(), &st) == 0 && st.st_size)
    {
        mcachefs_metadata_fd = open(mcachefs_config_get_metafile(), O_RDWR);

        if (mcachefs_metadata_fd == -1)
        {
            Err(
                    "Could not open '%s' : err=%d:%s\n", mcachefs_config_get_metafile(), errno, strerror (errno));
        }
        Log(
                "Openned metafile '%s' at fd=%d\n", mcachefs_config_get_metafile(), mcachefs_metadata_fd);

        struct mcachefs_metadata_head_t head;
        int res = pread(mcachefs_metadata_fd, &head,
                sizeof(struct mcachefs_metadata_head_t), 0);

        if (res != sizeof(struct mcachefs_metadata_head_t))
        {
            Err("Could not read metadata header !");
        }
        else if (strcmp(head.magic, MCACHEFS_METADATA_MAGIC))
        {
            Err("Invalid magic '%s'\n", head.magic);
        }
        else
        {
            is_valid = 1;
        }
    }
    else
    {
        mcachefs_metadata_fd =
                open(mcachefs_config_get_metafile(), O_CREAT | O_TRUNC | O_RDWR,
                        (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH
                                | S_IWOTH));
    }

    if (mcachefs_metadata_fd == -1)
    {
        Err("Could not open metafile '%s'\n", mcachefs_config_get_metafile());
        exit(-1);
    }

    if (!is_valid)
    {
        Log("Formatting metafile.\n");
        mcachefs_metadata_format();

        if (fstat(mcachefs_metadata_fd, &st))
        {
            Err("Could not fstat() formatted file.\n");
            exit(-1);
        }
        is_valid = 1;
    }
    else
    {
        Log( "Openned file ok.\n");
    }

    mcachefs_metadata_head = mmap(NULL, MCACHEFS_METADATA_ENTRY_SIZE,
            PROT_READ | PROT_WRITE, MAP_SHARED, mcachefs_metadata_fd, 0);

    if (mcachefs_metadata_head == NULL || mcachefs_metadata_head == MAP_FAILED )
    {
        Err("Could not open metadata !\n");
        exit(-1);
    }
    Log(
            "Openned metafile '%s', head=%p\n", mcachefs_config_get_metafile(), mcachefs_metadata_head);

    mcachefs_resize_metadata_map();
    mcachefs_metadata_reset_fh();

    mcachefs_metadata_populate_vops();

}

void
mcachefs_metadata_close()
{
    if (mcachefs_metadata_head)
    {
        if (munmap(mcachefs_metadata_head, MCACHEFS_METADATA_ENTRY_SIZE ))
        {
            Err("Could not munmap : err=%d:%s\n", errno, strerror (errno));
            exit(-1);
        }
        mcachefs_metadata_head = NULL;
    }
    if (mcachefs_metadata_fd != -1)
    {
        close(mcachefs_metadata_fd);
        mcachefs_metadata_fd = -1;
    }
}

void
mcachefs_metadata_flush()
{
    Info("Flushing metadata :\n");
    mcachefs_metadata_lock ();

    Info("\tClosing metadata...\n");
    mcachefs_metadata_close();
    Info("\tTruncating '%s'\n", mcachefs_config_get_metafile());
    if (truncate(mcachefs_config_get_metafile(), 0))
    {
        Err(
                "Could not truncate '%s' : %d:%s\n", mcachefs_config_get_metafile(), errno, strerror (errno));
    }
    Info("\tUnlinking '%s'\n", mcachefs_config_get_metafile());
    if (unlink(mcachefs_config_get_metafile()))
    {
        Err(
                "Could not unlink '%s' : %d:%s\n", mcachefs_config_get_metafile(), errno, strerror (errno));
    }
    Info("\tRe-openning '%s'\n", mcachefs_config_get_metafile());
    mcachefs_metadata_open();
    mcachefs_metadata_unlock ();
}

/**
 * **************************************** LOW LEVEL *******************************************
 * fetch, extend and allocate
 */
struct mcachefs_metadata_t *
mcachefs_metadata_get(mcachefs_metadata_id id)
{
    mcachefs_metadata_check_locked();
    return mcachefs_metadata_do_get(id);
}

struct mcachefs_metadata_t *
mcachefs_metadata_get_root()
{
    mcachefs_metadata_check_locked();
    return mcachefs_metadata_do_get(mcachefs_metadata_id_root);
}

#if 0
void
mcachefs_metadata_extend()
{
    struct mcachefs_metadata_t *metadata_old = mcachefs_metadata;

    struct mcachefs_metadata_head_t *head =
    (struct mcachefs_metadata_head_t *) mcachefs_metadata;
    struct mcachefs_metadata_t *newmeta;
    mcachefs_metadata_id alloced_nb = head->alloced_nb, first_free =
    head->alloced_nb;
    mcachefs_metadata_id current;

    Log("Could not allocate, now extend file.\n");

    munmap(mcachefs_metadata, mcachefs_metadata_size);

    alloced_nb += MCACHEFS_METADATA_EXTEND_ALLOC;

    if (ftruncate(mcachefs_metadata_fd,
                    alloced_nb * sizeof(struct mcachefs_metadata_t)))
    {
        Bug(
                "Could not ftruncate up to %llu records : err=%d:%s\n", alloced_nb, errno, strerror (errno));
    }

    mcachefs_metadata_size = alloced_nb * sizeof(struct mcachefs_metadata_t);

    mcachefs_metadata = mmap(mcachefs_metadata, mcachefs_metadata_size,
            PROT_READ | PROT_WRITE, MAP_SHARED, mcachefs_metadata_fd, 0);

    if (mcachefs_metadata == NULL || mcachefs_metadata == MAP_FAILED )
    {
        Err("Could not open metadata !\n");
        exit(-1);
    }
    Log("RE-Openned metafile '%s'\n", mcachefs_config_get_metafile());

    head = (struct mcachefs_metadata_head_t *) mcachefs_metadata;
    head->first_free = first_free;
    head->alloced_nb = alloced_nb;

    for (current = first_free; current < alloced_nb; current++)
    {
        newmeta = mcachefs_metadata_do_get(current);
        // Useless memset()
        memset(newmeta, 0, sizeof(struct mcachefs_metadata_t));
        newmeta->id = current;
        newmeta->next = current + 1 < alloced_nb ? current + 1 : 0;
    }

    if (metadata_old != mcachefs_metadata)
    {
        Log(
                "metadata : remapped mcachefs_metadata from %p to %p\n", metadata_old, mcachefs_metadata);
    }
}

mcachefs_metadata_id
mcachefs_metadata_allocate()
{
    struct mcachefs_metadata_head_t *head =
    (struct mcachefs_metadata_head_t *) mcachefs_metadata;
    struct mcachefs_metadata_t *next;

    if (!head->first_free)
    {
        mcachefs_metadata_extend();
        head = (struct mcachefs_metadata_head_t *) mcachefs_metadata;
    }
    next = mcachefs_metadata_do_get(head->first_free);
    head->first_free = next->next;

    if (next->id == 0)
    {
        Bug("Invalid next !\n");
    }
    Log(
            "Allocate : provided next=%llu, next->next=%llu\n", next->id, next->next);

    return next->id;
}
#else
mcachefs_metadata_id
mcachefs_metadata_allocate()
{
    if (!mcachefs_metadata_head->first_free)
    {
        Log("Allocate : alloced_nb=%llu\n", mcachefs_metadata_head->alloced_nb);
        mcachefs_metadata_id first_alloced = mcachefs_metadata_head->alloced_nb;
        mcachefs_metadata_id last_alloced = first_alloced
                + MCACHEFS_METADATA_BLOCK_SIZE;

        mcachefs_metadata_extend_free_entries(first_alloced, last_alloced);

        mcachefs_metadata_head->first_free = first_alloced;
        mcachefs_metadata_head->alloced_nb = last_alloced;

        Log(
                "Allocate : first_free=%llu, alloced_nb=%llu\n", mcachefs_metadata_head->first_free, mcachefs_metadata_head->alloced_nb);
        mcachefs_resize_metadata_map();
    }
    struct mcachefs_metadata_t* next = mcachefs_metadata_do_get(
            mcachefs_metadata_head->first_free);
    mcachefs_metadata_head->first_free = next->next;

    if (next->id == 0)
    {
        Bug("Invalid next !\n");
    }
    Log(
            "Allocate : provided next=%llu, next->next=%llu\n", next->id, next->next);

    return next->id;
}
#endif

int
mcachefs_metadata_equals(struct mcachefs_metadata_t *mdata, const char *path,
        int path_size)
{
    int path_start;

    while (1)
    {
        Log(
                "Equals : mdata=%p:%s (father=%llu), path=%s, path_size=%d\n", mdata, mdata->d_name, mdata->father, path, path_size);

        if (path_size <= 1)
            return mdata->father == 0;

        path_start = path_size ? path_size - 1 : 0;
        while (path_start && path[path_start] != '/')
            path_start--;

        Log("=>path_start=%d\n", path_start);

        if (strncmp(mdata->d_name, &(path[path_start + 1]),
                path_size - path_start - 1))
        {
            Log(
                    "Colliding : mdata=%s, rpath=%s (up to %d)\n", mdata->d_name, &(path[path_start + 1]), path_size - path_start - 1);
            return 0;
        }

        if (!mdata->father)
            return 0;

        mdata = mcachefs_metadata_do_get(mdata->father);
        path_size = path_start;
    }
    Bug("Shall not be here.\n");
    return -1;
    // return mcachefs_metadata_equals ( mcachefs_metadata_get ( mdata->father ), path, path_start );
}

/**
 * **************************************** HASH FUNCTIONS *******************************************
 * get, insert, remove, find and rehash
 */
static inline void
mcachefs_metadata_cleanup_hash(struct mcachefs_metadata_t *meta)
{
    meta->up = meta->left = meta->right = 0;
}

void
mcachefs_metadata_insert_hash(struct mcachefs_metadata_t *newmeta)
{
    mcachefs_metadata_cleanup_hash(newmeta);
    struct mcachefs_metadata_t *current = mcachefs_metadata_get_root();

    while (1)
    {
        if (newmeta->hash < current->hash)
        {
            if (current->left)
            {
                current = mcachefs_metadata_do_get(current->left);
                continue;
            }
            current->left = newmeta->id;
            newmeta->up = current->id;

            Log_M ("HASH Insert (%llu)->left=%llu\n", current->id,
                    newmeta->id);
            return;
        }
        else if (current->hash < newmeta->hash)
        {
            if (current->right)
            {
                current = mcachefs_metadata_do_get(current->right);
                continue;
            }
            current->right = newmeta->id;
            newmeta->up = current->id;
            Log_M ("HASH Insert (%llu)->right=%llu\n", current->id,
                    newmeta->id);
            return;

        }
        // Here we know that current->hash == newmeta->hash
        if (current->collision_next)
        {
            current = mcachefs_metadata_do_get(current->collision_next);
            continue;
        }
        Err(
                "Added collider : cur=%llu '%s', new=%llu '%s', hash='%llx'\n", current->id, current->d_name, newmeta->id, newmeta->d_name, current->hash);
        Bug("Bug because added collider (? ? ?)\n");
        current->collision_next = newmeta->id;
        newmeta->collision_previous = current->id;
        return;
    }
}

void
mcachefs_metadata_remove_hash(struct mcachefs_metadata_t *mdata)
{
    struct mcachefs_metadata_t *up, *child, *left, *right, *cup, *cleft;
    int iamleft;

    if (mdata->collision_previous)
    {
        Log("Removing a collider, prev=%llu\n", mdata->collision_previous);
        left = mcachefs_metadata_do_get(mdata->collision_previous);
        left->collision_next = mdata->collision_next;
        if (mdata->collision_next)
        {
            right = mcachefs_metadata_do_get(mdata->collision_next);
            right->collision_previous = mdata->collision_previous;
        }
        return;
    }

    if (mdata->up == 0)
    {
        Bug("Attempt to remove the '/' hash !\n");
    }

    up = mcachefs_metadata_do_get(mdata->up);
    iamleft = (up->left == mdata->id);

    if (!mdata->left && !mdata->right)
    {
        if (iamleft)
            up->left = 0;
        else
            up->right = 0;
        mcachefs_metadata_cleanup_hash(mdata);
        return;
    }

    if (!mdata->left || !mdata->right)
    {
        child = mcachefs_metadata_do_get(
                mdata->left ? mdata->left : mdata->right);
        child->up = up->id;
        if (iamleft)
            up->left = child->id;
        else
            up->right = child->id;
        mcachefs_metadata_cleanup_hash(mdata);
        return;
    }

    left = mcachefs_metadata_do_get(mdata->left);
    right = mcachefs_metadata_do_get(mdata->right);

    if (!left->right)
    {
        // Most simple case, where our left child has no right
        if (iamleft)
            up->left = left->id;
        else
            up->right = left->id;
        left->up = up->id;

        left->right = mdata->right;
        right->up = left->id;
        mcachefs_metadata_cleanup_hash(mdata);
        return;
    }

    if (!right->left)
    {
        // Second simple case, where our right child has no left
        if (iamleft)
            up->left = right->id;
        else
            up->right = right->id;
        right->up = up->id;

        right->left = mdata->left;
        left->up = right->id;
        mcachefs_metadata_cleanup_hash(mdata);
        return;

    }

    for (child = left; child->right;
            child = mcachefs_metadata_do_get(child->right))
        ;
    // We now that child has no right
    if (child->right)
    {
        Bug("Child had a right !\n");
    }

    // child now is the direct predecessor
    cup = mcachefs_metadata_do_get(child->up);
    if (cup->hash == child->hash)
    {
        Bug("Hash collision detected in hash remove !\n");
    }

    if (cup->right != child->id)
    {
        Bug("Invalid !\n");
    }

    if (child->left)
    {
        cleft = mcachefs_metadata_do_get(child->left);
        cleft->up = cup->id;
    }
    cup->right = child->left;

    // Link my up's to this new child
    if (iamleft)
        up->left = child->id;
    else
        up->right = child->id;
    child->up = up->id;

    // Link the child's right to the right of the mdata to remove
    child->right = right->id;
    right->up = child->id;

    // Link the child's left to the left of the mdata to remove
    child->left = left->id;
    left->up = child->id;

    mcachefs_metadata_cleanup_hash(mdata);
}

struct mcachefs_metadata_t *
mcachefs_metadata_find_hash(const char *path, hash_t hash, int path_size)
{
    struct mcachefs_metadata_t *current = mcachefs_metadata_get_root();

    Log(
            "Finding hash for '%s' (up to %d chars) : hash=%llx \n", path, path_size, hash);

    while (current)
    {
        Log(
                "At current=%p (%s), hash=%llx\n", current, current->d_name, current->hash);
        if (hash < current->hash)
        {
            current = mcachefs_metadata_do_get(current->left);
            continue;
        }
        if (current->hash < hash)
        {
            current = mcachefs_metadata_do_get(current->right);
            continue;
        }

        if (mcachefs_metadata_equals(current, path, path_size))
        {
            return current;
        }
        Log(
                "Got a collision with current=%llu:'%s' (hash=%llx), path=%s, hash=%llx!\n", current->id, current->d_name, current->hash, path, hash);
        current = mcachefs_metadata_do_get(current->collision_next);
    }
    return NULL ;
}

void
mcachefs_metadata_build_hash(struct mcachefs_metadata_t *father,
        struct mcachefs_metadata_t *child)
{
    child->hash =
            father->father ? continueHash(father->hash, "/") : father->hash;
    child->hash = continueHash(child->hash, child->d_name);
}

void
mcachefs_metadata_update_fh_path(struct mcachefs_metadata_t *mdata);

void
mcachefs_metadata_rehash_children(struct mcachefs_metadata_t *mdata)
{
    struct mcachefs_metadata_t *father = NULL;
    struct mcachefs_metadata_t *child = NULL;

    mcachefs_metadata_id rootid = mdata->id;
    mcachefs_metadata_id fatherid = rootid;

    Log("mdata_rehash_children at mdata=%llu '%s'\n", mdata->id, mdata->d_name);

    child = mcachefs_metadata_get_child(mdata);
    mdata = NULL;

    if (!child)
    {
        mdata = mcachefs_metadata_do_get(rootid);
        Log(
                "Empty child ? rootid=%llu, mdata->child=%llu\n", rootid, mdata->child);
        return;
    }

    mcachefs_metadata_id childid = child->id;

    while (1)
    {
        Log(
                "childid=%llu, fatherid=%llu, rootid=%llu\n", childid, fatherid, rootid);

        child = mcachefs_metadata_do_get(childid);

        if (!child)
        {
            Bug("NULL child !\n");
        }

        father = mcachefs_metadata_do_get(fatherid);

        mcachefs_metadata_remove_hash(child);
        mcachefs_metadata_build_hash(father, child);
        mcachefs_metadata_insert_hash(child);

        if (child->fh)
        {
            Log(
                    "!!! rehash_children(%llu, %s) on a metadata with an openned fh !!!\n", child->id, child->d_name);
            mcachefs_metadata_update_fh_path(child);
        }

        father = NULL;

        if (S_ISDIR (child->st.st_mode))
        {
            if (!child->child)
            {
                Err("!!!!!!! Hash lookup was so partial !!!!!!!!!!\n");
                return;
                mcachefs_metadata_get_child(child);
                child = mcachefs_metadata_do_get(childid);
            }
            if (!child->child)
            {
                Bug("Could not lookup child !\n");
            }
            if (child->child != mcachefs_metadata_id_EMPTY)
            {
                fatherid = childid;
                childid = child->child;
                continue;
            }
        }
        while (!child->next)
        {
            if (child->father == rootid)
            {
                Log("Finished re-indexing children !\n");
                return;
            }

            child = mcachefs_metadata_do_get(child->father);
            childid = child->id;
            fatherid = child->father;
        }
        if (child->id == rootid)
            Bug(".");

        childid = child->next;
    }
}

/**
 * **************************************** TREE FUNCTIONS *******************************************
 * add, fetch, interpolate (to be done)
 */
void
mcachefs_metadata_link_entry(struct mcachefs_metadata_t *father,
        struct mcachefs_metadata_t *child)
{
    child->father = father->id;

    if (father->child != mcachefs_metadata_id_EMPTY)
        child->next = father->child;
    else
        child->next = 0;
    father->child = child->id;
}

void
mcachefs_metadata_unlink_entry(struct mcachefs_metadata_t *mdata)
{
    struct mcachefs_metadata_t *father, *next;

    if (!mdata->father)
    {
        Bug("Shall not unlink '/'.\n");
    }
    father = mcachefs_metadata_do_get(mdata->father);

    Log(
            "unlink_entry, mdata=%llu:%s, father=%llu:%s\n", mdata->id, mdata->d_name, father->id, father->d_name);

    if (father->child == mdata->id)
    {
        father->child = mdata->next ? mdata->next : mcachefs_metadata_id_EMPTY;
        return;
    }

    for (next = mcachefs_metadata_do_get(father->child); next->next; next =
            mcachefs_metadata_do_get(next->next))
    {
        if (next->next == mdata->id)
        {
            next->next = mdata->next;
            return;
        }
    }

    Bug(
            "Child '%s' (%llu) is not child of father '%s' (%llu)\n", mdata->d_name, mdata->id, father->d_name, mdata->id);
}

void
mcachefs_metadata_do_add_child(struct mcachefs_metadata_t *father,
        struct mcachefs_metadata_t *child)
{
    child->father = father->id;
    if (father->child == 0 || father->child == mcachefs_metadata_id_EMPTY)
    {
        child->next = 0;
    }
    else
    {
        child->next = father->child;
    }
    father->child = child->id;

    mcachefs_metadata_build_hash(father, child);
    mcachefs_metadata_insert_hash(child);
}

void
mcachefs_metadata_add_child_ids(mcachefs_metadata_id father_id,
        mcachefs_metadata_id child_id)
{
    struct mcachefs_metadata_t* father = mcachefs_metadata_do_get(father_id);
    struct mcachefs_metadata_t* child = mcachefs_metadata_do_get(child_id);
    mcachefs_metadata_do_add_child(father, child);
}

int
mcachefs_metadata_recurse_open(struct mcachefs_metadata_t *father)
{
    int fd, fd2;

    if (!father->father)
    {
        return open(mcachefs_config_get_source(), O_RDONLY);
    }
    fd = mcachefs_metadata_recurse_open(
            mcachefs_metadata_do_get(father->father));
    if (fd == -1)
        return fd;
    fd2 = openat(fd, father->d_name, O_RDONLY);
    if (fd2 == -1)
    {
        Err(
                "Could not open '%s', err=%d:%s\n", father->d_name, errno, strerror (errno));
    }

    Log("fd=%d, fd2=%d, d_name=%s\n", fd, fd2, father->d_name);
    close(fd);
    return fd2;
}

struct mcachefs_metadata_t *
mcachefs_metadata_get_child(struct mcachefs_metadata_t *father)
{
    mcachefs_metadata_id fatherid = father->id;

    if (father->child == mcachefs_metadata_id_EMPTY)
    {
        Log("Explicit EMPTY!\n");
        return NULL ;
    }
    else if (father->child)
    {
        Log(
                "Father %s (%llu) has an explicit child %llu\n", father->d_name, father->id, father->child);
        return mcachefs_metadata_do_get(father->child);
    }

    Log(
            "We must lookup child for father=%s (%p:%llu)\n", father->d_name, father, father->id);

    if (!S_ISDIR (father->st.st_mode))
    {
        Err("Record %p:%llu not a directory !\n", father, father->id);
        return NULL ;
    }

    if (mcachefs_config_get_read_state() == MCACHEFS_STATE_HANDSUP)
    {
        Err(
                "While looking up childrens of '%s' : mcachefs state set to HANDSUP.\n", father->d_name);
        return NULL ;
    }

    int fd = mcachefs_metadata_recurse_open(father);
    if (fd == -1)
    {
        Err("Could not recurse open !\n");
        return NULL ;
    }

    DIR* dp = fdopendir(fd);

    if (dp == NULL )
    {
        Err("Could not fdopendir() : err=%d:%s\n", errno, strerror (errno));
        close(fd);
        return NULL ;
    }

    struct dirent *de;
    while ((de = readdir(dp)) != NULL )
    {
        Log_M ("de=%p : name=%s, type=%d\n", de, de->d_name, de->d_type);
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        /*
         * If the mcachefs target mountpoint is mounted via mcachefs, we have to skip the
         * original '.mcachefs' directory in the target mountpoint.
         */
        if (fatherid == mcachefs_metadata_id_root
                && (strcmp(de->d_name, ".mcachefs") == 0))
        {
            Info("Skipping target '.mcachefs' !\n");
            continue;
        }

        /*
         * Be really carefull, as mcachefs_metadata_allocate blurs the pointers.
         */
        mcachefs_metadata_id newid = mcachefs_metadata_allocate();
        struct mcachefs_metadata_t *newmeta = mcachefs_metadata_do_get(newid);

        strncpy(newmeta->d_name, de->d_name, NAME_MAX + 1);

        if (fstatat(fd, newmeta->d_name, &(newmeta->st), AT_SYMLINK_NOFOLLOW))
        {
            Err(
                    "Could not fstatat(%d, %s, %p) : err=%d:%s\n", fd, newmeta->d_name, &(newmeta->st), errno, strerror (errno));
        }
        newmeta->st.st_ino = 0;

        mcachefs_metadata_add_child_ids(fatherid, newid);
    }
    closedir(dp);
    close(fd);

    if (!father->child)
    {
        father->child = mcachefs_metadata_id_EMPTY;
        return NULL ;
    }

    return mcachefs_metadata_do_get(father->child);
}

void
mcachefs_metadata_fetch_children(struct mcachefs_metadata_t *mroot)
{
    struct mcachefs_metadata_t *current = NULL;

    mcachefs_metadata_check_locked ();
    mcachefs_metadata_id rootid = mroot->id;
    mcachefs_metadata_id currentid = rootid;
    mroot = NULL;

    while (1)
    {
        Log("Current : %llu\n", currentid);
        current = mcachefs_metadata_do_get(currentid);
        if (S_ISDIR (current->st.st_mode))
        {
            if (current->child == 0)
            {
                Info("Lookup : at '%s'\n", current->d_name);
                mcachefs_metadata_get_child(current);
                current = mcachefs_metadata_do_get(currentid);
            }
            if (current->child != mcachefs_metadata_id_EMPTY)
            {
                currentid = current->child;
                current = NULL;
                continue;
            }
        }
        while (!current->next)
        {
            if (current->father == rootid)
                return;
            currentid = current->father;
            current = mcachefs_metadata_do_get(currentid);
        }
        if (current->id == rootid)
            return;
        currentid = current->next;
    }
}

struct mcachefs_metadata_t *
mcachefs_metadata_walk_down(struct mcachefs_metadata_t *father,
        const char *path, int path_size)
{
    const char *rpath = NULL;
    hash_t father_hash, hash;
    int rpath_size;

    walk_down_start:

    path_size++;
    rpath = &(path[path_size]);

    rpath_size = 0;
    while (rpath[rpath_size] != '\0' && rpath[rpath_size] != '/')
        rpath_size++;

    father_hash = father->hash;
    hash = 0;

    Log(
            "May walk down : father=(%p:%llu) %s, path=%s (path_size=%d), rpath=%s (rpath_size=%d)\n", father, father->id, father->d_name, path, path_size, rpath, rpath_size);

    struct mcachefs_metadata_t *child = mcachefs_metadata_get_child(father);
    father = NULL;  // get_child may have garbaged father, do not use it anymore

    if (!child)
        return NULL ;

    Log("=> Now I will walk that list down.\n");

    father_hash = continueHash(father_hash, "/");
    hash = continueHashPartial(father_hash, rpath, rpath_size);

    for (; child; child = mcachefs_metadata_do_get(child->next))
    {
        Log("walk_down, at child='%s' (%llu)\n", child->d_name, child->id);
        if (child->hash == hash
                && strncmp(child->d_name, rpath, rpath_size) == 0)
        {
            Log("==> Found it !\n");
            if (rpath[rpath_size])
            {
                Log("rpath(rpath_size)=%c\n", rpath[rpath_size]);
                father = child;
                path_size += rpath_size + 1;
                goto walk_down_start;
            }
            return child;
        }
    }
    return NULL ;
}

struct mcachefs_metadata_t *
mcachefs_metadata_find_locked(const char *path)
{
    struct mcachefs_metadata_t *metadata;
    int path_size = strlen(path);
    hash_t hash = doHash(path);

    Log("Finding path '%s'\n", path);

    metadata = mcachefs_metadata_find_hash(path, hash, path_size);
    if (metadata)
    {
        Log("Found : id=%llu\n", metadata->id);
        return metadata;
    }

    Log("Could not get '%s'\n", path);

    while (1)
    {
        while (path_size && path[path_size] != '/')
        {
            path_size--;
        }
        if (path_size <= 1)
        {
            metadata = mcachefs_metadata_get_root();
            break;
        }
        hash = doHashPartial(path, path_size);

        metadata = mcachefs_metadata_find_hash(path, hash, path_size);

        if (metadata)
            break;
        path_size--;
    }
    Log("After semi-lookup : metadata=%p, path_size=%d\n", metadata, path_size);
    return mcachefs_metadata_walk_down(metadata, path, path_size);
}

struct mcachefs_metadata_t *
mcachefs_metadata_find(const char *path)
{
    struct mcachefs_metadata_t *metadata;
    mcachefs_metadata_lock ()
    ;
    metadata = mcachefs_metadata_find_locked(path);

    if (!metadata)
        mcachefs_metadata_unlock ();
    return metadata;
}

void
mcachefs_metadata_release(struct mcachefs_metadata_t *mdata)
{
    (void) mdata;
    mcachefs_metadata_unlock ();
}

void
mcachefs_metadata_remove(struct mcachefs_metadata_t *metadata)
{
    struct mcachefs_file_t *mfile;

    if (metadata->fh)
    {
        mfile = mcachefs_file_get(metadata->fh);
        mfile->metadata_id = 0;
    }

    mcachefs_metadata_id id = metadata->id;
    memset(metadata, 0, sizeof(struct mcachefs_metadata_t));
    metadata->id = id;

    metadata->next = mcachefs_metadata_head->first_free;
    mcachefs_metadata_head->first_free = metadata->id;
}

void
mcachefs_metadata_clean_fh_locked(mcachefs_metadata_id id)
{
    struct mcachefs_metadata_t *mdata;
    mcachefs_metadata_check_locked ();
    if (id)
    {
        mdata = mcachefs_metadata_do_get(id);
        mdata->fh = 0;
    }
}

/**
 * **************************************** HIGH-LEVEL FUNCTIONS *******************************************
 * remove children, flush entries, rename, mknod, rmdir, ...
 */
void
mcachefs_metadata_remove_children(struct mcachefs_metadata_t *metadata)
{
    struct mcachefs_metadata_t *current, *next, *father;

    if (!metadata->child || metadata->child == mcachefs_metadata_id_EMPTY)
    {
        Log("Nothing to flush out !\n");
        return;
    }

    // Remove hashes, put all that in remove
    current = mcachefs_metadata_do_get(metadata->child);

    while (current)
    {
        Log(
                "At current=%llu (father=%llu, d_name=%s)\n", current->id, current->father, current->d_name);

        if (current->child && current->child != mcachefs_metadata_id_EMPTY)
        {
            Log(
                    "Directory %s has children, and not empty ones.\n", current->d_name);
            current = mcachefs_metadata_do_get(current->child);
            continue;
        }
        next = mcachefs_metadata_do_get(current->next);
        father = mcachefs_metadata_do_get(current->father);

        /**
         * Remove from hash tree
         */
        Log("=> removing hash %llu:'%llx'\n", current->id, current->hash);

        // Now we trash up the current
        mcachefs_metadata_remove_hash(current);
        mcachefs_metadata_remove(current);

        // Make sure we don't reference current anymore
        current = NULL;

        /**
         * now, if we have a next, focus on it
         */
        if (next)
        {
            current = next;
            continue;
        }

        /**
         * We don't have children, we don't have next, we are at the tail, rewind
         */
        while (!next)
        {
            if (!father)
            {
                Bug("Shall have had a father !\n");
            }
            if (father->id == metadata->id)
            {
                current = NULL;
                break;
            }
            current = father;

            father = mcachefs_metadata_do_get(current->father);
            next = mcachefs_metadata_do_get(current->next);

            Log(
                    "=> removing (father) hash %llu:'%llx'\n", current->id, current->hash);
            mcachefs_metadata_remove_hash(current);
            mcachefs_metadata_remove(current);

            // set current back to null
            current = NULL;
        }

        Log("==> JUMP to next=%llu\n", next ? next->id : 0);
        current = next;
    }

    metadata->child = 0;
}

int
mcachefs_metadata_getattr(const char *path, struct stat *stbuf)
{
    struct mcachefs_metadata_t *mdata = mcachefs_metadata_find(path);

    if (!mdata)
    {
        Log("Could not find '%s'\n", path);
        return -ENOENT;
    }

    Log(
            "Found '%llu' : '%s', (hash=%llx)\n", mdata->id, mdata->d_name, mdata->hash);
    memcpy(stbuf, &(mdata->st), sizeof(struct stat));

    Log(
            "dev=%ld, ino=%ld, mode=%lo, link=%lx, uid=%ld, gid=%ld, rdev=%ld, size=%lu, blksz=%ld, blkcnt=%ld, at=%ld, mt=%ld, ct=%ld\n", (long) mdata->st.st_dev, (long) mdata->st.st_ino, (long) mdata->st.st_mode, (long) mdata->st.st_nlink, (long) mdata->st.st_uid, (long) mdata->st.st_gid, (long) mdata->st.st_rdev, (long) mdata->st.st_size, (long) mdata->st.st_blksize, (long) mdata->st.st_blocks, mdata->st.st_atime, mdata->st.st_mtime, mdata->st.st_ctime);
    mcachefs_metadata_release(mdata);

    if (S_ISDIR (stbuf->st_mode) && stbuf->st_size == 0)
    {
        stbuf->st_size = 1 << 12;
    }
    return 0;
}

int
mcachefs_metadata_make_entry(const char *path, mode_t mode, dev_t rdev)
{
    char *dpath;
    const char *lname;
    time_t now;

    struct mcachefs_metadata_t *mdata, *child, *father;
    mcachefs_metadata_id childid, fatherid;

    Log("mcachefs_metadata_make_entry : '%s', mode=%d\n", path, mode);

    mdata = mcachefs_metadata_find(path);

    if (mdata)
    {
        mcachefs_metadata_release(mdata);
        return -EEXIST;
    }

    dpath = mcachefs_split_path(path, &lname);

    Log(
            "mcachefs_metadata_make_entry : path='%s', dpath='%s', lname='%s'\n", path, dpath, lname);

    father = mcachefs_metadata_find(dpath);
    fatherid = father->id;

    free(dpath);
    if (!father)
        return -ENOENT;
    if (!father->child)
    {
        Err("We did not fetch father contents !!!\n");
        mcachefs_metadata_get_child(father);
    }

    father = NULL;

    childid = mcachefs_metadata_allocate();
    child = mcachefs_metadata_do_get(childid);

    memset(child, 0, sizeof(struct mcachefs_metadata_t));
    child->id = childid;

    father = mcachefs_metadata_do_get(fatherid);

    strncpy(child->d_name, lname, NAME_MAX + 1);
    /*
     * Now insert hash
     */
    mcachefs_metadata_build_hash(father, child);
    mcachefs_metadata_insert_hash(child);

    /*
     * Now build up the child stat
     */

    Log(
            "In mcachefs_metadata_make_entry : caller=%lu, uid=%lu, gid=%lu\n", (long) fuse_get_context ()->pid, (long) fuse_get_context ()->uid, (long) fuse_get_context ()->gid);

    child->st.st_uid = fuse_get_context()->uid;
    child->st.st_gid = fuse_get_context()->gid;
    child->st.st_nlink = 1;
    child->st.st_mode = mode;
    child->st.st_rdev = rdev;

    now = time(NULL );
    child->st.st_atime = now;
    child->st.st_ctime = now;
    child->st.st_mtime = now;

    if (S_ISDIR (mode))
        child->child = mcachefs_metadata_id_EMPTY;

    mcachefs_metadata_link_entry(father, child);

    mcachefs_metadata_release(father);
    return 0;
}

int
mcachefs_metadata_rmdir_unlink(const char *path, int isDir)
{
    struct mcachefs_metadata_t *mdata;
    mcachefs_metadata_id mdataid;

    Log("metadata_rmdir : '%s'\n", path);

    mdata = mcachefs_metadata_find(path);

    if (!mdata)
    {
        return -ENOENT;
    }

    if (isDir)
    {
        if (!S_ISDIR (mdata->st.st_mode))
        {
            mcachefs_metadata_release(mdata);
            return -ENOTDIR;
        }
        if (mdata->child == 0)
        {
            mdataid = mdata->id;
            mcachefs_metadata_get_child(mdata);
            mdata = mcachefs_metadata_do_get(mdataid);
        }
        if (mdata->child != mcachefs_metadata_id_EMPTY)
        {
            mcachefs_metadata_release(mdata);
            return -ENOTEMPTY;
        }
    }
    else
    {
        if (!S_ISREG (mdata->st.st_mode) && !S_ISLNK (mdata->st.st_mode))
        {
            Err(
                    "Not supported : unlink(%s), mode=%lo\n", path, (long) mdata->st.st_mode);
            mcachefs_metadata_release(mdata);
            return -ENOSYS;
        }
    }

    mcachefs_metadata_unlink_entry(mdata);
    mcachefs_metadata_remove_hash(mdata);
    mcachefs_metadata_remove(mdata);

    mcachefs_metadata_release(mdata);
    return 0;
}

char *
mcachefs_metadata_get_path(struct mcachefs_metadata_t *mdata)
{
    struct mcachefs_metadata_t *hierarchy[MCACHEFS_METADATA_MAX_LEVELS];
    struct mcachefs_metadata_t *mcurrent;
    char *newpath;

    int level = 0, l;
    int pathsz = 1;

    for (mcurrent = mdata; mcurrent->father; mcurrent =
            mcachefs_metadata_do_get(mcurrent->father))
    {
        hierarchy[level] = mcurrent;
        level++;
        pathsz += strlen(mcurrent->d_name) + 1;
    }
    if (strcmp(mcurrent->d_name, "/"))
    {
        Bug(
                "Wrong last mcurrent name : '%s' (mcurrent->id=%llu)\n", mcurrent->d_name, mcurrent->id);
    }
    newpath = (char *) malloc(pathsz);
    newpath[0] = 0;
    for (l = level - 1; l >= 0; l--)
    {
        strcat(newpath, "/");
        strcat(newpath, hierarchy[l]->d_name);
    }
    return newpath;
}

void
mcachefs_metadata_update_fh_path(struct mcachefs_metadata_t *mdata)
{
    struct mcachefs_file_t *mfile;
    char *oldpath, *newpath;

    if (!mdata->fh)
    {
        return;
    }

    newpath = mcachefs_metadata_get_path(mdata);

    mfile = mcachefs_file_get(mdata->fh);
    Info("Updated : old path='%s', new path='%s'\n", mfile->path, newpath);

    mcachefs_file_lock_file(mfile);
    oldpath = mfile->path;
    mfile->path = newpath;
    free(oldpath);
    mcachefs_file_unlock_file(mfile);
}

int
mcachefs_metadata_rename_entry(const char *path, const char *to)
{
    struct mcachefs_metadata_t *mdata, *mtarget;
    mcachefs_metadata_id mdataid;

    const char *lname;
    char *targetpath;

    Log("RENAME : %s => %s\n", path, to);
    mcachefs_metadata_lock ()
    ;
    mdata = mcachefs_metadata_find_locked(path);

    if (!mdata)
    {
        mcachefs_metadata_unlock ();
        return -ENOENT;
    }
    if (S_ISDIR (mdata->st.st_mode))
    {
        /*
         * We have to fully fetch the source mdata if it is a directory
         * Because after renaming, that will be too late !
         */
        mcachefs_metadata_fetch_children(mdata);
    }

    mdataid = mdata->id;
    mdata = NULL;

    if ((mtarget = mcachefs_metadata_find_locked(to)) != NULL )
    {
        mcachefs_metadata_unlock ();
        return -EEXIST;
    }

    targetpath = mcachefs_split_path(to, &lname);

    Log("\ttarget=%s, lname=%s\n", targetpath, lname);

    mtarget = mcachefs_metadata_find_locked(targetpath);
    free(targetpath);

    if (!mtarget)
    {
        mcachefs_metadata_unlock ();
        return -ENOENT;
    }
    if (!S_ISDIR (mtarget->st.st_mode))
    {
        mcachefs_metadata_unlock ();
        return -ENOTDIR;
    }

    mdata = mcachefs_metadata_do_get(mdataid);

    Log("mdata : %llu, child=%llu\n", mdata->id, mdata->child);

    mcachefs_metadata_unlink_entry(mdata);
    mcachefs_metadata_remove_hash(mdata);

    strncpy(mdata->d_name, lname, NAME_MAX + 1);

    mcachefs_metadata_link_entry(mtarget, mdata);

    mcachefs_metadata_build_hash(mtarget, mdata);
    mcachefs_metadata_insert_hash(mdata);

    if (S_ISDIR (mdata->st.st_mode)
            && mdata->child != mcachefs_metadata_id_EMPTY)
    {
        mcachefs_metadata_rehash_children(mdata);
    }

    if (mdata->fh)
    {
        Log(
                "!!! rename(%s, %s) on a metadata with an openned fh !!!\n", path, to);
        mcachefs_metadata_update_fh_path(mdata);
    }

    mcachefs_metadata_unlock ();

    return 0;

}

/**
 * **************************************** HIGHLEVEL VOPS FUNCTIONS *******************************************
 * Fill, flush
 */

/**
 * Find a metadata from a path
 * Locks mcachefs_metadata lock if result is != NULL
 */
struct mcachefs_metadata_t *
mcachefs_metadata_find_entry(const char *path)
{
    struct mcachefs_metadata_t *metadata;

    int mountpoint_sz = strlen(mcachefs_config_get_mountpoint());
    int path_sz = 0;
    if (strncmp(path, mcachefs_config_get_mountpoint(), mountpoint_sz) == 0)
    {
        Log("Find entry : mountpoint prefixed !\n");
        path = &(path[mountpoint_sz]);
    }

    while (path[path_sz] && path[path_sz] != '\n' && path[path_sz] != '\r')
        path_sz++;

    if (path_sz && path[path_sz] == '/')
        path_sz--;

    mcachefs_metadata_lock ()
    ;

    Log("Find : path=[%s], path_sz=%d\n", path, path_sz);

    if (path_sz == 0)
    {
        // We are finding the root entry
        metadata = mcachefs_metadata_do_get(mcachefs_metadata_id_root);
    }
    else
    {
        metadata = mcachefs_metadata_find_hash(path,
                doHashPartial(path, path_sz), path_sz);
    }
    if (metadata == NULL )
    {
        mcachefs_metadata_unlock ();
    }
    return metadata;
}

void
mcachefs_metadata_flush_entry(const char *path)
{
    struct mcachefs_metadata_t *metadata = mcachefs_metadata_find_entry(path);
    if (metadata)
    {
        Log("\t=> Found at %llu, hash=%llx\n", metadata->id, metadata->hash);
        mcachefs_metadata_remove_children(metadata);
        mcachefs_metadata_unlock ();
    }
    else
    {
        Err("Path '%s' not found in metadata cache !\n", path);
    }
}

void
mcachefs_metadata_vops_remove_previous(struct mcachefs_metadata_t* mdata_root)
{
    struct mcachefs_metadata_t* mdata_child;
    for (mdata_child = mcachefs_metadata_get_child(mdata_root);
            mdata_child != NULL ;)
    {
        Log("[VOPS] Root @child %s\n", mdata_child->d_name);
        if (strncmp(mdata_child->d_name, MCACHEFS_VOPS_DIR + 1, NAME_MAX) == 0)
        {
            Log("[VOPS] Remove previous VOPS entry %llu\n", mdata_child->id);
            mcachefs_metadata_remove_children(mdata_child);
            mcachefs_metadata_unlink_entry(mdata_child);
            mcachefs_metadata_remove_hash(mdata_child);
            mcachefs_metadata_remove(mdata_child);
            break;
        }
        mdata_child = mcachefs_metadata_do_get(mdata_child->next);
    }
}

mcachefs_metadata_id
mcachefs_metadata_vops_create_dir(mcachefs_metadata_id root_id)
{
    mcachefs_metadata_id vops_meta_id = mcachefs_metadata_allocate();
    struct mcachefs_metadata_t* vops_meta = mcachefs_metadata_do_get(
            vops_meta_id);

    strncpy(vops_meta->d_name, MCACHEFS_VOPS_DIR + 1, NAME_MAX + 1);
    vops_meta->st.st_mode = S_IFDIR | 0700;
    vops_meta->st.st_uid = getuid();
    vops_meta->st.st_gid = getgid();
    vops_meta->st.st_nlink = 1;
    mcachefs_metadata_add_child_ids(root_id, vops_meta_id);

    return vops_meta_id;
}

void
mcachefs_metadata_vops_create_file(const char* vops_name,
        mcachefs_metadata_id vops_meta_id)
{
    mcachefs_metadata_id vops_meta_file_id = mcachefs_metadata_allocate();
    struct mcachefs_metadata_t* vops_meta_file = mcachefs_metadata_do_get(
            vops_meta_file_id);
    Log(
            "d_name at %p, vops_name at %p (%s)\n", vops_meta_file->d_name, vops_name, vops_name);
    strncpy(vops_meta_file->d_name, vops_name, NAME_MAX + 1);
    vops_meta_file->st.st_mode = S_IFREG | 0600;
    vops_meta_file->st.st_uid = getuid();
    vops_meta_file->st.st_gid = getgid();
    vops_meta_file->st.st_nlink = 1;
    vops_meta_file->st.st_size = 1 << 20;
    mcachefs_metadata_add_child_ids(vops_meta_id, vops_meta_file_id);
}

/**
 * VOPS metadata functions
 */
void
mcachefs_metadata_populate_vops()
{
    struct mcachefs_metadata_t* mdata_root = mcachefs_metadata_find("/");
    mcachefs_metadata_id mdata_root_id = mdata_root->id;

    /**
     * Lookup of children may blur initial mdata_root value
     */
    mcachefs_metadata_get_child(mdata_root);

    mdata_root = mcachefs_metadata_do_get(mdata_root_id);
    Log("Root Id is %llu\n", mdata_root->id);
    mcachefs_metadata_vops_remove_previous(mdata_root);
    Log("After remove_previous, Root Id is %llu\n", mdata_root->id);

    mdata_root = NULL;

    mcachefs_metadata_id vops_meta_id = mcachefs_metadata_vops_create_dir(
            mdata_root_id);

    const char **vops_list;
    for (vops_list = mcachefs_vops_get_vops_list(); *vops_list; vops_list++)
    {
        mcachefs_metadata_vops_create_file(*vops_list, vops_meta_id);
    }

    Log("Populated VOPS : .mcachefs entry at %llu\n", vops_meta_id);

    mdata_root = mcachefs_metadata_do_get(mdata_root_id);
    Log(
            "Now Root (%llu,%p) has child (%llu)\n", mdata_root->id, mdata_root, mdata_root->child);
    mcachefs_metadata_release(mdata_root);
}

#ifdef __MCACHEFS_METADATA_HAS_FILLENTRY

void
mcachefs_metadata_schedule_fill_entry(struct mcachefs_metadata_t *metadata)
{
    struct mcachefs_file_t *mfile;
    mcachefs_fh_t fh = mcachefs_fileid_get(metadata, NULL,
            mcachefs_file_type_dir);
    mfile = mcachefs_file_get(fh);
    mcachefs_transfer_queue_file(mfile, MCACHEFS_TRANSFER_TYPE_METADATA);
}

void
mcachefs_metadata_schedule_fill_child_entries(struct mcachefs_metadata_t *mdata)
{
    struct mcachefs_metadata_t *child;
    for (child = mcachefs_metadata_get_child(mdata); child; child =
            mcachefs_metadata_do_get(child->next))
    {
        if (S_ISDIR (child->st.st_mode))
        {
            if (mdata->id == mcachefs_metadata_id_root
                    && strcmp(child->d_name, ".mcachefs") == 0)
            {
                Log("Skipping virtual .mcachefs directory.\n");
            }
            else
            {
                Log("Scheduling child '%s'\n", child->d_name);
                mcachefs_metadata_schedule_fill_entry(child);
            }
        }
    }
}

struct mcachefs_metadata_t *
mcachefs_metadata_dir_get_child(struct mcachefs_metadata_t *father,
        const char *d_name)
{
    struct mcachefs_metadata_t *child;
    if (father->child == 0 || father->child == mcachefs_metadata_id_EMPTY)
        return NULL ;
    child = mcachefs_metadata_do_get(father->child);

    while (child)
    {
        if (strcmp(child->d_name, d_name) == 0)
        {
            Log("Found '%s'.\n", d_name);
            return child;
        }
        child = mcachefs_metadata_do_get(child->next);
    }
    return NULL ;
}

void
mcachefs_metadata_compare_entries(const char *path, const char *d_name,
        struct mcachefs_metadata_t *existingmeta, struct stat *newstat)
{
    Log("At '%s' : inserting '%s' : already exists !\n", path, d_name);
    struct stat *oldstat = &(existingmeta->st);
    if ((oldstat->st_mode & S_IFMT) != (newstat->st_mode & S_IFMT))
    {
        Warn(
                "Update %s/%s : Divering modes ! existing mode=%x, stat=%x", path, d_name, oldstat->st_mode, newstat->st_mode);
    }
    if (S_ISREG (oldstat->st_mode) && S_ISREG (newstat->st_mode))
    {
        if (oldstat->st_mtime < newstat->st_mtime)
        {
            Info("Update %s/%s : Cached version is older !\n", path, d_name);
            // oldstat->st_mtime = newstat->st_mtime;
        }
        if (oldstat->st_size != newstat->st_size)
        {
            Info(
                    "Update %s/%s : Sizes differ (old=%lu, new=%lu)!\n", path, d_name, (unsigned long) oldstat->st_size, (unsigned long) newstat->st_size);
            // oldstat->st_size = newstat->st_size;
        }
    }
}

int
mcachefs_metadata_browse_dir(const char *path, int fd, struct stat **pstats,
        char ***pnames)
{
    DIR *dp;
    struct dirent *de;
    int alloced = 0;
    int nb = 0;

    dp = fdopendir(fd);

    if (dp == NULL )
    {
        Err(
                "Could not fdopendir('%s') : err=%d:%s\n", path, errno, strerror (errno));
        close(fd);
        return 0;
    }

    Log("file=%s : fd=%d, dp=%p\n", path, fd, dp);
    while ((de = readdir(dp)) != NULL )
    {
        Log("FILL : '%s'\n", de->d_name);
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (nb == alloced)
        {
            alloced += 32;
            *pstats = (struct stat *) realloc(*pstats,
                    sizeof(struct stat) * alloced);
            *pnames = (char **) realloc(*pnames, sizeof(char *) * alloced);
        }
        (*pnames)[nb] = strdup(de->d_name);
        if (fstatat(fd, (*pnames)[nb], &((*pstats)[nb]), AT_SYMLINK_NOFOLLOW))
        {
            Bug(
                    "Could not fstatat(%d, %s, %p) : err=%d:%s\n", fd, (*pnames)[nb], &((*pstats)[nb]), errno, strerror (errno));
        }
        nb++;
    }
    closedir(dp);
    return nb;
}

void
mcachefs_metadata_fill_entry(struct mcachefs_file_t *mfile)
{
    mcachefs_metadata_id metaid = mfile->metadata_id, newid;

    mcachefs_metadata_lock ();
    struct mcachefs_metadata_t* mdata = mcachefs_metadata_do_get(metaid);

    int virgindir = (mdata->child == 0);

    Log("Fill Entry : mdata='%s', file='%s'\n", mdata->d_name, mfile->path);

    int fd = mcachefs_metadata_recurse_open(mdata);

    mcachefs_metadata_unlock ();

    if (fd == -1)
    {
        Err("Could not recurse open %s !\n", mfile->path);
        return;
    }

    struct stat *stats = NULL;
    char **names = NULL;
    int nb = mcachefs_metadata_browse_dir(mfile->path, fd, &stats, &names);

    close(fd);

    mcachefs_metadata_lock ();

    mdata = mcachefs_metadata_do_get(metaid);

    int cur;
    for (cur = 0; cur < nb; cur++)
    {
        Log("At '%s' : inserting '%s'\n", mdata->d_name, names[cur]);
        if (!virgindir)
        {
            struct mcachefs_metadata_t* existingmeta =
                    mcachefs_metadata_dir_get_child(mdata, names[cur]);
            if (existingmeta)
            {
                mcachefs_metadata_compare_entries(mfile->path, names[cur],
                        existingmeta, &(stats[cur]));
                continue;
            }
        }
        newid = mcachefs_metadata_allocate();
        struct mcachefs_metadata_t* newmeta = mcachefs_metadata_do_get(newid);

        /*
         * mcachefs_metadata_allocate() blurs the existing pointers. reload it.
         */
        mdata = mcachefs_metadata_do_get(metaid);

        strcpy(newmeta->d_name, names[cur]);
        free(names[cur]);
        memcpy(&(newmeta->st), &(stats[cur]), sizeof(struct stat));
        mcachefs_metadata_add_child_ids(metaid, newid);
    }
    if (stats)
    {
        free(stats);
    }
    if (names)
    {
        free(names);
    }

    mcachefs_metadata_schedule_fill_child_entries(mdata);

    mcachefs_metadata_unlock ();
}

void
mcachefs_metadata_fill(const char *path)
{
    struct mcachefs_metadata_t *metadata = mcachefs_metadata_find_entry(path);
    if (metadata)
    {
        Log(
                "\tFound '%s' at %llu, hash=%llx\n", path, metadata->id, metadata->hash);
        mcachefs_metadata_schedule_fill_entry(metadata);
        Info("Scheduled metadata fetch for '%s'\n", path);
        mcachefs_metadata_unlock ();
    }
    else
    {
        Err("Path '%s' not found in metadata cache !\n", path);
    }
}

void
mcachefs_metadata_fill_meta()
{
    if (mcachefs_config_get_cache_prefix() != NULL )
    {
        mcachefs_metadata_fill(mcachefs_config_get_cache_prefix());
    }
}

#else

void
mcachefs_metadata_fill_entry(struct mcachefs_file_t *mfile)
{
    (void) mfile;
}
void
mcachefs_metadata_fill_meta()
{

}

#endif
/**
 * **************************************** DUMP FUNCTIONS *******************************************
 * Dump the contents of the meta file
 */
static char dspace[1024];

#define __SET_DSPACE(__depth) \
  int j ; for ( j = 0 ; j < __depth * 2 ; j++ ) dspace[j] = ' '; \
    dspace[__depth*2] = '\0';

static unsigned long mcachefs_dump_mdata_tree_nb = 0;
static unsigned long mcachefs_dump_mdata_hashtree_nb = 0;

void
mcachefs_metadata_dump_meta(struct mcachefs_file_t *mvops,
        struct mcachefs_metadata_t *mdata, int depth)
{
    mcachefs_dump_mdata_tree_nb++;
    __SET_DSPACE(depth);

    __VOPS_WRITE(mvops,
            "%s[%llu] h=%llx : '%s' (c=%llu,n=%llu,f=%llu, ulr=%llu:%llu:%llu), fh=%lx\n", dspace, mdata->id, mdata->hash, mdata->d_name, mdata->child, mdata->next, mdata->father, mdata->up, mdata->left, mdata->right, (unsigned long) mdata->fh);

    if (mdata->child && mdata->child != mcachefs_metadata_id_EMPTY)
        mcachefs_metadata_dump_meta(mvops,
                mcachefs_metadata_do_get(mdata->child), depth + 1);

    if (mdata->next)
        mcachefs_metadata_dump_meta(mvops,
                mcachefs_metadata_do_get(mdata->next), depth);
}

void
mcachefs_metadata_dump_hash(struct mcachefs_file_t *mvops,
        struct mcachefs_metadata_t *mdata, int depth, hash_t min, hash_t max)
{
    mcachefs_dump_mdata_hashtree_nb++;
    __SET_DSPACE(depth);

    __VOPS_WRITE(mvops,
            "%s[%llu], hash=%llx : '%s' (region [%llx:%llx]), ulr=%llu/%llu/%llu, coll=n:%llu/p:%llu\n", dspace, mdata->id, mdata->hash, mdata->d_name, min, max, mdata->up, mdata->left, mdata->right, mdata->collision_next, mdata->collision_previous);

    if (mdata->hash < min)
    {
        __VOPS_WRITE(mvops, "==> corrupted on min !\n");
    }
    if (mdata->hash > max)
    {
        __VOPS_WRITE(mvops, "==> corrupted on max !\n");
    }

    if (mdata->left)
        mcachefs_metadata_dump_hash(mvops,
                mcachefs_metadata_do_get(mdata->left), depth + 1, min,
                mdata->hash - 1);
    if (mdata->right)
        mcachefs_metadata_dump_hash(mvops,
                mcachefs_metadata_do_get(mdata->right), depth + 1, mdata->hash,
                max);
    if (mdata->collision_next)
        mcachefs_metadata_dump_hash(mvops,
                mcachefs_metadata_do_get(mdata->collision_next), depth,
                mdata->hash - 1, mdata->hash + 1);
}

void
mcachefs_metadata_dump(struct mcachefs_file_t *mvops)
{
    mcachefs_metadata_lock ();

    mcachefs_dump_mdata_tree_nb = 0;
    mcachefs_dump_mdata_hashtree_nb = 0;

    __VOPS_WRITE(mvops,
            "--------------- Metadata tree root=%s -----------------\n", mcachefs_metadata_get_root ()->d_name);
    mcachefs_metadata_dump_meta(mvops, mcachefs_metadata_get_root(), 0);

    __VOPS_WRITE(mvops,
            "--------------- Metadata hash tree root=%s -----------------\n", mcachefs_metadata_get_root ()->d_name);
    mcachefs_metadata_dump_hash(mvops, mcachefs_metadata_get_root(), 0, 0,
            ~((hash_t) 0));

    if (mcachefs_dump_mdata_tree_nb != mcachefs_dump_mdata_hashtree_nb)
    {
        Err(
                "Diverging counts : mcachefs_dump_mdata_tree_nb=%lu, mcachefs_dump_mdata_tree_nb=%lu\n", mcachefs_dump_mdata_tree_nb, mcachefs_dump_mdata_hashtree_nb);
        __VOPS_WRITE(mvops,
                "Diverging counts : mcachefs_dump_mdata_tree_nb=%lu, mcachefs_dump_mdata_hashtree_nb=%lu\n", mcachefs_dump_mdata_tree_nb, mcachefs_dump_mdata_hashtree_nb);
    }

    mcachefs_metadata_unlock ();
}

#ifdef __MCACHEFS_METADATA_HAS_SYNC
void
mcachefs_metadata_sync()
{
    if (mcachefs_metadata == NULL )
    {
        Err("Could not sync a NULL metadata !\n");
        return;
    }
    mcachefs_metadata_lock ()
    ;
    if (msync(mcachefs_metadata, mcachefs_metadata_size, MS_SYNC))
    {
        Err("Could not sync metadata : err=%d:%s\n", errno, strerror (errno));
        mcachefs_metadata_unlock ();
        return;
    }
    mcachefs_metadata_unlock ();
    Log("Metadata synced (size=%lu)\n", (unsigned long) mcachefs_metadata_size);
}
#endif // __MCACHEFS_METADATA_HAS_SYNC
