#ifndef STATION_H
#define STATION_H
#include "plane.h"

typedef struct {
    int id;
    Point position;
    int capacity;
    int charging_speed;
    Plane **charging;
    int charging_count;
} Station;

Station create_station(int id, Point position, int capacity, int charging_speed);
void destroy_station(Station *station);
int station_arrive(Station *station, Plane *plane);
void station_tick(Station *station, int time, int verbose);
#endif
