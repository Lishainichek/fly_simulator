#include <stdlib.h>
#include "../inc/station.h"

Station create_station(int id, Point position,
                       int capacity, int charging_speed,
                       int queue_capacity) {
    Station station = {0};
    station.id = id;
    station.position = position;
    if (capacity <= 0 || charging_speed <= 0 ||
        queue_capacity < 0) {
        return station;
    }
    station.capacity = capacity;
    station.charging_speed = charging_speed;
    station.queue_capacity = queue_capacity;
    station.charging = calloc(capacity, sizeof(Plane *));
    if (station.charging == NULL) {
        station.capacity = 0;
        return station;
    }
    if (queue_capacity > 0) {
        station.queue = calloc(queue_capacity, sizeof(Plane *));
        if (station.queue == NULL) {
            free(station.charging);
            station.charging = NULL;
            station.capacity = 0;
            return station;
        }
    }
    return station;
}

void destroy_station(Station *station) {
    if (station == NULL) {
        return;
    }

    for (int i = 0; i < station->charging_count; ++i) {
        Plane *plane = station->charging[i];

        plane->station_id = -1;
        plane->status = PLANE_IDLE;
    }

    for (int i = 0; i < station->queue_size; ++i) {
        Plane *plane = station->queue[i];

        plane->station_id = -1;
        plane->status = PLANE_IDLE;
    }

    free(station->charging);
    free(station->queue);

    station->charging = NULL;
    station->queue = NULL;

    station->charging_count = 0;
    station->queue_size = 0;
    station->capacity = 0;
    station->queue_capacity = 0;
}

int station_arrive(Station *station, Plane *plane) {
    if (station == NULL || plane == NULL ||
        station->charging == NULL) {
        return 0;
    }

    if (plane->station_id != -1) {
        return 0;
    }
    if (plane->position.x != station->position.x ||
        plane->position.y != station->position.y) {
        return 0;
    }
    if (plane->energy >= plane->max_energy) {
        return 0;
    }
    if (station->charging_count < station->capacity) {
        station->charging[station->charging_count++] = plane;
        plane->station_id = station->id;
        plane->status = PLANE_CHARGING;
        return 1;
    }
    if (station->queue_size >= station->queue_capacity) {
        return 0;
    }
    station->queue[station->queue_size++] = plane;
    plane->station_id = station->id;
    plane->status = PLANE_WAITING;
    return 1;
}

void station_tick(Station *station) {
    if (station == NULL || station->charging == NULL) {
        return;
    }

    for (int i = 0; i < station->charging_count;) {
        Plane *plane = station->charging[i];

        int missing = plane->max_energy - plane->energy;

        if (missing <= station->charging_speed) {
            plane->energy = plane->max_energy;
            plane->status = PLANE_IDLE;
            plane->station_id = -1;

            for (int j = i; j + 1 < station->charging_count; ++j) {
                station->charging[j] = station->charging[j + 1];
            }

            --station->charging_count;
            station->charging[station->charging_count] = NULL;
        } else {
            plane->energy += station->charging_speed;
            ++i;
        }
    }

    while (station->charging_count < station->capacity &&
           station->queue_size > 0) {
        Plane *plane = station->queue[0];

        for (int i = 0; i + 1 < station->queue_size; ++i) {
            station->queue[i] = station->queue[i + 1];
        }

        --station->queue_size;
        station->queue[station->queue_size] = NULL;

        station->charging[station->charging_count++] = plane;
        plane->status = PLANE_CHARGING;
    }
}