#ifndef SCENARIO_H
#define SCENARIO_H

#include "map.h"
#include "plane.h"
#include "order.h"
#include "station.h"

#include <stddef.h>

typedef struct {
    Map map;
    Point warehouse;
    int has_warehouse;
    Plane *planes;
    int plane_count;
    Station *stations;
    int station_count;
    Order *orders;
    int order_count;
    int max_time;
    int weather_interval;
} Scenario;
int scenario_load(Scenario *scenario, const char *filename,
                  char *error, size_t error_size);
void scenario_destroy(Scenario *scenario);

#endif
