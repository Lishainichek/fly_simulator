#ifndef PLANE_H
#define PLANE_H

#include "map.h"

typedef enum {
    PLANE_IDLE,
    PLANE_DELIVERING,
    PLANE_RETURNING,
    PLANE_CHARGING,
    PLANE_WAITING
} PlaneStatus;

typedef struct {
    int id;
    Point position;
    int station_id;
    int capacity;
    int energy;
    int max_energy;
    int consumption;

    PlaneStatus status;
} Plane;

Plane create_plane(int id, Point position, int capacity,
                   int max_energy, int consumption);

int plane_can_carry(const Plane *plane, int weight);

int plane_move(Plane *plane, Point next, const Map *map);

int plane_can_reach(const Plane *plane, int distance);

#endif