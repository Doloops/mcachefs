#ifndef __MCACHEFS_CONFIG_H
#define __MCACHEFS_CONFIG_H

/**
 * Types of transfer set
 */
#define MCACHEFS_TRANSFER_TYPES 3
#define MCACHEFS_TRANSFER_TYPE_BACKUP    0
#define MCACHEFS_TRANSFER_TYPE_WRITEBACK 1
#define MCACHEFS_TRANSFER_TYPE_METADATA  2

struct mcachefs_config
{
    /*
     * The actual fuse mountpoint
     */
    char* mountpoint;
    
    /*
     * Source mountpoint
     */
    char* source;
    
    /*
     * Cache mountpoint point
     */
    char* cache;
    
    /*
     * Metadata cache file
     */
    char* metafile;
    
    /*
     * Journal cache file
     */
    char* journal;

    /*
     * Log verbosity
     */    
    int verbose;
    
    /**
     * Log filedescriptor
     */
    FILE *log_fd;
    
    /**
     * Number of threads per thread type
     */
    int transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPES];
    
    /**
     * The actual fuse arguments as passed to libfuse
     */
    struct fuse_args fuse_args;
};

struct mcachefs_config* mcachefs_parse_config(int argc, char* argv[]);

void mcachefs_dump_config(struct mcachefs_config* config);

void mcachefs_set_current_config(struct mcachefs_config* config);

/**
 * Access to current instance of config
 */
extern struct mcachefs_config* current_config;

const char* mcachefs_config_mountpoint();

const char* mcachefs_config_source();

const char* mcachefs_config_cache();

const char* mcachefs_config_metafile();

const char* mcachefs_config_journal();

static inline int mcachefs_config_verbose()
{
    if ( current_config == NULL )
    {
        return 100;
    }
    return current_config->verbose;
}

static inline FILE* mcachefs_config_log_fd()
{
    if ( current_config == NULL )
    {
        return stderr;
    }
    return current_config->log_fd;
}

int mcachefs_config_transfer_threads_nb(int type);

#endif // __MCACHEFS_CONFIG_H
