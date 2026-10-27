#ifndef SIMULATION_H
#define SIMULATION_H

#include "dispatcher.h"
#include "weather.h"
typedef enum {
    DELIVERY_TO_WAREHOUSE,
    DELIVERY_TO_DESTINATION,
    DELIVERY_TO_STATION,
    DELIVERY_FINISHED
} DeliveryStage;

typedef struct {
    int plane_id;
    int order_id;
    int station_id;
    DeliveryStage stage;

    Point *path;
    int path_length;
    int path_index;
} Delivery;

typedef struct {
    Dispatcher dispatcher;

    Order *orders;
    int order_count;

    Delivery *deliveries;
    int delivery_count;
    Weather weather;
    int current_time;
    int max_time;
    long long total_distance;
} Simulation;

Simulation create_simulation(Dispatcher dispatcher,
                             Order *orders, int order_count,
                             int max_time);

void simulation_tick(Simulation *sim);

void simulation_run(Simulation *sim);

void destroy_simulation(Simulation *sim);

void simulation_print_statistics(const Simulation *sim);
void simulation_check(const Simulation* sim);
#endif