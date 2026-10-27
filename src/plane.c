#include "../inc/plane.h"

#include <stdlib.h>

Plane create_plane(int id, Point position, int capacity,
                   int max_energy, int consumption) {
    Plane plane;
    plane.station_id = -1;
    plane.id = id;
    plane.position = position;

    plane.capacity = capacity;
    plane.energy = max_energy;
    plane.max_energy = max_energy;
    plane.consumption = consumption;

    plane.status = PLANE_IDLE;

    return plane;
}

int plane_can_carry(const Plane *plane, int weight) {
    if (plane == NULL) {
        return 0;
    }

    return weight >= 0 && weight <= plane->capacity;
}

int plane_can_reach(const Plane *plane, int distance) {
    if (plane == NULL || distance < 0 ||
        plane->consumption <= 0) {
        return 0;
    }

    return distance <= plane->energy / plane->consumption;
}

int plane_move(Plane *plane, Point next, const Map *map) {
    if (plane == NULL || map == NULL) {
        return 0;
    }

    if (!is_valid(map, next)) {
        return 0;
    }

    int distance = abs(plane->position.x - next.x) +
                   abs(plane->position.y - next.y);

    if (distance != 1) {
        return 0;
    }

    if (!plane_can_reach(plane, distance)) {
        return 0;
    }

    plane->position = next;
    plane->energy -= plane->consumption;

    return 1;
}