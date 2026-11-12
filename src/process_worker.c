#include "process_snapshot.h"
#include "process_ipc.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted = 0;
static void on_signal(int signum) { (void)signum; interrupted = 1; }

static int shm_worker(ShmIPC *ipc, Simulation *sim, int role) {
    if (sem_post(ipc->ready[role]) != 0) return 0;
    while (!interrupted) {
        if (!sem_wait_stop(ipc->start[role], &interrupted, 0)) return interrupted != 0;
        if (ipc->shared->quit) break;
        if (!sem_wait_stop(ipc->lock, &interrupted, 0)) return 0;
        int success = snapshot_decode(sim, ipc->shared->data, (size_t)ipc->shared->bytes);
        if (success) {
            simulation_step_role(sim, role);
            success = snapshot_encode(sim, ipc->shared->data, (size_t)ipc->shared->bytes);
        }
        fflush(stdout);
        sem_post(ipc->lock);
        if (!success || sem_post(ipc->done[role]) != 0) return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *transport = NULL, *role_name = NULL;
    const char *instance = "default", *scenario = "scenarios/demo.txt";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--transport") && i + 1 < argc) transport = argv[++i];
        else if (!strcmp(argv[i], "--role") && i + 1 < argc) role_name = argv[++i];
        else if (!strcmp(argv[i], "--instance") && i + 1 < argc) instance = argv[++i];
        else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) scenario = argv[++i];
        else {
            fprintf(stderr, "Usage: fly_process_worker --transport shm --role orders|dispatcher|drones|stations --instance ID --scenario FILE\n");
            return 2;
        }
    }
    int role = role_name ? process_role_id(role_name) : -1;
    if (!transport || strcmp(transport, "shm") || role < 0 || !instance_valid(instance)) return 2;
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    Scenario local;
    Simulation sim;
    if (!process_load(scenario, &local, &sim, 1)) return 1;
    int success = 0, connected = 0;
    for (int attempt = 0; attempt < 150 && !interrupted; ++attempt) {
        ShmIPC ipc;
        if (shmipc_connect(&ipc, instance)) {
            connected = 1;
            printf("[worker %s] pid=%ld attached via shared memory\n",
                   role_name, (long)getpid());
            success = shm_worker(&ipc, &sim, role);
            shmipc_close(&ipc);
            break;
        }
        process_sleep(100);
    }
    if (!connected && !interrupted)
        fprintf(stderr, "[worker %s] IPC namespace %s unavailable\n", role_name, instance);
    process_unload(&local, &sim);
    return (success || interrupted) ? 0 : 1;
}
