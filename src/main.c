#include "../inc/map.h"
#include "../inc/plane.h"
#include "../inc/order.h"
#include "../inc/station.h"
#include "../inc/dispatcher.h"
#include "../inc/simulation.h"
#include "../inc/weather.h"

#include <stdio.h>
#include <stdlib.h>

void print_menu(void) {
    printf("\n========== DRONE DELIVERY SIMULATOR ==========\n");
    printf("1. Start simulation\n");
    printf("2. Show map\n");
    printf("3. Show drones and stations\n");
    printf("4. Exit\n");
    printf("Choose an option: ");
}

void print_entities(const Dispatcher *dispatcher) {
    printf("\n=== DRONES ===\n");

    for (int i = 0; i < dispatcher->plane_count; ++i) {
        const Plane *plane = &dispatcher->planes[i];

        printf(
            "Plane #%d: position=(%d,%d), "
            "energy=%d/%d, capacity=%d, consumption=%d\n",
            plane->id,
            plane->position.x,
            plane->position.y,
            plane->energy,
            plane->max_energy,
            plane->capacity,
            plane->consumption
        );
    }

    printf("\n=== STATIONS ===\n");

    for (int i = 0; i < dispatcher->station_count; ++i) {
        const Station *station = &dispatcher->stations[i];

        printf(
            "Station #%d: position=(%d,%d), "
            "capacity=%d, charging_speed=%d\n",
            station->id,
            station->position.x,
            station->position.y,
            station->capacity,
            station->charging_speed
        );
    }
}

void print_orders(const Simulation *sim) {
    printf("\n=== ORDERS ===\n");

    for (int i = 0; i < sim->order_count; ++i) {
        const Order *order = &sim->orders[i];

        printf(
            "Order #%d: destination=(%d,%d), "
            "weight=%d, created_at=%d, deadline=%d, status=%d\n",
            order->id,
            order->destination.x,
            order->destination.y,
            order->weight,
            order->created_at,
            order->deadline,
            order->status
        );
    }
}

void run_menu(Simulation *sim) {
    int choice;
    int simulation_started = 0;

    while (1) {
        print_menu();

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input\n");
            break;
        }

        if (choice == 1) {
            if (simulation_started) {
                printf("Simulation has already been completed.\n");
                continue;
            }

            simulation_started = 1;

            printf("\n=== SIMULATION START ===\n");

            simulation_run(sim);

            printf("\n=== SIMULATION FINISHED ===\n");

            simulation_print_statistics(sim);

        } else if (choice == 2) {
            printf("\n=== MAP ===\n");
            print_map(sim->dispatcher.map);

        } else if (choice == 3) {
            print_entities(&sim->dispatcher);
            print_orders(sim);

        } else if (choice == 4) {
            printf("Exiting...\n");
            break;

        } else {
            printf("Unknown option\n");
        }
    }
}

int main(void) {
    Map map = create_map(10, 10);

    Point warehouse = make_point(0, 0);
    set_cell(&map, warehouse, CELL_WAREHOUSE);

    for (int y = 2; y <= 7; ++y) {
        set_cell(&map, make_point(4, y), CELL_BLOCKED);
    }

    Point station_positions[2] = {
        make_point(0, 9),
        make_point(9, 9)
    };

    for (int i = 0; i < 2; ++i) {
        set_cell(&map, station_positions[i], CELL_STATION);
    }

    Station stations[2] = {
        create_station(1, station_positions[0], 2, 10, 3),
        create_station(2, station_positions[1], 1, 15, 3)
    };

    Plane planes[3] = {
        create_plane(1, station_positions[0], 5, 150, 5),
        create_plane(2, station_positions[0], 10, 200, 5),
        create_plane(3, station_positions[1], 7, 180, 5)
    };

    Order orders[6] = {
        create_order(1, make_point(2, 2), 3, 0, 40),
        create_order(2, make_point(8, 2), 6, 5, 60),
        create_order(3, make_point(7, 7), 4, 10, 80),
        create_order(4, make_point(3, 8), 9, 20, 100),
        create_order(5, make_point(8, 8), 5, 30, 120),
        create_order(6, make_point(1, 5), 2, 40, 130)
    };

    Dispatcher dispatcher = create_dispatcher(
        &map,
        warehouse,
        planes,
        3,
        stations,
        2
    );

    Simulation sim = create_simulation(
        dispatcher,
        orders,
        6,
        150
    );

    run_menu(&sim);

    destroy_simulation(&sim);

    for (int i = 0; i < 2; ++i) {
        destroy_station(&stations[i]);
    }

    destroy_map(&map);

    return 0;
}