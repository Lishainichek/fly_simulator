#ifndef MAP_H
#define MAP_H

typedef struct {
    int x;
    int y;
} Point;

typedef enum {
    CELL_FREE,
    CELL_BLOCKED,
    CELL_STATION,
    CELL_WAREHOUSE
} CellType;

typedef struct {
    CellType **field;
    int row;
    int colomn;
} Map;

Point make_point(int x, int y);

Map create_map(int row, int colomn);
void destroy_map(Map *map);

int is_valid(const Map *map, Point p);
void set_cell(Map *map, Point p, CellType type);

Point *nearest_way(const Map *map, Point start, Point finish, int *path_length);
void print_map(const Map *map);
void print_map_with_path(const Map *map, const Point *path, int length);

#endif