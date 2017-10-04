#include "mcachefs.h"
#include "mcachefs-journal.h"
#include "mcachefs-transfer.h"
#include "mcachefs-vops.h"

#include <sys/sendfile.h>

// #define  __MCACHEFS_TRANSFER_DO_FTRUNCATE_TARGET

static const off_t mcachefs_transfer_window_size_min = 4 * (1 << 10);
static const off_t mcachefs_transfer_window_size_max = 128 * (1 << 10);

struct mcachefs_transfer_thread_t
{
    pthread_t threadid;
    struct mcachefs_file_t *currentfile;
    int type;
};

static struct mcachefs_transfer_thread_t *mcachefs_transfer_threads;

int mcachefs_transfer_threads_nb = 0;

struct mcachefs_transfer_queue_t
{
    int type;
    struct mcachefs_file_t *mfile;
    struct mcachefs_transfer_queue_t *next;
};

struct mcachefs_transfer_queue_t *mcachefs_transfer_queue_head = NULL,
    *mcachefs_transfer_queue_tail = NULL;

sem_t mcachefs_transfer_sem[MCACHEFS_TRANSFER_TYPES];

struct mcachefs_file_t *mcachefs_transfer_get_next_file_to_back_locked(int
                                                                       type);
void mcachefs_transfer_do_transfer(struct mcachefs_file_t *mfile);
void *mcachefs_transfer_thread(void *arg);

#define TIME_DIFF(NOW, LAST) ((NOW.tv_sec-LAST.tv_sec)*1000000 + (NOW.tv_usec-LAST.tv_usec))

/**
 * Backing frontend
 */
int
mcachefs_transfer_backfile(struct mcachefs_file_t *mfile)
{
    /**
     * Called from mcachefs.c : mcachefs_fileid_get() with the mcachefs_file_lock held
     */
    mcachefs_file_lock_file(mfile);

    if (mfile->cache_status == MCACHEFS_FILE_BACKING_IN_PROGRESS)
    {
        Log("Backing in progress for file '%s'\n", mfile->path);
        mcachefs_file_unlock_file(mfile);
        return 0;
    }

    if (mcachefs_fileincache(mfile->path))
    {
        mfile->cache_status = MCACHEFS_FILE_BACKING_DONE;
        Log("Backing ok for file '%s' (status set to %d)\n", mfile->path,
            mfile->cache_status);
        mcachefs_file_unlock_file(mfile);
        return 0;
    }

    Log("Asking backing for file '%s'\n", mfile->path);

    mfile->cache_status = MCACHEFS_FILE_BACKING_ASKED;
    mfile->use++;
    mcachefs_file_unlock_file(mfile);

    mcachefs_transfer_queue_file(mfile, MCACHEFS_TRANSFER_TYPE_BACKUP);
    return 0;
}

/**
 * Writeback frontend
 */
int
mcachefs_transfer_writeback(const char *path)
{
    struct mcachefs_metadata_t *mdata;
    struct mcachefs_file_t *mfile;

    mcachefs_fh_t fh;

    if (!mcachefs_fileincache(path))
    {
        Err("Will not sync '%s' : file not in backing !\n", path);
        return -EIO;
    }

    mdata = mcachefs_metadata_find(path);
    if (!mdata)
    {
        Err("Will not sync '%s' : could not find metadata !\n", path);
        return -ENOENT;
    }

    /**
     * This will increment usage, which will be decremented in mcachefs_do_write_back_file
     */
    fh = mcachefs_fileid_get(mdata, path, mcachefs_file_type_file);

    mcachefs_metadata_release(mdata);

    mfile = mcachefs_file_get(fh);
    mfile->cache_status = MCACHEFS_FILE_BACKING_DONE;

    if (mfile->cache_status != MCACHEFS_FILE_BACKING_DONE)
    {
        Bug("Error ! mfile=%s has backing=%d\n", mfile->path,
            mfile->cache_status);
    }
    return mcachefs_transfer_queue_file(mfile,
                                        MCACHEFS_TRANSFER_TYPE_WRITEBACK);
}

void
mcachefs_transfer_start_threads()
{
    int cur;
    int curt;
    int type;

    mcachefs_transfer_threads_nb = 0;

    for (type = 0; type < MCACHEFS_TRANSFER_TYPES; type++)
    {
        mcachefs_transfer_threads_nb +=
            mcachefs_config_get_transfer_threads_nb(type);
    }

    Info("Total threads : %d\n", mcachefs_transfer_threads_nb);


    mcachefs_transfer_threads = (struct mcachefs_transfer_thread_t *)
        malloc(sizeof(struct mcachefs_transfer_thread_t) *
               mcachefs_transfer_threads_nb);
    memset(mcachefs_transfer_threads, 0,
           sizeof(struct mcachefs_transfer_thread_t) *
           mcachefs_transfer_threads_nb);

    for (type = 0; type < MCACHEFS_TRANSFER_TYPES; type++)
    {
        sem_init(&(mcachefs_transfer_sem[type]), 0, 0);
    }

    pthread_t thid;
    pthread_attr_t attrs;
    pthread_attr_init(&attrs);
    pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);

    cur = 0;
    for (type = 0; type < MCACHEFS_TRANSFER_TYPES; type++)
    {
        for (curt = 0; curt < mcachefs_config_get_transfer_threads_nb(type);
             curt++)
        {
            mcachefs_transfer_threads[cur].type = type;
            pthread_create(&thid, &attrs, mcachefs_transfer_thread,
                           &(mcachefs_transfer_threads[cur]));
            mcachefs_transfer_threads[cur].threadid = thid;
            Log("Created new thread %lx, cur=%d, type=%d\n", thid, cur, type);
            cur++;
        }
    }
}

void
mcachefs_transfer_stop_threads()
{
    int cur, res;
    pthread_t thid;
    void *arg;

    if (mcachefs_config_get_read_state() != MCACHEFS_STATE_QUITTING)
    {
        Bug("Shall have set state to QUITTING !\n");
    }

    for (cur = 0; cur < mcachefs_transfer_threads_nb; cur++)
    {

        thid = mcachefs_transfer_threads[cur].threadid;
        if (thid == 0)
        {
            Err("Invalid thid=0 for cur=%d, type=%d\n", cur,
                mcachefs_transfer_threads[cur].type);
            continue;
        }
        Log("Posting STOP for thid=%lx, type=%d\n",
            mcachefs_transfer_threads[cur].threadid,
            mcachefs_transfer_threads[cur].type);
        sem_post(&
                 (mcachefs_transfer_sem
                  [mcachefs_transfer_threads[cur].type]));
    }
    for (cur = 0; cur < mcachefs_transfer_threads_nb; cur++)
    {
        thid = mcachefs_transfer_threads[cur].threadid;
        if (thid == 0)
            continue;
        Info("Waiting for thread %lx\n", thid);
        if ((res = pthread_join(thid, &arg)) != 0)
        {
            Err("Could not join transfer thread %lx : err=%d:%s\n", thid,
                res, strerror(res));
        }
    }
    Info("Transfer threads interrupted.\n");
}

void *
mcachefs_transfer_thread(void *arg)
{
    struct mcachefs_file_t *mfile = NULL;
    struct mcachefs_transfer_thread_t *me =
        (struct mcachefs_transfer_thread_t *) arg;
    int type = ~0;

    if (me == NULL)
    {
        Bug("Invalid NULL me !");
    }

    type = me->type;

    Info("Transfer thread %lx (type=%d) up and running.\n",
         (unsigned long) pthread_self(), type);

    while (1)
    {
        sem_wait(&(mcachefs_transfer_sem[type]));
        Log("Waking up transfer thread !\n");

        if (mcachefs_config_get_read_state() == MCACHEFS_STATE_QUITTING)
        {
            Log("Interrupting transfer thread %lx\n",
                (unsigned long) pthread_self());
            return NULL;
        }

        mcachefs_transfer_lock();
        mfile = mcachefs_transfer_get_next_file_to_back_locked(type);

        if (!mfile)
        {
            Bug("Could not locate which file to back !\n");
        }

        Log("Locked Transfer lock. file to transfer '%s', locking...\n",
            mfile->path);

        mcachefs_file_lock_file(mfile);

        Log("File locked.\n");

        if (mfile->cache_status == MCACHEFS_FILE_BACKING_ASKED)
        {
            mfile->cache_status = MCACHEFS_FILE_BACKING_IN_PROGRESS;
        }
        mcachefs_file_unlock_file(mfile);
        me->currentfile = mfile;
        mcachefs_transfer_unlock();

        Log("Transfer file '%s'\n", mfile->path);

        mcachefs_transfer_do_transfer(mfile);
        mcachefs_transfer_lock();
        me->currentfile = NULL;
        mcachefs_transfer_unlock();
        mcachefs_file_release(mfile);
    }
    return NULL;
}

int
mcachefs_transfer_queue_file(struct mcachefs_file_t *mfile, int type)
{
    struct mcachefs_transfer_queue_t *transfer;
    mcachefs_transfer_lock();

    for (transfer = mcachefs_transfer_queue_head; transfer; transfer
         = transfer->next)
    {
        if (transfer->mfile == mfile)
        {
            Err("Already asked transfer for file '%s'\n", mfile->path);
            mcachefs_transfer_unlock();
            return -EEXIST;
        }
    }

    transfer =
        (struct mcachefs_transfer_queue_t *)
        malloc(sizeof(struct mcachefs_transfer_queue_t));
    transfer->mfile = mfile;
    transfer->next = NULL;
    transfer->type = type;

    if (mcachefs_transfer_queue_tail)
    {
        mcachefs_transfer_queue_tail->next = transfer;
        mcachefs_transfer_queue_tail = transfer;
    }
    else
    {
        mcachefs_transfer_queue_head = transfer;
        mcachefs_transfer_queue_tail = transfer;
    }
    mcachefs_transfer_unlock();

    Log("USECNT %s => %d\n", mfile->path, mfile->use);
    sem_post(&(mcachefs_transfer_sem[type]));
    return 0;
}

struct mcachefs_file_t *
mcachefs_transfer_get_next_file_to_back_locked(int type)
{
    struct mcachefs_file_t *mfile;
    struct mcachefs_transfer_queue_t *transfer, *last = NULL;

    if (mcachefs_transfer_queue_head == NULL)
    {
        Bug("NULL HEAD !\n");
    }

    for (transfer = mcachefs_transfer_queue_head; transfer;
         transfer = transfer->next)
    {
        if (transfer->type == type)
            break;
        last = transfer;
    }

    if (!transfer)
    {
        Bug("No transfer set for type=%d\n", type);
    }


    mfile = transfer->mfile;

    Log("[NEXT : %s (type=%d, asked=%d)\n", mfile->path, transfer->type,
        type);

    if (last)
    {
        last->next = transfer->next;
        if (!transfer->next)
        {
            if (mcachefs_transfer_queue_tail != transfer)
            {
                Bug("Tail=%p is not transfer=%p, but next is NULL !",
                    mcachefs_transfer_queue_tail, transfer);
            }
            mcachefs_transfer_queue_tail = last;
        }
    }
    else
    {
        mcachefs_transfer_queue_head = transfer->next;
        if (!transfer->next)
        {
            if (mcachefs_transfer_queue_tail != transfer)
            {
                Bug("Tail=%p is not transfer=%p, but next is NULL !",
                    mcachefs_transfer_queue_tail, transfer);
            }
            mcachefs_transfer_queue_tail = NULL;
        }
    }
    free(transfer);

    return mfile;
}

void mcachefs_transfer_do_backing(struct mcachefs_file_t *mfile);

void
mcachefs_transfer_do_writeback(struct mcachefs_file_t *mfile,
                               struct utimbuf *timbuf);
int mcachefs_transfer_file(struct mcachefs_file_t *mfile, int tobacking);

void
mcachefs_transfer_do_transfer(struct mcachefs_file_t *mfile)
{
    int tobacking;
    off_t size;
    struct mcachefs_metadata_t *mdata;
    struct utimbuf timbuf;

    if (mcachefs_config_get_read_state() == MCACHEFS_STATE_HANDSUP)
    {
        Err("While backing file for '%s' : mcachefs state set to HANDSUP.\n",
            mfile->path);
        return;
    }

    if (mfile->type == mcachefs_file_type_dir)
    {
        Log("Filling entry : '%s'\n", mfile->path);
        mcachefs_metadata_fill_entry(mfile);
        return;
    }

    mdata = mcachefs_file_get_metadata(mfile);
    if (!mdata)
    {
        Err("Could not get stat for '%s'\n", mfile->path);
        return;
    }

    size = mdata->st.st_size;
    timbuf.actime = mdata->st.st_atime;
    timbuf.modtime = mdata->st.st_mtime;

    mcachefs_metadata_release(mdata);

    mcachefs_file_lock_file(mfile);
    tobacking = (mfile->cache_status == MCACHEFS_FILE_BACKING_IN_PROGRESS);

    mfile->transfer.tobacking = tobacking;
    mfile->transfer.total_size = size;
    mfile->transfer.transfered_size = 0;
    mfile->transfer.rate = 0;
    mfile->transfer.total_time = 0;
    mcachefs_file_unlock_file(mfile);

    if (tobacking)
    {
        mcachefs_transfer_do_backing(mfile);
    }
    else
    {
        mcachefs_transfer_do_writeback(mfile, &timbuf);
    }
}

void
mcachefs_transfer_do_backing(struct mcachefs_file_t *mfile)
{
    char *backingpath;
    if (mcachefs_createfile_cache(mfile->path, 0644))
    {
        Err("Could not create backing path for '%s' !\n", mfile->path);
        return;
    }

    Log("Create backing file OK, now transfering...\n");

    if (mcachefs_transfer_file(mfile, 1) == 0)
    {
        return;
    }

    mcachefs_file_check_unlocked_file(mfile);

    mcachefs_file_lock_file(mfile);
    mfile->cache_status = MCACHEFS_FILE_BACKING_ERROR;
    mfile->transfer.transfered_size = 0;
    mcachefs_file_unlock_file(mfile);

    Err("Could not backup that file !\n");
    backingpath = mcachefs_makepath_cache(mfile->path);
    if (!backingpath)
    {
        Err("OOM : Could not allocate backing path.\n");
    }
    else
    {
        if (unlink(backingpath))
        {
            Err("Could not unlink(%s) : err=%d:%s\n", backingpath, errno,
                strerror(errno));
        }
        free(backingpath);
    }
    return;
}

void
mcachefs_transfer_do_writeback(struct mcachefs_file_t *mfile,
                               struct utimbuf *timbuf)
{
    struct stat realstat;
    char *realpath;

    realpath = mcachefs_makepath_source(mfile->path);
    if (!realpath)
    {
        Bug("OOM.\n");
    }
    if (stat(realpath, &realstat) == 0)
    {
        if (realstat.st_mtime >= timbuf->modtime && realstat.st_size
            == mfile->transfer.total_size)
        {
            free(realpath);
#ifdef __MCACHEFS_TRANSFER_SKIP_FRESHER_SAME_SIZE
            Info("Will not write back file '%s' : real file seems fresher, and both have same size !\n", mfile->path);
            mcachefs_notify_sync_end(mfile->path, 1);
            return;
#else
            Info("File '%s' : real file seems fresher, and both have same size, but sync anyway !\n", mfile->path);
#endif
        }
        Log("Will write back : real mtime=%lu, real size=%lu, metatstat mtime=%lu, metastat size=%lu\n", realstat.st_mtime, (unsigned long) realstat.st_size, timbuf->modtime, (unsigned long) mfile->transfer.total_size);
    }
    if (mcachefs_transfer_file(mfile, 0) == 0)
    {
        if (utime(realpath, timbuf))
        {
            Err("Could not utime(%s) : err=%d:%s\n", realpath, errno,
                strerror(errno));
            mcachefs_notify_sync_end(mfile->path, 0);
        }
        else
        {
            Log("utime '%s' : ok.\n", realpath);
            mcachefs_notify_sync_end(mfile->path, 1);
        }
        free(realpath);
        return;
    }

    Err("Could not write back '%s'\n", realpath);
    mcachefs_notify_sync_end(mfile->path, 0);
    free(realpath);
}

int
mcachefs_transfer_file(struct mcachefs_file_t *mfile, int tobacking)
{
    int source_fd, target_fd;
    off_t size = mfile->transfer.total_size;
    off_t offset, remains, rate, global_rate, adjusted_rate, sendfile_offset;
    ssize_t tocopy, copied;
    off_t window_size = mcachefs_transfer_window_size_min;
    off_t window_size_alloced = window_size;
    off_t window_size_max = mcachefs_transfer_window_size_max;
    char *window = NULL;
    struct timeval now, last, begin_copy, before;
    time_t interval, copy_interval, global_interval;
    struct stat source_stat;
    struct timespec penalty;

    Log("Acquiring fd...\n");

    source_fd = mcachefs_file_getfd(mfile, tobacking ? 1 : 0, O_RDONLY);

    Log("Got source_fd=%d\n", source_fd);

    if (source_fd < 0)
    {
        Err("Could not get source_fd !\n");
        return -EIO;
    }

    target_fd = mcachefs_file_getfd(mfile, tobacking ? 0 : 1, O_RDWR);

    Log("Got target_fd=%d\n", target_fd);

    if (target_fd < 0)
    {
        Err("Could not get target_fd !\n");
        mcachefs_file_putfd(mfile, tobacking ? 1 : 0);
        return -EIO;
    }

    if (fstat(source_fd, &source_stat))
    {
        Bug("Could not get source stat !\n");
    }

    if (source_stat.st_size != size)
    {
      /**
       * This situation can be normal at backup, when we already performed a truncate() on that file :
       * this changed metadata, but not the real file yet (waiting for apply)
       */
        Err("Diverging sizes for %s : source size=%lu, asked size=%lu\n",
            mfile->path, (unsigned long) source_stat.st_size,
            (unsigned long) size);
        size = size < source_stat.st_size ? size : source_stat.st_size;
        Err("Corrected size to %lu\n", (unsigned long) size);
    }
#ifdef __MCACHEFS_TRANSFER_DO_FTRUNCATE_TARGET
    if (ftruncate(target_fd, size) < 0)
    {
        Err("Transfer : couldn't allocate space for transfer of data for '%s' (fd=%d) : err=%d:%s\n", mfile->path, target_fd, errno, strerror(errno));
        goto copyerr;
    }
#endif
    gettimeofday(&last, NULL);
    begin_copy = last;

    remains = size;
    offset = 0;

    window = (char *) malloc(window_size_alloced);

    while (1)
    {
        if (mcachefs_config_get_read_state() == MCACHEFS_STATE_QUITTING)
        {
            Err("Interrupting transfer !\n");
            goto copyerr;
        }

        gettimeofday(&before, NULL);

        tocopy = remains > window_size ? window_size : remains;

        if (0)
        {
            /*
             * Do not even try to use sendfile() for the moment
             */
            sendfile_offset = offset;
            copied = sendfile(target_fd, source_fd, &sendfile_offset, tocopy);
            if (copied == -1)
            {
                Err("Could not copy ! err=%d:%s\n", errno, strerror(errno));
                goto copyerr;
            }
            if (copied != tocopy)
            {
                Err("Could not write !! : copied=%ld tocopy=%ld, err=%d:%s\n",
                    (unsigned long) copied, (unsigned long) tocopy, errno,
                    strerror(errno));
                goto copyerr;
            }
        }
        else
        {
            Log("tocopy=%ld\n", (unsigned long) tocopy);
            copied = pread(source_fd, window, tocopy, offset);
            if (tocopy != copied)
            {
                Err("Could not read !! : copied=%ld tocopy=%ld, err=%d:%s\n",
                    (unsigned long) copied, (unsigned long) tocopy, errno,
                    strerror(errno));
                goto copyerr;
            }
            if (mcachefs_config_get_read_state() == MCACHEFS_STATE_QUITTING)
            {
                Err("Interrupting copy of '%s'\n", mfile->path);
                goto copyerr;
            }

            copied = pwrite(target_fd, window, tocopy, offset);
            if (tocopy != copied)
            {
                Err("Could not write !! : copied=%ld tocopy=%ld, err=%d:%s\n",
                    (unsigned long) copied, (unsigned long) tocopy, errno,
                    strerror(errno));
                goto copyerr;
            }

        }

        if (mcachefs_config_get_read_state() == MCACHEFS_STATE_QUITTING)
        {
            Err("Interrupting copy of '%s'\n", mfile->path);
            goto copyerr;
        }

        offset += copied;
        remains -= copied;

        if (!remains)
            break;

        gettimeofday(&now, NULL);
        interval = TIME_DIFF(now, last);
        global_interval = TIME_DIFF(now, begin_copy);

        last = now;
        rate = (copied * 1000) / interval;
        global_rate = (offset * 1000) / global_interval;

        adjusted_rate = (global_rate + rate) / 2;

        if (mcachefs_config_get_transfer_max_rate() && adjusted_rate
            >= mcachefs_config_get_transfer_max_rate())
        {
          /**
           * rate = max kb/s
           * window_size / rate = time that should have been spent copying
           * interval = time spent to copy window_size worth of data
           * ( window_size / max rate ) - interval
           */
            copy_interval = TIME_DIFF(now, before);

            penalty.tv_sec = 0;
            Log("window_size=%lu, max_rate=%lu\n",
                (unsigned long) window_size,
                (unsigned long) mcachefs_config_get_transfer_max_rate());
            penalty.tv_nsec =
                ((window_size << 20) /
                 mcachefs_config_get_transfer_max_rate());
            Log("penalty raw=%ldns, copy_interval=%ldns\n", penalty.tv_nsec,
                (long) copy_interval << 10);
            if (penalty.tv_nsec > (copy_interval << 10))
                penalty.tv_nsec -= (copy_interval << 10);
            if (penalty.tv_nsec > 900000000)
                penalty.tv_nsec = 900000000;
            Log("penalty : %lu\n", (unsigned long) penalty.tv_nsec);
            nanosleep(&penalty, NULL);
        }

        Log("Transfered %luk of %luk (%lu%%), rate current=%lukb/s, global=%lukb/s, adjusted=%lukb/s, window size=%lu\n", ((unsigned long) offset) >> 10, ((unsigned long) size) >> 10, (unsigned long) (offset * 100 / size), (unsigned long) rate, (unsigned long) global_rate, (unsigned long) adjusted_rate, (unsigned long) window_size);

        if (global_rate < 10 && window_size > 1 << 12)
        {
            window_size = window_size / 2;
            Log("Reducing window size to %lu, interval=%lu\n",
                (unsigned long) window_size, (unsigned long) interval);
        }
        else if (global_rate > 100 && window_size < window_size_max)
        {
            window_size = window_size * 2;
            Log("Augmenting window size to %lu, interval=%lu\n",
                (unsigned long) window_size, (unsigned long) interval);
            if (window_size_alloced < window_size)
            {
                window = (char *) realloc(window, window_size);
                if (window == NULL)
                {
                    Err("OOM : could not realloc window up to size=%lu\n",
                        (unsigned long) window_size);
                    goto copyerr;
                }
                window_size_alloced = window_size;
            }
        }

        Log("Locking file '%s' to update stats\n", mfile->path);
        mcachefs_file_lock_file(mfile);
        Log("Locked file.\n");
        mfile->transfer.transfered_size = offset;
        mfile->transfer.rate = adjusted_rate;
        mfile->transfer.total_time = global_interval;
        mcachefs_file_unlock_file(mfile);
        Log("Released file for stats update\n");
    }

    free(window);
    window = NULL;

    gettimeofday(&now, NULL);
    interval = TIME_DIFF(now, begin_copy);
    Log("End of transfer (to %s) for '%s' : %ld usec, copied '%lu', rate=%lu kb/sec\n", tobacking ? "cache" : "source", mfile->path, interval, (unsigned long) size, (unsigned long) ((size * 1000) / interval));

    mcachefs_file_putfd(mfile, MCACHEFS_FILE_SOURCE_REAL);
    mcachefs_file_putfd(mfile, MCACHEFS_FILE_SOURCE_BACKING);

    mcachefs_file_lock_file(mfile);
    if (tobacking)
        mfile->cache_status = MCACHEFS_FILE_BACKING_DONE;
    if (mfile->sources[MCACHEFS_FILE_SOURCE_REAL].use == 0)
    {
        close(mfile->sources[MCACHEFS_FILE_SOURCE_REAL].fd);
        mfile->sources[MCACHEFS_FILE_SOURCE_REAL].fd = -1;
    }
    mcachefs_file_unlock_file(mfile);

    return 0;
  copyerr:
    Err("Could not backup file '%s'\n", mfile->path);

    mcachefs_file_putfd(mfile, MCACHEFS_FILE_SOURCE_REAL);
    mcachefs_file_putfd(mfile, MCACHEFS_FILE_SOURCE_BACKING);
    return -EIO;
}

void
mcachefs_transfer_dump(struct mcachefs_file_t *mvops)
{
    int cur;
    int tobacking;
    int totaltotransfer = 0;
    struct mcachefs_file_t *mfile;

    struct mcachefs_transfer_queue_t *mqueue;
    off_t offset, size, rate;
    off_t total_transfered = 0, total_size = 0, total_rate = 0;
    mcachefs_transfer_lock();
    __VOPS_WRITE(mvops, "Current transfers :\n");

    for (cur = 0; cur < mcachefs_transfer_threads_nb; cur++)
    {
        __VOPS_WRITE(mvops, "[Thread %lx, type=%d]\n",
                     mcachefs_transfer_threads[cur].threadid,
                     mcachefs_transfer_threads[cur].type);
        mfile = mcachefs_transfer_threads[cur].currentfile;
        if (mfile == NULL)
            continue;

        if (mfile->type == mcachefs_file_type_dir)
        {
            __VOPS_WRITE(mvops, "\t%s %s\n", "Fill meta", mfile->path);
        }
        else
        {
            mcachefs_file_lock_file(mfile);
            offset = mfile->transfer.transfered_size;
            total_transfered += offset;
            size = mfile->transfer.total_size;
            total_size += size;
            rate = mfile->transfer.rate;
            total_rate += rate;
            tobacking =
                (mfile->cache_status == MCACHEFS_FILE_BACKING_IN_PROGRESS
                 || mfile->cache_status == MCACHEFS_FILE_BACKING_ASKED);
            mcachefs_file_unlock_file(mfile);

            __VOPS_WRITE(mvops,
                         "\t%s %s : %luk/%luk (%lu%%), rate=%lukb/s\n",
                         (tobacking ? "Backup   " : "Writeback"),
                         mfile->path, ((unsigned long) offset) >> 10,
                         ((unsigned long) size) >> 10,
                         size ? (unsigned long) (offset * 100 / size) : 0,
                         (unsigned long) rate);
        }
    }
    if (total_size)
    {
        __VOPS_WRITE(mvops,
                     "Total transfered : %luk/%luk (%lu%%), rate=%lukb/s\n",
                     ((unsigned long) total_transfered) >> 10,
                     ((unsigned long) total_size) >> 10,
                     (unsigned long) (total_transfered * 100 / total_size),
                     (unsigned long) total_rate);
    }
    for (mqueue = mcachefs_transfer_queue_head; mqueue; mqueue = mqueue->next)
    {
        totaltotransfer++;
    }
    if (mcachefs_transfer_queue_head)
    {
        __VOPS_WRITE(mvops, "\nFiles to be transfered (%d files) :\n",
                     totaltotransfer);
    }
    for (mqueue = mcachefs_transfer_queue_head; mqueue; mqueue = mqueue->next)
    {
        tobacking =
            (mqueue->mfile->cache_status != MCACHEFS_FILE_BACKING_DONE);
        __VOPS_WRITE(mvops, "\t%s %s\n",
                     (mqueue->mfile->type ==
                      mcachefs_file_type_dir) ? "Fill meta" : (tobacking ?
                                                               "Backup   " :
                                                               "Writeback"),
                     mqueue->mfile->path);
    }
    mcachefs_transfer_unlock();
}
