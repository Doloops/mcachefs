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

FILE *LOG_FD;

int
main(int argc, char *argv[])
{
    LOG_FD = stderr;

    struct mcachefs_config *config;

    printf("mcachefs " __MCACHEFS_VERSION__ " starting up...\n");

    config = mcachefs_parse_config(argc, argv);

    if (config == NULL)
    {
        return 1;
    }

    mcachefs_set_current_config(config);

    mcachefs_config_run_pre_mount_cmd();

    mcachefs_file_timeslice_init_variables();


    mcachefs_metadata_lock();
    mcachefs_metadata_open();
    mcachefs_metadata_unlock();

    mcachefs_metadata_populate_vops();

#ifdef MCACHEFS_DISABLE_WRITE
    Info("Serving read-only !\n");
#else
    Info("Serving read-write !\n");
#endif

    void *user_data = NULL;
    int fuse_argc = config->fuse_args.argc;
    char **fuse_argv = config->fuse_args.argv;
    fuse_main(fuse_argc, fuse_argv, &mcachefs_oper, user_data);

    Info("Serving finished !\n");
    return 0;
}
