#include "dispatcher.h"
#include <limits.h>
#include <stdlib.h>

Dispatcher create_dispatcher(Map *map, Point warehouse, Plane *planes,
                             int plane_count, Station *stations, int station_count) {
    Dispatcher d = {map, warehouse, planes, plane_count, stations, station_count};
    return d;
}

static int distance(const Map *map, Point from, Point to) {
    int length = 0;
    Point *path = nearest_way(map, from, to, &length);
    free(path);
    return length == 0 ? -1 : length - 1;
}

int dispatcher_assign(Dispatcher *d, Order *order, int time) {
    if (order->status != ORDER_PENDING || time < order->created_at ||
        time > order->deadline) return 0;

    int to_client = distance(d->map, d->warehouse, order->destination);
    if (to_client < 0) return 0;
    int to_station = INT_MAX;
    for (int j = 0; j < d->station_count; ++j) {
        Station *s = &d->stations[j];
        if (s->capacity <= 0) continue;
        int steps = distance(d->map, order->destination, s->position);
        if (steps >= 0 && steps < to_station) to_station = steps;
    }
    if (to_station == INT_MAX) return 0;

    int best = -1;
    long long best_eta = LLONG_MAX;
    for (int i = 0; i < d->plane_count; ++i) {
        Plane *p = &d->planes[i];
        if (p->status != PLANE_IDLE || !plane_can_carry(p, order->weight))
            continue;
        int to_warehouse = distance(d->map, p->position, d->warehouse);
        if (to_warehouse < 0) continue;
        int pickup_ticks = to_warehouse > 0 ? to_warehouse : 1;
        int delivery_ticks = to_client > 0 ? to_client : 1;
        long long eta = (long long)time + pickup_ticks + delivery_ticks - 1;
        long long total_steps = (long long)to_warehouse + to_client + to_station;
        if (eta > order->deadline || total_steps > INT_MAX ||
            !plane_can_reach(p, (int)total_steps)) continue;
        if (eta < best_eta) { best = i; best_eta = eta; }
    }
    if (best < 0) return 0;
    Plane *p = &d->planes[best];
    if (!order_assign(order, p->id)) return 0;
    p->status = PLANE_DELIVERING;
    return 1;
}
