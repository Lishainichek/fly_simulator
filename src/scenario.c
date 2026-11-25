#include "scenario.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void scenario_destroy(Scenario *s) {
    if (!s) return;
    for (int i = 0; i < s->station_count; ++i)
        destroy_station(&s->stations[i]);
    free(s->stations);
    free(s->planes);
    free(s->orders);
    destroy_map(&s->map);
    memset(s, 0, sizeof(*s));
}

static int add_plane(Scenario *s, Plane plane) {
    if (s->plane_count >= 10000) return 0;
    Plane *new_array = realloc(s->planes,
        ((size_t)s->plane_count + 1) * sizeof(*s->planes));
    if (!new_array) return 0;
    s->planes = new_array;
    s->planes[s->plane_count++] = plane;
    return 1;
}

static int add_station(Scenario *s, Station station) {
    if (s->station_count >= 10000) return 0;
    Station *new_array = realloc(s->stations,
        ((size_t)s->station_count + 1) * sizeof(*s->stations));
    if (!new_array) return 0;
    s->stations = new_array;
    s->stations[s->station_count++] = station;
    return 1;
}

static int add_order(Scenario *s, Order order) {
    if (s->order_count >= 10000) return 0;
    Order *new_array = realloc(s->orders,
        ((size_t)s->order_count + 1) * sizeof(*s->orders));
    if (!new_array) return 0;
    s->orders = new_array;
    s->orders[s->order_count++] = order;
    return 1;
}

static int valid_free_cell(const Scenario *s, Point p) {
    return is_valid(&s->map, p) && s->map.field[p.x][p.y] == CELL_FREE;
}

static int next_tokens(const char *line, char *command, int *v, int *count) {
    char rest[256];
    size_t length = strcspn(line, "#\n\r");
    if (length >= sizeof(rest)) return -1;
    memcpy(rest, line, length);
    rest[length] = '\0';
    char *token = strtok(rest, " \t");
    if (!token) return 0;
    if (strlen(token) >= 32) return -1;
    strcpy(command, token);
    int n = 0;
    while ((token = strtok(NULL, " \t")) != NULL) {
        if (n == 8) return -1;
        char *end = NULL;
        errno = 0;
        long number = strtol(token, &end, 10);
        if (errno != 0 || !end || *end || number < INT_MIN || number > INT_MAX)
            return -1;
        v[n++] = (int)number;
    }
    *count = n;
    return 1;
}

int scenario_load(Scenario *s, const char *filename,
                  char *error, size_t error_size) {
    if (!s || !filename || !error || error_size == 0) return 0;
    memset(s, 0, sizeof(*s));
    error[0] = '\0';
    FILE *f = fopen(filename, "r");
    if (!f) {
        snprintf(error, error_size, "Cannot open scenario: %s", filename);
        return 0;
    }
    s->max_time = 150;
    s->weather_interval = 5;
    char line[512];
    int line_no = 0;
    while (fgets(line, sizeof(line), f)) {
        ++line_no;
        if (!strchr(line, '\n') && !feof(f)) goto invalid;
        char command[32];
        int v[8] = {0}, n = 0;
        int status = next_tokens(line, command, v, &n);
        if (status == 0) continue;
        if (status < 0) goto invalid;
        if (strcmp(command, "size") == 0 && n == 2 && !s->map.field &&
            v[0] > 0 && v[1] > 0 && v[0] <= 500 && v[1] <= 500) {
            s->map = create_map(v[0], v[1]);
            if (!s->map.field) goto invalid;
        } else if (strcmp(command, "warehouse") == 0 && n == 2 &&
                   !s->has_warehouse && valid_free_cell(s, make_point(v[0], v[1]))) {
            s->warehouse = make_point(v[0], v[1]);
            s->has_warehouse = 1;
            set_cell(&s->map, s->warehouse, CELL_WAREHOUSE);
        } else if (strcmp(command, "block") == 0 && n == 2 &&
                   valid_free_cell(s, make_point(v[0], v[1]))) {
            set_cell(&s->map, make_point(v[0], v[1]), CELL_BLOCKED);
        } else if (strcmp(command, "station") == 0 && n == 5 &&
                   v[0] >= 0 && v[3] > 0 && v[3] <= 10000 &&
                   v[4] > 0 &&
                   valid_free_cell(s, make_point(v[1], v[2]))) {
            for (int i = 0; i < s->station_count; ++i)
                if (s->stations[i].id == v[0]) goto invalid;
            Station station = create_station(v[0], make_point(v[1], v[2]),
                                              v[3], v[4]);
            if (!station.charging) goto invalid;
            if (!add_station(s, station)) {
                destroy_station(&station);
                goto invalid;
            }
            set_cell(&s->map, station.position, CELL_STATION);
        } else if (strcmp(command, "drone") == 0 && n == 7 &&
                   v[0] >= 0 && v[3] > 0 && v[4] > 0 &&
                   v[5] >= 0 && v[5] <= v[4] && v[6] > 0 &&
                   is_valid(&s->map, make_point(v[1], v[2]))) {
            for (int i = 0; i < s->plane_count; ++i)
                if (s->planes[i].id == v[0]) goto invalid;
            Plane plane = create_plane(v[0], make_point(v[1], v[2]),
                                        v[3], v[4], v[6]);
            plane.energy = v[5];
            if (!add_plane(s, plane)) goto invalid;
        } else if (strcmp(command, "order") == 0 && n == 6 &&
                   v[0] >= 0 && v[3] >= 0 && v[4] >= 0 && v[5] >= v[4] &&
                   is_valid(&s->map, make_point(v[1], v[2]))) {
            for (int i = 0; i < s->order_count; ++i)
                if (s->orders[i].id == v[0]) goto invalid;
            if (!add_order(s, create_order(v[0], make_point(v[1], v[2]),
                                           v[3], v[4], v[5]))) goto invalid;
        } else if (strcmp(command, "weather_interval") == 0 && n == 1 && v[0] >= 0) {
            s->weather_interval = v[0];
        } else if (strcmp(command, "max_time") == 0 && n == 1 && v[0] >= 0) {
            s->max_time = v[0];
        } else {
            goto invalid;
        }
    }
    if (ferror(f)) goto invalid;
    fclose(f);
    if (!s->map.field || !s->has_warehouse || !s->station_count ||
        !s->plane_count) {
        snprintf(error, error_size,
                 "Scenario must define size, warehouse, station and drone");
        scenario_destroy(s);
        return 0;
    }
    for (int i = 0; i < s->plane_count; ++i) {
        if (!is_valid(&s->map, s->planes[i].position)) {
            snprintf(error, error_size, "Drone is placed in a blocked cell");
            scenario_destroy(s);
            return 0;
        }
    }
    for (int i = 0; i < s->order_count; ++i) {
        if (!is_valid(&s->map, s->orders[i].destination)) {
            snprintf(error, error_size, "Order destination is blocked");
            scenario_destroy(s);
            return 0;
        }
    }
    return 1;
invalid:
    snprintf(error, error_size, "Invalid scenario data near line %d", line_no);
    fclose(f);
    scenario_destroy(s);
    return 0;
}
