#include "mcachefs.h"

#include <libgen.h>

/**********************************************************************
 Utility functions
**********************************************************************/

char *
mcachefs_makepath(const char *path, const char *prefix)
{
    char *newpath;
    int len;

    len = strlen(path) + strlen(prefix) + 1;
    newpath = malloc(len + 1);
    if (newpath == NULL)
    {
        Err("  Failed to convert path\n");
        return NULL;
    }

    snprintf(newpath, len, "%s%s", prefix, path);
    return newpath;
}

char *
mcachefs_makepath_source(const char *path)
{
    return mcachefs_makepath(path, mcachefs_config_get_source());
}

char *
mcachefs_makepath_cache(const char *path)
{
    return mcachefs_makepath(path, mcachefs_config_get_cache());
}

const char *
__basename(const char *path)
{
    const char *bname = path;
    const char *cur;
    for (cur = path; *cur != '\0'; cur++)
    {
        if (*cur == '/')
        {
            bname = cur;
            bname++;
        }
    }
    return bname;
}

int
mcachefs_fileincache(const char *path)
{
    char *cachepath;
    struct stat st;
    int res;

    cachepath = mcachefs_makepath_cache(path);
    res = lstat(cachepath, &st);
    free(cachepath);
    return res == 0;
}

int
mcachefs_check_fileincache(struct mcachefs_file_t *mfile,
                           struct stat *metadata_st)
{
  char *cachepath;
  struct stat st;
  int res;

  // check if cache file exists
  cachepath = mcachefs_makepath_cache(mfile->path);
  res = lstat(cachepath, &st);
  free(cachepath);
  // file doesn't exist, so return false and download it!
  if ( ! res == 0 )
    return 0;

  // if the file size differs from cache file and metadata, return
  // false so we can re-download it
  if ( metadata_st->st_size != st.st_size )
    return 0;

  // if we got here, the file is correct, so return true
  return 1;
}


int
mcachefs_createpath(const char *prefix, const char *cpath, int lastIsDir)
{
    int prefixfd, tempfd, res;
    struct stat sb;

    Log("createpath(prefix=%s, cpath=%s, lastIsDir=%d)\n", prefix, cpath, lastIsDir);

    if (cpath[0] == '/')
    {
        cpath++;
    }

    if (cpath[0] == '\0')
    {
        Log("At root, ok\n");
        return 0;
    }

    char *path = strdup(cpath), *parentname;

    if (path == NULL)
    {
        return -ENOMEM;
    }

    if (!lastIsDir)
    {
        path = dirname(path);
        /*
         * It seems that dirname() returns a constant '.' when file is at root
         */
        if (strncmp(path, ".", 2) == 0)
        {
            path = strdup(path);
        }
        Log("not lastIsDir, considering path=%s\n", path);
    }

    if (strncmp(path, "/", 2) == 0)
    {
        Log("At root (for file %s), ok\n", cpath);
        free(path);
        return 0;
    }

    prefixfd = open(prefix, O_RDONLY);

    if (prefixfd == -1)
    {
        Err("Could not get prefix '%s' : error %d:%s\n", prefix, errno, strerror(errno));
        free(path);
        return -errno;
    }

    if (fstatat(prefixfd, path, &sb, 0) == 0)
    {
        free(path);
        close(prefixfd);
        return 0;
    }
    else
    {
        const char *cname;
        cname = __basename(path);
        parentname = dirname(path);
        Log("cname=%s, parentname=%s\n", cname, parentname);

        res = mcachefs_createpath(prefix, parentname, 1);
        if (res != 0)
        {
            Err("Could not create parent (prefix=%s, parentname=%s)\n", prefix, parentname);
            free(path);
            close(prefixfd);
            return res;
        }
        tempfd = openat(prefixfd, parentname, O_RDONLY);
        if (tempfd == -1)
        {
            free(path);
            close(prefixfd);
            return -1;
        }
        Log("Opened prefixfd=%d, parentname=%s, tempfd=%d\n", prefixfd, parentname, tempfd);
        if (mkdirat(tempfd, cname, S_IRWXU) != 0)
        {
            Err("Could not mkdirat(tempfd=%d, cname=%s) : err=%d:%s\n", tempfd, cname, errno, strerror(errno));
            free(path);
            close(prefixfd);
            close(tempfd);
            return -1;
        }
        Log("created new path for (prefix=%s, cpath=%s)\n", prefix, cpath);
    }
    free(path);
    close(prefixfd);
    close(tempfd);
    return 0;
}

int
mcachefs_createpath_cache(const char *path, int lastIsDir)
{
    /* Does the path in the backing store exist? */
    Log("Creating cache path for '%s'\n", path);
    return mcachefs_createpath(mcachefs_config_get_cache(), path, lastIsDir);
}

int
mcachefs_createfile_cache(const char *path, mode_t mode)
{
    int res;
    struct stat st;

    res = mcachefs_createpath_cache(path, 0);
    if (res)
        return res;

    char *backing_path = mcachefs_makepath_cache(path);

    if (lstat(backing_path, &st) == 0 && S_ISREG(st.st_mode))
    {
        Log("File already exist !\n");
        if (truncate(backing_path, 0))
        {
            Err("Could not truncate existing backing path '%s' err=%d:%s\n", backing_path, errno, strerror(errno));
            res = -errno;
        }
        free(backing_path);
        return res;
    }

    res = mknod(backing_path, mode, 0);
    Log("mknod(%s), res=%d\n", backing_path, res);

    free(backing_path);
    return res;
}

char *
mcachefs_split_path(const char *path, const char **lname)
{
    int i, lastSlash = 0;
    char *dpath;
    for (i = 0; path[i]; i++)
    {
        if (path[i] == '/')
        {
            lastSlash = i;
            *lname = &(path[i]);
        }
    }
    if (lastSlash == 0)
        lastSlash++;
    Log("lastSlash = %d\n", lastSlash);

    dpath = (char *) malloc(lastSlash + 1);
    memcpy(dpath, path, lastSlash);
    dpath[lastSlash] = 0;

    (*lname)++;
    Log("Path : '%s', dir : '%s', lname='%s'\n", path, dpath, *lname);
    return dpath;
}
