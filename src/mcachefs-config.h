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
    char *mountpoint;
    char *target;
    char *backing;
    char *metafile;
    char *journal;
    
    int verbose;
    FILE *log_fd;
    
    int transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPES];
};

struct mcachefs_config* mcachefs_parse_config(int argc, char* argv[]);

void mcachefs_dump_config(struct mcachefs_config* config);

void mcachefs_set_current_config(struct mcachefs_config* config);

/**
 * Access to current instance of config
 */
extern struct mcachefs_config* current_config;

const char* mcachefs_config_mountpoint();

const char* mcachefs_config_target();

const char* mcachefs_config_backing();

const char* mcachefs_config_metafile();

const char* mcachefs_config_journal();

int mcachefs_config_verbose();

FILE* mcachefs_config_log_fd();

int mcachefs_config_transfer_threads_nb(int type);

#endif // __MCACHEFS_CONFIG_H
