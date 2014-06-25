#if 0
/* 
 * A config file parser. Copyright (c) Michael Still (mikal@stillhq.com) 2004, 
 * released under the terms of the GNU GPL version 2 
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include "config.h"

#ifdef TESTING

int
main (int argc, char *argv[])
{
    config_state *cfg;
    char *val;

    /* 
     * This will result in the following config files being checked for existance:
     *    - /etc/testing
     *    - ~/.testing
     *    - ./testing
     */
    cfg = config_open ("testing");
    if (!cfg)
    {
        printf ("Config file reading failed\n");
        return 2;
    }

    /*
     * Now we can read values from that config file
     */
    printf ("A banana is a %s\n", config_getstring (cfg, "banana"));

    /*
     * Now we can close the config file just like a normal file
     */
    config_close (cfg);
}

#endif

config_state *
config_open (char *name)
{
    int fd;
    config_state *cfg;
    struct stat sb;

    cfg = malloc (sizeof (config_state));
    if (!cfg)
    {
        perror ("config: could not allocate a config structure");
        return NULL;
    }

    fd = config_file_open ("/etc", name, 0);

    if (fd == -1)
    {
        char *home = getenv ("HOME");
        // fprintf ( stderr, "home=%p:%s\n", home, home );
        fd = config_file_open (home, name, 1);
        // if(home) free(home);
    }

    if (fd == -1)
    {
        char *fname = malloc (strlen (name) + 5);
        if (!fname)
        {
            perror ("config: could not allocate memory for config filename");
            free (cfg);
            return NULL;
        }
        snprintf (fname, strlen (name) + 5, "%s.cfg", name);
        fd = config_file_open (".", fname, 0);
        free (fname);
    }

    if (fd == -1)
    {
        free (cfg);
        return NULL;
    }

    /*
     * We return a mmapped view of the file
     */

    if (fstat (fd, &sb) < 0)
    {
        perror ("config: could not stat config file");
        close (fd);
        return NULL;
    }

    cfg->size = sb.st_size;
    if ((cfg->file = (char *) mmap (NULL, cfg->size, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE, fd, 0)) == MAP_FAILED)
    {
        perror ("config: could not mmap config file");
        close (fd);
        return NULL;
    }

    /*
     * We prebuild a list of the config strings in this file, to save
     * repeatedly parsing the file over and over later
     */
    cfg->lines = config_parse (cfg->file);
    return cfg;
}

void
config_close (config_state * cfg)
{
    if (cfg->file)
    {
        munmap (cfg->file, cfg->size);
    }
}

int
config_file_open (char *dir, char *name, int dotted)
{
    char *path;
    int fd, chars;

    chars = strlen (dir) + strlen (name) + 3;
    path = malloc (sizeof (char) * chars);
    if (path == NULL)
    {
        perror ("config: couldn't allocate memory to open config file");
        return -1;
    }

    snprintf (path, chars, "%s/%s%s", dir, dotted ? "." : "", name);
    fd = open (path, O_RDONLY);
    return fd;
}

struct config_list *
config_parse (char *file)
{
    struct config_list *head, *p;
    struct list_head *lh;
    char *line;

    head = (struct config_list *) malloc (sizeof (struct config_list));
    if (!head)
        return NULL;

    /*
     * Work through the file and build a linked list of lines
     */

    line = strtok (file, "\n");
    INIT_LIST_HEAD (&head->list);
    head->line = line;
    head->key = NULL;
    head->value = NULL;

    while ((line = strtok (NULL, "\n")) != NULL)
    {
        p = (struct config_list *) malloc (sizeof (struct config_list));
        if (!p)
            return NULL;

        p->line = line;
        p->key = NULL;
        p->value = NULL;

        list_add_tail (&p->list, &head->list);
    }

    /*
     * Now split those lines in the key value pairs
     */

    config_tokenizeline (head);
    list_for_each (lh, &head->list)
    {
        config_tokenizeline (list_entry (lh, struct config_list, list));
    }

    return head;
}

void
config_tokenizeline (struct config_list *cfg)
{
    cfg->key = strtok (cfg->line, "\t");
    cfg->value = strtok (NULL, "\t");
}

char *
config_getstring (config_state * cfg, char *key)
{
    struct list_head *lh;

    if (strcmp (cfg->lines->key, key) == 0)
        return cfg->lines->value;

    list_for_each (lh, &cfg->lines->list)
    {
        struct config_list *item = list_entry (lh, struct config_list, list);
        if (strcmp (item->key, key) == 0)
            return item->value;
    }

    return NULL;
}

#endif
