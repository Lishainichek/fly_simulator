#ifndef SIMULATION_H
#define SIMULATION_H
#include "dispatcher.h"
#include "weather.h"
#include <pthread.h>
#include <signal.h>

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
    int replan_required;
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
    int delay_ms;
    int verbose;
    volatile sig_atomic_t stop_requested;
    long long total_distance;
    pthread_mutex_t mutex;
    int mutex_ready;
} Simulation;

Simulation create_simulation(Dispatcher dispatcher, Order *orders,
                             int order_count, int max_time);
int simulation_tick(Simulation *sim);
int simulation_run(Simulation *sim);
void destroy_simulation(Simulation *sim);
void simulation_print_statistics(const Simulation *sim);
#endif
