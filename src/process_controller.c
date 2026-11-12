#include "process_snapshot.h"
#include "process_ipc.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef DEFAULT_MODE
#define DEFAULT_MODE "fork-unnamed"
#endif

static volatile sig_atomic_t interrupted = 0;
static void on_signal(int signal_number) {
    (void)signal_number;
    interrupted = 1;
}

static void print_usage(const char *program) {
    printf("Usage: %s --mode MODE [--instance ID] [--scenario FILE] [--quiet] [--delay MS] [--max-time N] [--weather-interval N]\n", program);
    puts("MODE: fork-unnamed (4-5), fork-named (6), external-shm (7-8)");
    puts("For external-shm start four ./fly_process_worker instances (orders, dispatcher, drones, stations).");
    puts("To clean abandoned IPC objects: --cleanup --instance ID");
}

static int parse_nonnegative(const char *s, int *result) {
    char *end = NULL;
    errno = 0;
    long number = strtol(s, &end, 10);
    if (!*s || errno || !end || *end || number < 0 || number > INT_MAX) return 0;
    *result = (int)number;
    return 1;
}

static int read_options(int argc, char **argv,
                        const char **mode, const char **instance,
                        const char **scenario_path, int *quiet, int *delay,
                        int *max_time, int *weather, int *cleanup) {
    *mode = DEFAULT_MODE;
    *instance = "default";
    *scenario_path = "scenarios/demo.txt";
    *quiet = *delay = *cleanup = 0;
    *max_time = *weather = -1;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) return 2;
        if (!strcmp(argv[i], "--cleanup")) *cleanup = 1;
        else if (!strcmp(argv[i], "--quiet")) *quiet = 1;
        else if (!strcmp(argv[i], "--auto")) {}
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) *mode = argv[++i];
        else if (!strcmp(argv[i], "--instance") && i + 1 < argc) *instance = argv[++i];
        else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) *scenario_path = argv[++i];
        else if (!strcmp(argv[i], "--delay") && i + 1 < argc &&
                 parse_nonnegative(argv[++i], delay) && *delay <= 60000) {}
        else if (!strcmp(argv[i], "--max-time") && i + 1 < argc &&
                 parse_nonnegative(argv[++i], max_time)) {}
        else if (!strcmp(argv[i], "--weather-interval") && i + 1 < argc &&
                 parse_nonnegative(argv[++i], weather)) {}
        else return 0;
    }
    return 1;
}

static int phase_shm(ShmIPC *ipc, Simulation *sim, int role) {
    if (!snapshot_encode(sim, ipc->shared->data, (size_t)ipc->shared->bytes)) return 0;
    if (sem_post(ipc->start[role]) != 0) return 0;
    if (!sem_wait_stop(ipc->done[role], &interrupted, 15)) {
        fprintf(stderr, "Worker %s did not acknowledge phase\n", process_role_name(role));
        return 0;
    }
    return snapshot_decode(sim, ipc->shared->data, (size_t)ipc->shared->bytes);
}

static int run_fork_worker(ShmIPC *ipc, Simulation *sim, int role) {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    for (;;) {
        if (sem_wait(ipc->start[role]) == -1) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (ipc->shared->quit) return 1;
        while (sem_wait(ipc->lock) == -1) {
            if (errno != EINTR) return 0;
        }
        int success = snapshot_decode(sim, ipc->shared->data,
                                      (size_t)ipc->shared->bytes);
        if (success) {
            simulation_step_role(sim, role);
            success = snapshot_encode(sim, ipc->shared->data,
                                      (size_t)ipc->shared->bytes);
        }
        fflush(stdout);
        sem_post(ipc->lock);
        if (!success || sem_post(ipc->done[role]) != 0) return 0;
    }
}

static void shutdown_fork(ShmIPC *ipc, pid_t children[DELIVERY_ROLES]) {
    ipc->shared->quit = 1;
    for (int role = 0; role < DELIVERY_ROLES; ++role)
        sem_post(ipc->start[role]);
    for (int role = 0; role < DELIVERY_ROLES; ++role)
        if (children[role] > 0) {
            int status;
            while (waitpid(children[role], &status, 0) == -1 && errno == EINTR) {}
        }
}

static int run_shm(ShmIPC *ipc, Simulation *sim, int external, int delay) {
    pid_t children[DELIVERY_ROLES] = {0};
    if (!snapshot_encode(sim, ipc->shared->data, (size_t)ipc->shared->bytes)) return 0;
    if (external) {
        for (int i = 0; i < DELIVERY_ROLES; ++i) {
            if (!sem_wait_stop(ipc->ready[i], &interrupted, 12)) {
                fprintf(stderr, "Timed out waiting for external %s worker\n",
                        process_role_name(i));
                ipc->shared->quit = 1;
                for (int j = 0; j < DELIVERY_ROLES; ++j) sem_post(ipc->start[j]);
                return 0;
            }
        }
    } else {
        for (int i = 0; i < DELIVERY_ROLES; ++i) {
            children[i] = fork();
            if (children[i] == 0) _exit(run_fork_worker(ipc, sim, i) ? 0 : 1);
            if (children[i] == -1) {
                perror("fork");
                shutdown_fork(ipc, children);
                return 0;
            }
        }
    }
    printf("[processes] Four %s workers started, controller pid=%ld\n",
           external ? "independent" : "forked", (long)getpid());
    int success = 1;
    while (!interrupted && (sim->max_time == 0 || sim->current_time < sim->max_time) &&
           !simulation_all_done(sim)) {
        simulation_step_weather(sim);
        for (int role = 0; role < DELIVERY_ROLES; ++role) {
            if (interrupted || !phase_shm(ipc, sim, role)) {
                success = 0;
                break;
            }
        }
        if (!success) break;
        ++sim->current_time;
        process_sleep(delay);
    }
    if (success) simulation_finish_time_limit(sim);
    if (external) {
        ipc->shared->quit = 1;
        for (int i = 0; i < DELIVERY_ROLES; ++i) sem_post(ipc->start[i]);
    } else shutdown_fork(ipc, children);
    return success && !interrupted;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *mode, *instance, *scenario_path;
    int quiet, delay, max_time, weather, cleanup;
    int parsed = read_options(argc, argv, &mode, &instance, &scenario_path,
                              &quiet, &delay, &max_time, &weather, &cleanup);
    if (parsed == 2) { print_usage(argv[0]); return 0; }
    if (!parsed || !instance_valid(instance)) {
        print_usage(argv[0]); return 2;
    }
    if (cleanup) {
        shmipc_unlink(instance);
        printf("Cleaned IPC namespace: %s\n", instance);
        return 0;
    }
    int external_shm = !strcmp(mode, "external-shm");
    int fork_named = !strcmp(mode, "fork-named");
    int fork_unnamed = !strcmp(mode, "fork-unnamed");
    if (!external_shm && !fork_named && !fork_unnamed) {
        print_usage(argv[0]); return 2;
    }
    Scenario scenario;
    Simulation sim;
    if (!process_load(scenario_path, &scenario, &sim, !quiet)) return 1;
    if (max_time >= 0) sim.max_time = max_time;
    if (weather >= 0) sim.weather = create_weather(weather);
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    printf("=== DRONE DELIVERY: %s ===\n", mode);
    printf("Scenario: %s, drones=%d, orders=%d, stations=%d\n",
           scenario_path, sim.dispatcher.plane_count, sim.order_count,
           sim.dispatcher.station_count);
    ShmIPC ipc;
    int named = !fork_unnamed;
    if (!shmipc_create(&ipc, instance, snapshot_size(&sim), named, external_shm)) {
        process_unload(&scenario, &sim);
        return 1;
    }
    int okay = run_shm(&ipc, &sim, external_shm, delay);
    shmipc_close(&ipc);
    if (named) shmipc_unlink(instance);
    if (interrupted) puts("Simulation interrupted by user (SIGINT/SIGTERM).");
    simulation_print_statistics(&sim);
    process_unload(&scenario, &sim);
    return okay ? 0 : 1;
}
