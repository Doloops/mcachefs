/*
 * mcachefs-lowlevel.c
 *
 *  Created on: 9 oct. 2010
 *      Author: francois
 */

/*********************************************************************/
/*                    FUSE CALLBACKS                                 */
/*********************************************************************/

#include "mcachefs.h"
#include "mcachefs-io.h"
#include "mcachefs-journal.h"
#include "mcachefs-transfer.h"
#include "mcachefs-vops.h"

#if 0
struct stat mcachefs_target_stat;

void
__mcachefs_fill_stat(struct stat *st, int type, off_t size)
{
    memset(st, 0, sizeof(struct stat));
    st->st_mode = type
        | ((type == S_IFDIR) ?
           (mcachefs_target_stat.st_mode & 0700) :
           (mcachefs_target_stat.st_mode & 0600));
    st->st_uid = mcachefs_target_stat.st_uid;
    st->st_gid = mcachefs_target_stat.st_gid;
    st->st_size = size;
}
#endif

#define __MCACHEFS_IS_VOPS_DIR(__path) ( strcmp(path, MCACHEFS_VOPS_DIR) == 0 )
#define __MCACHEFS_IS_VOPS_FILE(__path) ( strncmp(path, MCACHEFS_VOPS_FILE_PREFIX, 11 ) == 0 )

static int
mcachefs_getattr(const char *path, struct stat *stbuf)
{
    Log("mcachefs_getattr(path = %s, stbuf = ...)\n", path);
    return mcachefs_metadata_getattr(path, stbuf);
}

static int
mcachefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *info)
{
    int res = 0;
    struct mcachefs_metadata_t *mfather, *mchild;
    struct stat st;

    (void) offset;
    (void) info;

    Log("readdir '%s'\n", path);

    memset(&st, 0, sizeof(struct stat));
    st.st_mode = S_IFDIR;
    res = filler(buf, ".", &st, 0);
    if (res)
        return res;
    res = filler(buf, "..", &st, 0);
    if (res)
        return res;

    Log("READDIR path='%s'\n", path);
    mfather = mcachefs_metadata_find(path);

    if (!mfather)
        return -ENOENT;

    for (mchild = mcachefs_metadata_get_child(mfather); mchild; mchild =
         mcachefs_metadata_get(mchild->next))
    {
        Log("READDIR    '%s' (%p, next=%llu)\n", mchild->d_name, mchild,
            mchild->next);
        res = filler(buf, mchild->d_name, &(mchild->st), 0);
        if (res)
            break;
    }

    mcachefs_metadata_release(mfather);
    return res;
}

static int
mcachefs_readlink(const char *path, char *buf, size_t size)
{
    struct mcachefs_metadata_t *mdata;
    char *backingpath, *realpath;
    ssize_t res;
    Log("mcachefs_readlink(path = %s, buf = ..., size = %lu)\n", path,
        (long) size);

    memset(buf, 0, size);
    mdata = mcachefs_metadata_find(path);

    if (!mdata)
        return -ENOENT;

    if (!S_ISLNK(mdata->st.st_mode))
    {
        mcachefs_metadata_release(mdata);
        return -EINVAL;
    }
    mcachefs_metadata_release(mdata);

    backingpath = mcachefs_makepath_cache(path);
    if (!backingpath)
        return -ENOMEM;

    if ((res = readlink(backingpath, buf, size)) != -1)
    {
        free(backingpath);
        return 0;
    }

    Log("Could not read from backing... %s\n", backingpath);
    realpath = mcachefs_makepath_source(path);

    if ((res = readlink(realpath, buf, size)) != -1)
    {
        if (mcachefs_createpath_cache(path, 0))
        {
            Err("Could not create new backing path '%s'\n", backingpath);
        }

        if (symlink(buf, backingpath))
        {
            Err("Could not put link to back path.\n");
        }
        free(realpath);
        free(backingpath);
        return 0;
    }

    free(realpath);
    free(backingpath);
    Err("Could not get link from real : err=%d:%s\n", errno, strerror(errno));
    return -errno;
}

static int
mcachefs_symlink(const char *path, const char *to)
{
    Log("mcachefs_symlink(path = %s, to = %s)\n", path, to);
    int res;
    char *backingto;

    if ((res = mcachefs_metadata_make_entry(to, S_IFLNK | 0777, 0)) != 0)
    {
        Err("Could not make symlink entry : err=%d:%s\n", -res,
            strerror(-res));
        return -res;
    }

    if ((res = mcachefs_createpath_cache(to, 0)) != 0)
    {
        Err("Could not create backing path '%s' : err=%d:%s\n", to, -res,
            strerror(-res));
        return res;
    }

    backingto = mcachefs_makepath_cache(to);

    if (mcachefs_createpath_cache(to, 0))
    {
        Err("Could not create new backing path '%s'\n", backingto);
    }

    if ((res = symlink(path, backingto)) != 0)
    {
        Err("Could not make backing symlink entry : err=%d:%s\n", errno,
            strerror(errno));
        free(backingto);
        mcachefs_metadata_rmdir_unlink(to, 0);
        return -errno;
    }
    free(backingto);

    mcachefs_journal_append(mcachefs_journal_op_symlink, to, path, 0, 0, 0,
                            0, 0, NULL);

    return 0;
}

static int
mcachefs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;

    Log("mcachefs_mknod(path = %s, mode = %lo, rdev = %ld\n", path,
        (long) mode, (long) rdev);

    if ((res = mcachefs_metadata_make_entry(path, mode, rdev)) != 0)
    {
        Err("mknod '%s' : Making metadata entry failed : err=%d:%s\n", path,
            -res, strerror(-res));
        return res;
    }

    mcachefs_journal_append(mcachefs_journal_op_mknod, path, NULL, mode,
                            rdev, fuse_get_context()->uid,
                            fuse_get_context()->gid, 0, NULL);

    Log("mknod : OK.\n");

    if (S_ISREG(mode))
    {
        /*
         * Create entry in backup
         */
        if ((res = mcachefs_createfile_cache(path, mode)) != 0)
        {
            Err("mknod : Making backing file for '%s' failed : err=%d:%s\n",
                path, -res, strerror(-res));
            return res;
        }
    }
    return 0;
}

static int
mcachefs_mkdir(const char *path, mode_t mode)
{
    int res;

    Log("mcachefs_mkdir(path = %s)\n", path);

    if ((res = mcachefs_metadata_make_entry(path, mode | S_IFDIR, 0)) != 0)
    {
        Err("mkdir %s : err=%d:%s\n", path, res, strerror(-res));
        return res;
    }

    Log("mkdir : OK.\n");

    mcachefs_journal_append(mcachefs_journal_op_mkdir, path, NULL, mode, 0,
                            fuse_get_context()->uid,
                            fuse_get_context()->gid, 0, NULL);

    return 0;
}

static int
mcachefs_unlink(const char *path)
{
    int res;
    char *backingpath;

    Log("mcachefs_unlink(path = %s)\n", path);

    if ((res = mcachefs_metadata_rmdir_unlink(path, 0)) != 0)
    {
        Err("rmdir '%s' : err=%d:%s\n", path, res, strerror(-res));
        return res;
    }
    if (mcachefs_fileincache(path))
    {
        backingpath = mcachefs_makepath_cache(path);
        if (unlink(backingpath))
        {
            Err("Could not unlink backing path '%s' : err=%d:%s\n",
                backingpath, errno, strerror(errno));
        }
        free(backingpath);
    }

    mcachefs_journal_append(mcachefs_journal_op_unlink, path, NULL, 0, 0, 0, 0, 0, NULL);
    return 0;
}

static int
mcachefs_rmdir(const char *path)
{
    int res;
    char *backingpath;

    Log("mcachefs_rmdir(path = %s)\n", path);

    if ((res = mcachefs_metadata_rmdir_unlink(path, 1)) != 0)
    {
        Err("rmdir '%s' : err=%d:%s\n", path, res, strerror(-res));
        return res;
    }

    mcachefs_journal_append(mcachefs_journal_op_rmdir, path, NULL, 0, 0, 0,
                            0, 0, NULL);

    if (mcachefs_fileincache(path))
    {
        backingpath = mcachefs_makepath_cache(path);
        if (rmdir(backingpath))
        {
            Err("Could not rmdir backing path '%s' : err=%d:%s\n",
                backingpath, errno, strerror(errno));
        }
        free(backingpath);
    }

    Log("rmdir : OK.\n");

    return 0;
}

static int
mcachefs_rename(const char *path, const char *to)
{
    int res;
    char *backingpath, *backingto;

    Log("mcachefs_rename(from = %s, to = %s)\n", path, to);

    if ((res = mcachefs_metadata_rename_entry(path, to)) != 0)
    {
        Log("rename '%s' => '%s' : err=%d:%s\n", path, to, res,
            strerror(-res));
        return res;
    }

    mcachefs_journal_append(mcachefs_journal_op_rename, path, to, 0, 0, 0, 0,
                            0, NULL);

    if (mcachefs_fileincache(path))
    {
        backingpath = mcachefs_makepath_cache(path);
        backingto = mcachefs_makepath_cache(to);
        if (mcachefs_createpath_cache(to, 0))
        {
            Err("Could not create new cache path '%s'\n", backingto);
        }
        if (rename(backingpath, backingto))
        {
            Err("Could not rename cache path '%s' '%s' : err=%d:%s\n",
                backingpath, backingto, errno, strerror(errno));
        }
        free(backingpath);
        free(backingto);
    }
    return 0;
}

static int
mcachefs_link(const char *from, const char *to)
{
    int res;
    char *backingfrom, *backingto;

    struct mcachefs_metadata_t* meta = mcachefs_metadata_find(from);
    if ( meta == NULL )
    {
        return -ENOENT;
    }
    struct stat fromst = meta->st;
    mcachefs_metadata_id fromid = meta->id;
    mcachefs_metadata_id next_hardlink = meta->hardlink;
    mcachefs_metadata_release(meta);

    backingfrom = mcachefs_makepath_cache(from);
    if (backingfrom == NULL)
    {
        return -ENOMEM;
    }

    backingto = mcachefs_makepath_cache(to);
    if (backingto == NULL)
    {
        free(backingfrom);
        return -ENOMEM;
    }

    res = link(backingfrom, backingto);

    free(backingfrom);
    free(backingto);
    if (res == -1)
        return -errno;

    res = mcachefs_metadata_make_entry(to, fromst.st_mode, fromst.st_dev);
    if ( res )
    {
        return res;
    }
    meta = mcachefs_metadata_find(to);
    mcachefs_metadata_id toid = 0;
    if ( !meta )
    {
        Bug("Could not get meta for to=%s\n", to);
    }
    toid = meta->id;
    meta->st = fromst;
    meta->hardlink = next_hardlink ? next_hardlink : fromid;
    meta->st.st_nlink ++;
    mcachefs_metadata_release(meta);

    meta = mcachefs_metadata_find(from);
    if ( !meta )
    {
        Bug("Could not get meta for from=%s\n", from);
    }
    meta->st.st_nlink ++;
    meta->hardlink = toid;
    mcachefs_metadata_release(meta);
    mcachefs_journal_append(mcachefs_journal_op_link, from, to, 0, 0, 0, 0, 0, NULL);
    return 0;
}

static int
mcachefs_chmod(const char *path, mode_t mode)
{
    struct mcachefs_metadata_t *mdata;
    static const mode_t umask = 0777;
    Log("mcachefs_chmod(path = %s, mode = %lo)\n", path, (long) mode);

    mdata = mcachefs_metadata_find(path);
    if (!mdata)
        return -ENOENT;

    mdata->st.st_mode = ((mdata->st.st_mode & ~umask) | (mode & umask));
    mcachefs_metadata_notify_update(mdata);
    mcachefs_metadata_release(mdata);

    mcachefs_journal_append(mcachefs_journal_op_chmod, path, NULL, mode, 0,
                            0, 0, 0, NULL);

    return 0;
}

static int
mcachefs_chown(const char *path, uid_t uid, gid_t gid)
{
    struct mcachefs_metadata_t *mdata;

    Log("mcachefs_chown(path = %s, uid = %ld, gid = %ld)\n", path,
        (long) uid, (long) gid);

    if (__MCACHEFS_IS_VOPS_FILE(path))
        return -ENOSYS;

    mdata = mcachefs_metadata_find(path);
    if (!mdata)
        return -ENOENT;


    if (uid==0xFFFFFFFF)
      uid = mdata->st.st_uid;
    else
      mdata->st.st_uid = uid;
    if (gid==0xFFFFFFFF)
      gid = mdata->st.st_gid;
    else
      mdata->st.st_gid = gid;
    mcachefs_metadata_notify_update(mdata);
    mcachefs_metadata_release(mdata);

    mcachefs_journal_append(mcachefs_journal_op_chown, path, NULL, 0, 0, uid,
                            gid, 0, NULL);

    return 0;
}

static int
mcachefs_truncate(const char *path, off_t size)
{
    struct mcachefs_file_t *mfile;
    char *backingpath;
    struct mcachefs_metadata_t *mdata;

    Log("mcachefs_truncate(path = %s, size = %llu)\n", path,
        (unsigned long long) size);

    mdata = mcachefs_metadata_find(path);
    if (!mdata)
        return -ENOENT;

    mdata->st.st_size = size;

    if (mdata->fh)
    {
        mfile = mcachefs_file_get(mdata->fh);
        if (mfile->type == mcachefs_file_type_vops)
        {
            mcachefs_file_lock_file(mfile);
            while (mfile->use > 1)
            {
                mcachefs_file_unlock_file(mfile);
                Log("VOPS file '%s' in use, waiting...\n", mfile->path);
                sleep(3);
                mcachefs_file_lock_file(mfile);
            }
            mfile->contents_size = 0;
            mfile->contents_alloced = 0;
            free(mfile->contents);
            mfile->contents = NULL;
            mcachefs_file_unlock_file(mfile);
            mcachefs_metadata_release(mdata);
            return 0;
        }
    }
    mcachefs_metadata_release(mdata);

    if (__MCACHEFS_IS_VOPS_FILE(path))
    {
        /**
         * We have done the update at the metadata level, now just cut off journal updating.
         */
        return 0;
    }

    /*
     * Create the journal entry
     */
    mcachefs_journal_append(mcachefs_journal_op_truncate, path, NULL, 0, 0,
                            0, 0, size, NULL);

    /*
     * Now shrink the temp file path if we need to
     */
    if (mcachefs_fileincache(path))
    {
        backingpath = mcachefs_makepath_cache(path);
        if (!backingpath)
            return -ENOMEM;
        if (truncate(backingpath, size))
        {
            Err("Could not truncate cache path '%s' for file '%s', err=%d:%s\n", backingpath, path, errno, strerror(errno));
        }
        free(backingpath);
    }
    return 0;
}

static int
mcachefs_utime(const char *path, struct utimbuf *buf)
{
    struct mcachefs_metadata_t *mdata;
    Log("mcachefs_utime(path = %s, act=%lu, mod=%lu)\n", path, buf->actime,
        buf->modtime);

    if ((mdata = mcachefs_metadata_find(path)) == NULL)
        return -ENOENT;

    mdata->st.st_atime = buf->actime;
    mdata->st.st_mtime = buf->modtime;

    mcachefs_metadata_release(mdata);

    mcachefs_journal_append(mcachefs_journal_op_utime, path, NULL, 0, 0, 0,
                            0, 0, buf);

    return 0;
}

/* mcachefs_open() is effectively an existance check. We also take the
 opportunity to fire off a copy process to pull the content into the backing
 store. This will hopefully have pulled the data that the client is going
 to ask for before the client needs it. If it hasn't then we're going
 to have to block... */
static int
mcachefs_open(const char *path, struct fuse_file_info *info)
{
    struct mcachefs_file_t *mfile;
    struct mcachefs_metadata_t *mdata;

    mcachefs_file_type_t type = mcachefs_file_type_file;

    mdata = mcachefs_metadata_find(path);

    if (mdata == NULL)
    {
        Err("open(%s) : does not exist\n", path);
        return -ENOENT;
    }
    if (!S_ISREG(mdata->st.st_mode))
    {
        Err("open(%s) : not a regular file !\n", path);
        mcachefs_metadata_release(mdata);
        return -EISDIR;
    }

    if (__MCACHEFS_IS_VOPS_FILE(path))
    {
        type = mcachefs_file_type_vops;
    }
    else
    {
#ifdef MCACHEFS_DISABLE_WRITE
        if (__IS_WRITE(info->flags))
        {
            mcachefs_metadata_release(mdata);
            return -EROFS;
        }
#endif
        type = mcachefs_file_type_file;
    }

    info->fh = mcachefs_fileid_get(mdata, path, type);
    mcachefs_metadata_release(mdata);

    mfile = mcachefs_file_get(info->fh);

    if (!mfile)
    {
        return -ENOMEM;
    }
    return mcachefs_open_mfile(mfile, info, type);
}

static int
mcachefs_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *info)
{
    struct mcachefs_file_t *mfile;

    Log("mcachefs_read(path = %s, buf = ..., size = %llu, offset = %llu, fh=%llx)\n", path, (unsigned long long) size, (unsigned long long) offset, (unsigned long long) info->fh);

    mfile = mcachefs_file_get(info->fh);

    if (!mfile)
    {
        Err("Invalid descriptor for '%s'\n", path);
        return -EBADF;
    }

    mcachefs_file_timeslice_freshen(mfile);

#if PARANOID
    /*
     * paranoid check !
     */
    if (strcmp(mfile->path, path))
    {
        Warn("Diverging paths : '%s' != '%s' (file has been renamed ?)\n",
             path, mfile->path);
    }
#endif

    Log("mfile at %p\n", mfile);
    return mcachefs_read_mfile(mfile, buf, size, offset);
}

static int
mcachefs_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *info)
{
    struct mcachefs_file_t *mfile;

    Log("mcachefs_write(path = %s, buf = ..., size = %llu, offset = %llu)\n",
        path, (unsigned long long) size, (unsigned long long) offset);

    if (size == 0)
    {
        Err("mcachefs_write() : attempt to write zero bytes in '%s'\n", path);
        return 0;
    }

    mfile = mcachefs_file_get(info->fh);

    if (!mfile)
    {
        Err("Invalid descriptor for '%s'\n", path);
        return -EBADF;
    }

    mcachefs_file_timeslice_freshen(mfile);
    return mcachefs_write_mfile(mfile, buf, size, offset);
}

static int
mcachefs_fsync(const char *path, int sync, struct fuse_file_info *info)
{
    (void) sync;

    struct mcachefs_file_t *mfile;
    Log("mcachefs_fsync (%s, %d, fh=%llu)\n", path, sync,
        (unsigned long long) info->fh);

    mfile = mcachefs_file_get(info->fh);

    if (!mfile)
    {
        Err("Invalid descriptor for '%s'\n", path);
        return -EBADF;
    }
    return mcachefs_fsync_mfile(mfile);
}

static int
mcachefs_release(const char *path, struct fuse_file_info *info)
{
    Log("mcachefs_release (path=%s, flags=%x)\n", path, info->flags);
    struct mcachefs_file_t *mfile;

    mfile = mcachefs_file_get(info->fh);

    if (!mfile)
    {
        Err("Invalid descriptor for '%s'\n", path);
        return -EBADF;
    }

    return mcachefs_release_mfile(mfile, info);
}

#if 0
static int
mcachefs_statfs(const char *path, struct statvfs *fsinfo)
{
    (void) path;
    (void) fsinfo;
    return -ENOSYS;
}
#endif

static int
mcachefs_flush(const char *path, struct fuse_file_info *info)
{
    (void) path;
    (void) info;
    Log("[NOT IMPLEMENTED] mcachefs_flush(%s,fh=%lx)\n", path,
        (unsigned long) info->fh);
    return 0;
}

static void *
mcachefs_init(struct fuse_conn_info *conn)
{
    (void) conn;

    mcachefs_file_start_thread();
    mcachefs_transfer_start_threads();
    mcachefs_journal_init();

    Info("Filesystem now serving requests...\n");

    return NULL;
}

static void
mcachefs_destroy(void *conn)
{
    (void) conn;

    Info("Waiting for mcachefs background threads to end...\n");
    mcachefs_config_set_read_state(MCACHEFS_STATE_QUITTING);

    mcachefs_file_stop_thread();
    mcachefs_transfer_stop_threads();
    mcachefs_config_run_post_umount_cmd();
}

struct fuse_operations mcachefs_oper =
    {.getattr = mcachefs_getattr,.readlink = mcachefs_readlink,
    .getdir = NULL,.mknod = mcachefs_mknod,.mkdir = mcachefs_mkdir,
    .unlink = mcachefs_unlink,.rmdir = mcachefs_rmdir,.symlink =
        mcachefs_symlink,.rename = mcachefs_rename,.link =
        mcachefs_link,.chmod = mcachefs_chmod,.chown =
        mcachefs_chown,.truncate = mcachefs_truncate,.utime =
        mcachefs_utime,.open = mcachefs_open,
    .read = mcachefs_read,.write = mcachefs_write,.statfs = NULL,
    .flush = mcachefs_flush,.release = mcachefs_release,.fsync =
        mcachefs_fsync,.setxattr = NULL,.getxattr = NULL,
    .listxattr = NULL,.opendir = NULL,.readdir = mcachefs_readdir,
    .fsyncdir = NULL,.init = mcachefs_init,
    .destroy = mcachefs_destroy,.access = NULL,.create = NULL,
    .ftruncate = NULL,.fgetattr = NULL,.lock = NULL,.utimens = NULL,
    .bmap = NULL,
};
