#include "scenario.h"
#include "simulation.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t *interrupt_flag = NULL;

static void on_interrupt(int signum) {
    (void)signum;
    if (interrupt_flag) *interrupt_flag = 1;
}

static void print_menu(void) {
    puts("\n========== DRONE DELIVERY SIMULATOR ==========");
    puts("1. Start simulation");
    puts("2. Show map");
    puts("3. Show drones, stations and orders");
    puts("4. Exit");
    printf("Choose an option: ");
    fflush(stdout);
}

static const char *plane_status_name(PlaneStatus status) {
    static const char *const labels[] = {
        "IDLE", "DELIVERING", "RETURNING", "CHARGING", "WAITING"
    };
    return status >= PLANE_IDLE && status <= PLANE_WAITING
        ? labels[status] : "UNKNOWN";
}

static void print_entities(const Simulation *sim) {
    puts("\n=== DRONES ===");
    for (int i = 0; i < sim->dispatcher.plane_count; ++i) {
        const Plane *p = &sim->dispatcher.planes[i];
        printf("Drone #%d: position=(%d,%d), energy=%d/%d, payload=%d, "
               "consumption=%d, state=%s\n", p->id, p->position.x,
               p->position.y, p->energy, p->max_energy, p->capacity,
               p->consumption, plane_status_name(p->status));
    }
    puts("\n=== CHARGING STATIONS ===");
    for (int i = 0; i < sim->dispatcher.station_count; ++i) {
        const Station *s = &sim->dispatcher.stations[i];
        printf("Station #%d: position=(%d,%d), places=%d, speed=%d, "
               "charging=%d\n", s->id, s->position.x,
               s->position.y, s->capacity, s->charging_speed,
               s->charging_count);
    }
    puts("\n=== ORDERS ===");
    static const char *states[] = {
        "PENDING", "ASSIGNED", "IN_DELIVERY", "DELIVERED", "FAILED"
    };
    for (int i = 0; i < sim->order_count; ++i) {
        const Order *o = &sim->orders[i];
        printf("Order #%d: destination=(%d,%d), weight=%d, arrival=%d, "
               "deadline=%d, state=%s\n", o->id, o->destination.x,
               o->destination.y, o->weight, o->created_at,
               o->deadline, states[o->status]);
    }
}

static int parse_int(const char *text, int *result) {
    errno = 0;
    char *end;
    long value = strtol(text, &end, 10);
    if (errno || !text[0] || *end || value < 0 || value > INT_MAX) return 0;
    *result = (int)value;
    return 1;
}

static void usage(const char *name) {
    printf("Usage: %s [--scenario FILE] [--auto] [--quiet] "
           "[--delay MS] [--max-time T] [--weather-interval T]\n"
           "  No options: interactive menu using scenarios/demo.txt\n"
           "  --auto:     run without prompting and print final statistics\n"
           "  --quiet:    suppress event logs (statistics remain visible)\n", name);
}

int main(int argc, char **argv) {
    const char *scenario_path = "scenarios/demo.txt";
    int auto_run = 0, quiet = 0, delay = 0;
    int max_time_override = -1, weather_override = -1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--auto") == 0) auto_run = 1;
        else if (strcmp(argv[i], "--quiet") == 0) quiet = 1;
        else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc)
            scenario_path = argv[++i];
        else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc &&
                 parse_int(argv[++i], &delay) && delay <= 60000) {}
        else if (strcmp(argv[i], "--max-time") == 0 && i + 1 < argc &&
                 parse_int(argv[++i], &max_time_override)) {}
        else if (strcmp(argv[i], "--weather-interval") == 0 && i + 1 < argc &&
                 parse_int(argv[++i], &weather_override)) {}
        else {
            fprintf(stderr, "Invalid option: %s\n", argv[i]);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    Scenario scenario;
    char error[256];
    if (!scenario_load(&scenario, scenario_path, error, sizeof(error))) {
        fprintf(stderr, "Configuration error: %s\n", error);
        return EXIT_FAILURE;
    }
    Dispatcher dispatcher = create_dispatcher(&scenario.map, scenario.warehouse,
                                              scenario.planes, scenario.plane_count,
                                              scenario.stations, scenario.station_count);
    Simulation sim = create_simulation(dispatcher, scenario.orders,
                                        scenario.order_count, scenario.max_time);
    sim.weather = create_weather(weather_override >= 0 ? weather_override
                                                       : scenario.weather_interval);
    if (max_time_override >= 0) sim.max_time = max_time_override;
    sim.delay_ms = delay;
    sim.verbose = !quiet;
    int result = EXIT_SUCCESS;
    struct sigaction action = {0};
    action.sa_handler = on_interrupt;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    interrupt_flag = &sim.stop_requested;

    if (auto_run) {
        if (!simulation_run(&sim)) {
            fprintf(stderr, "Simulation failed to start or execute\n");
            result = EXIT_FAILURE;
        }
        if (sim.stop_requested) puts("Simulation interrupted by user.");
        simulation_print_statistics(&sim);
    } else {
        int started = 0;
        char input[64];
        for (;;) {
            print_menu();
            if (!fgets(input, sizeof(input), stdin)) break;
            input[strcspn(input, "\r\n")] = 0;
            int choice;
            if (!parse_int(input, &choice)) {
                puts("Invalid input");
                continue;
            }
            if (choice == 1) {
                if (started) { puts("Simulation has already run."); continue; }
                started = 1;
                puts("\n=== SIMULATION START ===");
                if (!simulation_run(&sim)) {
                    fprintf(stderr, "Simulation failed\n");
                    result = EXIT_FAILURE;
                    break;
                }
                puts("\n=== SIMULATION FINISHED ===");
                if (sim.stop_requested) puts("Simulation interrupted by user.");
                simulation_print_statistics(&sim);
            } else if (choice == 2) {
                print_map(&scenario.map);
            } else if (choice == 3) {
                print_entities(&sim);
            } else if (choice == 4) {
                break;
            } else {
                puts("Unknown option");
            }
        }
    }
    interrupt_flag = NULL;
    destroy_simulation(&sim);
    scenario_destroy(&scenario);
    return result;
}
