#ifndef STATION_H
#define STATION_H

#include "plane.h"

typedef struct {
    int id;
    Point position;

    int capacity;
    int charging_count;
    int charging_speed;

    Plane **charging;

    Plane **queue;
    int queue_size;
    int queue_capacity;
} Station;

Station create_station(int id, Point position,
                       int capacity, int charging_speed,
                       int queue_capacity);

void destroy_station(Station *station);

int station_arrive(Station *station, Plane *plane);

void station_tick(Station *station);

#endif