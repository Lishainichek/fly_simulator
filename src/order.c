#include "../inc/order.h"
#include <stddef.h>
Order create_order(int id, Point destination, int weight,
                   int created_at, int deadline) {
    Order order;

    order.id = id;
    order.destination = destination;
    order.weight = weight;

    order.created_at = created_at;
    order.deadline = deadline;

    order.plane_id = -1;
    order.status = ORDER_PENDING;

    return order;
}

int order_assign(Order *order, int plane_id) {
    if (order == NULL || plane_id < 0) {
        return 0;
    }

    if (order->status != ORDER_PENDING) {
        return 0;
    }

    order->plane_id = plane_id;
    order->status = ORDER_ASSIGNED;

    return 1;
}

int order_start_delivery(Order *order) {
    if (order == NULL) {
        return 0;
    }

    if (order->status != ORDER_ASSIGNED) {
        return 0;
    }

    order->status = ORDER_IN_DELIVERY;

    return 1;
}

int order_deliver(Order *order, int plane_id, int current_time) {
    if (order == NULL) {
        return 0;
    }

    if (order->status != ORDER_IN_DELIVERY) {
        return 0;
    }

    if (order->plane_id != plane_id) {
        return 0;
    }

    if (current_time < order->created_at ||
        current_time > order->deadline) {
        return 0;
    }

    order->status = ORDER_DELIVERED;

    return 1;
}

int order_fail(Order *order) {
    if (order == NULL) {
        return 0;
    }

    if (order->status == ORDER_DELIVERED ||
        order->status == ORDER_FAILED) {
        return 0;
    }

    order->status = ORDER_FAILED;

    return 1;
}