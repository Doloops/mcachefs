#include "mcachefs.h"
#include "mcachefs-journal.h"
#include "mcachefs-transfer.h"
#include "mcachefs-vops.h"

void
mcachefs_vops_cleanup_vops(struct mcachefs_file_t *mvops)
{
    if (mvops->contents)
    {
        Log("VOPS CLEANUP for %s, at %p\n", mvops->path, mvops->contents);
        free(mvops->contents);
        mvops->contents = NULL;
    }
    mvops->contents_size = 0;
    mvops->contents_alloced = 0;
}

typedef int (*proc_get_int) ();

typedef void (*proc_set_int) (int);

typedef const char *(*proc_get_string) ();

typedef void (*proc_set_string) (const char *);

typedef void (*proc_extern_build) (struct mcachefs_file_t * mvops);

struct mcachefs_vops_proc
{
    char *name;
    proc_get_int get_int;
    proc_set_int set_int;
    proc_get_string get_string;
    proc_set_string set_string;
    const char **int_string_map;
    proc_extern_build extern_build;
};

static const char *read_state_names[] =
    { "normal", "full", "handsup", "nocache", "quitting", NULL };

static const char *write_state_names[] = { "cache", "flush", "force", NULL };

void
mcachefs_vops_call_none()
{

}

typedef void (*proc_extern_call) ();

static const char *vops_action_names[] =
    { "none", "apply_journal", "flush_metadata", "cleanup_cache",
    "fill_cache_meta", NULL
};

static const proc_extern_call vops_action_calls[] =
    { &mcachefs_vops_call_none, &mcachefs_journal_apply,
    &mcachefs_metadata_flush, &mcachefs_cleanup_backing,
    &mcachefs_metadata_fill_meta, NULL
};

void mcachefs_call_action(int action);

int mcachefs_get_default_action();

struct mcachefs_vops_proc vops_procs[] = {
    {"read_state", &mcachefs_config_get_read_state,
     &mcachefs_config_set_read_state, NULL, NULL, read_state_names,
     NULL},
    {"write_state", &mcachefs_config_get_write_state,
     &mcachefs_config_set_write_state, NULL, NULL, write_state_names,
     NULL},
    {"file_thread_interval", &mcachefs_config_get_file_thread_interval,
     &mcachefs_config_set_file_thread_interval, NULL, NULL, NULL,
     NULL},
    {"file_ttl", &mcachefs_config_get_file_ttl,
     &mcachefs_config_set_file_ttl, NULL, NULL, NULL, NULL},
    {"metadata_map_ttl", &mcachefs_config_get_metadata_map_ttl,
     &mcachefs_config_set_metadata_map_ttl, NULL, NULL, NULL, NULL},
    {"transfer_max_rate", &mcachefs_config_get_transfer_max_rate,
     &mcachefs_config_set_transfer_max_rate, NULL, NULL,
     NULL, NULL},
    {"transfer", NULL, NULL, NULL, NULL, NULL,
     &mcachefs_transfer_dump},
    {"journal", NULL, NULL, NULL, NULL, NULL,
     &mcachefs_journal_dump},
    {"metadata", NULL, NULL, NULL, NULL, NULL,
     &mcachefs_metadata_dump},
    {"timeslices", NULL, NULL, NULL, NULL, NULL,
     &mcachefs_file_timeslices_dump},
    {"cache_prefix", NULL, NULL,
     &mcachefs_config_get_cache_prefix,
     &mcachefs_config_set_cache_prefix, NULL, NULL},
    {"action", &mcachefs_get_default_action, &mcachefs_call_action,
     NULL, NULL, vops_action_names, NULL},
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL}
};

const char **names = NULL;

const char **
mcachefs_vops_get_vops_list()
{
    if (!names)
    {
        int number_of_entries = sizeof(vops_procs)
            / sizeof(struct mcachefs_vops_proc) + 1;
        Log("Defining %d entries\n", number_of_entries);
        names = (const char **) malloc(sizeof(char *) * number_of_entries);

        int i;
        for (i = 0; vops_procs[i].name != NULL; i++)
        {
            Log("Defined vops : %s\n", vops_procs[i].name);
            names[i] = vops_procs[i].name;
        }
        names[i] = NULL;
    }
    return names;
}

struct mcachefs_vops_proc *
mcachefs_vops_proc_find(struct mcachefs_file_t *mvops)
{
    const char *file = &(mvops->path[11]);

    int i;
    for (i = 0; vops_procs[i].name != NULL; i++)
    {
        if (strcmp(vops_procs[i].name, file) == 0)
        {
            return &(vops_procs[i]);
        }
    }
    Err("Invalid vops proc file '%s'\n", file);
    return NULL;
}

void
vops_build_int_map(struct mcachefs_file_t *mvops,
                   struct mcachefs_vops_proc *proc)
{
    int value = proc->get_int();

    int valueindex = 0;
    for (; proc->int_string_map[valueindex] != NULL; valueindex++)
    {
        if (valueindex)
        {
            __VOPS_WRITE(mvops, " ");
        }
        if (value == valueindex)
        {
            __VOPS_WRITE(mvops, "[");
        }
        __VOPS_WRITE(mvops, "%s", proc->int_string_map[valueindex]);
        if (value == valueindex)
        {
            __VOPS_WRITE(mvops, "]");
        }
    }
    __VOPS_WRITE(mvops, "\n");
}

void
vops_build_int(struct mcachefs_file_t *mvops, struct mcachefs_vops_proc *proc)
{
    int value = proc->get_int();
    Log("Set value %d for vops=%s\n", value, mvops->path);
    __VOPS_WRITE(mvops, "%d\n", value);
}

void
vops_build_string(struct mcachefs_file_t *mvops,
                  struct mcachefs_vops_proc *proc)
{
    const char *value = proc->get_string();
    if (value != NULL)
    {
        __VOPS_WRITE(mvops, "%s\n", value);
    }
    else
    {
        __VOPS_WRITE(mvops, "\n");
    }
}

void
mcachefs_vops_build(struct mcachefs_file_t *mvops)
{
    if (mvops->contents)
    {
        Log("VOPS file '%s' already has data, skipping.\n", mvops->path);
        return;
    }

    struct mcachefs_vops_proc *proc = mcachefs_vops_proc_find(mvops);
    if (!proc)
    {
        return;
    }

    if (proc->get_int != NULL)
    {
        if (proc->int_string_map != NULL)
        {
            vops_build_int_map(mvops, proc);
        }
        else
        {
            vops_build_int(mvops, proc);
        }
        return;
    }
    if (proc->get_string != NULL)
    {
        vops_build_string(mvops, proc);
        return;
    }
    if (proc->extern_build != NULL)
    {
        proc->extern_build(mvops);
        return;
    }
}

int
vops_parse_int_raw_map(struct mcachefs_file_t *mvops, const char *values[])
{
    int iter;
    off_t t;

    for (t = 0; t < mvops->contents_alloced; t++)
    {
        if (mvops->contents[t] == '\n')
        {
            mvops->contents[t] = '\0';
            break;
        }
    }

    for (iter = 0; values[iter]; iter++)
    {
        if (strcmp(mvops->contents, values[iter]) == 0)
            return iter;

    }
    return -1;
}

void
vops_parse_int_map(struct mcachefs_file_t *mvops,
                   struct mcachefs_vops_proc *proc)
{
    int value = vops_parse_int_raw_map(mvops, proc->int_string_map);

    if (value != -1)
    {
        Log("Setting value %d for file=%s\n", value, mvops->path);
        proc->set_int(value);
    }
    else
    {
        Err("Invalid value %s for file %s\n", mvops->contents, mvops->path);
    }
}

void
vops_cleanup_first_line(struct mcachefs_file_t *mvops)
{
    off_t t;
    for (t = 0; t < mvops->contents_alloced; t++)
    {
        if (mvops->contents[t] == '\n')
        {
            mvops->contents[t] = '\0';
            break;
        }
    }
}

void
vops_parse_int(struct mcachefs_file_t *mvops, struct mcachefs_vops_proc *proc)
{
    vops_cleanup_first_line(mvops);

    int result = atoi(mvops->contents);
    proc->set_int(result);
}

void
vops_parse_string(struct mcachefs_file_t *mvops,
                  struct mcachefs_vops_proc *proc)
{
    vops_cleanup_first_line(mvops);
    proc->set_string(mvops->contents);
}

void
mcachefs_vops_parse(struct mcachefs_file_t *mvops)
{
    struct mcachefs_vops_proc *proc = mcachefs_vops_proc_find(mvops);
    if (!proc)
    {
        return;
    }

    if (proc->set_int != NULL)
    {
        if (proc->int_string_map != NULL)
        {
            vops_parse_int_map(mvops, proc);
        }
        else
        {
            vops_parse_int(mvops, proc);
        }
        return;
    }
    if (proc->set_string != NULL)
    {
        vops_parse_string(mvops, proc);
        return;
    }
}

void
mcachefs_call_action(int action)
{
    Log("Calling action #%d : %s\n", action, vops_action_names[action]);
    proc_extern_call call = vops_action_calls[action];

    if (call != NULL)
    {
        call();
    }
}

int
mcachefs_get_default_action()
{
    return 0;
}
