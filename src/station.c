#include "station.h"
#include <stdio.h>
#include <stdlib.h>

Station create_station(int id, Point position, int capacity, int speed) {
    Station s = {0};
    s.id = id;
    s.position = position;
    if (capacity <= 0 || speed <= 0) return s;
    s.capacity = capacity;
    s.charging_speed = speed;
    s.charging = calloc((size_t)capacity, sizeof(*s.charging));
    if (!s.charging) s.capacity = 0;
    return s;
}

void destroy_station(Station *s) {
    free(s->charging);
    s->charging = NULL;
    s->charging_count = 0;
}

int station_arrive(Station *s, Plane *p) {
    if (!s || !p || !s->charging || s->charging_count >= s->capacity ||
        p->station_id != -1 || p->energy >= p->max_energy ||
        p->position.x != s->position.x || p->position.y != s->position.y)
        return 0;
    s->charging[s->charging_count++] = p;
    p->status = PLANE_CHARGING;
    p->station_id = s->id;
    return 1;
}

void station_tick(Station *s, int time, int verbose) {
    for (int i = 0; i < s->charging_count;) {
        Plane *p = s->charging[i];
        int missing = p->max_energy - p->energy;
        if (missing <= s->charging_speed) {
            p->energy = p->max_energy;
            p->status = PLANE_IDLE;
            p->station_id = -1;
            if (verbose) printf("[%d] Drone #%d finished charging at station #%d\n",
                                time, p->id, s->id);
            s->charging[i] = s->charging[--s->charging_count];
        } else {
            p->energy += s->charging_speed;
            if (verbose) printf("[%d] Drone #%d charging: %d/%d\n",
                                time, p->id, p->energy, p->max_energy);
            ++i;
        }
    }
}
