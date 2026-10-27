#ifndef DISPATCHER_H
#define DISPATCHER_H

#include "map.h"
#include "plane.h"
#include "order.h"
#include "station.h"

typedef struct {
    Map *map;

    Point warehouse;

    Plane *planes;
    int plane_count;

    Station *stations;
    int station_count;
} Dispatcher;

Dispatcher create_dispatcher(Map *map, Point warehouse,
                             Plane *planes, int plane_count,
                             Station *stations, int station_count);

int dispatcher_assign(Dispatcher *dispatcher, Order *order,
                      int current_time);

#endif  