/* 
 * A config file parser. Copyright (c) Michael Still (mikal@stillhq.com) 2004, 
 * released under the terms of the GNU GPL version 2 
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "list.h"

/*
 * Internal structures
 */
struct config_list
{
    struct list_head list;
    char *line;
    char *key;
    char *value;
};

/*
 * State is handled by this structure
 */
typedef struct config_state_internal
{
    struct config_list *lines;
    char *file;
    off_t size;
}
config_state;

/*
 * Externally used functions
 */

config_state *config_open (char *);

char *config_getstring (config_state *, char *);

/*
 * Internal helper functions
 */

int config_file_open (char *, char *, int);
struct config_list *config_parse (char *);
void config_close (config_state *);
void config_tokenizeline (struct config_list *);

#endif
