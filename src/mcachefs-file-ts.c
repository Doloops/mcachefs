#include "mcachefs.h"

#if 0
#define Log_TS Log
#else
#define Log_TS(...)
#endif

#define MCACHEFS_FILE_TIMESLICE_NB 1024
const int mcachefs_file_timeslice_nb = MCACHEFS_FILE_TIMESLICE_NB;

static const int mcachefs_file_timeslice_garbage = MCACHEFS_FILE_TIMESLICE_NB;
static const int mcachefs_file_timeslice_freed =
    MCACHEFS_FILE_TIMESLICE_NB + 1;

struct mcachefs_file_t *mcachefs_file_timeslices[MCACHEFS_FILE_TIMESLICE_NB +
                                                 2];

int mcachefs_file_timeslice_current = 0;

/**
 * Timeslicing is a circular head-buffer double-linked-list with periodical push
 * with n = now = mcachefs_file_timeslice_current, we have
 * [ n - m, n - (m-1), ..., n - 1, n, n + 1, ..., n + p ]
 * with m + p + 1 = mcachefs_file_timeslice_nb
 * 
 * So last is n+1, and antelast = n+2
 */

void
mcachefs_file_timeslice_init_variables ()
{
    /*
     * mcachefs_file_timeslices init
     */
    memset (mcachefs_file_timeslices, 0, sizeof (mcachefs_file_timeslices));
}

void
mcachefs_file_timeslice_insert_in_ts (struct mcachefs_file_t *mfile,
                                      int timeslice)
{
    /**
     * We are supposed to have the file lock held here
     */
    mcachefs_file_check_locked ();
    /**
     * Always insert at current mcachefs_file_timeslice_current
     */
    if (mfile->timeslice != -1 || mfile->timeslice_previous
        || mfile->timeslice_next)
    {
        Bug ("Invalid timeslice while inserting : mfile=%p, ts=%d, previous=%p, next=%p\n", mfile, mfile->timeslice, mfile->timeslice_previous, mfile->timeslice_next);
    }

    mfile->timeslice = timeslice;

    if (mcachefs_file_timeslices[mfile->timeslice])
    {
        mcachefs_file_timeslices[mfile->timeslice]->timeslice_previous =
            mfile;
    }
    mfile->timeslice_next = mcachefs_file_timeslices[mfile->timeslice];
    mcachefs_file_timeslices[mfile->timeslice] = mfile;

    Log ("post-insert : mfile=%p, timeslice=%d, head=%p, next=%p, next->previous=%p (next->next=%p)\n", mfile, mfile->timeslice, mcachefs_file_timeslices[mfile->timeslice], mfile->timeslice_next, mfile->timeslice_next ? mfile->timeslice_next->timeslice_previous : NULL, mfile->timeslice_next ? mfile->timeslice_next->timeslice_next : NULL);

    if (mfile->timeslice_next && mfile->timeslice_next->timeslice_next
        && mfile->timeslice_next->timeslice_next->timeslice_previous !=
        mfile->timeslice_next)
    {
        Bug (".");
    }
}

void
mcachefs_file_timeslice_insert (struct mcachefs_file_t *mfile)
{
    mcachefs_file_timeslice_insert_in_ts (mfile,
                                          mcachefs_file_timeslice_current);
}

void
mcachefs_file_timeslice_insert_in_freed (struct mcachefs_file_t *mfile)
{
    mcachefs_file_timeslice_insert_in_ts (mfile,
                                          mcachefs_file_timeslice_freed);
}

struct mcachefs_file_t *
mcachefs_file_timeslice_get_freed ()
{
    mcachefs_file_check_locked ();
    struct mcachefs_file_t *mfile =
        mcachefs_file_timeslices[mcachefs_file_timeslice_freed];
    if (mfile)
    {
        mcachefs_file_timeslice_remove (mfile);
    }
    return mfile;
}

void
mcachefs_file_timeslice_remove (struct mcachefs_file_t *mfile)
{
    mcachefs_file_check_locked ();

    if (mfile->timeslice_previous == NULL)
    {
        // We are supposed to be the head of our list
        if (mcachefs_file_timeslices[mfile->timeslice] != mfile)
        {
            Bug ("Not the head of list !\n");
        }
        mcachefs_file_timeslices[mfile->timeslice] = mfile->timeslice_next;
    }
    else
    {
        if (mfile->timeslice_previous->timeslice_next != mfile)
        {
            Bug ("Corrupted list : mfile=%p, previous=%p, previous->next=%p\n", mfile, mfile->timeslice_previous, mfile->timeslice_previous->timeslice_next);
        }
        mfile->timeslice_previous->timeslice_next = mfile->timeslice_next;
    }
    if (mfile->timeslice_next)
    {
        if (mfile->timeslice_next->timeslice_previous != mfile)
        {
            Bug ("Corrupted list : mfile=%p, next=%p, next->previous=%p\n",
                 mfile, mfile->timeslice_next,
                 mfile->timeslice_next->timeslice_previous);
        }
        mfile->timeslice_next->timeslice = mfile->timeslice;
        mfile->timeslice_next->timeslice_previous = mfile->timeslice_previous;
    }

    Log ("post-remove : mfile=%p, prev=%p, prev->next=%p, next=%p, next->prev=%p\n", mfile, mfile->timeslice_previous, mfile->timeslice_previous ? mfile->timeslice_previous->timeslice_next : NULL, mfile->timeslice_next, mfile->timeslice_next ? mfile->timeslice_next->timeslice_previous : NULL);

    if (mfile->timeslice_previous && mfile->timeslice_next)
    {
        if (mfile->timeslice_previous->timeslice_next->timeslice_previous !=
            mfile->timeslice_previous)
        {
            Bug (".");
        }
        if (mfile->timeslice_next->timeslice_previous->timeslice_next !=
            mfile->timeslice_next)
        {
            Bug (".");
        }
    }

    mfile->timeslice_previous = NULL;
    mfile->timeslice_next = NULL;
    mfile->timeslice = -1;
}

void
mcachefs_file_timeslice_do_freshen (struct mcachefs_file_t *mfile)
{
    mcachefs_file_check_locked ();
    Log_TS ("freshening %p '%s'\n", mfile, mfile->path);
    /**
     * put the given mfile in the freshest use timeslice
     */
    /**
     * First, unlink the given mfile to its present timeslice
     */
    mcachefs_file_timeslice_remove (mfile);

    mfile->timeslice = -1;
    mfile->timeslice_previous = NULL;
    mfile->timeslice_next = NULL;

    /**
     * Now, mfile shall not be linked to any file
     * put it in the first timeslice
     */
    mcachefs_file_timeslice_insert (mfile);
}

void
mcachefs_file_timeslice_freshen (struct mcachefs_file_t *mfile)
{
    mcachefs_file_lock ();
    mcachefs_file_timeslice_do_freshen (mfile);
    mcachefs_file_unlock ();
}

void
mcachefs_file_timeslice_cleanup_list (int age,
                                      void (*action) (struct mcachefs_file_t *
                                                      mfile))
{
    int ts_to_cleanup =
        (mcachefs_file_timeslice_current + mcachefs_file_timeslice_nb -
         age) % mcachefs_file_timeslice_nb;
    struct mcachefs_file_t *head =
        mcachefs_file_timeslices[ts_to_cleanup], *mfile, *mnext;

    for (mfile = head; mfile;)
    {
        mnext = mfile->timeslice_next;
        action (mfile);
        mfile = mnext;
    }

}

void
mcachefs_file_timeslice_cleanup_freed ()
{
    struct mcachefs_file_t *mfile, *mnext;
    Log ("TS : Cleanup freed !\n");
    for (mfile = mcachefs_file_timeslices[mcachefs_file_timeslice_freed];
         mfile;)
    {
        mnext = mfile->timeslice_next;
        Log ("TS : Cleanup freed %p\n", mfile);
        free (mfile);
        mfile = mnext;
    }
    mcachefs_file_timeslices[mcachefs_file_timeslice_freed] = NULL;

}

void
mcachefs_file_timeslice_cleanup ()
{
    /**
     * Shall be called with mcachefs_file_lock held
     */

    /**
     * Cleanup files
     */
    mcachefs_file_timeslice_cleanup_list (mcachefs_get_file_ttl (),
                                          &mcachefs_file_cleanup_file);


#if 0
    /**
     * Cleanup dirs
     */
    mcachefs_file_timeslice_cleanup_list (mcachefs_get_metadata_ttl (),
                                          &mcachefs_metadata_cleanup_dir);
#endif

    /**
     * Cleanup vops
     */
    mcachefs_file_timeslice_cleanup_list (1, &mcachefs_vops_cleanup_vops);

    /**
     * Cleanup free list
     */
    if (mcachefs_file_timeslice_current == 0)
    {
        mcachefs_file_timeslice_cleanup_freed ();
    }
}

void
mcachefs_file_timeslice_update ()
{
    /**
     * Shall be called with mcachefs_file_lock held
     */
    struct mcachefs_file_t *mfile;

    int last_timeslice =
        (mcachefs_file_timeslice_current + mcachefs_file_timeslice_nb +
         1) % mcachefs_file_timeslice_nb;

    Log_TS ("last=%d => %p\n", last_timeslice,
            mcachefs_file_timeslices[last_timeslice]);
    Log_TS ("garbage=%d => %p\n", mcachefs_file_timeslice_garbage,
            mcachefs_file_timeslices[mcachefs_file_timeslice_garbage]);


    // If the next slice is non-empty, we must put it in the garbage slice
    if (mcachefs_file_timeslices[last_timeslice])
    {
        /*
         * We shall put all files in garbage
         */
        if (mcachefs_file_timeslices[mcachefs_file_timeslice_garbage])
        {
            for (mfile = mcachefs_file_timeslices[last_timeslice];
                 mfile->timeslice_next; mfile = mfile->timeslice_next);

            if (mfile->timeslice_next)
            {
                Bug ("had a next ?\n");
            }

            // mfile is now the tail in n+1 list, we must append it to garbage
            mfile->timeslice_next =
                mcachefs_file_timeslices[mcachefs_file_timeslice_garbage];

            if (mcachefs_file_timeslices[mcachefs_file_timeslice_garbage]->
                timeslice_previous)
            {
                Bug ("had a previous ?\n");
            }

            mcachefs_file_timeslices[mcachefs_file_timeslice_garbage]->
                timeslice_previous = mfile;

        }
        mcachefs_file_timeslices[mcachefs_file_timeslice_garbage] =
            mcachefs_file_timeslices[last_timeslice];
        mcachefs_file_timeslices[last_timeslice] = NULL;

        if (!mcachefs_file_timeslices[mcachefs_file_timeslice_garbage])
        {
            Bug ("Shall not be here\n");
        }

        mcachefs_file_timeslices[mcachefs_file_timeslice_garbage]->timeslice =
            mcachefs_file_timeslice_garbage;
    }

    mcachefs_file_timeslice_current =
        (mcachefs_file_timeslice_current + 1) % mcachefs_file_timeslice_nb;
}

void
mcachefs_file_timeslices_dump_source (struct mcachefs_file_t *mvops,
                                      struct mcachefs_file_source_t *source,
                                      const char *label)
{
    if (source->fd != -1 || source->nbrd || source->nbwr)
    {
        __VOPS_WRITE (mvops, ",%s=%d/%d/%d", label,
                      source->fd, source->use, source->wr);
        if (source->nbrd)
        {
            __VOPS_WRITE (mvops, " (read #%lu : %luk)",
                          (unsigned long) source->nbrd,
                          (unsigned long) source->bytesrd >> 10);
        }
        if (source->nbwr)
        {
            __VOPS_WRITE (mvops, " (write #%lu : %luk)",
                          (unsigned long) source->nbwr,
                          (unsigned long) source->byteswr >> 10);
        }
    }
}

void
mcachefs_file_timeslices_dump_ts (struct mcachefs_file_t *mvops,
                                  struct mcachefs_file_t *mhead)
{
    char ctype;
    static const char *ctypes = "?FDV????";
    struct mcachefs_file_t *mfile;
    for (mfile = mhead; mfile; mfile = mfile->timeslice_next)
    {
#if 0
        __VOPS_WRITE (mvops,
                      "\t%p:%s, type=%d, ts=%d, hash=%llx, prev=%p, next=%p, metaid=%lu",
                      mfile, mfile->path, mfile->type, mfile->timeslice,
                      mfile->hash, mfile->timeslice_previous,
                      mfile->timeslice_next,
                      (unsigned long) mfile->metadata_id);
#endif
        if (mfile->type > 3)
            ctype = '?';
        else
            ctype = ctypes[mfile->type];
        __VOPS_WRITE (mvops, "\t%s %c,use=%d", mfile->path, ctype,
                      mfile->use);
        if (mfile->type == mcachefs_file_type_file)
        {
            mcachefs_file_timeslices_dump_source (mvops,
                                                  &(mfile->
                                                    sources
                                                    [MCACHEFS_FILE_SOURCE_BACKING]),
                                                  "back");
            mcachefs_file_timeslices_dump_source (mvops,
                                                  &(mfile->
                                                    sources
                                                    [MCACHEFS_FILE_SOURCE_REAL]),
                                                  "real");

            switch (mfile->backing_status)
            {
            case MCACHEFS_FILE_BACKING_ASKED:
                __VOPS_WRITE (mvops, ",backing=asked");
                break;
            case MCACHEFS_FILE_BACKING_IN_PROGRESS:
                __VOPS_WRITE (mvops, ",backing=inprogress");
                break;
            case MCACHEFS_FILE_BACKING_DONE:
                break;
            case MCACHEFS_FILE_BACKING_ERROR:
            default:
                __VOPS_WRITE (mvops, ",backing=error");
                break;

            }
        }
        else if (mfile->type == mcachefs_file_type_vops)
        {
            __VOPS_WRITE (mvops, ", contents=%p, size=%lu, alloced=%lu",
                          mfile->contents,
                          (unsigned long) mfile->contents_size,
                          (unsigned long) mfile->contents_alloced);
        }
        else if (mfile->type == mcachefs_file_type_dir)
        {
            __VOPS_WRITE (mvops, ", directory");
        }
        else
        {
            __VOPS_WRITE (mvops, " (unknown type)");
        }
        __VOPS_WRITE (mvops, "\n");
    }
}

void
mcachefs_file_timeslices_dump (struct mcachefs_file_t *mvops)
{
    int j = mcachefs_file_timeslice_current;

    mcachefs_file_lock ();

    while (1)
    {
        if (mcachefs_file_timeslices[j])
        {
            __VOPS_WRITE (mvops, "Timeslice : %ds ago (ts=%d)\n",
                          (mcachefs_file_timeslice_current +
                           MCACHEFS_FILE_TIMESLICE_NB -
                           j) % MCACHEFS_FILE_TIMESLICE_NB, j);
            mcachefs_file_timeslices_dump_ts (mvops,
                                              mcachefs_file_timeslices[j]);
        }
        if (j == 0)
            j = MCACHEFS_FILE_TIMESLICE_NB - 1;
        else
            j--;
        if (j == mcachefs_file_timeslice_current)
            break;
    }
    if (mcachefs_file_timeslices[mcachefs_file_timeslice_garbage])
    {
        __VOPS_WRITE (mvops, "Garbage :\n");
        mcachefs_file_timeslices_dump_ts (mvops,
                                          mcachefs_file_timeslices
                                          [mcachefs_file_timeslice_garbage]);
    }
#if 0
    for (j = 0; j < mcachefs_file_timeslice_nb + 2; j++)
    {
        if (!mcachefs_file_timeslices[j])
            continue;
        __VOPS_WRITE (mvops, "At timeslice %d : head=%p%s%s%s\n", j,
                      mcachefs_file_timeslices[j],
                      j ==
                      mcachefs_file_timeslice_current ? " => current" : "",
                      j ==
                      mcachefs_file_timeslice_garbage ? " => garbage" : "",
                      j == mcachefs_file_timeslice_freed ? " => freed" : "");
        mcachefs_file_timeslices_dump_ts (mvops, mcachefs_file_timeslices[j]);
    }
#endif

    mcachefs_file_unlock ();
}
