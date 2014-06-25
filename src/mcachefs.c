/*
 mcachefs -- a caching filesystem to deal with slow filesystem accesses. This
 filesystem assumes that the backing store is fast and local! The filesystem is
 currently read only
 Copyright (C) 2004  Michael Still (mikal@stillhq.com)

 Heavily based on example code that is:
 Copyright (C) 2001  Miklos Szeredi (mszeredi@inf.bme.hu)

 This program can be distributed under the terms of the GNU GPL.
 See the file COPYING.
 */

#include "mcachefs.h"
#include "config.h"

char *mcachefs_mountpoint = NULL;
char *mcachefs_target = NULL;
char *mcachefs_backing = NULL;
char *mcachefs_metadir = NULL;
char *mcachefs_metafile = NULL;
char *mcachefs_journal = NULL;

int mcachefs_verbose = 0;
int mcachefs_transfer_default_threads_nb = 1;

FILE *mcachefs_log_fd = NULL;


#ifndef __MCACHEFS_USES_SYSLOG
void
setlogfile (const char *mountpoint)
{
    char log_file[PATH_MAX];
    char *log_file_key, *c;

    log_file_key = strdup (mountpoint);
    for (c = log_file_key; *c; c++)
        if (*c == '/')
            *c = '_';

    sprintf (log_file, "/var/log/mcachefs/mcachefs.log.%s", log_file_key);

    mcachefs_log_fd = fopen (log_file, "w+");

    if (mcachefs_log_fd == NULL)
    {
        fprintf (stderr, "Could not open log file '%s'\n", log_file);
        exit (-1);
    }
}
#endif

int
config_getnbthreads (config_state * cfg, const char *mountpoint,
                     const char *value)
{
    static const int keylen = 4096;
    char key[keylen];
    char *val;
    int ival;

    snprintf (key, keylen, "%s/%s", mountpoint, value);
    val = config_getstring (cfg, key);
    if (val == NULL)
        return mcachefs_transfer_default_threads_nb;
    ival = atoi (val);

    if (ival == 0 || ival >= MCACHEFS_TRANSFER_THREADS_MAX_NUMBER)
        return mcachefs_transfer_default_threads_nb;
    else
        return ival;
}

int
main (int argc, char *argv[])
{
    config_state *cfg;
    char *key, *val;
    int keylen;
    struct mcachefs_metadata_t *mdata_root;

    /*
     * Default values
     */

    mcachefs_log_fd = stderr;

    printf ("mcachefs " __MCACHEFS_VERSION__ " starting up...\n");

    if (argc == 1 || argv[1][0] == '-')
    {
        fprintf (stderr,
                 "\tError : first argument shall be the the mcachefs_mountpoint !\n");
        exit (2);
    }

    if (argc > 2 && strcmp (argv[1], "none") == 0)
    {
        mcachefs_mountpoint = argv[2];
        argv[1] = argv[0];
        argv++;
        argc--;
    }
    else
    {
        mcachefs_mountpoint = argv[1];
    }

#ifdef __MCACHEFS_USES_SYSLOG
    openlog ("mcachefs", LOG_CONS | LOG_PID, LOG_USER);
    setlogmask (LOG_UPTO (LOG_INFO));
#else
    setlogfile (mcachefs_mountpoint);
#endif

    Info ("mcachefs " __MCACHEFS_VERSION__ " starting up...\n");

    cfg = config_open ("mcachefs");
    if (!cfg)
    {
        printf ("Couldn't open config file\n");
        return 2;
    }

    keylen = strlen (argv[1]) + 256;
    key = (char *) malloc (keylen);
    if (!key)
    {
        perror ("Couldn't allocate memory for key value");
        exit (2);
    }

    snprintf (key, keylen, "%s/target", mcachefs_mountpoint);
    mcachefs_target = config_getstring (cfg, key);
    if (!mcachefs_target)
    {
        fprintf (stderr, "No 'target' value for mount point '%s'\n",
                 mcachefs_mountpoint);
        exit (-1);
    }

    snprintf (key, keylen, "%s/backing", mcachefs_mountpoint);
    mcachefs_backing = config_getstring (cfg, key);
    if (!mcachefs_backing)
    {
        fprintf (stderr,
                 "No 'backing' value for mount point '%s'\n",
                 mcachefs_mountpoint);
        exit (-1);
    }

    snprintf (key, keylen, "%s/metafile", mcachefs_mountpoint);
    mcachefs_metafile = config_getstring (cfg, key);
    if (!mcachefs_metafile)
    {
        fprintf (stderr,
                 "No 'metafile' value for mount point '%s'\n",
                 mcachefs_mountpoint);
        exit (-1);
    }

    snprintf (key, keylen, "%s/journal", mcachefs_mountpoint);
    mcachefs_journal = config_getstring (cfg, key);
    if (!mcachefs_journal)
    {
        fprintf (stderr,
                 "No 'journal' value for mount point '%s'\n",
                 mcachefs_mountpoint);
        exit (-1);
    }

    snprintf (key, keylen, "%s/verbose", mcachefs_mountpoint);
    val = config_getstring (cfg, key);
    if (val)
        mcachefs_verbose = atoi (val);

    mcachefs_transfer_default_threads_nb =
        config_getnbthreads (cfg, mcachefs_mountpoint, "threads");
    mcachefs_transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_BACKUP] =
        config_getnbthreads (cfg, mcachefs_mountpoint, "backup_threads");
    mcachefs_transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_WRITEBACK] =
        config_getnbthreads (cfg, mcachefs_mountpoint, "writeback_threads");
    mcachefs_transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_METADATA] =
        config_getnbthreads (cfg, mcachefs_mountpoint, "metadata_threads");

    Info ("Configured with following settings :\n");
    Info ("  target = %s\n", mcachefs_target);
    Info ("  backing = %s\n", mcachefs_backing);
    Info ("  metafile = %s\n", mcachefs_metafile);
    Info ("  journal = %s\n", mcachefs_journal);
    Info ("  verbosity = %d\n", mcachefs_verbose);
    Info ("  threads :\n");
    Info ("    backup    : %d\n",
          mcachefs_transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_BACKUP]);
    Info ("    writeback : %d\n",
          mcachefs_transfer_threads_type_nb
          [MCACHEFS_TRANSFER_TYPE_WRITEBACK]);
    Info ("    metadata  : %d\n",
          mcachefs_transfer_threads_type_nb[MCACHEFS_TRANSFER_TYPE_METADATA]);

    mcachefs_file_timeslice_init_variables ();

    mcachefs_setstate (MCACHEFS_STATE_NORMAL);
    mcachefs_setwrstate (MCACHEFS_WRSTATE_CACHE);

    mcachefs_metadata_open ();

    mdata_root = mcachefs_metadata_find ("/");
    if (!mdata_root)
    {
        Err ("Could not get metadata for root folder.\n");
        exit (-1);
    }

    memcpy (&mcachefs_target_stat, &(mdata_root->st), sizeof (struct stat));
    mcachefs_metadata_release (mdata_root);

#ifdef MCACHEFS_DISABLE_WRITE
    Info ("Serving read-only !\n");
#else
    Info ("Serving read-write !\n");
#endif

    void *user_data = NULL;
    fuse_main (argc, argv, &mcachefs_oper, user_data);

    Info ("Serving finished !\n");
    return 0;
}
