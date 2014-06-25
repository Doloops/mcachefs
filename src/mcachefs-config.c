#include "mcachefs.h"

#include <libgen.h>

const char* DEFAULT_PREFIX = "/tmp/mcachefs";

const int DEFAULT_VERBOSE = 0;

void trim_last_separator(char* path);
void set_default_config(struct mcachefs_config* config);
int check_dir_exists(const char* cpath);
void check_file_dir_exists(const char* cpath);
    
struct mcachefs_config* mcachefs_parse_config(int argc, char* argv[])
{
    if ( argc < 3 )
    {
        Err("Invalid number of arguments !\n");
        return NULL;
    }
    if ( argv[1][0] == '-' )
    {
        Err("Invalid argument for source : %s\n", argv[1]);        
        return NULL;
    }
    if ( argv[2][0] == '-' )
    {
        Err("Invalid argument for mountpoint : %s\n", argv[2]);
        return NULL;
    }
    struct mcachefs_config* config = (struct mcachefs_config*) malloc(sizeof(struct mcachefs_config));
    memset(config, 0, sizeof(struct mcachefs_config));
    config->source = strdup(argv[1]);
    trim_last_separator(config->source);
    
    config->mountpoint = strdup(argv[2]);
    trim_last_separator(config->mountpoint);

    set_default_config(config);
    
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
    char* normalized_mp = strdup(config->mountpoint), *cur;
  
    for ( cur = normalized_mp ; *cur != 0 ; cur++ )
    {
        if ( *cur == '/' )
        {
            *cur = '_';
        }
    }
    Info("Normalized mountpoint : %s\n", normalized_mp);
    config->cache = (char*) malloc(PATH_MAX);
    config->metafile = (char*) malloc(PATH_MAX);
    config->journal = (char*) malloc(PATH_MAX);
    
    snprintf(config->cache, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX, normalized_mp, "cache");
    snprintf(config->metafile, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX, normalized_mp, "metafile");    
    snprintf(config->journal, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX, normalized_mp, "journal");
}

void mcachefs_dump_config(struct mcachefs_config* config)
{
    Info("mcachefs Configuration :\n");
    Info("* Mountpoint %s\n", config->mountpoint);
    Info("* Source %s\n", config->source);
    Info("* Cache %s\n", config->cache);
    Info("* Metafile %s\n", config->metafile);
    Info("* Journal %s\n", config->journal);    
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
        Err("Path %s does not exist !\n", cpath);
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
