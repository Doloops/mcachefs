/*
 * mcachefs-io.c
 *
 *  Created on: 9 oct. 2010
 *      Author: francois
 */

#include "mcachefs.h"
#include "mcachefs-journal.h"
#include "mcachefs-transfer.h"
#include "mcachefs-vops.h"

static const int WAIT_CACHE_INTERVAL = 5 * 1000 * 1000;

int
mcachefs_open_mfile (struct mcachefs_file_t *mfile,
                     struct fuse_file_info *info, mcachefs_file_type_t type)
{
    if (type == mcachefs_file_type_file)
    {
#if 0
        info->direct_io = 1;
#endif
        if (mcachefs_getstate () != MCACHEFS_STATE_NOCACHE
            || __IS_WRITE (info->flags))
        {
            mcachefs_transfer_backfile (mfile);
        }
        else
        {
            Log ("MCachefs : skipping backup of file %s, state=%d\n",
                 mfile->path, mcachefs_getstate ());
            if (mfile->backing_status == MCACHEFS_FILE_BACKING_ASKED)
            {
                if (mcachefs_fileincache (mfile->path))
                {
                    mfile->backing_status = MCACHEFS_FILE_BACKING_DONE;
                }
            }
        }
    }
    else if (type == mcachefs_file_type_vops)
    {
        Log ("VOPS OPEN %s : fh=%llu, contents=%p\n", mfile->path,
             (unsigned long long) info->fh, mfile->contents);
        info->direct_io = 1;
        if (!__IS_WRITE (info->flags))
            mcachefs_vops_build (mfile);
    }

    return 0;
}

int
mcachefs_read_vops (struct mcachefs_file_t *mfile, char *buf, size_t size,
                    off_t offset)
{
    int res = 0;
    if (mfile->contents == NULL)
    {
        Log ("Empty VOPS contents !\n");
        return -EIO;
    }
    if (offset >= mfile->contents_size)
    {
        Log ("VOPS : Exceeding size for '%s' : offset=%llu, mfile->contents_size=%llu !\n", mfile->path, (unsigned long long) offset, (unsigned long long) mfile->contents_size);
        return 0;
    }
    res = ((off_t) size <= mfile->contents_size - offset) ? (int) size
        : (int) (mfile->contents_size - offset);
    memcpy (buf, &(mfile->contents[offset]), res);
    Log ("VOPS '%s' : read %d\n", mfile->path, res);
    return res;
}

static int
mcachefs_read_wait_accessible (struct mcachefs_file_t *mfile, size_t size,
                               off_t offset)
{
    int use_real = 1;
    int waited_backing = 0, waited_backing_max = 10;
    struct timespec read_wait_time;

    mcachefs_file_lock_file (mfile);
    while (1)
    {
        if (mfile->backing_status == MCACHEFS_FILE_BACKING_DONE)
        {
            use_real = 0;
            break;
        }
        if (mcachefs_getstate () == MCACHEFS_STATE_NOCACHE)
        {
            use_real = 1;
            break;
        }
        if (mfile->backing_status == MCACHEFS_FILE_BACKING_ASKED
            || mfile->backing_status == MCACHEFS_FILE_BACKING_ERROR)
        {
            use_real = 1;
            break;
        }
        if (mfile->backing_status == MCACHEFS_FILE_BACKING_IN_PROGRESS
            && mfile->transfer.transfered_size >= offset + (off_t) size)
        {
            use_real = 0;
            break;
        }
        if (waited_backing == waited_backing_max)
        {
            Err ("Sick of waiting for end of backing, try to use real one.\n");
            use_real = 1;
            break;
        }
        mcachefs_file_unlock_file (mfile);
        read_wait_time.tv_sec = 0;
        read_wait_time.tv_nsec = WAIT_CACHE_INTERVAL;
        nanosleep (&read_wait_time, NULL);
        Log ("Waiting for end of backing for file '%s', offset=%luk, size=%luk, end of segment=%luk\n", 
            mfile->path, (unsigned long) offset >> 10, (unsigned long) size >> 10, (unsigned long) (offset + (off_t) size) >> 10);
        mcachefs_file_lock_file (mfile);
        waited_backing++;
    }
    mcachefs_file_unlock_file (mfile);
    Log ("Waiting read path='%s' : mfile->backing_status=%d, use_real=%d\n",
         mfile->path, mfile->backing_status, use_real);
    return use_real;
}

int
mcachefs_read_file (struct mcachefs_file_t *mfile, char *buf, size_t size,
                    off_t offset)
{
    int res = 0;
    int fd;
    int use_real;
    struct mcachefs_metadata_t *mdata;

    use_real = mcachefs_read_wait_accessible (mfile, size, offset);

    if (use_real && mcachefs_getstate () == MCACHEFS_STATE_HANDSUP)
    {
        Err ("While reading '%s' : mcachefs state set to HANDSUP.\n",
             mfile->path);
        return -EIO;
    }

    fd = mcachefs_file_getfd (mfile, use_real, O_RDONLY);
    Log ("reading : fd=%d, use_real=%d\n", fd, use_real);

    if (fd < 0)
    {
        Err ("Could not get file descriptor for '%s' on %s : err=%d:%s\n",
             mfile->path, use_real ? "target" : "backing", -fd,
             strerror (-fd));
        return -EIO;
    }

    res = pread (fd, buf, size, offset);
    if (res < 0)
    {
        res = -errno;
        Err ("Error while reading '%s' on fd=%d, real=%d : %d:%s\n",
             mfile->path, fd, use_real, errno, strerror (errno));
    }
    else
    {
        mfile->sources[use_real].nbrd++;
        mfile->sources[use_real].bytesrd += res;
    }
    mcachefs_file_putfd (mfile, use_real);

    if (res > 0)
    {
        mcachefs_file_update_metadata (mfile, 0, 0);
    }
    if (res != (int) size)
    {
        mdata = mcachefs_file_get_metadata (mfile);
        if (!mdata)
        {
            Err ("Could not fetch metadata for '%s' !\n", mfile->path);
        }
        else if ((off_t) size + offset > mdata->st.st_size)
        {
            Log ("Read after tail '%s' : size=%lu, offset=%lu, end=%lu, size=%lu\n", mfile->path, (unsigned long) size, (unsigned long) offset, (unsigned long) ((off_t) size + offset), (unsigned long) mdata->st.st_size);
        }
        else
        {
            Err ("Could not fully read '%s' : asked=%lu, had %d, offset=%lu, max=%lu, tail=%lu\n", mfile->path, (unsigned long) size, res, (unsigned long) offset, (unsigned long) (offset + size), (unsigned long) mdata->st.st_size);
        }
        if (mdata)
            mcachefs_metadata_release (mdata);
    }
    Log ("read : res=%d\n", res);
    return res;
}


int
mcachefs_read_mfile (struct mcachefs_file_t *mfile, char *buf, size_t size,
                     off_t offset)
{
    if (mfile->type == mcachefs_file_type_vops)
    {
        return mcachefs_read_vops (mfile, buf, size, offset);
    }
    else if (mfile->type == mcachefs_file_type_file)
    {
        return mcachefs_read_file (mfile, buf, size, offset);
    }

    Err ("Invalid type for read() : %s has type %d\n", mfile->path,
         mfile->type);
    return -ENOSYS;
}


int
mcachefs_write_vops (struct mcachefs_file_t *mfile, const char *buf,
                     size_t size, off_t offset)
{
    mcachefs_file_lock_file (mfile);
    if (offset + (off_t) size > mfile->contents_alloced)
    {
        mfile->contents_alloced = offset + size;
        mfile->contents = (char *) realloc (mfile->contents,
                                            mfile->contents_alloced);

        if (mfile->contents == NULL)
        {
            mfile->contents_alloced = 0;
            mfile->contents_size = 0;
            mcachefs_file_unlock_file (mfile);
            return -ENOMEM;
        }
    }

    memcpy (&(mfile->contents[offset]), buf, size);
    Log ("VOPS WRITE '%s' : write %llu, now alloced=%llu, size=%llu\n",
         mfile->path, (unsigned long long) size,
         (unsigned long long) mfile->contents_alloced,
         (unsigned long long) mfile->contents_size);
    if (offset + (off_t) size > mfile->contents_size)
    {
        mfile->contents_size = offset + (off_t) size;
    }
    mcachefs_file_unlock_file (mfile);
    return size;
}

int
mcachefs_write_file (struct mcachefs_file_t *mfile, const char *buf,
                     size_t size, off_t offset)
{
    ssize_t bytes;
    int fd;
    int use_real = 0;

    mcachefs_file_lock_file (mfile);
    while (mfile->backing_status != MCACHEFS_FILE_BACKING_DONE)
    {
        Info ("write(%s) : waiting for backing to complete...\n",
              mfile->path);
        if (mfile->backing_status == MCACHEFS_FILE_BACKING_ERROR)
        {
            Err ("write(%s) : backing failed !\n", mfile->path);
            mcachefs_file_unlock_file (mfile);
            return -EIO;
        }
        mcachefs_file_unlock_file (mfile);
        sleep (1);
        mcachefs_file_lock_file (mfile);
    }
    mcachefs_file_unlock_file (mfile);

    fd = mcachefs_file_getfd (mfile, use_real, O_RDWR);
    if (fd == -1)
    {
        Err ("Could not get backing fd ?\n");
        return -EIO;
    }

    Log ("write to '%s', fd=%d\n", mfile->path, fd);

    bytes = pwrite (fd, buf, size, offset);
    mcachefs_file_putfd (mfile, use_real);

    if (bytes != (int) size)
    {
        Err ("Could not write to '%s' : fd=%d, err=%d:%s\n", mfile->path, fd,
             errno, strerror (errno));
        return -errno;
    }

    mfile->sources[use_real].nbwr++;
    mfile->sources[use_real].byteswr += bytes;

    mcachefs_file_lock_file (mfile);
    if (!mfile->dirty)
    {
        /**
         * We have to append the fsync command on the journal
         * Do it with no lock on the file to prevent deadlocks
         */
        mfile->dirty = 1;
        mcachefs_file_unlock_file (mfile);
        mcachefs_journal_append (mcachefs_journal_op_fsync, mfile->path, NULL,
                                 0, 0, 0, 0, 0, NULL);
    }
    else
    {
        mcachefs_file_unlock_file (mfile);
    }

    mcachefs_file_update_metadata (mfile, offset + size, 1);
    Log ("write to '%s' ok : written %ld bytes at offset %ld\n", mfile->path,
         (long) bytes, (long) offset);
    return (int) bytes;
}

int
mcachefs_write_mfile (struct mcachefs_file_t *mfile, const char *buf,
                      size_t size, off_t offset)
{
    if (mfile->type == mcachefs_file_type_vops)
    {
        return mcachefs_write_vops (mfile, buf, size, offset);
    }
    else if (mfile->type == mcachefs_file_type_file)
    {
#ifdef MCACHEFS_DISABLE_WRITE
        return -EROFS;
#endif
        return mcachefs_write_file (mfile, buf, size, offset);
    }
    Err ("Invalid type for write() : %s has type %d\n", mfile->path,
         mfile->type);
    return -ENOSYS;
}

int
mcachefs_fsync_mfile (struct mcachefs_file_t *mfile)
{
    mcachefs_file_lock_file (mfile);
    if (mfile->sources[MCACHEFS_FILE_SOURCE_BACKING].fd != -1
        && mfile->sources[MCACHEFS_FILE_SOURCE_BACKING].wr)
    {
        if (fsync (mfile->sources[MCACHEFS_FILE_SOURCE_BACKING].fd))
        {
            Err ("Could not fsync(%s) : err=%d:%s\n", mfile->path, errno,
                 strerror (errno));
        }
    }
    mcachefs_file_unlock_file (mfile);
    return 0;

}

int
mcachefs_release_mfile (struct mcachefs_file_t *mfile,
                        struct fuse_file_info *info)
{
    if (mfile->type == mcachefs_file_type_vops)
    {
        Log ("VOPS release '%s'\n", mfile->path);
        if (__IS_WRITE (info->flags))
        {
            mcachefs_vops_parse (mfile);
        }
        else
        {
            mcachefs_vops_cleanup_vops (mfile);
        }
    }
    else if (mfile->type == mcachefs_file_type_file)
    {
        Log ("[STATS] '%s' : rd=%lu,%lu,wr=%lu,%lu, real=%lu,%lu\n",
             mfile->path,
             (unsigned long) mfile->sources[0].nbrd,
             (unsigned long) mfile->sources[0].bytesrd,
             (unsigned long) mfile->sources[0].nbwr,
             (unsigned long) mfile->sources[0].byteswr,
             (unsigned long) mfile->sources[1].nbrd,
             (unsigned long) mfile->sources[1].bytesrd);
        if (__IS_WRITE (info->flags))
        {
            Log ("release O_RDWR file '%s'\n", mfile->path);
#ifdef MCACHEFS_DISABLE_WRITE
            mcachefs_fileid_put (info->fh);
            return -EROFS;
#endif
            // mcachefs_fsync(mfile->path, 0, info);
            mcachefs_fsync_mfile (mfile);
        }
    }
    else
    {
        Err ("Invalid file type %d\n", mfile->type);
    }
    mcachefs_fileid_put (info->fh);
    return 0;
}
