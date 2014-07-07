#include "mcachefs.h"
#include "mcachefs-hash.h"
#include "mcachefs-journal.h"

/**
 * mcachefs File handling
 */

static const char *mcachefs_file_path_deleted = "DELETED";

mcachefs_fh_t mcachefs_file_add ();
struct mcachefs_file_t *mcachefs_file_get (mcachefs_fh_t fdi);

static pthread_t mcachefs_file_threadid = 0;

time_t __mcachefs_jiffy_sec = 0;

void
mcachefs_file_source_init (struct mcachefs_file_source_t *source)
{
    source->use = 0;
    source->fd = -1;
    source->wr = 0;
    source->bytesrd = 0;
    source->nbrd = 0;
    source->byteswr = 0;
    source->nbwr = 0;
}

void
mcachefs_file_init (mcachefs_fh_t fh, char *path, hash_t hash,
                    mcachefs_file_type_t type)
{
    struct mcachefs_file_t *mfile = mcachefs_file_get (fh);

    memset (mfile, 0, sizeof (struct mcachefs_file_t));

    mfile->metadata_id = 0;
    mfile->hash = hash;
    mfile->path = path;

    Log ("USECNT %s init (1)\n", mfile->path);

    mfile->use = 1;

    mcachefs_file_source_init (&
                               (mfile->
                                sources[MCACHEFS_FILE_SOURCE_BACKING]));
    mcachefs_file_source_init (&(mfile->sources[MCACHEFS_FILE_SOURCE_REAL]));

    mfile->cache_status = MCACHEFS_FILE_BACKING_ASKED;

    mfile->timeslice = -1;
    mfile->timeslice_previous = NULL;
    mfile->timeslice_next = NULL;
    mfile->type = type;

    mcachefs_mutex_init (&(mfile->mutex));
}

struct mcachefs_file_t *
mcachefs_file_get (mcachefs_fh_t fdi)
{
    struct mcachefs_file_t *mfile =
        (struct mcachefs_file_t *) (unsigned long) fdi;
    return mfile;
}

mcachefs_fh_t
mcachefs_file_add ()
{
    mcachefs_fh_t fh;
    struct mcachefs_file_t *mfile;

    if ((mfile = mcachefs_file_timeslice_get_freed ()) == NULL)
    {
        mfile =
            (struct mcachefs_file_t *)
            malloc (sizeof (struct mcachefs_file_t));
    }
    if (mfile == NULL)
    {
        Bug ("Out of memory !\n");
    }
    memset (mfile, 0, sizeof (struct mcachefs_file_t));
    fh = (mcachefs_fh_t) (unsigned long) mfile;
    return fh;
}

mcachefs_fh_t
mcachefs_fileid_get (struct mcachefs_metadata_t * mdata, const char *path,
                     mcachefs_file_type_t type)
{
    struct mcachefs_file_t *mfile;
    char *mypath;

    mcachefs_metadata_check_locked ();

    mcachefs_file_lock ();

    if (mdata->fh)
    {
        mfile = mcachefs_file_get (mdata->fh);
        if (mfile->metadata_id != mdata->id)
        {
            Bug ("Incoherent mdata !\n");
        }
        if (mfile->type != type)
        {
            Bug ("Incoherent type !\n");
        }
        mcachefs_file_lock_file (mfile);
        mfile->use++;
        mcachefs_file_unlock_file (mfile);

        mcachefs_file_unlock ();
        return mdata->fh;
    }
    mdata->fh = mcachefs_file_add ();

    if (path == NULL)
    {
        mypath = mcachefs_metadata_get_path (mdata);
    }
    else
    {
        mypath = strdup (path);
    }
    mcachefs_file_init (mdata->fh, mypath, doHash (mypath), type);

    mfile = mcachefs_file_get (mdata->fh);
    mfile->metadata_id = mdata->id;
    mcachefs_file_timeslice_insert (mfile);
    mcachefs_file_unlock ();
    return mdata->fh;
}

static void
mcachefs_file_cleanup_file_source (struct mcachefs_file_source_t *source)
{
    if (source->use == 0 && source->fd != -1)
    {
        close (source->fd);
        source->fd = -1;
    }
}

void
mcachefs_file_cleanup_file (struct mcachefs_file_t *mfile)
{
    if (mfile->type != mcachefs_file_type_file)
        return;
    mcachefs_file_lock_file (mfile);

    mcachefs_file_cleanup_file_source (&
                                       (mfile->
                                        sources[MCACHEFS_FILE_SOURCE_REAL]));

    if (mfile->cache_status == MCACHEFS_FILE_BACKING_DONE)
        mcachefs_file_cleanup_file_source (&
                                           (mfile->
                                            sources
                                            [MCACHEFS_FILE_SOURCE_BACKING]));

    mcachefs_file_unlock_file (mfile);
}

void
mcachefs_file_remove (struct mcachefs_file_t *mfile)
{
    mcachefs_file_check_locked ();
    mcachefs_metadata_check_locked ();

    Log ("REMOVE : removing mfile=%p\n", mfile);

    mcachefs_file_timeslice_remove (mfile);

    if (mfile->metadata_id)
    {
        mcachefs_metadata_clean_fh_locked (mfile->metadata_id);
        if (mfile->use)
        {
            Bug ("We lost the race... what a pitty.\n");
        }
    }

    if (mfile->path == mcachefs_file_path_deleted)
    {
        Bug ("mfile already deleted !!!\n");
    }
    mcachefs_mutex_destroy (&(mfile->mutex), mfile->path);

    free (mfile->path);
    mfile->path = (char *) mcachefs_file_path_deleted;

    mcachefs_file_timeslice_insert_in_freed (mfile);
}

void
mcachefs_file_release (struct mcachefs_file_t *mfile)
{
    /*
     * We have to lock both metadata and file, in order to move mfile to the freed timeslice
     * and to cleanup the metadata fh link.
     */
    mcachefs_metadata_lock ();
    mcachefs_file_lock ();
    if (!mfile->use)
    {
        Bug ("Zero use for '%s'\n", mfile->path);
    }
    else
    {
        mfile->use--;
    }

    Log ("USECNT mcachefs_file_release %p '%s' : use=%d\n", mfile,
         mfile->path, mfile->use);

    if (mfile->use == 0)
    {
        Log ("releasing '%s' (type %d)\n", mfile->path, mfile->type);

        if (mfile->type == mcachefs_file_type_file)
        {
            mcachefs_file_cleanup_file (mfile);
            mcachefs_file_remove (mfile);
        }
        else if (mfile->type == mcachefs_file_type_vops)
        {
        }
        else if (mfile->type == mcachefs_file_type_dir)
        {
            mcachefs_file_remove (mfile);
        }
        else
        {
            Bug ("Not handled ! type=%d\n", mfile->type);
        }
    }
    mcachefs_file_unlock ();
    mcachefs_metadata_unlock ();
}

void
mcachefs_fileid_put (mcachefs_fh_t fdi)
{
    struct mcachefs_file_t *mfile;
    mfile = mcachefs_file_get (fdi);
    mcachefs_file_release (mfile);
}

/**
 * Check if we can reuse openned source file
 * returns 1 if we can reuse, 0 otherwise
 */
int
mcachefs_file_may_reuse_source (struct mcachefs_file_t *mfile,
                                struct mcachefs_file_source_t *source,
                                int asked_wr)
{
    if (source->fd != -1)
    {
        if (asked_wr <= source->wr)
        {
          /**
           * We may have lost the race, or just already openned it the right way
           */
            source->use++;
            // mcachefs_file_unlock_file ( mfile );
            Log ("File '%s' : already openned with wr=%d (asked_wr=%d)\n",
                 mfile->path, source->wr, asked_wr);
            return 1;           // source->fd;
        }
        /*
         * We now that asked_wr > *wr, that is asked_wr==1 and *wr==0
         */
        Log ("File '%s' : shall re-open it O_WRONLY, closing it first !\n",
             mfile->path);
        while (source->use)
        {
            mcachefs_file_unlock_file (mfile);
            sleep (1);
            Info ("File '%s' : waiting for release to re-open O_WRONLY... \n",
                  mfile->path);
            mcachefs_file_lock_file (mfile);
        }
        Log ("File '%s' : use is zero, closing %d\n", mfile->path,
             source->fd);
        close (source->fd);
        source->fd = -1;
    }
    return 0;                   // source->fd;
}

int
mcachefs_file_do_open (struct mcachefs_file_t *mfile, int flags, mode_t mode,
                       struct mcachefs_file_source_t *source,
                       char *(*path_translator) (const char *path))
{
    char *translated_path;
    int asked_wr = __IS_WRITE (flags) ? 1 : 0;

    Log ("do_open(%s) : locking\n", mfile->path);
    mcachefs_file_lock_file (mfile);
    Log ("do_open(%s) : locked.\n", mfile->path);

    if (mcachefs_file_may_reuse_source (mfile, source, asked_wr))
    {
        mcachefs_file_unlock_file (mfile);
        return source->fd;
    }

    translated_path = path_translator (mfile->path);

    if (translated_path == NULL)
    {
        Err ("No mem to create translated path !\n");
        mcachefs_file_unlock_file (mfile);
        return -ENOMEM;
    }

    if (flags & O_NONBLOCK)
        Info ("Openning '%s' with O_NONBLOCK flag !\n", translated_path);

    Log ("Preparing to open with translated_path='%s', flags=%x, mode=%x\n",
         translated_path, flags, mode);

    if (flags & O_CREAT)
        source->fd = open (translated_path, flags, mode);
    else
        source->fd = open (translated_path, flags);

    Log ("OPEN path='%s', translated_path='%s' => fd=%d, flags=%lo, mode=%lo, use=%d, wr=%d, asked=%d\n", mfile->path, translated_path, source->fd, (long) flags, (long) mode, source->use, source->wr, asked_wr);

    if (source->fd == -1)
    {
        Err ("mcachefs_file_do_open : openning from '%s' returned error %d:%s\n", translated_path, errno, strerror (errno));
        free (translated_path);
        mcachefs_file_unlock_file (mfile);
        return -errno;
    }
    free (translated_path);

    Log ("=> rfd = %d\n", source->fd);

    source->use++;
    source->wr = asked_wr;
    mcachefs_file_unlock_file (mfile);
    return source->fd;
}

int
mcachefs_file_getfd_mode (struct mcachefs_file_t *mfile, int real, int flags,
                          mode_t mode)
{
    int fd;

    Log ("Getting fd '%s', real=%d, flags=%x, mode=%x\n", mfile->path, real,
         flags, mode);
    if (real)
    {
        if (mcachefs_config_get_read_state () == MCACHEFS_STATE_HANDSUP)
        {
            Bug ("While openning real file for '%s' : mcachefs state set to HANDSUP.\n", mfile->path);
            return -EIO;
        }
        fd = mcachefs_file_do_open (mfile, flags, mode,
                                    &(mfile->
                                      sources[MCACHEFS_FILE_SOURCE_REAL]),
                                    &mcachefs_makepath_source);
        if (fd < 0)
        {
          /**
           * Here we may have a journal issue :
           * - The file we are trying to open is already renamed in the metafile,
           * - But the journal has not been applied, so the file in the target may not have been renamed
           */
            Err ("Could not open REAL file '%s', checking journal...\n",
                 mfile->path);
            if (mcachefs_journal_was_renamed (mfile->path))
            {
#if 1
                Err ("File %s was renamed, must apply journal first !\n",
                     mfile->path);
                return -EIO;
#endif
                Info ("File '%s' was renamed, applying journal to find the real file...\n", mfile->path);
                mcachefs_journal_apply ();
                fd = mcachefs_file_do_open (mfile, flags, mode,
                                            &(mfile->
                                              sources
                                              [MCACHEFS_FILE_SOURCE_REAL]),
                                            &mcachefs_makepath_source);
            }
            else
            {
                Err ("File really doesn't exist !\n");
            }
        }
    }
    else
    {
        if (mode == O_RDONLY
            && mfile->cache_status == MCACHEFS_FILE_BACKING_ASKED)
        {
            Err ("Asking for backing while backing not done !\n");
            return -EIO;
        }
        if ((mode & O_CREAT)
            && mfile->sources[MCACHEFS_FILE_SOURCE_BACKING].fd != -1)
        {
            Err ("Backing : asking non-rdonly, but mfile already exists !\n");
            return -EIO;
        }
        fd = mcachefs_file_do_open (mfile, flags, mode,
                                    &(mfile->
                                      sources[MCACHEFS_FILE_SOURCE_BACKING]),
                                    &mcachefs_makepath_cache);
    }
    return fd;
}

int
mcachefs_file_getfd (struct mcachefs_file_t *mfile, int real, int flags)
{
    if (flags & O_CREAT)
    {
        Err ("flags has O_CREAT set, but no mode is provided !\n");
        return -EPERM;
    }
    return mcachefs_file_getfd_mode (mfile, real, flags, 0);
}

void
mcachefs_file_source_putfd (struct mcachefs_file_t *mfile,
                            struct mcachefs_file_source_t *source)
{
    mcachefs_file_lock_file (mfile);

    if (source->use == 0)
    {
        Err ("use_fd==0 for '%s'\n", mfile->path);
    }
    else
    {
        source->use--;
    }
    mcachefs_file_unlock_file (mfile);
}

void
mcachefs_file_putfd (struct mcachefs_file_t *mfile, int real)
{
    if (real < 0 || real > 1)
    {
        Bug ("mfile=%s : Invalid real value : %d\n", mfile->path, real);
    }
    mcachefs_file_source_putfd (mfile, &(mfile->sources[real]));
}

struct mcachefs_metadata_t *
mcachefs_file_get_metadata (struct mcachefs_file_t *mfile)
{
    struct mcachefs_metadata_t *mdata;
    if (mfile->metadata_id == 0)
    {
        Err ("mfile '%s' : could not fetch metadata !\n", mfile->path);
        return NULL;
    }

    mcachefs_metadata_lock ();
    mdata = mcachefs_metadata_get (mfile->metadata_id);
    return mdata;
}

void
mcachefs_file_update_metadata (struct mcachefs_file_t *mfile, off_t size,
                               int modified)
{
    time_t now = time (NULL);
    struct mcachefs_metadata_t *mdata;
    mdata = mcachefs_file_get_metadata (mfile);

    if (!mdata)
    {
        Err ("Could not extend size of '%s', no metadat found !\n",
             mfile->path);
        return;
    }

    if (mdata->st.st_size < size)
        mdata->st.st_size = size;

    mdata->st.st_atime = now;
    if (modified)
        mdata->st.st_mtime = now;

    mcachefs_metadata_release (mdata);
}

void *
mcachefs_file_thread (void *arg)
{
    Info ("File thread %lx up and running.\n",
          (unsigned long) pthread_self ());
    (void) arg;
    while (1)
    {
        if (mcachefs_config_get_read_state () == MCACHEFS_STATE_QUITTING)
        {
            Log ("Interrupting file thread %lx\n",
                 (unsigned long) pthread_self ());
            return NULL;
        }
        time(&__mcachefs_jiffy_sec);
        mcachefs_file_lock ();

        // First, we purge the last timeslice in search for files to remove
        mcachefs_file_timeslice_cleanup ();

        // Then, we update the timeslices
        mcachefs_file_timeslice_update ();

        // Finally, unlock file lock
        mcachefs_file_unlock ();

        mcachefs_metadata_lock();
        mcachefs_metadata_unlock();

        sleep (mcachefs_config_get_file_thread_interval ());
    }
    return NULL;
}

void
mcachefs_file_start_thread ()
{
    pthread_attr_t attrs;
    pthread_attr_init (&attrs);
    pthread_attr_setdetachstate (&attrs, PTHREAD_CREATE_JOINABLE);
    pthread_create (&mcachefs_file_threadid, &attrs, mcachefs_file_thread,
                    NULL);
}

void
mcachefs_file_stop_thread ()
{
    int res;
    void *arg;
    if ((res = pthread_join (mcachefs_file_threadid, &arg)) != 0)
    {
        Err ("Could not join file thread %lx : err=%d:%s\n",
             mcachefs_file_threadid, res, strerror (res));
    }
    Info ("File thread interrupted.\n");
}
