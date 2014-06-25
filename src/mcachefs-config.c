#include "mcachefs.h"

#include <libgen.h>
#include <stddef.h>

const char* DEFAULT_PREFIX = "/tmp/mcachefs";

const int DEFAULT_VERBOSE = 0;

void trim_last_separator(char* path);
void set_default_config(struct mcachefs_config* config);
int check_dir_exists(const char* cpath);
void check_file_dir_exists(const char* cpath);
    
static int mcachefs_arg_proc(void *data, const char *arg, int key,
                               struct fuse_args *outargs)
{
    (void) outargs;
    Log("arg data=%p, arg=%s, key=%d, outargs=%p\n", data, arg, key, outargs);

    struct mcachefs_config* config = (struct mcachefs_config*) data;
    switch ( key )
    {
    case FUSE_OPT_KEY_NONOPT:
        if ( config->source == NULL )
        {
            config->source = strdup(arg);
            trim_last_separator(config->source);
            return 0;
        }
        if ( config->mountpoint == NULL )
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
    { "cache=%s", offsetof(struct mcachefs_config,cache), 0 },
    { "metafile=%s", offsetof(struct mcachefs_config,metafile), 0 },
    { "journal=%s", offsetof(struct mcachefs_config,journal), 0 },
    { "verbose=%lu", offsetof(struct mcachefs_config,verbose), 0 },
    FUSE_OPT_END
};

    
struct mcachefs_config* mcachefs_parse_config(int argc, char* argv[])
{
    if ( argc < 3 )
    {
        Err("Invalid number of arguments !\n");
        return NULL;
    }

    struct mcachefs_config* config = (struct mcachefs_config*) malloc(sizeof(struct mcachefs_config));
    memset(config, 0, sizeof(struct mcachefs_config));

    Log("config at %p\n", config);
    
    config->fuse_args.argc = argc;
    config->fuse_args.argv = argv;
    config->fuse_args.allocated = 0;
    int res = fuse_opt_parse(&(config->fuse_args), config, mcachefs_opts, mcachefs_arg_proc);

    if ( res != 0 )
    {
        Err("Could not parse arguments !");
        free(config);
        return NULL;
    }

    Log("After fuse_opt_parse res=%d\n", res);
    Log("After parse mp=%s\n", config->mountpoint);

    set_default_config(config);
    
    mcachefs_dump_config(config);

    return config;
}

void trim_last_separator(char* path)
{
    char* mp;    
    for ( mp = path ; ; mp++ )
    {
        if ( mp[1] == 0 )
        {
            if ( mp[0] == '/' )
            {
                mp[0] = 0;
            }
            break;
        }
    }
}

void set_default_config(struct mcachefs_config* config)
{
    const char* mp = config->mountpoint;
    if ( *mp == '/' )
    {
        mp++;
    }
    char* normalized_mp = strdup(mp), *cur;
  
    for ( cur = normalized_mp ; *cur != 0 ; cur++ )
    {
        if ( *cur == '/' )
        {
            *cur = '_';
        }
    }
    Info("Normalized mountpoint : %s\n", normalized_mp);

    if ( config->cache == NULL )
    {
        config->cache = (char*) malloc(PATH_MAX);
        snprintf(config->cache, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX, normalized_mp, "cache");
    }

    if ( config->metafile == NULL )
    {    
        config->metafile = (char*) malloc(PATH_MAX);
        snprintf(config->metafile, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX, normalized_mp, "metafile");    
    }
    
    if ( config->journal == NULL )
    {
        config->journal = (char*) malloc(PATH_MAX);
        snprintf(config->journal, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX, normalized_mp, "journal");
    }
}

void mcachefs_dump_config(struct mcachefs_config* config)
{
    Info("mcachefs Configuration :\n");
    Info("* Mountpoint %s\n", config->mountpoint);
    Info("* Source %s\n", config->source);
    Info("* Cache %s\n", config->cache);
    Info("* Metafile %s\n", config->metafile);
    Info("* Journal %s\n", config->journal);    
    
    int argc;
    for ( argc = 0 ; argc < config->fuse_args.argc ; argc++ )
    {
        Info("* Extra arg : %s\n", config->fuse_args.argv[argc]);
    }
}


void mcachefs_set_current_config(struct mcachefs_config* config)
{
    check_dir_exists(config->cache);
    check_file_dir_exists(config->metafile);
    check_file_dir_exists(config->journal);

    config->verbose = DEFAULT_VERBOSE;    
    config->log_fd = stderr;

    int threadtype;
    for ( threadtype = 0 ; threadtype < MCACHEFS_TRANSFER_TYPES ; threadtype++ )
    {
        config->transfer_threads_type_nb[threadtype] = 1;
    }

    current_config = config;
}

void check_file_dir_exists(const char* cpath)
{
    char* path = strdup(cpath);
    path = dirname(path);
    check_dir_exists(path);    
    free (path);
}

int check_dir_exists(const char* cpath)
{
    struct stat sb;
    int res;
    char* parentpath;
    if ( stat(cpath, &sb) != 0 )
    {
        Log("Path %s does not exist, creating it\n", cpath);
        parentpath = dirname(strdup(cpath));
        res = check_dir_exists(parentpath);
        free(parentpath);
        
        if ( res != 0 )
        {
            return res;
        }
        
        if ( mkdir(cpath, 0755) != 0 )
        {
            Err("Could not create path %s\n", cpath);
            return -1;
        }
        return 0;        
    }
    if ( ! S_ISDIR(sb.st_mode) )
    {
        Err("Path %s is not a directory\n", cpath);
        return -1;
    }
    return 0;
}

/**
 * Access to current instance of config
 */
struct mcachefs_config* current_config;

const char* mcachefs_config_mountpoint()
{
    return current_config->mountpoint;
}

const char* mcachefs_config_source()
{ 
    return current_config->source;
}

const char* mcachefs_config_cache()
{
    return current_config->cache;
}

const char* mcachefs_config_metafile()
{
    return current_config->metafile;
}

const char* mcachefs_config_journal()
{
    return current_config->journal;
}

int mcachefs_config_transfer_threads_nb(int type)
{
    return current_config->transfer_threads_type_nb[type];
}
