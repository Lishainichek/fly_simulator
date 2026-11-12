#define _GNU_SOURCE
#include "process_ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int instance_valid(const char *instance) {
    size_t length = strlen(instance);
    if (!length || length > 24) return 0;
    for (size_t i = 0; i < length; ++i)
        if (!((instance[i] >= '0' && instance[i] <= '9') ||
              (instance[i] >= 'A' && instance[i] <= 'Z') ||
              (instance[i] >= 'a' && instance[i] <= 'z') ||
               instance[i] == '_')) return 0;
    return 1;
}

static void name_shm(char *buffer, size_t size, const char *instance) {
    snprintf(buffer, size, "/dd_%s_state", instance);
}
static void name_sem(char *buffer, size_t size, const char *instance,
                     const char *kind, int role) {
    if (role >= 0) snprintf(buffer, size, "/dd_%s_%s%d", instance, kind, role);
    else snprintf(buffer, size, "/dd_%s_%s", instance, kind);
}
int sem_wait_stop(sem_t *sem, const volatile sig_atomic_t *stop, int max_seconds) {
    int waited = 0;
    while (!stop || !*stop) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        ++deadline.tv_sec;
        if (sem_timedwait(sem, &deadline) == 0) return 1;
        if (errno != EINTR && errno != ETIMEDOUT) return 0;
        if (max_seconds > 0 && ++waited >= max_seconds) return 0;
    }
    return 0;
}

static void init_ipc(ShmIPC *ipc, const char *instance) {
    memset(ipc, 0, sizeof(*ipc));
    ipc->fd = -1;
    snprintf(ipc->instance, sizeof(ipc->instance), "%s", instance);
}

void shmipc_unlink(const char *instance) {
    char name[96];
    name_shm(name, sizeof(name), instance); shm_unlink(name);
    name_sem(name, sizeof(name), instance, "lock", -1); sem_unlink(name);
    for (int i = 0; i < DELIVERY_ROLES; ++i) {
        name_sem(name, sizeof(name), instance, "start", i); sem_unlink(name);
        name_sem(name, sizeof(name), instance, "done", i); sem_unlink(name);
        name_sem(name, sizeof(name), instance, "ready", i); sem_unlink(name);
    }
}

static int open_named_sems(ShmIPC *ipc, int create) {
    char name[96];
    int flags = create ? (O_CREAT | O_EXCL) : 0;
    name_sem(name, sizeof(name), ipc->instance, "lock", -1);
    ipc->lock = sem_open(name, flags, 0600, 1);
    if (ipc->lock == SEM_FAILED) { ipc->lock = NULL; return 0; }
    for (int i = 0; i < DELIVERY_ROLES; ++i) {
        name_sem(name, sizeof(name), ipc->instance, "start", i);
        ipc->start[i] = sem_open(name, flags, 0600, 0);
        if (ipc->start[i] == SEM_FAILED) { ipc->start[i] = NULL; return 0; }
        name_sem(name, sizeof(name), ipc->instance, "done", i);
        ipc->done[i] = sem_open(name, flags, 0600, 0);
        if (ipc->done[i] == SEM_FAILED) { ipc->done[i] = NULL; return 0; }
        name_sem(name, sizeof(name), ipc->instance, "ready", i);
        ipc->ready[i] = sem_open(name, flags, 0600, 0);
        if (ipc->ready[i] == SEM_FAILED) { ipc->ready[i] = NULL; return 0; }
    }
    return 1;
}

int shmipc_create(ShmIPC *ipc, const char *instance,
                  size_t data_bytes, int named, int external) {
    if (!instance_valid(instance) || data_bytes > 32u * 1024u * 1024u) return 0;
    init_ipc(ipc, instance);
    ipc->named = named;
    ipc->creator = 1;
    ipc->map_bytes = sizeof(SharedControl) + data_bytes;
    char name[96];
    if (named) {
        name_shm(name, sizeof(name), instance);
        ipc->fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (ipc->fd == -1) { perror("shm_open: instance already exists?"); return 0; }
        if (ftruncate(ipc->fd, (off_t)ipc->map_bytes) == -1) goto failed;
        ipc->shared = mmap(NULL, ipc->map_bytes, PROT_READ | PROT_WRITE,
                            MAP_SHARED, ipc->fd, 0);
    } else {
        ipc->shared = mmap(NULL, ipc->map_bytes, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    }
    if (ipc->shared == MAP_FAILED) { ipc->shared = NULL; goto failed; }
    memset(ipc->shared, 0, ipc->map_bytes);
    ipc->shared->bytes = (int)data_bytes;
    if (named) {
        if (!open_named_sems(ipc, 1)) goto failed;
    } else {
        if (sem_init(&ipc->shared->anonymous_lock, 1, 1) != 0) goto failed;
        ipc->lock = &ipc->shared->anonymous_lock;
        for (int i = 0; i < DELIVERY_ROLES; ++i) {
            if (sem_init(&ipc->shared->anonymous_start[i], 1, 0) != 0 ||
                sem_init(&ipc->shared->anonymous_done[i], 1, 0) != 0) goto failed;
            ipc->start[i] = &ipc->shared->anonymous_start[i];
            ipc->done[i] = &ipc->shared->anonymous_done[i];
        }
    }
    (void)external;
    return 1;
failed:
    shmipc_close(ipc);
    if (named) shmipc_unlink(instance);
    return 0;
}

int shmipc_connect(ShmIPC *ipc, const char *instance) {
    if (!instance_valid(instance)) return 0;
    init_ipc(ipc, instance);
    ipc->named = 1;
    char name[96];
    name_shm(name, sizeof(name), instance);
    ipc->fd = shm_open(name, O_RDWR, 0);
    if (ipc->fd == -1) return 0;
    struct stat st;
    if (fstat(ipc->fd, &st) == -1 || st.st_size < (off_t)sizeof(SharedControl)) goto fail;
    ipc->map_bytes = (size_t)st.st_size;
    ipc->shared = mmap(NULL, ipc->map_bytes, PROT_READ | PROT_WRITE,
                       MAP_SHARED, ipc->fd, 0);
    if (ipc->shared == MAP_FAILED) { ipc->shared = NULL; goto fail; }
    if (ipc->shared->bytes <= 0 ||
        sizeof(SharedControl) + (size_t)ipc->shared->bytes > ipc->map_bytes) goto fail;
    if (!open_named_sems(ipc, 0)) goto fail;
    return 1;
fail:
    shmipc_close(ipc);
    return 0;
}

void shmipc_close(ShmIPC *ipc) {
    if (ipc->named) {
        if (ipc->lock) sem_close(ipc->lock);
        for (int i = 0; i < DELIVERY_ROLES; ++i) {
            if (ipc->start[i]) sem_close(ipc->start[i]);
            if (ipc->done[i]) sem_close(ipc->done[i]);
            if (ipc->ready[i]) sem_close(ipc->ready[i]);
        }
    } else if (ipc->creator && ipc->shared) {
        sem_destroy(&ipc->shared->anonymous_lock);
        for (int i = 0; i < DELIVERY_ROLES; ++i) {
            if (ipc->start[i]) sem_destroy(ipc->start[i]);
            if (ipc->done[i]) sem_destroy(ipc->done[i]);
        }
    }
    if (ipc->shared) munmap(ipc->shared, ipc->map_bytes);
    if (ipc->fd != -1) close(ipc->fd);
}

