#include "mcachefs-mutex.h"
#include "mcachefs-log.h"
#include "mcachefs.h"

#if 0
#define Log_Mutex Log
#else
#define Log_Mutex(...)
#endif

#ifdef  __MCACHEFS_MUTEX_DEBUG
#define MCACHEFS_MUTEX_INITIALIZER { .mutex = PTHREAD_MUTEX_INITIALIZER, .owner = 0, .context = NULL }
#else
#define MCACHEFS_MUTEX_INITIALIZER { .mutex = PTHREAD_MUTEX_INITIALIZER }
#endif
struct mcachefs_mutex_t mcachefs_metadata_mutex = MCACHEFS_MUTEX_INITIALIZER;
struct mcachefs_mutex_t mcachefs_file_mutex = MCACHEFS_MUTEX_INITIALIZER;
struct mcachefs_mutex_t mcachefs_journal_mutex = MCACHEFS_MUTEX_INITIALIZER;
struct mcachefs_mutex_t mcachefs_transfer_mutex = MCACHEFS_MUTEX_INITIALIZER;

void
mcachefs_mutex_init(struct mcachefs_mutex_t *mutex)
{
    int res;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    if ((res = pthread_mutex_init(&(mutex->mutex), &attr)) != 0)
    {
        Bug("Could not init mutex : err=%d:%s\n", res, strerror(res));
    }
}

void
mcachefs_mutex_destroy(struct mcachefs_mutex_t *mutex, const char *name)
{
    int res;
    if ((res = pthread_mutex_destroy(&(mutex->mutex))) != 0)
    {
#ifdef  __MCACHEFS_MUTEX_DEBUG
        Err("Could not destroy mutex '%s' : err=%d:%s, locked at %s by %lx\n", name, res, strerror(res), mutex->context, (unsigned long) mutex->owner);
#else
        Err("Could not destroy mutex '%s' : err=%d:%s\n", name, res, strerror(res));
#endif
    }
}

#ifdef  __MCACHEFS_MUTEX_DEBUG

void
mcachefs_mutex_lock(struct mcachefs_mutex_t *mutex, const char *name, const char *context)
{
    int res;
    pthread_t me = pthread_self();

    Log_Mutex("MUTEX LOCKING mutex %s from %s\n", name, context);
    res = pthread_mutex_lock(&(mutex->mutex));
    if (res == 0)
    {
        Log_Mutex("MUTEX LOCKED mutex %s from %s\n", name, context);
        mutex->owner = me;
        mutex->context = context;
        return;
    }
    Bug("Could not lock mutex '%s' at %s by %lx : err=%d:%s, locked at %s by %lx\n", name, context, (unsigned long) me,
        res, strerror(res), mutex->context, (unsigned long) mutex->owner);
}

void
mcachefs_mutex_check_unlocked(struct mcachefs_mutex_t *mutex, const char *name, const char *context)
{
    pthread_t me = pthread_self();
    if (mutex->owner == me)
    {
        Bug("Mutex '%s' locked by myself %lx at %s : locked by %s at %lx\n", name, (unsigned long) me, context, mutex->context, (unsigned long) mutex->owner);
    }
}

void
mcachefs_mutex_check_locked(struct mcachefs_mutex_t *mutex, const char *name, const char *context)
{
    pthread_t me = pthread_self();
    if (mutex->owner != me)
    {
        Bug("Mutex '%s' not locked by myself %lx at %s : locked by %s at %lx\n", name, (unsigned long) me, context,
            mutex->context, (unsigned long) mutex->owner);
    }
}

void
mcachefs_mutex_unlock(struct mcachefs_mutex_t *mutex, const char *name, const char *context)
{
    Log_Mutex("MUTEX UNLOCK mutex %s from %s\n", name, context);
    int res;
    pthread_t me = pthread_self();

    if (mutex->owner != me)
    {
        Bug("Mutex '%s' was not held by myself ! locked by %lx at %s, unlocked by %lx at %s\n", name,
            (unsigned long) mutex->owner, mutex->context, (unsigned long) me, context);
    }

    mutex->owner = 0;
    mutex->context = NULL;
    res = pthread_mutex_unlock(&(mutex->mutex));
    if (res == 0)
    {
        return;
    }
    Bug("Could not unlock mutex '%s' by %lx at %s : err=%d:%s, locked by %lx at %s\n", name, (unsigned long) me,
        context, res, strerror(res), (unsigned long) mutex->owner, mutex->context);

}

#endif
