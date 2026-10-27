#include <stdlib.h>
#include "../inc/map.h"
#include <stdio.h>
Point make_point(int x, int y) {
    Point p;
    p.x = x;
    p.y = y;
    return p;
}

Map create_map(int row, int colomn) {
    Map map;
    map.row = row;
    map.colomn = colomn;
    map.field = NULL;

    if (row <= 0 || colomn <= 0) {
        return map;
    }

    map.field = malloc(row * sizeof(*map.field));

    if (map.field == NULL) {
        return map;
    }

    for (int i = 0; i < row; ++i) {
        map.field[i] = malloc(colomn * sizeof(*map.field[i]));

        if (map.field[i] == NULL) {
            for (int j = 0; j < i; ++j) {
                free(map.field[j]);
            }
            free(map.field);
            map.field = NULL;
            return map;
        }

        for (int j = 0; j < colomn; ++j) {
            map.field[i][j] = CELL_FREE;
        }
    }

    return map;
}

void destroy_map(Map *map) {
    if (map == NULL || map->field == NULL) {
        return;
    }

    for (int i = 0; i < map->row; ++i) {
        free(map->field[i]);
    }

    free(map->field);
    map->field = NULL;
}

int is_valid(const Map *map, Point p) {
    if (map == NULL || map->field == NULL) {
        return 0;
    }

    if (p.x < 0 || p.x >= map->row) {
        return 0;
    }

    if (p.y < 0 || p.y >= map->colomn) {
        return 0;
    }

    return map->field[p.x][p.y] != CELL_BLOCKED;
}

void set_cell(Map *map, Point p, CellType type) {
    if (map == NULL || map->field == NULL) {
        return;
    }

    if (p.x < 0 || p.x >= map->row ||
        p.y < 0 || p.y >= map->colomn) {
        return;
    }

    map->field[p.x][p.y] = type;
}

Point *nearest_way(const Map *map, Point start, Point finish, int *path_length) {
    if (path_length == NULL) {
        return NULL;
    }

    *path_length = 0;

    if (!is_valid(map, start) || !is_valid(map, finish)) {
        return NULL;
    }

    int n = map->row;
    int m = map->colomn;
    int total = n * m;

    Point *queue = malloc(total * sizeof(Point));
    Point *parent = malloc(total * sizeof(Point));
    int *used = calloc(total, sizeof(int));

    if (queue == NULL || parent == NULL || used == NULL) {
        free(queue);
        free(parent);
        free(used);
        return NULL;
    }

    int head = 0;
    int tail = 0;

    queue[tail++] = start;
    used[start.x * m + start.y] = 1;

    int dx[4] = {1, -1, 0, 0};
    int dy[4] = {0, 0, 1, -1};

    while (head < tail) {
        Point cur = queue[head++];

        if (cur.x == finish.x && cur.y == finish.y) {
            break;
        }

        for (int i = 0; i < 4; ++i) {
            Point next = make_point(cur.x + dx[i], cur.y + dy[i]);

            if (!is_valid(map, next)) {
                continue;
            }

            int id = next.x * m + next.y;

            if (used[id]) {
                continue;
            }

            used[id] = 1;
            parent[id] = cur;
            queue[tail++] = next;
        }
    }

    int finish_id = finish.x * m + finish.y;

    if (!used[finish_id]) {
        free(queue);
        free(parent);
        free(used);
        return NULL;
    }

    int length = 1;
    Point cur = finish;

    while (cur.x != start.x || cur.y != start.y) {
        cur = parent[cur.x * m + cur.y];
        ++length;
    }

    Point *path = malloc(length * sizeof(Point));

    if (path == NULL) {
        free(queue);
        free(parent);
        free(used);
        return NULL;
    }

    cur = finish;

    for (int i = length - 1; i >= 0; --i) {
        path[i] = cur;

        if (i > 0) {
            cur = parent[cur.x * m + cur.y];
        }
    }
    *path_length = length;
    free(queue);
    free(parent);
    free(used);
    return path;
}


void print_map_with_path(const Map *map, const Point *path, int length) {
    if (map == NULL || map->field == NULL) {
        return;
    }

    printf("   ");
    for (int j = 0; j < map->colomn; ++j) {
        printf("%3d", j);
    }
    printf("\n");

    for (int i = 0; i < map->row; ++i) {
        printf("%3d", i);

        for (int j = 0; j < map->colomn; ++j) {
            char symbol = '.';

            switch (map->field[i][j]) {
                case CELL_FREE:
                    symbol = '.';
                    break;
                case CELL_BLOCKED:
                    symbol = '#';
                    break;
                case CELL_STATION:
                    symbol = 'C';
                    break;
                case CELL_WAREHOUSE:
                    symbol = 'W';
                    break;
            }

            for (int k = 0; k < length; ++k) {
                if (path[k].x == i && path[k].y == j) {
                    symbol = '*';
                    break;
                }
            }

            if (path != NULL && length > 0) {
                if (path[0].x == i && path[0].y == j) {
                    symbol = 'S';
                }

                if (path[length - 1].x == i &&
                    path[length - 1].y == j) {
                    symbol = 'F';
                }
            }

            printf("%3c", symbol);
        }

        printf("\n");
    }
}

void print_map(const Map *map) {
    print_map_with_path(map, NULL, 0);
}