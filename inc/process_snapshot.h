#ifndef PROCESS_SNAPSHOT_H
#define PROCESS_SNAPSHOT_H

#include "simulation.h"
#include "scenario.h"
#include <stddef.h>
#include <stdint.h>

#define DELIVERY_IPC_MAGIC 0x44524f4eu
#define DELIVERY_ROLES 4

typedef struct {
    int order_id;
    int station_id;
    int stage;
    int replan_required;
} FlatDelivery;

typedef struct {
    uint32_t magic;
    uint32_t total_size;
    int nplanes;
    int norders;
    int nstations;
    int time;
    int max_time;
    int verbose;
    Weather weather;
    int stop_requested;
    long long total_distance;
} SnapshotHeader;

size_t snapshot_size(const Simulation *sim);
int snapshot_encode(const Simulation *sim, void *buffer, size_t capacity);
int snapshot_decode(Simulation *sim, const void *buffer, size_t capacity);
void process_sleep(int ms);
int process_load(const char *path, Scenario *scenario, Simulation *sim, int verbose);
void process_unload(Scenario *scenario, Simulation *sim);
const char *process_role_name(int role);
int process_role_id(const char *text);

#endif
