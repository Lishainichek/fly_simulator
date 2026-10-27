#include "../inc/simulation.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
int same_point(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}

Plane *find_plane(Simulation *sim, int id) {
    for (int i = 0; i < sim->dispatcher.plane_count; ++i) {
        if (sim->dispatcher.planes[i].id == id) {
            return &sim->dispatcher.planes[i];
        }
    }
    return NULL;
}

Order *find_order(Simulation *sim, int id) {
    for (int i = 0; i < sim->order_count; ++i) {
        if (sim->orders[i].id == id) {
            return &sim->orders[i];
        }
    }
    return NULL;
}

Station *find_station(Simulation *sim, int id) {
    for (int i = 0; i < sim->dispatcher.station_count; ++i) {
        if (sim->dispatcher.stations[i].id == id) {
            return &sim->dispatcher.stations[i];
        }
    }
    return NULL;
}

int build_path(Simulation *sim, Delivery *delivery,
                      Point start, Point finish) {
    int length = 0;

    Point *path = nearest_way(
        sim->dispatcher.map, start, finish, &length
    );

    if (path == NULL) {
        return 0;
    }

    free(delivery->path);

    delivery->path = path;
    delivery->path_length = length;
    delivery->path_index = 0;

    return 1;
}

Station *nearest_station(Simulation *sim, Point position) {
    Station *best = NULL;
    int best_distance = INT_MAX;

    for (int i = 0; i < sim->dispatcher.station_count; ++i) {
        Station *station = &sim->dispatcher.stations[i];

        int length = 0;
        Point *path = nearest_way(
            sim->dispatcher.map,
            position,
            station->position,
            &length
        );

        if (path == NULL) {
            continue;
        }

        free(path);

        int distance = length - 1;

        if (distance < best_distance) {
            best_distance = distance;
            best = station;
        }
    }

    return best;
}

Simulation create_simulation(Dispatcher dispatcher,
                             Order *orders, int order_count,
                             int max_time) {
    Simulation sim = {0};
    sim.dispatcher = dispatcher;
    sim.orders = orders;
    sim.order_count = order_count;
    sim.max_time = max_time;
    sim.weather = create_weather(5);

    sim.delivery_count = dispatcher.plane_count;

    sim.deliveries = calloc(
        sim.delivery_count, sizeof(Delivery)
    );

    if (sim.deliveries == NULL) {
        sim.delivery_count = 0;
        return sim;
    }

    for (int i = 0; i < sim.delivery_count; ++i) {
        sim.deliveries[i].plane_id = dispatcher.planes[i].id;
        sim.deliveries[i].order_id = -1;
        sim.deliveries[i].station_id = -1;
        sim.deliveries[i].stage = DELIVERY_FINISHED;
    }

    return sim;
}

void assign_orders(Simulation *sim) {
    for (int i = 0; i < sim->order_count; ++i) {
        Order *order = &sim->orders[i];

        if (order->status != ORDER_PENDING ||
            order->created_at > sim->current_time) {
            continue;
        }

        if (!dispatcher_assign(
                &sim->dispatcher, order, sim->current_time)) {
            continue;
        }

        Plane *plane = find_plane(sim, order->plane_id);

        if (plane == NULL) {
            continue;
        }

        for (int j = 0; j < sim->delivery_count; ++j) {
            Delivery *delivery = &sim->deliveries[j];

            if (delivery->plane_id != plane->id) {
                continue;
            }

            delivery->order_id = order->id;
            delivery->stage = DELIVERY_TO_WAREHOUSE;

            if (!build_path(
                    sim, delivery,
                    plane->position,
                    sim->dispatcher.warehouse)) {
                order_fail(order);
                plane->status = PLANE_IDLE;
                delivery->order_id = -1;
                delivery->stage = DELIVERY_FINISHED;
                continue;
            }

            printf(
                "[%d] Order #%d assigned to Plane #%d\n",
                sim->current_time, order->id, plane->id
            );

            break;
        }
    }
}

void finish_delivery(Simulation *sim, Delivery *delivery,
                            Plane *plane, Order *order) {
    if (order_deliver(order, plane->id, sim->current_time)) {
        printf(
            "[%d] Plane #%d delivered Order #%d\n",
            sim->current_time, plane->id, order->id
        );
    } else {
        order_fail(order);

        printf(
            "[%d] Plane #%d failed to deliver Order #%d\n",
            sim->current_time, plane->id, order->id
        );
    }

    Station *station = nearest_station(sim, plane->position);

    if (station == NULL) {
        delivery->stage = DELIVERY_FINISHED;
        delivery->order_id = -1;
        return;
    }

    delivery->station_id = station->id;
    delivery->stage = DELIVERY_TO_STATION;

    plane->status = PLANE_RETURNING;

    if (!build_path(
            sim, delivery,
            plane->position,
            station->position)) {
        delivery->stage = DELIVERY_FINISHED;
        delivery->order_id = -1;
    }
}

void process_arrival(Simulation *sim, Delivery *delivery,
                            Plane *plane) {
    if (delivery->stage == DELIVERY_TO_WAREHOUSE) {
        Order *order = find_order(sim, delivery->order_id);

        if (order == NULL) {
            return;
        }

        order_start_delivery(order);

        printf(
            "[%d] Plane #%d picked up Order #%d\n",
            sim->current_time, plane->id, order->id
        );

        delivery->stage = DELIVERY_TO_DESTINATION;

        if (!build_path(
                sim, delivery,
                plane->position,
                order->destination)) {
            order_fail(order);
            delivery->stage = DELIVERY_FINISHED;
        }

    } else if (delivery->stage == DELIVERY_TO_DESTINATION) {
        Order *order = find_order(sim, delivery->order_id);

        if (order != NULL) {
            finish_delivery(sim, delivery, plane, order);
        }

    }  else if (delivery->stage == DELIVERY_TO_STATION) {
    Station *station = find_station(sim, delivery->station_id);

    if (station != NULL) {
        if (plane->energy == plane->max_energy) {
            plane->status = PLANE_IDLE;

            printf(
                "[%d] Plane #%d arrived at Station #%d fully charged\n",
                sim->current_time, plane->id, station->id
            );
        } else if (station_arrive(station, plane)) {
            printf(
                "[%d] Plane #%d arrived at Station #%d\n",
                sim->current_time, plane->id, station->id
            );
        } else {
            printf(
                "[%d] Plane #%d could not enter Station #%d\n",
                sim->current_time, plane->id, station->id
            );
        }
    }

    delivery->stage = DELIVERY_FINISHED;
    delivery->order_id = -1;
    delivery->station_id = -1;

    free(delivery->path);
    delivery->path = NULL;
    delivery->path_length = 0;
    delivery->path_index = 0;
    }
}

void move_planes(Simulation *sim) {
    for (int i = 0; i < sim->delivery_count; ++i) {
        Delivery *delivery = &sim->deliveries[i];

        if (delivery->stage == DELIVERY_FINISHED) {
            continue;
        }

        Plane *plane = find_plane(sim, delivery->plane_id);

        if (plane == NULL || delivery->path == NULL) {
            continue;
        }

        if (delivery->path_index + 1 < delivery->path_length) {
            Point next = delivery->path[delivery->path_index + 1];

            if (!plane_move(
                    plane, next, sim->dispatcher.map)) {
                continue;
            }

            delivery->path_index++;
            sim->total_distance++;

            printf(
                "[%d] Plane #%d moved to (%d, %d), energy=%d\n",
                sim->current_time,
                plane->id,
                plane->position.x,
                plane->position.y,
                plane->energy
            );
        }

        if (delivery->path_index + 1 >= delivery->path_length) {
            process_arrival(sim, delivery, plane);
        }
    }
}

void update_stations(Simulation *sim) {
    for (int i = 0; i < sim->dispatcher.station_count; ++i) {
        station_tick(&sim->dispatcher.stations[i]);
    }
}

void check_deadlines(Simulation *sim) {
    for (int i = 0; i < sim->order_count; ++i) {
        Order *order = &sim->orders[i];

        if (order->status == ORDER_PENDING &&
            sim->current_time > order->deadline) {
            order_fail(order);

            printf(
                "[%d] Order #%d expired\n",
                sim->current_time, order->id
            );
        }
    }
}

void simulation_tick(Simulation *sim) {
    WeatherType previous = sim->weather.type;
    weather_tick(&sim->weather, sim->current_time);
    if (previous != sim->weather.type) {
        printf(
            "[%d] Weather changed to %s\n",
            sim->current_time,
            weather_name(&sim->weather)
        );
    }
    check_deadlines(sim);
    if (weather_can_fly(&sim->weather)) {
        assign_orders(sim);
        move_planes(sim);
    }
    update_stations(sim);
    sim->current_time++;
    simulation_check(sim);
}

void simulation_run(Simulation *sim) {
    while (sim->current_time < sim->max_time) {
        simulation_tick(sim);
    }
}

void destroy_simulation(Simulation *sim) {
    for (int i = 0; i < sim->delivery_count; ++i) {
        free(sim->deliveries[i].path);
    }

    free(sim->deliveries);

    sim->deliveries = NULL;
    sim->delivery_count = 0;
}
void simulation_print_statistics(const Simulation *sim) {
    int delivered = 0;
    int failed = 0;
    int pending = 0;
    int in_progress = 0;

    for (int i = 0; i < sim->order_count; ++i) {
        const Order *order = &sim->orders[i];

        if (order->status == ORDER_DELIVERED) {
            delivered++;
        } else if (order->status == ORDER_FAILED) {
            failed++;
        } else if (order->status == ORDER_PENDING) {
            pending++;
        } else {
            in_progress++;
        }
    }

    printf("\n=== SIMULATION STATISTICS ===\n");
    printf("Simulation time: %d\n", sim->current_time);
    printf("Total orders: %d\n", sim->order_count);
    printf("Delivered orders: %d\n", delivered);
    printf("Failed orders: %d\n", failed);
    printf("Pending orders: %d\n", pending);
    printf("Orders in progress: %d\n", in_progress);
    printf("Total flight distance: %lld\n", sim->total_distance);

    if (sim->order_count > 0) {
        printf(
            "Delivery success rate: %.2f%%\n",
            100.0 * delivered / sim->order_count
        );
    }
}

void simulation_check(const Simulation *sim) {
    for (int i = 0; i < sim->dispatcher.plane_count; ++i) {
        const Plane *plane = &sim->dispatcher.planes[i];

        assert(plane->energy >= 0);
        assert(plane->energy <= plane->max_energy);

        assert(is_valid(sim->dispatcher.map, plane->position));

        if (plane->status == PLANE_CHARGING ||
            plane->status == PLANE_WAITING) {

            assert(plane->station_id != -1);

            const Station *station = NULL;

            for (int j = 0; j < sim->dispatcher.station_count; ++j) {
                if (sim->dispatcher.stations[j].id == plane->station_id) {
                    station = &sim->dispatcher.stations[j];
                    break;
                }
            }

            assert(station != NULL);

            assert(plane->position.x == station->position.x);
            assert(plane->position.y == station->position.y);
        }
    }

    for (int i = 0; i < sim->order_count; ++i) {
        const Order *order = &sim->orders[i];

        if (order->status == ORDER_DELIVERED) {
            assert(order->plane_id != -1);
        }
    }

    for (int i = 0; i < sim->delivery_count; ++i) {
        const Delivery *delivery = &sim->deliveries[i];

        if (delivery->stage == DELIVERY_FINISHED) {
            continue;
        }

        assert(delivery->path != NULL);

        for (int j = i + 1; j < sim->delivery_count; ++j) {
            const Delivery *other = &sim->deliveries[j];

            if (other->stage != DELIVERY_FINISHED) {
                assert(delivery->plane_id != other->plane_id);
                assert(delivery->order_id != other->order_id ||
                       delivery->order_id == -1);
            }
        }
    }
}