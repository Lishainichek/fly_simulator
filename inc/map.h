

typedef struct {
    int x;
    int y;
} Point;
void make_point(Point* point, int x, int y);

enum CellType {
    CELL_FREE,
    CELL_BLOCKED,
    CELL_STATION,
    CELL_WAREHOUSE
};

typedef struct {
    enum CellType **field;
    int row;
    int colomn;
} Map;


void create_map(Map* map, int count_row, int count_colomns);
Point* nearest_way(Point X, Point Y);