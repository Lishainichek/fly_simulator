#ifndef ORDER_H
#define ORDER_H
#include "map.h"

typedef enum {
    ORDER_PENDING,
    ORDER_ASSIGNED,
    ORDER_IN_DELIVERY,
    ORDER_DELIVERED,
    ORDER_FAILED
} OrderStatus;

typedef struct {
    int id;

    Point destination;
    int weight;

    int created_at;
    int deadline;

    int plane_id;
    OrderStatus status;
} Order;

Order create_order(int id, Point destination, int weight,
                   int created_at, int deadline);

int order_assign(Order *order, int plane_id);

int order_start_delivery(Order *order);

int order_deliver(Order *order, int plane_id, int current_time);

int order_fail(Order *order);

#endif