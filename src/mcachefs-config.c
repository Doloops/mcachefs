#include "mcachefs.h"

#include <libgen.h>

const char* DEFAULT_PREFIX = "/tmp/mcachefs";

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
        Err("Invalid argument for target : %s\n", argv[1]);        
        return NULL;
    }
    if ( argv[2][0] == '-' )
    {
        Err("Invalid argument for target : %s\n", argv[1]);        
        return NULL;
    }
    struct mcachefs_config* config = (struct mcachefs_config*) malloc(sizeof(struct mcachefs_config));
    memset(config, 0, sizeof(struct mcachefs_config));
    config->target = strdup(argv[1]);
    trim_last_separator(config->target);
    
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
    config->backing = (char*) malloc(PATH_MAX);
    config->metafile = (char*) malloc(PATH_MAX);
    config->journal = (char*) malloc(PATH_MAX);
    
    snprintf(config->backing, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX, normalized_mp, "cache");
    snprintf(config->metafile, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX, normalized_mp, "metafile");    
    snprintf(config->journal, PATH_MAX, "%s/%s/%s", DEFAULT_PREFIX, normalized_mp, "journal");
}

void mcachefs_dump_config(struct mcachefs_config* config)
{
    Info("mcachefs Configuration :\n");
    Info("* Target %s\n", config->target);
    Info("* Mountpoint %s\n", config->mountpoint);
    Info("* Cache %s\n", config->backing);
    Info("* Metafile %s\n", config->metafile);
    Info("* Journal %s\n", config->journal);    
}


void mcachefs_set_current_config(struct mcachefs_config* config)
{
    check_dir_exists(config->backing);
    check_file_dir_exists(config->metafile);
    check_file_dir_exists(config->journal);
    
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

const char* mcachefs_config_target()
{ 
    return current_config->target;
}

const char* mcachefs_config_backing()
{
    return current_config->backing;
}

const char* mcachefs_config_metafile()
{
    return current_config->metafile;
}

const char* mcachefs_config_journal()
{
    return current_config->journal;
}

int mcachefs_config_verbose()
{
    if ( current_config == NULL )
    {
        return 100;
    }
    return current_config->verbose;
}

FILE* mcachefs_config_log_fd()
{
    if ( current_config == NULL )
    {
        return stderr;
    }
    return current_config->log_fd;
}

int mcachefs_config_transfer_threads_nb(int type)
{
    return current_config->transfer_threads_type_nb[type];
}
