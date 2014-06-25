#include "mcachefs.h"

#include <sys/types.h>

#include <dirent.h>

DIR *fdopendir (int __fd);

struct mcachefs_backing_file
{
    char *path;
    time_t age;
    off_t size;
    struct mcachefs_backing_file *previous;
    struct mcachefs_backing_file *next;
};

struct mcachefs_backing_files
{
    struct mcachefs_backing_file *head;
    struct mcachefs_backing_file *tail;
};

void
mcachefs_backing_append_file (struct mcachefs_backing_files *filelist,
                              char *path, struct stat *st)
{
    struct mcachefs_backing_file *file;
    file =
        (struct mcachefs_backing_file *)
        malloc (sizeof (struct mcachefs_backing_file));
    file->path = path;
    file->age = 0;
    file->size = st->st_size;
    file->previous = NULL;
    file->next = NULL;
    if (filelist->head)
    {
        file->next = filelist->head;
        filelist->head->previous = file;
        filelist->head = file;
    }
    else
    {
        filelist->head = filelist->tail = file;
    }
}

static void
mcachefs_build_backing_files (struct mcachefs_backing_files *filelist,
                              const char *prefix, int dirfd)
{
    int fd;
    DIR *dp;
    struct dirent *de;
    struct stat st;
    char *path;

    Log ("filelist %p, prefix=%s, dirfd=%d\n", filelist, prefix, dirfd);

    if (dirfd == -1)
    {
        Err ("Invalid dirfd for prefix='%s'\n", prefix);
        return;
    }

    dp = fdopendir (dirfd);

    while ((de = readdir (dp)) != NULL)
    {
        if (strcmp (de->d_name, ".") == 0 || strcmp (de->d_name, "..") == 0)
            continue;
        path = (char *) malloc (strlen (prefix) + strlen (de->d_name) + 2);
        strcpy (path, prefix);
        strcat (path, de->d_name);

        if (fstatat (dirfd, de->d_name, &st, AT_SYMLINK_NOFOLLOW))
        {
            Err ("Could not stat : '%s'\n", path);
            free (path);
            continue;
        }

        Log ("Prefix=%s, file=%s => %s, type=%u\n", prefix, de->d_name, path,
             st.st_mode);
        if (S_ISDIR (st.st_mode))
        {
            strcat (path, "/");
            fd = openat (dirfd, de->d_name, O_RDONLY);
            mcachefs_build_backing_files (filelist, path, fd);
            close (fd);
            free (path);
        }
        else if (S_ISREG (st.st_mode))
        {
            mcachefs_backing_append_file (filelist, path, &st);
        }
        else
        {

        }
    }
    closedir (dp);
}

void
mcachefs_backing_update_metadata (struct mcachefs_backing_files *filelist)
{
    struct mcachefs_backing_file *file;
    struct mcachefs_metadata_t *mdata;

    time_t now = time (NULL);
    time_t last;

    mcachefs_metadata_lock ();

    for (file = filelist->head; file; file = file->next)
    {
        // __VOPS_WRITE ( mvops, "%s\n", file->path );
        mdata = mcachefs_metadata_find_locked (file->path);
        if (!mdata)
        {
            Err ("Does not exist : '%s'\n", file->path);
            file->age = ((time_t) 1) << 30;
            continue;
        }
        last = mdata->st.st_atime;
        if (last < mdata->st.st_mtime)
            last = mdata->st.st_mtime;
        else if (last < mdata->st.st_ctime)
            last = mdata->st.st_ctime;

        if (last > now)
            file->age = 0;
        else
            file->age = now - last;

        if (file->size != mdata->st.st_size)
        {
            Err ("Diverging sizes for '%s' : file->size=%luk, mdata->st.st_size=%luk\n", file->path, (unsigned long) file->size >> 10, (unsigned long) mdata->st.st_size >> 10);
        }
    }
    mcachefs_metadata_unlock ();
}

void
mcachefs_backing_sort (struct mcachefs_backing_files *filelist)
{
    struct mcachefs_backing_file *file =
        filelist->head, *file1, *previous, *next;
    if (!file)
        return;

    while (file->next)
    {
        if (file->next->age >= file->age)
        {
            file = file->next;
            continue;
        }
        // We have to flip file and file->next
        file1 = file->next;
        previous = file->previous;
        next = file->next->next;

        if (previous)
            previous->next = file1;
        else
            filelist->head = file1;
        file1->previous = previous;

        file1->next = file;
        file->previous = file1;

        file->next = next;
        if (next)
            next->previous = file;
        else
            filelist->tail = file;

        if (previous)
            file = previous;
        else
            file = filelist->head;
    }
}



void
mcachefs_cleanup_backing (struct mcachefs_file_t *mvops, int simulate)
{
    int rootfd = -1, backingfd;
    int max_age;
    char *prefix, *relative_path;
    off_t total = 0;
    struct mcachefs_backing_files filelist = { NULL, NULL };
    struct mcachefs_backing_file *file;

    max_age = mcachefs_get_cleanup_backing_age ();
    prefix = strdup (mcachefs_get_cleanup_backing_prefix ());

    Info ("Building cache list : prefix='%s', age=%d\n", prefix, max_age);

    backingfd = open (mcachefs_config_cache(), O_RDONLY);

    if (backingfd < 0)
    {
        Err ("Could not open '%s'\n", mcachefs_config_cache());
        return;
    }

    relative_path = prefix;
    if (*relative_path == '/')
        relative_path++;

    if (*relative_path)
    {
        rootfd = openat (backingfd, relative_path, O_RDONLY, 0);
        if (rootfd < 0)
        {
            Err ("Could not open '%s'\n", relative_path);
        }
    }
    else
    {
        rootfd = backingfd;
    }
    mcachefs_build_backing_files (&filelist, prefix, rootfd);

    mcachefs_backing_update_metadata (&filelist);

    mcachefs_backing_sort (&filelist);

    for (file = filelist.head; file; file = file->next)
    {
        if (file->age < max_age)
        {
            continue;
        }
        total += file->size;
        if (simulate)
        {
            __VOPS_WRITE (mvops, "%lu\t%lu\t%s\n", (unsigned long) file->age,
                          (unsigned long) file->size, file->path);
        }
        else
        {
            relative_path = file->path;
            if (*relative_path == '/')
                relative_path++;
            if (unlinkat (backingfd, relative_path, 0))
            {
                Err ("Could not unlink '%s' : err=%d:%s\n", file->path, errno,
                     strerror (errno));
            }
        }
    }
    if (simulate)
    {
        __VOPS_WRITE (mvops, "Total : %lu Mb\n",
                      ((unsigned long) total) >> 20);
    }
    free (prefix);

    for (file = filelist.head; file; file = file->next)
    {
        free (file->path);
        filelist.head = file->next;
        if (filelist.head)
            filelist.head->previous = NULL;
        free (file);
    }

    close (rootfd);
    if (backingfd != rootfd)
        close (backingfd);
}
