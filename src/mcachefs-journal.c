#include "mcachefs.h"
#include "mcachefs-journal.h"
#include "mcachefs-transfer.h"
#include "mcachefs-vops.h"

#if 0
#undef Log
#define Log Info
#endif

static const char *mcachefs_journal_op_label[] =
    { "none", "mknod", "mkdir", "unlink", "rmdir", "symlink", "rename",
    "link",
    "chmod", "chown", "truncate", "utime", "fsync", "INVALID"
};

enum mcachefs_journal_action_t
{
    mcachefs_journal_action_none,
    mcachefs_journal_action_update,
    mcachefs_journal_action_sync
};

struct mcachefs_journal_fsync_t
{
    int total_files;
    int files_ok;
    int files_error;
};

struct mcachefs_journal_fsync_t mcachefs_journal_fsync;

char *mcachefs_journal_fsync_path = NULL;

void
mcachefs_journal_init()
{
    struct stat st;
    mcachefs_journal_fsync_path = mcachefs_makepath(".fsync",
                                                    mcachefs_config_get_journal
                                                    ());

    if (mcachefs_journal_fsync_path == NULL)
        Bug("OOM.\n");

    if (stat(mcachefs_journal_fsync_path, &st) == 0)
    {
        Err("Journal fsync already exist !!!\n");
        Bug("Not implemented yet.\n");
    }
}

enum mcachefs_journal_action_t mcachefs_journal_action =
    mcachefs_journal_action_none;

/**
 * MCachefs Journal operations
 */

int
mcachefs_journal_append_entry(int fd, struct mcachefs_journal_entry_t *entry,
                              const char *path, const char *to)
{
    int res;

    entry->path_sz = strlen(path);
    if (to && *to)
        entry->to_sz = strlen(to);
    else
        entry->to_sz = 0;

    res = write(fd, entry, sizeof(struct mcachefs_journal_entry_t));
    if (res != (int) sizeof(struct mcachefs_journal_entry_t))
    {
        Err("Could not write to journal : err=%d:%s\n", errno,
            strerror(errno));
        return -EIO;
    }
    res = write(fd, path, entry->path_sz);
    if (res != (int) entry->path_sz)
    {
        Err("Could not write to journal : err=%d:%s\n", errno,
            strerror(errno));
        return -EIO;
    }
    if (entry->to_sz)
    {
        res = write(fd, to, entry->to_sz);
        if (res != (int) entry->to_sz)
        {
            Err("Could not write to journal : err=%d:%s\n", errno,
                strerror(errno));
            return -EIO;
        }
    }
    return 0;
}

int
mcachefs_journal_read_entry(int fd, struct mcachefs_journal_entry_t *entry,
                            char *path, char *to)
{
    int bytes = 0, res;
    Log("\tReading...\n");

    res = read(fd, entry, sizeof(struct mcachefs_journal_entry_t));
    if (res == 0)
        return 0;

    Log("\tRead : res=%d\n", res);

    if (res != (int) sizeof(struct mcachefs_journal_entry_t))
    {
        Err("Could not read journal entry : err=%d:%s\n", errno,
            strerror(errno));
        return -errno;
    }
    bytes += res;
    res = read(fd, path, entry->path_sz);
    if (res != entry->path_sz)
    {
        Err("Could not read journal entry : err=%d:%s\n", errno,
            strerror(errno));
        return -errno;
    }
    bytes += res;
    path[entry->path_sz] = '\0';
    if (entry->to_sz)
    {
        res = read(fd, to, entry->to_sz);
        if (res != entry->to_sz)
        {
            Err("Could not read journal entry : err=%d:%s\n", errno,
                strerror(errno));
            return -errno;
        }
        bytes += res;
    }
    to[entry->to_sz] = '\0';
    return bytes;
}

int mcachefs_journal_rebuild(const char *path, const char *to);

void
mcachefs_journal_append(mcachefs_journal_op op, const char *path,
                        const char *to, mode_t mode, dev_t rdev, uid_t uid,
                        gid_t gid, off_t size, struct utimbuf *utimbuf)
{
    int fd = 0, res;
    struct stat journal_stat;
    struct mcachefs_journal_entry_t entry;
    entry.op = op;
    entry.mode = mode;
    entry.rdev = rdev;
    entry.uid = uid;
    entry.gid = gid;
    entry.size = size;

    if (utimbuf)
        memcpy(&(entry.utimbuf), utimbuf, sizeof(struct utimbuf));
    else
        memset(&(entry.utimbuf), 0, sizeof(struct utimbuf));

    Log("New journal entry : op=%x : path='%s', to='%s' : "
        "mode=%lo, dev=%ld, uid=%ld, gid=%ld, size=%lu, utimebuf=%lu,%lu\n",
        entry.op, path, to, (long) entry.mode, (long) entry.rdev,
        (long) entry.uid, (long) entry.gid, (unsigned long) entry.size,
        entry.utimbuf.actime, entry.utimbuf.modtime);

    mcachefs_journal_lock();

    if (op == mcachefs_journal_op_rename)
    {
        res = stat(mcachefs_config_get_journal(), &journal_stat);
        if (res == 0 && journal_stat.st_size)
        {
            Log("Journal : rename() : rebuild all journal !\n");
            if ((res = mcachefs_journal_rebuild(path, to)) == 0)
            {
                Log("Rebuilt journal OK !\n");
                mcachefs_journal_unlock();
                return;
            }
            Err("Invalid rebuild !\n");
            exit(-1);
        }
    }

    fd = open(mcachefs_config_get_journal(), O_RDWR | O_CREAT | O_APPEND,
              0644);

    if (fd == -1)
    {
        Err("Could not open journal '%s' : err=%d:%s\n",
            mcachefs_config_get_journal(), errno, strerror(errno));
        exit(-1);
    }

    if ((res = mcachefs_journal_append_entry(fd, &entry, path, to)) != 0)
    {
        Err("Could not write journal : err=%d:%s\n", -res, strerror(-res));
    }

    close(fd);

    mcachefs_journal_unlock();
}

int
mcachefs_journal_rebuild(const char *rename_path, const char *rename_to)
{
    static const char *mcachefs_journal_suffix = ".tmp";
    struct mcachefs_journal_entry_t entry, rename_entry;

    int fd_src, fd_tgt, res;
    int rename_path_sz, rename_to_sz;

    char path[PATH_MAX], to[PATH_MAX];
    char altered_path[PATH_MAX];
    char *altered_path_suffix;
    // int exact_match, ;
    int already_renamed = 0;
    char *journal_tgt = NULL;

    rename_path_sz = strlen(rename_path);
    rename_to_sz = strlen(rename_to);

    memcpy(altered_path, rename_to, rename_to_sz);
    altered_path_suffix = &(altered_path[rename_to_sz]);

    fd_src = open(mcachefs_config_get_journal(), O_RDONLY);
    if (fd_src == -1)
    {
        Err("REBUILD : Could not open source journal '%s'\n",
            mcachefs_config_get_journal());
        return -errno;
    }

    journal_tgt =
        (char *) malloc(strlen(mcachefs_config_get_journal()) +
                        strlen(mcachefs_journal_suffix) + 1);
    strcpy(journal_tgt, mcachefs_config_get_journal());
    strcat(journal_tgt, mcachefs_journal_suffix);

    fd_tgt = open(journal_tgt, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd_tgt == -1)
    {
        Err("REBUILD : Could not open target journal '%s'\n", journal_tgt);
        free(journal_tgt);
        return -errno;
    }

    while (1)
    {
        res = mcachefs_journal_read_entry(fd_src, &entry, path, to);
        if (res <= 0)
            break;

        if (entry.op == mcachefs_journal_op_fsync
            && strncmp(path, rename_path, rename_path_sz) == 0)
        {
            strncpy(altered_path_suffix, &(path[rename_path_sz]),
                    PATH_MAX - rename_to_sz);
            res = mcachefs_journal_append_entry(fd_tgt, &entry, altered_path,
                                                to);
            if (res < 0)
                break;
            continue;
        }

#if 0
        if (*to && entry.op == mcachefs_journal_op_rename
            && strncmp(to, rename_path, rename_path_sz) == 0)
        {
            if (!(entry.op == mcachefs_journal_op_symlink
                  || entry.op == mcachefs_journal_op_link
                  || entry.op == mcachefs_journal_op_rename))
            {
                Bug("Invalid op for 'to' : %x\n", entry.op);
            }
            exact_match = (to[rename_path_sz] == '\0');
            if (exact_match)
            {
                already_renamed = 1;
                res =
                    mcachefs_journal_append_entry(fd_tgt, &entry, path,
                                                  rename_to);
                if (res < 0)
                    break;
                continue;
            }

        }
        if (strncmp(path, rename_path, rename_path_sz) == 0)
        {
            exact_match = (path[rename_path_sz] == '\0');
            Info("Journal : matching : path='%s', rename_path='%s', exact_match=%d\n", path, rename_path, exact_match);

            if (exact_match &&
                (entry.op == mcachefs_journal_op_mkdir
                 || entry.op == mcachefs_journal_op_mknod
                 || entry.op == mcachefs_journal_op_symlink
                 || entry.op == mcachefs_journal_op_link))
            {
                already_renamed = 1;
                res =
                    mcachefs_journal_append_entry(fd_tgt, &entry, rename_to,
                                                  to);
                if (res < 0)
                    break;
                continue;
            }
            if (!already_renamed)
            {
                already_renamed = 1;
                memset(&rename_entry, 0,
                       sizeof(struct mcachefs_journal_entry_t));
                rename_entry.op = mcachefs_journal_op_rename;
                res =
                    mcachefs_journal_append_entry(fd_tgt, &rename_entry,
                                                  rename_path, rename_to);
                if (res < 0)
                    break;
            }
            strncpy(altered_path_suffix, &(path[rename_path_sz]),
                    PATH_MAX - rename_to_sz);
            res =
                mcachefs_journal_append_entry(fd_tgt, &entry, altered_path,
                                              to);
            if (res < 0)
                break;
            continue;
        }
#endif
        res = mcachefs_journal_append_entry(fd_tgt, &entry, path, to);
        if (res < 0)
            break;
    }

    if (!res && !already_renamed)
    {
        Log("At last : was not found, append rename op.\n");
        memset(&rename_entry, 0, sizeof(struct mcachefs_journal_entry_t));
        rename_entry.op = mcachefs_journal_op_rename;
        res =
            mcachefs_journal_append_entry(fd_tgt, &rename_entry, rename_path,
                                          rename_to);
    }

    Log("Finished rebuilding journal.\n");
    if (res)
    {
        Err("Rebuilt with errors !\n");
        close(fd_src);
        close(fd_tgt);
        free(journal_tgt);
        exit(-1);
    }

    close(fd_src);
    close(fd_tgt);

    if (unlink(mcachefs_config_get_journal()))
    {
        Err("Could not unlink : err=%d:%s\n", errno, strerror(errno));
    }
    if (rename(journal_tgt, mcachefs_config_get_journal()))
    {
        Err("Could not rename %s to %s : err=%d:%s\n", journal_tgt,
            mcachefs_config_get_journal(), errno, strerror(errno));
        res = -errno;
    }

    free(journal_tgt);
    return res;
}

int
mcachefs_journal_was_renamed(const char *newpath)
{
    int fd, res;
    int wasrenamed = 0;
    char path[PATH_MAX], to[PATH_MAX];
    struct mcachefs_journal_entry_t entry;

    mcachefs_journal_lock();
    fd = open(mcachefs_config_get_journal(), O_RDONLY);

    if (fd < 0)
    {
        mcachefs_journal_unlock();
        Err("Could not open journal file, exiting.\n");
        return 0;
    }

    while (1)
    {
        res = mcachefs_journal_read_entry(fd, &entry, path, to);

        if (res == 0)
            break;
        if (res < 0)
        {
            Err("Could not read entry : err=%d:%s\n", -res, strerror(-res));
            Bug(".");
        }
        if (entry.op != mcachefs_journal_op_rename)
            continue;
        if (strncmp(newpath, to, entry.to_sz) == 0)
        {
            Log("Renamed file '%s' : renamed '%s' to '%s'\n", newpath, path,
                to);
            wasrenamed = 1;
            break;
        }
    }

    close(fd);
    mcachefs_journal_unlock();

    return wasrenamed;
}

void
mcachefs_journal_apply_entry(struct mcachefs_journal_entry_t *entry,
                             const char *path, const char *to)
{
    Log("Apply journal entry : op=%s (%x) : path='%s', to='%s' : "
        "mode=%lo, dev=%ld, uid=%ld, gid=%ld, size=%lu, utimebuf=%lu,%lu\n",
        entry->op <
        mcachefs_journal_op_MAX ? mcachefs_journal_op_label[entry->op] :
        "INVALID", entry->op, path, to, (long) entry->mode,
        (long) entry->rdev, (long) entry->uid, (long) entry->gid,
        (unsigned long) entry->size, entry->utimbuf.actime,
        entry->utimbuf.modtime);

    char *realpath = mcachefs_makepath_source(path);
    char *realto = NULL;
    if (to)
        realto = mcachefs_makepath_source(to);

    Log("Operating on real_path='%s'\n", realpath);
    switch (entry->op)
    {
    case mcachefs_journal_op_none:
        Err("Invalid none entry for path='%s'\n", path);
        break;
    case mcachefs_journal_op_mknod:
        if (mknod(realpath, entry->mode, entry->rdev))
        {
            Err("Could not mknod %s : err=%d:%s\n", realpath, errno,
                strerror(errno));
        }
        if (chown(realpath, entry->uid, entry->gid))
        {
            Err("After mknod : Could not chown %s : err=%d:%s\n", realpath,
                errno, strerror(errno));
        }
        break;
    case mcachefs_journal_op_mkdir:
        if (mkdir(realpath, entry->mode))
        {
            Err("Could not mkdir %s : err=%d:%s\n", realpath, errno,
                strerror(errno));
        }
        if (chown(realpath, entry->uid, entry->gid))
        {
            Err("After mkdir : Could not chown %s : err=%d:%s\n", realpath,
                errno, strerror(errno));
        }
        break;
    case mcachefs_journal_op_unlink:
        if (unlink(realpath))
        {
            Err("Could not unlink %s : err=%d:%s\n", realpath, errno,
                strerror(errno));
        }
        break;
    case mcachefs_journal_op_rmdir:
        if (rmdir(realpath))
        {
            Err("Could not rmdir %s : err=%d:%s\n", realpath, errno,
                strerror(errno));
        }
        break;
    case mcachefs_journal_op_rename:
        if (rename(realpath, realto))
        {
            Err("Could not rename %s -> %s : err=%d:%s\n", realpath, realto,
                errno, strerror(errno));
        }
        break;
    case mcachefs_journal_op_symlink:
        if (symlink(to, realpath))
        {
            Err("Could not symlink %s -> %s : err=%d:%s\n", realpath, realto,
                errno, strerror(errno));
        }
        break;
    case mcachefs_journal_op_link:
        Err("Not handled : op=%x\n", entry->op);
        break;
    case mcachefs_journal_op_chmod:
        if (chmod(realpath, entry->mode))
        {
            Err("Could not chmod %s : err=%d:%s\n", realpath, errno,
                strerror(errno));
        }
        break;
    case mcachefs_journal_op_chown:
        if (chown(realpath, entry->uid, entry->gid))
        {
            Err("Could not chown %s : err=%d:%s\n", realpath, errno,
                strerror(errno));
        }
        break;
    case mcachefs_journal_op_truncate:
        if (truncate(realpath, entry->size))
        {
            Err("Could not truncate %s : err=%d:%s\n", realpath, errno,
                strerror(errno));
        }
        break;
    case mcachefs_journal_op_utime:
        if (utime(realpath, &(entry->utimbuf)))
        {
            Err("Could not utime %s : err=%d:%s\n", realpath, errno,
                strerror(errno));
        }
        break;
#if 0
    case mcachefs_journal_op_fsync:
        if ((res = mcachefs_write_back(path)) != 0)
        {
            Err("Could not write back %s : err=%d:%s\n", path, -res,
                strerror(-res));
        }
        break;
#endif
    default:
        Bug("Invalid op=%x !\n", entry->op);
        break;
    }

    free(realpath);
    if (realto)
        free(realto);
}

int
mcachefs_journal_apply_updates(const char *journal)
{
    mcachefs_journal_check_locked();
    int fd, res;
    char path[PATH_MAX], to[PATH_MAX];
    struct mcachefs_journal_entry_t entry;

    fd = open(journal, O_RDONLY);

    if (fd == -1)
    {
        Err("Could not read journal '%s' : err=%d:%s\n",
            mcachefs_config_get_journal(), errno, strerror(errno));
        return -EIO;
    }

    Log("\tStart of journal apply updates.\n");

    while (1)
    {
        res = mcachefs_journal_read_entry(fd, &entry, path, to);

        if (res == 0)
            break;
        if (res < 0)
        {
            Bug("Could not read entry : err=%d:%s\n", -res, strerror(-res));
            mcachefs_journal_unlock();
        }
        if (entry.op == mcachefs_journal_op_fsync)
            continue;
        mcachefs_journal_apply_entry(&entry, path, to);
        Log("\tApply finished.\n");
    }

    Log("\tEnd of journal apply updates.\n");

    close(fd);
    return (res == 0) ? 0 : -EIO;
}

int
mcachefs_journal_apply_fsync(const char *journal)
{
    mcachefs_journal_check_locked();
    int fd, res, ret;
    char path[PATH_MAX], to[PATH_MAX];
    struct mcachefs_journal_entry_t entry;

    fd = open(journal, O_RDONLY);

    if (fd == -1)
    {
        Err("Could not read journal '%s' : err=%d:%s\n",
            mcachefs_config_get_journal(), errno, strerror(errno));
        return -EIO;
    }

    Log("\tStart of journal apply fsync.\n");

    while (1)
    {
        res = mcachefs_journal_read_entry(fd, &entry, path, to);

        if (res == 0)
            break;
        if (res < 0)
        {
            Bug("Could not read entry : err=%d:%s\n", -res, strerror(-res));
            return -EIO;
        }
        if (entry.op != mcachefs_journal_op_fsync)
            continue;
        Log("Fsync : %s\n", path);
        /**
         * We are protected by the journal lock here
         */

        ret = mcachefs_transfer_writeback(path);

        mcachefs_journal_check_locked();

        mcachefs_journal_fsync.total_files++;
        if (ret == -EEXIST)
        {
            mcachefs_journal_fsync.files_ok++;
        }
        else if (ret)
        {
            mcachefs_journal_fsync.files_error++;
        }
    }

    close(fd);

    return (res == 0) ? 0 : -EIO;
}

void
mcachefs_journal_apply()
{
    int res;

    Log("Apply journal...\n");

    mcachefs_journal_lock();

    if (mcachefs_journal_action != mcachefs_journal_action_none)
    {
        Err("Jounal already being applied, please wait...\n");
        mcachefs_journal_unlock();
        return;
    }
    if (mcachefs_journal_fsync.total_files)
    {
        Bug("Already have pending fsync files !\n");
    }
    mcachefs_journal_fsync.total_files = mcachefs_journal_fsync.files_ok =
        mcachefs_journal_fsync.files_error = 0;

    mcachefs_journal_action = mcachefs_journal_action_update;

    /**
     * First, apply non-fsync modifications
     */
    if (mcachefs_journal_apply_updates(mcachefs_config_get_journal()))
    {
        Err("Could not apply updates !\n");
        mcachefs_journal_unlock();
        return;
    }

    mcachefs_journal_action = mcachefs_journal_action_sync;

    Log("\tJournal fsync.\n");

    /*
     * Now fsync openned files
     */
    res = rename(mcachefs_config_get_journal(), mcachefs_journal_fsync_path);
    if (res == -1)
    {
        Bug("Could not rename '%s' to '%s' : err=%d:%s\n",
            mcachefs_config_get_journal(), mcachefs_journal_fsync_path,
            errno, strerror(errno));
    }

    if (mcachefs_journal_apply_fsync(mcachefs_journal_fsync_path))
    {
        Err("Could not apply fsync !\n");
        mcachefs_journal_unlock();
        return;
    }

    if (mcachefs_journal_fsync.files_ok + mcachefs_journal_fsync.files_error
        == mcachefs_journal_fsync.total_files)
    {
        Info("Journal has no files to sync (total=%d, ok=%d, error=%d).\n",
             mcachefs_journal_fsync.total_files,
             mcachefs_journal_fsync.files_ok,
             mcachefs_journal_fsync.files_error);
        if (unlink(mcachefs_journal_fsync_path))
        {
            Bug("Could not unlink '%s' : err=%d:%s\n",
                mcachefs_journal_fsync_path, errno, strerror(errno));
        }
        mcachefs_journal_action = mcachefs_journal_action_none;
    }
    mcachefs_journal_unlock();
}

void
mcachefs_notify_sync_end(const char *path, int success)
{
    (void) path;
    mcachefs_journal_lock();
    if (success)
        mcachefs_journal_fsync.files_ok++;
    else
        mcachefs_journal_fsync.files_error++;

    Info("Journal fsync : writeback of '%s' %s (total=%d, ok=%d, error=%d)\n",
         path, success ? "successful" : "failed",
         mcachefs_journal_fsync.total_files, mcachefs_journal_fsync.files_ok,
         mcachefs_journal_fsync.files_error);

    if (mcachefs_journal_fsync.files_ok + mcachefs_journal_fsync.files_error
        == mcachefs_journal_fsync.total_files)
    {
        Info("End of fsync : finishing !\n");
        if (unlink(mcachefs_journal_fsync_path))
        {
            Bug("Could not unlink '%s' : err=%d:%s\n",
                mcachefs_journal_fsync_path, errno, strerror(errno));
        }
        mcachefs_journal_fsync.total_files = mcachefs_journal_fsync.files_ok =
            mcachefs_journal_fsync.files_error = 0;
        mcachefs_journal_action = mcachefs_journal_action_none;

    }
    mcachefs_journal_unlock();
}

void
mcachefs_journal_drop()
{
    mcachefs_journal_lock();

    if (truncate(mcachefs_config_get_journal(), 0))
    {
        Err("Could not ftruncate journal : err=%d:%s\n", errno,
            strerror(errno));
        exit(-1);
    }

    mcachefs_journal_unlock();
}

void
mcachefs_journal_dump(struct mcachefs_file_t *mvops)
{
    int fd, res;
    struct mcachefs_journal_entry_t entry;
    char path[PATH_MAX];
    char to[PATH_MAX];
    unsigned long entry_nb = 0;

    Log("Dumping Journal...\n");

    mcachefs_journal_lock();

    Log("\tJournal mutex lock... OK.\n");

    fd = open(mcachefs_config_get_journal(), O_RDONLY);

    if (fd == -1)
    {
        Err("Could not read journal '%s' : err=%d:%s\n",
            mcachefs_config_get_journal(), errno, strerror(errno));
        mcachefs_journal_unlock();
        Log("\tJournal mutex unlock... OK.\n");
        return;
    }

    while (1)
    {
        res = mcachefs_journal_read_entry(fd, &entry, path, to);

        if (res == 0)
            break;
        if (res < 0)
        {
            Err("Could not read entry : err=%d:%s\n", -res, strerror(-res));
            __VOPS_WRITE(mvops, "** Unexpected end of journal !\n");
            break;
        }

        __VOPS_WRITE(mvops,
                     "[%lu] %s (%x) : path='%s', to='%s' : "
                     "mode=%lo, dev=%ld, uid=%ld, gid=%ld, size=%lu, utimebuf=%lu,%lu\n",
                     entry_nb,
                     entry.op <
                     mcachefs_journal_op_MAX ?
                     mcachefs_journal_op_label[entry.op] : "INVALID",
                     entry.op, path, to, (long) entry.mode,
                     (long) entry.rdev, (long) entry.uid, (long) entry.gid,
                     (unsigned long) entry.size, entry.utimbuf.actime,
                     entry.utimbuf.modtime);
        entry_nb++;
    }

    close(fd);

    mcachefs_journal_unlock();
    Log("\tJournal mutex unlock... OK.\n");
}
