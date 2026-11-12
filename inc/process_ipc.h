#ifndef PROCESS_IPC_H
#define PROCESS_IPC_H

#include "process_snapshot.h"
#include <semaphore.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct {
    int quit;
    int bytes;
    int ready[DELIVERY_ROLES];
    sem_t anonymous_lock;
    sem_t anonymous_start[DELIVERY_ROLES];
    sem_t anonymous_done[DELIVERY_ROLES];
    unsigned char data[];
} SharedControl;

typedef struct {
    int named;
    int creator;
    int fd;
    char instance[32];
    size_t map_bytes;
    SharedControl *shared;
    sem_t *lock;
    sem_t *start[DELIVERY_ROLES];
    sem_t *done[DELIVERY_ROLES];
    sem_t *ready[DELIVERY_ROLES];
} ShmIPC;

int instance_valid(const char *instance);
int shmipc_create(ShmIPC *ipc, const char *instance,
                  size_t data_bytes, int named, int external);
int shmipc_connect(ShmIPC *ipc, const char *instance);
void shmipc_close(ShmIPC *ipc);
void shmipc_unlink(const char *instance);
int sem_wait_stop(sem_t *semaphore, const volatile sig_atomic_t *stop, int max_seconds);

#endif
