#include "simulation.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int same_point(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}

static Plane *find_plane(Simulation *sim, int id) {
    for (int i = 0; i < sim->dispatcher.plane_count; ++i) {
        if (sim->dispatcher.planes[i].id == id) return &sim->dispatcher.planes[i];
    }
    return NULL;
}

static Order *find_order(Simulation *sim, int id) {
    for (int i = 0; i < sim->order_count; ++i) {
        if (sim->orders[i].id == id) return &sim->orders[i];
    }
    return NULL;
}

static Station *find_station(Simulation *sim, int id) {
    for (int i = 0; i < sim->dispatcher.station_count; ++i) {
        if (sim->dispatcher.stations[i].id == id) return &sim->dispatcher.stations[i];
    }
    return NULL;
}

static void clear_path(Delivery *delivery) {
    free(delivery->path);
    delivery->path = NULL;
    delivery->path_length = 0;
    delivery->path_index = 0;
    delivery->replan_required = 0;
}

static void complete_route(Delivery *delivery) {
    clear_path(delivery);
    delivery->order_id = -1;
    delivery->station_id = -1;
    delivery->stage = DELIVERY_FINISHED;
}

static int build_path(Simulation *sim, Delivery *delivery, Point from, Point to) {
    int n = 0;
    Point *path = nearest_way(sim->dispatcher.map, from, to, &n);
    if (!path) return 0;
    clear_path(delivery);
    delivery->path = path;
    delivery->path_length = n;
    return 1;
}

static int path_distance(const Dispatcher *dispatcher, Point a, Point b) {
    int n = 0;
    Point *path = nearest_way(dispatcher->map, a, b, &n);
    if (!path) return -1;
    free(path);
    return n - 1;
}

static Station *select_station(Simulation *sim, const Plane *plane) {
    Station *best = NULL;
    int shortest = INT_MAX;
    for (int i = 0; i < sim->dispatcher.station_count; ++i) {
        Station *s = &sim->dispatcher.stations[i];
        int distance = path_distance(&sim->dispatcher, plane->position, s->position);
        if (s->capacity > 0 && distance >= 0 && distance < shortest &&
            plane_can_reach(plane, distance)) {
            best = s;
            shortest = distance;
        }
    }
    return best;
}

static void route_to_station(Simulation *sim, Delivery *delivery, Plane *plane) {
    Station *station = select_station(sim, plane);
    if (!station) {
        if (sim->verbose)
            printf("[%d] Plane #%d: no reachable charging station, grounded at (%d,%d)\n",
                   sim->current_time, plane->id, plane->position.x, plane->position.y);
        plane->status = PLANE_IDLE;
        complete_route(delivery);
        return;
    }
    delivery->stage = DELIVERY_TO_STATION;
    delivery->station_id = station->id;
    delivery->order_id = -1;
    plane->status = PLANE_RETURNING;
    if (!build_path(sim, delivery, plane->position, station->position)) {
        plane->status = PLANE_IDLE;
        complete_route(delivery);
    } else if (sim->verbose) {
        printf("[%d] Plane #%d returning to Station #%d\n",
               sim->current_time, plane->id, station->id);
    }
}

static void order_step(Simulation *sim, int index) {
    Order *order = &sim->orders[index];
    if (order->status == ORDER_DELIVERED || order->status == ORDER_FAILED) return;
    if (sim->current_time <= order->deadline) {
        if (sim->current_time == order->created_at && sim->verbose)
            printf("[%d] Order #%d received: (%d,%d), weight=%d, deadline=%d\n",
                   sim->current_time, order->id, order->destination.x,
                   order->destination.y, order->weight, order->deadline);
        return;
    }
    if (sim->verbose)
        printf("[%d] Order #%d expired (deadline=%d)\n",
               sim->current_time, order->id, order->deadline);
    order_fail(order);
    for (int i = 0; i < sim->delivery_count; ++i) {
        Delivery *delivery = &sim->deliveries[i];
        if (delivery->order_id != order->id || delivery->stage == DELIVERY_FINISHED)
            continue;
        Plane *plane = find_plane(sim, delivery->plane_id);
        if (plane) route_to_station(sim, delivery, plane);
    }
}

static void dispatcher_step(Simulation *sim) {
    if (!weather_can_fly(&sim->weather)) return;
    for (int i = 0; i < sim->order_count; ++i) {
        Order *order = &sim->orders[i];
        if (order->status != ORDER_PENDING || order->created_at > sim->current_time)
            continue;
        if (!dispatcher_assign(&sim->dispatcher, order, sim->current_time)) continue;
        Plane *plane = find_plane(sim, order->plane_id);
        Delivery *delivery = NULL;
        for (int j = 0; j < sim->delivery_count; ++j) {
            if (sim->deliveries[j].plane_id == order->plane_id) {
                delivery = &sim->deliveries[j];
                break;
            }
        }
        if (!plane || !delivery || delivery->stage != DELIVERY_FINISHED ||
            !build_path(sim, delivery, plane->position, sim->dispatcher.warehouse)) {
            order_fail(order);
            if (plane) plane->status = PLANE_IDLE;
            if (delivery) complete_route(delivery);
            continue;
        }
        delivery->order_id = order->id;
        delivery->stage = DELIVERY_TO_WAREHOUSE;
        if (sim->verbose)
            printf("[%d] Order #%d assigned to Plane #%d\n",
                   sim->current_time, order->id, plane->id);
    }
    for (int j = 0; j < sim->delivery_count; ++j) {
        Delivery *delivery = &sim->deliveries[j];
        Plane *plane = find_plane(sim, delivery->plane_id);
        if (plane && delivery->stage == DELIVERY_FINISHED &&
            plane->status == PLANE_IDLE && plane->energy < plane->max_energy)
            route_to_station(sim, delivery, plane);
    }
}

static void arrive_at_station(Simulation *sim, Delivery *delivery, Plane *plane) {
    Station *station = find_station(sim, delivery->station_id);
    if (!station || !same_point(station->position, plane->position)) {
        route_to_station(sim, delivery, plane);
        return;
    }
    if (plane->energy == plane->max_energy) {
        plane->status = PLANE_IDLE;
        if (sim->verbose)
            printf("[%d] Plane #%d arrived at Station #%d fully charged\n",
                   sim->current_time, plane->id, station->id);
        complete_route(delivery);
        return;
    }
    if (station_arrive(station, plane)) {
        if (sim->verbose)
            printf("[%d] Plane #%d arrived at Station #%d: %s\n",
                   sim->current_time, plane->id, station->id,
                   "charging");
        complete_route(delivery);
    } else {
        if (plane->status != PLANE_WAITING && sim->verbose)
            printf("[%d] Plane #%d waiting for slot at Station #%d\n",
                   sim->current_time, plane->id, station->id);
        plane->status = PLANE_WAITING;
    }
}

static void arrive_at_destination(Simulation *sim, Delivery *delivery, Plane *plane) {
    Order *order = find_order(sim, delivery->order_id);
    if (order && order_deliver(order, plane->id, sim->current_time)) {
        if (sim->verbose)
            printf("[%d] Plane #%d delivered Order #%d at (%d,%d)\n",
                   sim->current_time, plane->id, order->id,
                   plane->position.x, plane->position.y);
    } else if (order) {
        order_fail(order);
        if (sim->verbose)
            printf("[%d] Plane #%d could not deliver Order #%d (deadline)\n",
                   sim->current_time, plane->id, order->id);
    }
    route_to_station(sim, delivery, plane);
}

static void plane_arrive(Simulation *sim, Delivery *delivery, Plane *plane) {
    switch (delivery->stage) {
        case DELIVERY_TO_WAREHOUSE: {
            Order *order = find_order(sim, delivery->order_id);
            if (!order || !order_start_delivery(order)) {
                route_to_station(sim, delivery, plane);
                break;
            }
            if (sim->verbose)
                printf("[%d] Plane #%d picked up Order #%d at warehouse\n",
                       sim->current_time, plane->id, order->id);
            delivery->stage = DELIVERY_TO_DESTINATION;
            if (!build_path(sim, delivery, plane->position, order->destination)) {
                order_fail(order);
                route_to_station(sim, delivery, plane);
            }
            break;
        }
        case DELIVERY_TO_DESTINATION:
            arrive_at_destination(sim, delivery, plane);
            break;
        case DELIVERY_TO_STATION:
            arrive_at_station(sim, delivery, plane);
            break;
        case DELIVERY_FINISHED:
            break;
    }
}

static void plane_step(Simulation *sim, int index) {
    Delivery *delivery = &sim->deliveries[index];
    if (delivery->stage == DELIVERY_FINISHED) return;
    Plane *plane = find_plane(sim, delivery->plane_id);
    if (!plane || !delivery->path) return;
    if (!weather_can_fly(&sim->weather)) {
        if (delivery->stage == DELIVERY_TO_STATION &&
            delivery->path_index + 1 == delivery->path_length)
            plane_arrive(sim, delivery, plane);
        return;
    }
    if (delivery->replan_required) {
        Point target = delivery->path[delivery->path_length - 1];
        if (!build_path(sim, delivery, plane->position, target)) {
            if (delivery->order_id != -1) {
                Order *order = find_order(sim, delivery->order_id);
                if (order) order_fail(order);
            }
            route_to_station(sim, delivery, plane);
            return;
        }
        if (sim->verbose)
            printf("[%d] Plane #%d route replanned after weather change\n",
                   sim->current_time, plane->id);
    }
    if (delivery->path_index + 1 < delivery->path_length) {
        Point next = delivery->path[delivery->path_index + 1];
        if (!plane_move(plane, next, sim->dispatcher.map)) {
            delivery->replan_required = 1;
            return;
        }
        ++delivery->path_index;
        ++sim->total_distance;
        if (sim->verbose)
            printf("[%d] Plane #%d moved to (%d,%d), energy=%d\n",
                   sim->current_time, plane->id, plane->position.x,
                   plane->position.y, plane->energy);
    }
    if (delivery->path_index + 1 >= delivery->path_length)
        plane_arrive(sim, delivery, plane);
}

static void station_step(Simulation *sim, int index) {
    station_tick(&sim->dispatcher.stations[index], sim->current_time,
                 sim->verbose);
}

Simulation create_simulation(Dispatcher dispatcher, Order *orders,
                             int order_count, int max_time) {
    Simulation sim = {0};
    sim.dispatcher = dispatcher;
    sim.orders = orders;
    sim.order_count = order_count;
    sim.max_time = max_time;
    sim.verbose = 1;
    sim.weather = create_weather(5);
    sim.delivery_count = dispatcher.plane_count;
    if (sim.delivery_count > 0) {
        sim.deliveries = calloc((size_t)sim.delivery_count, sizeof(*sim.deliveries));
        if (!sim.deliveries) {
            sim.delivery_count = 0;
            return sim;
        }
    }
    for (int i = 0; i < sim.delivery_count; ++i) {
        sim.deliveries[i].plane_id = dispatcher.planes[i].id;
        sim.deliveries[i].order_id = -1;
        sim.deliveries[i].station_id = -1;
        sim.deliveries[i].stage = DELIVERY_FINISHED;
    }
    return sim;
}
typedef struct {
    Simulation *sim;
    int index;
    int role;
} Worker;

static void *worker_main(void *arg) {
    Worker *w = arg;
    Simulation *sim = w->sim;
    pthread_mutex_lock(&sim->mutex);
    switch (w->role) {
        case 0: order_step(sim, w->index); break;
        case 1: dispatcher_step(sim); break;
        case 2: plane_step(sim, w->index); break;
        case 3: station_step(sim, w->index); break;
    }
    pthread_mutex_unlock(&sim->mutex);
    return NULL;
}

static int run_phase(Simulation *sim, int role, int count) {
    if (count == 0) return 1;
    pthread_t *threads = malloc((size_t)count * sizeof(*threads));
    Worker *workers = malloc((size_t)count * sizeof(*workers));
    if (!threads || !workers) {
        free(threads);
        free(workers);
        return 0;
    }
    int started = 0;
    for (int i = 0; i < count; ++i) {
        workers[i] = (Worker){sim, i, role};
        if (pthread_create(&threads[i], NULL, worker_main, &workers[i]) != 0)
            break;
        ++started;
    }
    for (int i = 0; i < started; ++i) pthread_join(threads[i], NULL);
    free(threads);
    free(workers);
    return started == count;
}

int simulation_tick(Simulation *sim) {
    if (!sim || (sim->delivery_count > 0 && !sim->deliveries)) return 0;
    if (!sim->mutex_ready) {
        if (pthread_mutex_init(&sim->mutex, NULL) != 0) return 0;
        sim->mutex_ready = 1;
    }
    WeatherType before = sim->weather.type;
    weather_tick(&sim->weather, sim->current_time);
    if (before != sim->weather.type) {
        if (sim->verbose)
            printf("[%d] Weather changed to %s (%s)\n",
                   sim->current_time, weather_name(&sim->weather),
                   weather_can_fly(&sim->weather) ? "flights resumed" : "flights suspended");
        if (weather_can_fly(&sim->weather)) {
            for (int i = 0; i < sim->delivery_count; ++i)
                if (sim->deliveries[i].stage != DELIVERY_FINISHED)
                    sim->deliveries[i].replan_required = 1;
        }
    }
    if (!run_phase(sim, 0, sim->order_count) ||
        !run_phase(sim, 1, 1) ||
        !run_phase(sim, 2, sim->delivery_count) ||
        !run_phase(sim, 3, sim->dispatcher.station_count)) return 0;
    ++sim->current_time;
    if (sim->delay_ms > 0) {
        struct timespec delay = {sim->delay_ms / 1000, (long)(sim->delay_ms % 1000) * 1000000L};
        nanosleep(&delay, NULL);
    }
    return 1;
}

static int all_done(const Simulation *sim) {
    for (int i = 0; i < sim->order_count; ++i)
        if (sim->orders[i].status != ORDER_DELIVERED &&
            sim->orders[i].status != ORDER_FAILED) return 0;
    for (int i = 0; i < sim->delivery_count; ++i)
        if (sim->deliveries[i].stage != DELIVERY_FINISHED) return 0;
    return 1;
}

int simulation_run(Simulation *sim) {
    if (!sim) return 0;
    if (sim->order_count > 0 && sim->delivery_count == 0 && sim->dispatcher.plane_count > 0)
        return 0;
    while (!sim->stop_requested &&
           (sim->max_time == 0 || sim->current_time < sim->max_time) &&
           !all_done(sim)) {
        if (!simulation_tick(sim)) return 0;
    }
    if (sim->max_time > 0 && sim->current_time >= sim->max_time) {
        for (int i = 0; i < sim->order_count; ++i) {
            if (order_fail(&sim->orders[i]) && sim->verbose)
                printf("[%d] Order #%d failed: simulation time limit\n",
                       sim->current_time, sim->orders[i].id);
        }
    }
    return 1;
}

void destroy_simulation(Simulation *sim) {
    if (!sim) return;
    if (sim->mutex_ready) {
        pthread_mutex_destroy(&sim->mutex);
        sim->mutex_ready = 0;
    }
    for (int i = 0; i < sim->delivery_count; ++i) clear_path(&sim->deliveries[i]);
    free(sim->deliveries);
    sim->deliveries = NULL;
    sim->delivery_count = 0;
}

void simulation_print_statistics(const Simulation *sim) {
    int delivered = 0, failed = 0, pending = 0, progress = 0;
    for (int i = 0; i < sim->order_count; ++i) {
        switch (sim->orders[i].status) {
            case ORDER_DELIVERED: ++delivered; break;
            case ORDER_FAILED: ++failed; break;
            case ORDER_PENDING: ++pending; break;
            default: ++progress; break;
        }
    }
    printf("\n=== SIMULATION STATISTICS ===\n");
    printf("Simulation time: %d\n", sim->current_time);
    printf("Total orders: %d\n", sim->order_count);
    printf("Delivered orders: %d\n", delivered);
    printf("Failed orders: %d\n", failed);
    printf("Pending orders: %d\n", pending);
    printf("Orders in progress: %d\n", progress);
    printf("Total flight distance: %lld\n", sim->total_distance);
    printf("Delivery success rate: %.2f%%\n",
           sim->order_count ? 100.0 * delivered / sim->order_count : 0.0);
}
