#ifndef __MCACHEFS_CONFIG_H
#define __MCACHEFS_CONFIG_H

/**
 * Types of transfer set
 */
#define MCACHEFS_TRANSFER_TYPES 3
#define MCACHEFS_TRANSFER_TYPE_BACKUP    0
#define MCACHEFS_TRANSFER_TYPE_WRITEBACK 1
#define MCACHEFS_TRANSFER_TYPE_METADATA  2

extern const int mcachefs_file_timeslice_nb;

struct mcachefs_config
{
    /*
     * The actual fuse mountpoint
     */
    char *mountpoint;

    /*
     * Source mountpoint
     */
    char *source;

    /*
     * Cache mountpoint point
     */
    char *cache;

    /*
     * Metadata cache file
     */
    char *metafile;

    /*
     * Journal cache file
     */
    char *journal;

    /*
     * Log verbosity
     */
    int verbose;

    /**
     * Number of threads per thread type
     */
    int transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPES];

    /**
     * The actual fuse arguments as passed to libfuse
     */
    struct fuse_args fuse_args;

    int read_state;

    int write_state;

    int file_thread_interval;

    int file_ttl;

    int metadata_map_ttl;

    int transfer_max_rate;

    int cleanup_cache_age;

    char *cache_prefix;

    char *cleanup_cache_prefix;
};

struct mcachefs_config *mcachefs_parse_config(int argc, char *argv[]);

void mcachefs_dump_config(struct mcachefs_config *config);

void mcachefs_set_current_config(struct mcachefs_config *config);

/**
 * Access to current instance of config
 */
extern struct mcachefs_config *current_config;

const char *mcachefs_config_get_mountpoint();

const char *mcachefs_config_get_source();

const char *mcachefs_config_get_cache();

const char *mcachefs_config_get_metafile();

const char *mcachefs_config_get_journal();

static inline int
mcachefs_config_get_verbose()
{
    if (current_config == NULL)
    {
        return 100;
    }
    return current_config->verbose;
}

int mcachefs_config_get_transfer_threads_nb(int type);

/**
 * General status and configuration retrival and setting
 */
void mcachefs_config_set_read_state(int rdstate);
int mcachefs_config_get_read_state();

void mcachefs_config_set_write_state(int wrstate);
int mcachefs_config_get_write_state();

int mcachefs_config_get_file_thread_interval();
void mcachefs_config_set_file_thread_interval(int interval);

int mcachefs_config_get_file_ttl();
void mcachefs_config_set_file_ttl(int ttl);

int mcachefs_config_get_metadata_map_ttl();
void mcachefs_config_set_metadata_map_ttl(int ttl);

int mcachefs_config_get_transfer_max_rate();
void mcachefs_config_set_transfer_max_rate(int rate);

/**
 * Cleanup Backing configuration
 */
int mcachefs_config_get_cleanup_cache_age();
const char *mcachefs_config_get_cleanup_cache_prefix();

const char *mcachefs_config_get_cache_prefix();
void mcachefs_config_set_cache_prefix(const char *prefix);

#endif // __MCACHEFS_CONFIG_H
