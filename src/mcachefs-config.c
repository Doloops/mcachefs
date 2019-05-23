#include "mcachefs.h"

#include <libgen.h>
#include <stddef.h>

const char *DEFAULT_PREFIX = "/tmp/mcachefs";

const int DEFAULT_VERBOSE = 0;

void trim_last_separator(char *path);
void set_default_config(struct mcachefs_config *config);
int check_dir_exists(const char *cpath);
void check_file_dir_exists(const char *cpath);

static int
mcachefs_arg_proc(void *data, const char *arg, int key,
                  struct fuse_args *outargs)
{
    (void) outargs;
    Log("arg data=%p, arg=%s, key=%d, outargs=%p\n", data, arg, key, outargs);

    struct mcachefs_config *config = (struct mcachefs_config *) data;
    switch (key)
    {
    case FUSE_OPT_KEY_NONOPT:
        if (config->source == NULL)
        {
            config->source = strdup(arg);
            trim_last_separator(config->source);
            return 0;
        }
        if (config->mountpoint == NULL)
        {
            Log("Setting mountpoint to %s\n", arg);
            config->mountpoint = strdup(arg);
            trim_last_separator(config->mountpoint);
            return 1;
        }
        Err("Syntax error : non-option argument '%s' is invalid !\n", arg);
        return -1;
    }
    return 1;
}

static struct fuse_opt mcachefs_opts[] = {
    {"cache=%s", offsetof(struct mcachefs_config, cache), 0},
    {"metafile=%s", offsetof(struct mcachefs_config, metafile), 0},
    {"journal=%s", offsetof(struct mcachefs_config, journal), 0},
    {"verbose=%lu", offsetof(struct mcachefs_config, verbose), 0},
    {"backup-threads=%lu", offsetof(struct mcachefs_config, transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_BACKUP]), 0},
    {"write-threads=%lu", offsetof(struct mcachefs_config, transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_WRITEBACK]), 0},
    {"metadata-threads=%lu", offsetof(struct mcachefs_config, transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_METADATA]), 0},
    FUSE_OPT_END
};

static void print_usage(const char* program_name) {
    Info("\n");
    Info("Basic usage : %s {source mountpoint} {target mountpoint}\n", program_name);
    Info("\twhere {source mountpoint} is the backend mount point to cache\n");
    Info("\tand {target mountpoint} is the cached mount point exposed by %s\n", program_name);
    Info("\n");
    Info("Optional arguments, provided as -o {argument}={value}[,{argument}={value}]\n");
    Info("\tcache\t\t: local cache path (must be a directory), defaults to %s/{mount point}/cache/\n", DEFAULT_PREFIX);
    Info("\tmetafile\t: local cache directory structure file, defaults to %s/{mount point}/metafile\n", DEFAULT_PREFIX);
    Info("\tjournal\t\t: local cache update journal, defaults to %s/{mount point}/journal\n", DEFAULT_PREFIX);
    Info("\tbackup-threads\t: number of threads to use for backup of files (download from source to target)\n");
    Info("\twrite-threads\t: number of threads to use for write files back to source (when 'apply_journal' is called)\n");
    Info("\tmetadata-threads: number of threads to use for retrieving metadata from source (retrieving folders and files information)\n");
    Info("\n");
    Info("Example:\n");
    Info("\t%s /mnt/backend /mnt/localcache -o cache=/tmp/mycache,journal=/tmp/cachejournal\n", program_name);
}

struct mcachefs_config *
mcachefs_parse_config(int argc, char *argv[])
{
    const char* program_name = argv[0];
    if (argc < 3)
    {
        Err("Invalid number of arguments !\n");
        print_usage(program_name);
        return NULL;
    }

    struct mcachefs_config *config =
        (struct mcachefs_config *) malloc(sizeof(struct mcachefs_config));
    memset(config, 0, sizeof(struct mcachefs_config));

    Log("config at %p\n", config);

    /**
     * Init default config values
     */
    config->read_state = MCACHEFS_STATE_NORMAL;
    config->write_state = MCACHEFS_WRSTATE_CACHE;
    config->file_thread_interval = 1;
    config->file_ttl = 300;
    config->metadata_map_ttl = 10;
    config->transfer_max_rate = 100000;
    config->cleanup_cache_age = 30 * 24 * 3600;
    config->cleanup_cache_prefix = NULL;
    config->cache_prefix = strdup("/");

    config->fuse_args.argc = argc;
    config->fuse_args.argv = argv;
    config->fuse_args.allocated = 0;
    int res = fuse_opt_parse(&(config->fuse_args), config, mcachefs_opts,
                             mcachefs_arg_proc);

    if (res != 0)
    {
        Err("Could not parse arguments !");
        free(config);
        print_usage(program_name);
        return NULL;
    }

    Log("After fuse_opt_parse res=%d\n", res);
    Log("After parse mp=%s\n", config->mountpoint);

    set_default_config(config);

    mcachefs_dump_config(config);

    return config;
}

void
trim_last_separator(char *path)
{
    char *mp;
    for (mp = path;; mp++)
    {
        if (mp[1] == 0)
        {
            if (mp[0] == '/')
            {
                mp[0] = 0;
            }
            break;
        }
    }
}

void
set_default_config(struct mcachefs_config *config)
{
    const char *mp = config->mountpoint;
    if (*mp == '/')
    {
        mp++;
    }
    char *normalized_mp = strdup(mp), *cur;

    for (cur = normalized_mp; *cur != 0; cur++)
    {
        if (*cur == '/')
        {
            *cur = '_';
        }
    }
    Info("Normalized mountpoint : %s\n", normalized_mp);

    if (config->cache == NULL)
    {
        config->cache = (char *) malloc(PATH_MAX);
        snprintf(config->cache, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX,
                 normalized_mp, "cache");
    }

    if (config->metafile == NULL)
    {
        config->metafile = (char *) malloc(PATH_MAX);
        snprintf(config->metafile, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX,
                 normalized_mp, "metafile");
    }

    if (config->journal == NULL)
    {
        config->journal = (char *) malloc(PATH_MAX);
        snprintf(config->journal, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX,
                 normalized_mp, "journal");
    }
}

void
mcachefs_dump_config(struct mcachefs_config *config)
{
    Info("mcachefs Configuration :\n");
    Info("* Mountpoint %s\n", config->mountpoint);
    Info("* Source %s\n", config->source);
    Info("* Cache %s\n", config->cache);
    Info("* Metafile %s\n", config->metafile);
    Info("* Journal %s\n", config->journal);
    Info("* Backup Threads %d\n", config->transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_BACKUP]);
    Info("* Write Back Threads %d\n", config->transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_WRITEBACK]);
    Info("* Metadata Threads %d\n", config->transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_METADATA]);

    int argc;
    for (argc = 0; argc < config->fuse_args.argc; argc++)
    {
        Info("* Extra arg : %s\n", config->fuse_args.argv[argc]);
    }
}

void
mcachefs_set_current_config(struct mcachefs_config *config)
{
    check_dir_exists(config->cache);
    check_file_dir_exists(config->metafile);
    check_file_dir_exists(config->journal);

    config->verbose = DEFAULT_VERBOSE;

    int threadtype;
    for (threadtype = 0; threadtype < MCACHEFS_TRANSFER_TYPES; threadtype++)
    {
        if ( ! config->transfer_threads_type_nb[threadtype] )
          config->transfer_threads_type_nb[threadtype] = 1;
    }

    current_config = config;
}

void
check_file_dir_exists(const char *cpath)
{
    char *path = strdup(cpath);
    path = dirname(path);
    check_dir_exists(path);
    if (strncmp(path, ".", 2) != 0)
    {
        free(path);
    }
}

int
check_dir_exists(const char *cpath)
{
    struct stat sb;
    int res;
    char *parentpath;
    if (stat(cpath, &sb) != 0)
    {
        Log("Path %s does not exist, creating it\n", cpath);
        parentpath = dirname(strdup(cpath));
        res = check_dir_exists(parentpath);
        free(parentpath);

        if (res != 0)
        {
            return res;
        }

        if (mkdir(cpath, 0755) != 0)
        {
            Err("Could not create path %s\n", cpath);
            return -1;
        }
        return 0;
    }
    if (!S_ISDIR(sb.st_mode))
    {
        Err("Path %s is not a directory\n", cpath);
        return -1;
    }
    return 0;
}

/**
 * Access to current instance of config
 */
struct mcachefs_config *current_config;

const char *
mcachefs_config_get_mountpoint()
{
    return current_config->mountpoint;
}

const char *
mcachefs_config_get_source()
{
    return current_config->source;
}

const char *
mcachefs_config_get_cache()
{
    return current_config->cache;
}

const char *
mcachefs_config_get_metafile()
{
    return current_config->metafile;
}

const char *
mcachefs_config_get_journal()
{
    return current_config->journal;
}

int
mcachefs_config_get_transfer_threads_nb(int type)
{
    return current_config->transfer_threads_type_nb[type];
}

void
mcachefs_config_set_read_state(int rdstate)
{
    current_config->read_state = rdstate;
}

int
mcachefs_config_get_read_state()
{
    return current_config->read_state;
}

void
mcachefs_config_set_write_state(int wrstate)
{
    current_config->write_state = wrstate;
}

int
mcachefs_config_get_write_state()
{
    return current_config->write_state;
}

int
mcachefs_config_get_file_thread_interval()
{
    return current_config->file_thread_interval;
}

void
mcachefs_config_set_file_thread_interval(int interval)
{
    current_config->file_thread_interval = interval;
}

int
mcachefs_config_get_file_ttl()
{
    return current_config->file_ttl;
}

extern const int mcachefs_file_timeslice_nb;

void
mcachefs_config_set_file_ttl(int ttl)
{
    if (0 < ttl && ttl < mcachefs_file_timeslice_nb)
    {
        current_config->file_ttl = ttl;
    }
    else
    {
        Err("Invalid value for ttl : %d\n", ttl);
    }
}

int
mcachefs_config_get_metadata_map_ttl()
{
    return current_config->metadata_map_ttl;
}

void
mcachefs_config_set_metadata_map_ttl(int ttl)
{
    current_config->metadata_map_ttl = ttl;
}

int
mcachefs_config_get_transfer_max_rate()
{
    return current_config->transfer_max_rate;
}

void
mcachefs_config_set_transfer_max_rate(int rate)
{
    current_config->transfer_max_rate = rate;
}

int
mcachefs_config_get_cleanup_cache_age()
{
    return current_config->cleanup_cache_age;
}

const char *
mcachefs_config_get_cleanup_cache_prefix()
{
    return current_config->cleanup_cache_prefix;
}

const char *
mcachefs_config_get_cache_prefix()
{
    return current_config->cache_prefix;
}

void
mcachefs_config_set_cache_prefix(const char *prefix)
{
    if (prefix == NULL)
    {
        return;
    }
    if (current_config->cache_prefix)
    {
        free(current_config->cache_prefix);
    }
    const char *mp = mcachefs_config_get_mountpoint();
    int mp_len = strlen(mp);
    if (strncmp(prefix, mp, mp_len) == 0)
    {
        Log("Provided prefix '%s' is based on root mountpoint '%s', removing mp prefix\n", prefix, mp);
        prefix = prefix + mp_len;
    }
    Log("Setting cache_prefix to %s\n", prefix);
    current_config->cache_prefix = strdup(prefix);

    trim_last_separator(current_config->cache_prefix);
}
