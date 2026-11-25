#include "process_snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

size_t snapshot_size(const Simulation *sim) {
    return sizeof(SnapshotHeader) + (size_t)sim->dispatcher.plane_count * sizeof(Plane)
         + (size_t)sim->order_count * sizeof(Order)
         + (size_t)sim->delivery_count * sizeof(FlatDelivery);
}

int snapshot_encode(const Simulation *sim, void *buffer, size_t capacity) {
    size_t length = snapshot_size(sim);
    if (!buffer || capacity < length || length > UINT32_MAX) return 0;
    SnapshotHeader header = {
        .magic = DELIVERY_IPC_MAGIC,
        .total_size = (uint32_t)length,
        .nplanes = sim->dispatcher.plane_count,
        .norders = sim->order_count,
        .nstations = sim->dispatcher.station_count,
        .time = sim->current_time,
        .max_time = sim->max_time,
        .verbose = sim->verbose,
        .weather = sim->weather,
        .stop_requested = sim->stop_requested,
        .total_distance = sim->total_distance,
    };
    unsigned char *cursor = buffer;
    memcpy(cursor, &header, sizeof(header)); cursor += sizeof(header);
    memcpy(cursor, sim->dispatcher.planes, (size_t)header.nplanes * sizeof(Plane));
    cursor += (size_t)header.nplanes * sizeof(Plane);
    if (header.norders > 0)
        memcpy(cursor, sim->orders, (size_t)header.norders * sizeof(Order));
    cursor += (size_t)header.norders * sizeof(Order);
    for (int i = 0; i < sim->delivery_count; ++i) {
        FlatDelivery d = {
            .order_id = sim->deliveries[i].order_id,
            .station_id = sim->deliveries[i].station_id,
            .stage = sim->deliveries[i].stage,
            .replan_required = sim->deliveries[i].replan_required,
        };
        memcpy(cursor, &d, sizeof(d)); cursor += sizeof(d);
    }
    return 1;
}

static Point delivery_target(const Simulation *sim, const Delivery *d, int *valid) {
    *valid = 1;
    if (d->stage == DELIVERY_TO_WAREHOUSE) return sim->dispatcher.warehouse;
    if (d->stage == DELIVERY_TO_DESTINATION) {
        for (int i = 0; i < sim->order_count; ++i)
            if (sim->orders[i].id == d->order_id) return sim->orders[i].destination;
    }
    if (d->stage == DELIVERY_TO_STATION) {
        for (int i = 0; i < sim->dispatcher.station_count; ++i)
            if (sim->dispatcher.stations[i].id == d->station_id)
                return sim->dispatcher.stations[i].position;
    }
    *valid = 0;
    return make_point(0, 0);
}

int snapshot_decode(Simulation *sim, const void *buffer, size_t capacity) {
    if (!buffer || capacity < sizeof(SnapshotHeader)) return 0;
    SnapshotHeader header;
    memcpy(&header, buffer, sizeof(header));
    if (header.magic != DELIVERY_IPC_MAGIC ||
        header.nplanes != sim->dispatcher.plane_count ||
        header.norders != sim->order_count ||
        header.nstations != sim->dispatcher.station_count ||
        header.nplanes != sim->delivery_count ||
        header.total_size != snapshot_size(sim) ||
        header.total_size > capacity) return 0;

    const unsigned char *cursor = (const unsigned char *)buffer + sizeof(header);
    memcpy(sim->dispatcher.planes, cursor, (size_t)header.nplanes * sizeof(Plane));
    cursor += (size_t)header.nplanes * sizeof(Plane);
    if (header.norders > 0)
        memcpy(sim->orders, cursor, (size_t)header.norders * sizeof(Order));
    cursor += (size_t)header.norders * sizeof(Order);
    sim->current_time = header.time;
    sim->max_time = header.max_time;
    sim->verbose = header.verbose;
    sim->weather = header.weather;
    sim->stop_requested = header.stop_requested;
    sim->total_distance = header.total_distance;
    for (int i = 0; i < sim->delivery_count; ++i) {
        FlatDelivery flat;
        memcpy(&flat, cursor, sizeof(flat)); cursor += sizeof(flat);
        Delivery *d = &sim->deliveries[i];
        free(d->path);
        d->path = NULL;
        d->path_index = 0;
        d->path_length = 0;
        d->order_id = flat.order_id;
        d->station_id = flat.station_id;
        d->stage = (DeliveryStage)flat.stage;
        d->replan_required = flat.replan_required;
        if (d->stage == DELIVERY_FINISHED) continue;
        int valid = 0;
        Point target = delivery_target(sim, d, &valid);
        if (!valid) return 0;
        int path_count = 0;
        d->path = nearest_way(sim->dispatcher.map,
                              sim->dispatcher.planes[i].position,
                              target, &path_count);
        if (!d->path) return 0;
        d->path_length = path_count;
    }
    for (int i = 0; i < sim->dispatcher.station_count; ++i) {
        Station *station = &sim->dispatcher.stations[i];
        station->charging_count = 0;
        for (int j = 0; j < sim->dispatcher.plane_count; ++j) {
            Plane *plane = &sim->dispatcher.planes[j];
            if (plane->status == PLANE_CHARGING && plane->station_id == station->id) {
                if (station->charging_count >= station->capacity) return 0;
                station->charging[station->charging_count++] = plane;
            }
        }
    }
    return 1;
}

void process_sleep(int ms) {
    if (ms <= 0) return;
    struct timespec duration = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&duration, &duration) == -1) {}
}

int process_load(const char *path, Scenario *scenario, Simulation *sim, int verbose) {
    char error[256];
    if (!scenario_load(scenario, path, error, sizeof(error))) {
        fprintf(stderr, "Scenario: %s\n", error);
        return 0;
    }
    Dispatcher dispatcher = create_dispatcher(&scenario->map, scenario->warehouse,
                                             scenario->planes, scenario->plane_count,
                                             scenario->stations, scenario->station_count);
    *sim = create_simulation(dispatcher, scenario->orders,
                             scenario->order_count, scenario->max_time);
    if (sim->delivery_count != scenario->plane_count) {
        fprintf(stderr, "Failed to allocate deliveries\n");
        destroy_simulation(sim);
        scenario_destroy(scenario);
        return 0;
    }
    sim->weather = create_weather(scenario->weather_interval);
    sim->verbose = verbose;
    return 1;
}

void process_unload(Scenario *scenario, Simulation *sim) {
    destroy_simulation(sim);
    scenario_destroy(scenario);
}

const char *process_role_name(int role) {
    static const char *names[] = {"orders", "dispatcher", "drones", "stations"};
    return role >= 0 && role < DELIVERY_ROLES ? names[role] : "invalid";
}

int process_role_id(const char *text) {
    for (int i = 0; i < DELIVERY_ROLES; ++i)
        if (!strcmp(text, process_role_name(i))) return i;
    return -1;
}
