#include "dispatcher.h"
#include "scenario.h"
#include "simulation.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int passed = 0;
#define RUN(test) do { printf("[TEST] %s ... ", #test); fflush(stdout); \
                       test(); ++passed; puts("OK"); } while (0)

static void test_map_bfs(void) {
    Map map = create_map(5, 5);
    assert(map.field);
    for (int col = 0; col < 4; ++col)
        set_cell(&map, make_point(2, col), CELL_BLOCKED);
    int n = 0;
    Point *path = nearest_way(&map, make_point(0, 0), make_point(4, 0), &n);
    assert(path && n == 13);
    assert(path[0].x == 0 && path[n-1].x == 4);
    for (int i = 1; i < n; ++i) {
        int dx = abs(path[i].x - path[i-1].x);
        int dy = abs(path[i].y - path[i-1].y);
        assert(dx + dy == 1 && is_valid(&map, path[i]));
    }
    free(path);
    path = nearest_way(&map, make_point(0, 0), make_point(0, 0), &n);
    assert(path && n == 1);
    free(path);
    set_cell(&map, make_point(2, 4), CELL_BLOCKED);
    path = nearest_way(&map, make_point(0, 0), make_point(4, 0), &n);
    assert(!path && n == 0);
    path = nearest_way(&map, make_point(-1, 0), make_point(0, 0), &n);
    assert(!path);
    destroy_map(&map);
}

static void test_plane_energy(void) {
    Map map = create_map(2, 3);
    Plane plane = create_plane(1, make_point(0, 0), 3, 10, 5);
    assert(!plane_can_carry(&plane, 4));
    assert(plane_can_carry(&plane, 3));
    assert(plane_can_reach(&plane, 2));
    assert(!plane_can_reach(&plane, 3));
    assert(!plane_move(&plane, make_point(1, 1), &map));
    assert(plane_move(&plane, make_point(0, 1), &map));
    assert(plane.energy == 5 && plane.position.y == 1);
    set_cell(&map, make_point(0, 2), CELL_BLOCKED);
    assert(!plane_move(&plane, make_point(0, 2), &map));
    assert(plane_move(&plane, make_point(1, 1), &map));
    assert(plane.energy == 0);
    assert(!plane_move(&plane, make_point(1, 2), &map));
    destroy_map(&map);
}

static void test_order_states(void) {
    Order o = create_order(1, make_point(0, 0), 1, 2, 5);
    assert(!order_start_delivery(&o));
    assert(order_assign(&o, 8));
    assert(!order_assign(&o, 9));
    assert(order_start_delivery(&o));
    assert(!order_deliver(&o, 9, 3));
    assert(!order_deliver(&o, 8, 6));
    assert(order_deliver(&o, 8, 5));
    assert(!order_fail(&o));
    assert(!order_deliver(&o, 8, 5));
}

static void test_station_capacity(void) {
    Station station = create_station(2, make_point(0, 0), 1, 5);
    Plane p1 = create_plane(1, make_point(0, 0), 1, 10, 1);
    Plane p2 = create_plane(2, make_point(0, 0), 1, 20, 1);
    p1.energy = 5;
    p2.energy = 10;
    assert(station_arrive(&station, &p1));
    assert(!station_arrive(&station, &p2));
    assert(station.charging_count == 1);
    station_tick(&station, 1, 0);
    assert(p1.energy == 10 && p1.status == PLANE_IDLE);
    assert(station_arrive(&station, &p2));
    station_tick(&station, 2, 0);
    station_tick(&station, 3, 0);
    assert(p2.energy == 20 && p2.status == PLANE_IDLE);
    assert(station.charging_count == 0);
    destroy_station(&station);
}

static void test_dispatcher_filters(void) {
    Map map = create_map(4, 4);
    Point wh = make_point(0, 0);
    Station station = create_station(1, make_point(3, 3), 1, 2);
    Plane drones[2] = {
        create_plane(1, wh, 2, 30, 1),
        create_plane(2, wh, 5, 30, 1)
    };
    Dispatcher d = create_dispatcher(&map, wh, drones, 2, &station, 1);
    Order o = create_order(3, make_point(1, 1), 3, 0, 10);
    assert(dispatcher_assign(&d, &o, 0));
    assert(o.plane_id == 2);
    assert(drones[0].status == PLANE_IDLE && drones[1].status == PLANE_DELIVERING);
    Order o2 = create_order(4, make_point(1, 2), 10, 0, 20);
    assert(!dispatcher_assign(&d, &o2, 0));
    Order o3 = create_order(5, make_point(1, 1), 1, 0, 1);
    assert(!dispatcher_assign(&d, &o3, 0));
    destroy_station(&station);
    destroy_map(&map);
}

static void test_simulation_delivery(void) {
    Map map = create_map(3, 3);
    Point wh = make_point(0, 0);
    set_cell(&map, wh, CELL_WAREHOUSE);
    Station station = create_station(1, make_point(0, 0), 1, 5);
    Plane drone = create_plane(1, wh, 5, 50, 1);
    Order orders[] = {create_order(1, make_point(2, 2), 3, 0, 20)};
    Dispatcher d = create_dispatcher(&map, wh, &drone, 1, &station, 1);
    Simulation sim = create_simulation(d, orders, 1, 30);
    sim.verbose = 0;
    sim.weather = create_weather(0);
    assert(simulation_run(&sim));
    assert(orders[0].status == ORDER_DELIVERED);
    assert(drone.status == PLANE_IDLE || drone.status == PLANE_CHARGING);
    assert(drone.energy >= 0 && drone.energy <= drone.max_energy);
    assert(sim.total_distance == 8);
    assert(sim.current_time < 30);
    destroy_simulation(&sim);
    destroy_station(&station);
    destroy_map(&map);
}

static void test_weather_pause(void) {
    Map map = create_map(2, 3);
    Point wh = make_point(0, 0);
    Station station = create_station(1, wh, 1, 2);
    Plane drone = create_plane(1, wh, 5, 20, 1);
    Order orders[] = {create_order(1, make_point(0, 2), 1, 0, 9)};
    Dispatcher d = create_dispatcher(&map, wh, &drone, 1, &station, 1);
    Simulation sim = create_simulation(d, orders, 1, 20);
    sim.verbose = 0;
    sim.weather = create_weather(2);
    assert(simulation_tick(&sim));
    assert(drone.position.y == 0);
    assert(simulation_tick(&sim));
    assert(drone.position.y == 1);
    assert(simulation_tick(&sim));
    assert(drone.position.y == 1);
    assert(simulation_tick(&sim));
    assert(drone.position.y == 1);
    assert(simulation_run(&sim));
    assert(orders[0].status == ORDER_DELIVERED);
    destroy_simulation(&sim);
    destroy_station(&station);
    destroy_map(&map);
}

static void test_deadline_expiration(void) {
    Map map = create_map(5, 5);
    Point wh = make_point(0, 0);
    Station station = create_station(1, wh, 1, 3);
    Plane drone = create_plane(1, wh, 5, 100, 1);
    Order orders[] = {create_order(1, make_point(4, 4), 1, 0, 2)};
    Dispatcher d = create_dispatcher(&map, wh, &drone, 1, &station, 1);
    Simulation sim = create_simulation(d, orders, 1, 15);
    sim.verbose = 0;
    sim.weather = create_weather(0);
    assert(simulation_run(&sim));
    assert(orders[0].status == ORDER_FAILED);
    assert(sim.current_time <= 4);
    destroy_simulation(&sim);
    destroy_station(&station);
    destroy_map(&map);
}

static void test_recharge_idle_drone(void) {
    Map map = create_map(3, 3);
    Point wh = make_point(0, 0);
    Station station = create_station(1, wh, 1, 4);
    Plane drone = create_plane(1, wh, 2, 20, 1);
    drone.energy = 2;
    Order orders[] = {create_order(1, make_point(0, 2), 1, 3, 19)};
    Dispatcher d = create_dispatcher(&map, wh, &drone, 1, &station, 1);
    Simulation sim = create_simulation(d, orders, 1, 35);
    sim.weather = create_weather(0);
    sim.verbose = 0;
    assert(simulation_run(&sim));
    assert(orders[0].status == ORDER_DELIVERED);
    destroy_simulation(&sim);
    destroy_station(&station);
    destroy_map(&map);
}

static void test_scenario_parser(void) {
    char name[] = "/tmp/drone-sim-test-XXXXXX";
    int fd = mkstemp(name);
    assert(fd != -1);
    FILE *out = fdopen(fd, "w");
    assert(out);
    fputs("size 2 3\nwarehouse 0 0\nstation 1 1 2 1 2\n"
          "drone 1 1 2 3 30 7 1\norder 1 0 2 2 0 12\n"
          "max_time 22\nweather_interval 0\n", out);
    assert(fclose(out) == 0);
    Scenario s;
    char error[256];
    assert(scenario_load(&s, name, error, sizeof(error)));
    assert(s.map.row == 2 && s.map.colomn == 3);
    assert(s.order_count == 1 && s.planes[0].energy == 7);
    assert(s.weather_interval == 0 && s.max_time == 22);
    scenario_destroy(&s);
    out = fopen(name, "w");
    assert(out);
    fputs("size 2 3\nwarehouse 0 0\nstation 1 2 2 1 2\n", out);
    assert(fclose(out) == 0);
    assert(!scenario_load(&s, name, error, sizeof(error)));
    assert(strstr(error, "line") != NULL);
    unlink(name);
}

int main(void) {
    RUN(test_map_bfs);
    RUN(test_plane_energy);
    RUN(test_order_states);
    RUN(test_station_capacity);
    RUN(test_dispatcher_filters);
    RUN(test_simulation_delivery);
    RUN(test_weather_pause);
    RUN(test_deadline_expiration);
    RUN(test_recharge_idle_drone);
    RUN(test_scenario_parser);
    printf("Passed %d tests.\n", passed);
    return 0;
}
