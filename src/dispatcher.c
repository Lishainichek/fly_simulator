#include "../inc/dispatcher.h"

#include <stdlib.h>
#include <limits.h>

Dispatcher create_dispatcher(Map *map, Point warehouse,
                             Plane *planes, int plane_count,
                             Station *stations, int station_count) {
    Dispatcher dispatcher;

    dispatcher.map = map;
    dispatcher.warehouse = warehouse;

    dispatcher.planes = planes;
    dispatcher.plane_count = plane_count;

    dispatcher.stations = stations;
    dispatcher.station_count = station_count;

    return dispatcher;
}

static int get_distance(const Map *map, Point start, Point finish) {
    int length = 0;

    Point *path = nearest_way(map, start, finish, &length);

    if (path == NULL) {
        return -1;
    }

    free(path);

    return length - 1;
}

static int nearest_station_distance(const Dispatcher *dispatcher,
                                    Point position) {
    int best = INT_MAX;

    for (int i = 0; i < dispatcher->station_count; ++i) {
        Station *station = &dispatcher->stations[i];

        if (station->capacity <= 0 || station->charging == NULL) {
            continue;
        }

        int distance = get_distance(
            dispatcher->map,
            position,
            station->position
        );

        if (distance >= 0 && distance < best) {
            best = distance;
        }
    }

    if (best == INT_MAX) {
        return -1;
    }

    return best;
}

int dispatcher_assign(Dispatcher *dispatcher, Order *order,
                      int current_time) {
    if (dispatcher == NULL || order == NULL ||
        dispatcher->map == NULL) {
        return 0;
    }

    if (order->status != ORDER_PENDING ||
        current_time < order->created_at ||
        current_time > order->deadline) {
        return 0;
    }

    int best_plane = -1;
    int best_time = INT_MAX;

    int delivery_distance = get_distance(
        dispatcher->map,
        dispatcher->warehouse,
        order->destination
    );

    if (delivery_distance < 0) {
        return 0;
    }

    int station_distance = nearest_station_distance(
        dispatcher,
        order->destination
    );

    if (station_distance < 0) {
        return 0;
    }

    for (int i = 0; i < dispatcher->plane_count; ++i) {
        Plane *plane = &dispatcher->planes[i];

        if (plane->status != PLANE_IDLE ||
            plane->station_id != -1) {
            continue;
        }

        if (!plane_can_carry(plane, order->weight)) {
            continue;
        }

        int warehouse_distance = get_distance(
            dispatcher->map,
            plane->position,
            dispatcher->warehouse
        );

        if (warehouse_distance < 0) {
            continue;
        }

        long long total_distance =
            (long long)warehouse_distance +
            delivery_distance +
            station_distance;

        long long delivery_time =
            (long long)current_time +
            warehouse_distance +
            delivery_distance;

        if (delivery_time > order->deadline) {
            continue;
        }

        if (total_distance > INT_MAX) {
            continue;
        }

        if (!plane_can_reach(plane, (int)total_distance)) {
            continue;
        }

        if (delivery_time < best_time) {
            best_time = (int)delivery_time;
            best_plane = i;
        }
    }

    if (best_plane == -1) {
        return 0;
    }

    Plane *plane = &dispatcher->planes[best_plane];

    if (!order_assign(order, plane->id)) {
        return 0;
    }

    plane->status = PLANE_DELIVERING;

    return 1;
}