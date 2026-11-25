#include "scenario.h"
#include "simulation.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 64
#define OUT_CAP 65536
#define ROLE_OBSERVER 5

typedef struct {
    int fd;
    int role;
    char input[128];
    size_t input_len;
    char output[OUT_CAP];
    size_t output_len;
    size_t output_sent;
} Client;

typedef struct {
    Client *clients;
    Simulation *simulation;
    int quiet;
} Server;

static volatile sig_atomic_t stop_requested = 0;
static const char *const role_names[] = {
    "orders", "dispatcher", "drones", "stations", "weather", "observer"
};

static void on_signal(int number) {
    (void)number;
    stop_requested = 1;
}

static long long milliseconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int parse_integer(const char *text, int low, int high, int *value) {
    char *end = NULL;
    errno = 0;
    long number = strtol(text, &end, 10);
    if (errno || !*text || !end || *end || number < low || number > high) return 0;
    *value = (int)number;
    return 1;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void close_client(Client *client) {
    if (client->fd >= 0) close(client->fd);
    client->fd = -1;
    client->role = -1;
    client->input_len = client->output_len = client->output_sent = 0;
}

static void queue_text(Client *client, const char *text) {
    if (client->fd < 0) return;
    size_t length = strlen(text);
    if (client->output_sent) {
        memmove(client->output, client->output + client->output_sent,
                client->output_len - client->output_sent);
        client->output_len -= client->output_sent;
        client->output_sent = 0;
    }
    if (length > OUT_CAP - client->output_len) {
        close_client(client);
        return;
    }
    memcpy(client->output + client->output_len, text, length);
    client->output_len += length;
}

static void queue_format(Client *client, const char *format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    queue_text(client, line);
}

static void flush_client(Client *client) {
    while (client->fd >= 0 && client->output_sent < client->output_len) {
        ssize_t sent = send(client->fd, client->output + client->output_sent,
                            client->output_len - client->output_sent, MSG_NOSIGNAL);
        if (sent > 0) {
            client->output_sent += (size_t)sent;
        } else if (sent < 0 && errno == EINTR) {
            continue;
        } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            close_client(client);
            return;
        }
    }
    if (client->output_sent == client->output_len)
        client->output_sent = client->output_len = 0;
}

static void count_orders(const Simulation *sim, int *delivered, int *failed) {
    *delivered = *failed = 0;
    for (int i = 0; i < sim->order_count; ++i) {
        if (sim->orders[i].status == ORDER_DELIVERED) ++*delivered;
        if (sim->orders[i].status == ORDER_FAILED) ++*failed;
    }
}

static const char *plane_status_name(PlaneStatus status) {
    static const char *names[] = {
        "IDLE", "DELIVERING", "RETURNING", "CHARGING", "WAITING"
    };
    return (status >= PLANE_IDLE && status <= PLANE_WAITING)
           ? names[status] : "UNKNOWN";
}

static const char *order_status_name(OrderStatus status) {
    static const char *names[] = {
        "PENDING", "ASSIGNED", "IN_DELIVERY", "DELIVERED", "FAILED"
    };
    return (status >= ORDER_PENDING && status <= ORDER_FAILED)
           ? names[status] : "UNKNOWN";
}

static void queue_state(Client *client, const Simulation *sim, int detailed) {
    int delivered, failed;
    count_orders(sim, &delivered, &failed);
    queue_format(client, "STATE tick=%d total=%d delivered=%d failed=%d distance=%lld weather=%s\n",
                 sim->current_time, sim->order_count, delivered, failed,
                 sim->total_distance, weather_name(&sim->weather));
    if (!detailed) return;
    for (int i = 0; i < sim->dispatcher.plane_count; ++i) {
        const Plane *p = &sim->dispatcher.planes[i];
        queue_format(client, "DRONE id=%d position=(%d,%d) energy=%d/%d status=%s\n",
                     p->id, p->position.x, p->position.y,
                     p->energy, p->max_energy, plane_status_name(p->status));
    }
    for (int i = 0; i < sim->order_count; ++i) {
        const Order *o = &sim->orders[i];
        queue_format(client, "ORDER id=%d destination=(%d,%d) status=%s plane=%d\n",
                     o->id, o->destination.x, o->destination.y,
                     order_status_name(o->status), o->plane_id);
    }
    for (int i = 0; i < sim->dispatcher.station_count; ++i) {
        const Station *s = &sim->dispatcher.stations[i];
        queue_format(client, "STATION id=%d charging=%d/%d\n",
                     s->id, s->charging_count, s->capacity);
    }
}

static void event_callback(int role, const char *message, void *context) {
    Server *server = context;
    if (!server->quiet && role == 4) puts(message);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        Client *client = &server->clients[i];
        if (client->fd < 0 || (client->role != ROLE_OBSERVER &&
            client->role != role && !(role == 4 && client->role == 2))) continue;
        queue_format(client, "EVENT %s %s\n", role_names[role], message);
    }
}

static void handshake(Server *server, Client *client, const char *line) {
    if (strncmp(line, "ROLE ", 5) != 0) {
        close_client(client);
        return;
    }
    for (int role = 0; role < 6; ++role) {
        if (role == 4) continue; /* Weather is a system event, not a separate client. */
        if (strcmp(line + 5, role_names[role]) == 0) {
            client->role = role;
            queue_format(client, "WELCOME %s\n", role_names[role]);
            if (role == ROLE_OBSERVER) queue_state(client, server->simulation, 1);
            return;
        }
    }
    close_client(client);
}

static void read_client(Server *server, Client *client) {
    char buffer[256];
    for (;;) {
        ssize_t count = recv(client->fd, buffer, sizeof(buffer), 0);
        if (count == 0) { close_client(client); return; }
        if (count < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) close_client(client);
            return;
        }
        for (ssize_t i = 0; i < count; ++i) {
            char ch = buffer[i];
            if (ch == '\n') {
                client->input[client->input_len] = '\0';
                if (client->input_len > 0 && client->input[client->input_len - 1] == '\r')
                    client->input[--client->input_len] = '\0';
                if (client->role == -1) handshake(server, client, client->input);
                client->input_len = 0;
                if (client->fd < 0) return;
            } else if (client->input_len + 1 < sizeof(client->input)) {
                client->input[client->input_len++] = ch;
            } else {
                close_client(client);
                return;
            }
        }
    }
}

static void network_events(Server *server, int listen_fd, int timeout_ms) {
    struct pollfd descriptors[MAX_CLIENTS + 1];
    descriptors[0] = (struct pollfd){.fd = listen_fd, .events = POLLIN};
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        Client *client = &server->clients[i];
        descriptors[i + 1] = (struct pollfd){
            .fd = client->fd,
            .events = POLLIN | (client->output_len > client->output_sent ? POLLOUT : 0)
        };
    }
    int ready = poll(descriptors, MAX_CLIENTS + 1, timeout_ms);
    if (ready <= 0) return;
    if (descriptors[0].revents & POLLIN) {
        for (;;) {
            int fd = accept(listen_fd, NULL, NULL);
            if (fd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    perror("accept");
                break;
            }
            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; ++i)
                if (server->clients[i].fd < 0) { slot = i; break; }
            if (slot < 0 || !set_nonblocking(fd)) close(fd);
            else {
                server->clients[slot].fd = fd;
                server->clients[slot].role = -1;
            }
        }
    }
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        Client *client = &server->clients[i];
        if (client->fd < 0) continue;
        short flags = descriptors[i + 1].revents;
        if (flags & (POLLERR | POLLNVAL | POLLHUP)) { close_client(client); continue; }
        if (flags & POLLIN) read_client(server, client);
        if (client->fd >= 0 && ((flags & POLLOUT) || client->output_len))
            flush_client(client);
    }
}

static int open_listener(const char *address, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in endpoint = {0};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, address, &endpoint.sin_addr) != 1 ||
        bind(fd, (struct sockaddr *)&endpoint, sizeof(endpoint)) != 0 ||
        listen(fd, 32) != 0 || !set_nonblocking(fd)) {
        perror("listen/bind");
        close(fd);
        return -1;
    }
    return fd;
}

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s --bind IPv4 --port PORT [--scenario FILE] [--delay MS] "
                    "[--max-time T] [--weather-interval T] [--startup-ms MS] [--quiet]\n",
            program);
}

int main(int argc, char **argv) {
    const char *address = NULL, *scenario_path = "scenarios/demo.txt";
    int port = 0, delay = 100, startup = 800, quiet = 0;
    int max_time = -1, weather_interval = -1;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--bind") && ++i < argc) address = argv[i];
        else if (!strcmp(argv[i], "--port") && ++i < argc &&
                 parse_integer(argv[i], 1, 65535, &port)) {}
        else if (!strcmp(argv[i], "--scenario") && ++i < argc) scenario_path = argv[i];
        else if (!strcmp(argv[i], "--delay") && ++i < argc &&
                 parse_integer(argv[i], 1, 60000, &delay)) {}
        else if (!strcmp(argv[i], "--startup-ms") && ++i < argc &&
                 parse_integer(argv[i], 0, 60000, &startup)) {}
        else if (!strcmp(argv[i], "--max-time") && ++i < argc &&
                 parse_integer(argv[i], 0, 10000000, &max_time)) {}
        else if (!strcmp(argv[i], "--weather-interval") && ++i < argc &&
                 parse_integer(argv[i], 0, 10000000, &weather_interval)) {}
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else { usage(argv[0]); return 1; }
    }
    if (!address || !port) { usage(argv[0]); return 1; }

    Scenario scenario;
    char error[256];
    if (!scenario_load(&scenario, scenario_path, error, sizeof(error))) {
        fprintf(stderr, "Scenario error: %s\n", error);
        return 1;
    }
    Dispatcher dispatcher = create_dispatcher(&scenario.map, scenario.warehouse,
                      scenario.planes, scenario.plane_count,
                      scenario.stations, scenario.station_count);
    Simulation sim = create_simulation(dispatcher, scenario.orders,
                                      scenario.order_count, scenario.max_time);
    if (max_time >= 0) sim.max_time = max_time;
    sim.weather = create_weather(weather_interval >= 0 ? weather_interval :
                                 scenario.weather_interval);
    sim.verbose = 1; /* Events are always available to network clients. */

    Server server = {.simulation = &sim, .quiet = quiet};
    server.clients = calloc(MAX_CLIENTS, sizeof(*server.clients));
    if (!server.clients || (sim.dispatcher.plane_count > 0 && !sim.deliveries)) {
        fprintf(stderr, "Out of memory\n");
        free(server.clients);
        destroy_simulation(&sim);
        scenario_destroy(&scenario);
        return 1;
    }
    for (int i = 0; i < MAX_CLIENTS; ++i) server.clients[i].fd = -1;
    int listener = open_listener(address, port);
    if (listener < 0) {
        free(server.clients);
        destroy_simulation(&sim);
        scenario_destroy(&scenario);
        return 1;
    }
    sim.event_callback = event_callback;
    sim.event_context = &server;
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    printf("TCP server listening on %s:%d, scenario=%s\n", address, port, scenario_path);
    fflush(stdout);

    long long next_tick = milliseconds() + startup;
    while (!stop_requested && (sim.max_time == 0 || sim.current_time < sim.max_time)
           && !simulation_all_done(&sim)) {
        long long now = milliseconds();
        int remaining = (int)(next_tick - now);
        if (remaining > 0) {
            network_events(&server, listener, remaining > 100 ? 100 : remaining);
            continue;
        }
        simulation_step_weather(&sim);
        for (int role = 0; role < 4; ++role) simulation_step_role(&sim, role);
        ++sim.current_time;
        if (!quiet && sim.current_time % 10 == 0)
            printf("[server] tick %d\n", sim.current_time);
        for (int i = 0; i < MAX_CLIENTS; ++i)
            if (server.clients[i].fd >= 0 && server.clients[i].role == ROLE_OBSERVER)
                queue_state(&server.clients[i], &sim, 0);
        network_events(&server, listener, 0);
        next_tick = milliseconds() + delay;
    }
    if (!stop_requested) simulation_finish_time_limit(&sim);
    simulation_print_statistics(&sim);
    int delivered, failed;
    count_orders(&sim, &delivered, &failed);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        Client *client = &server.clients[i];
        if (client->fd < 0) continue;
        if (client->role == ROLE_OBSERVER) queue_state(client, &sim, 1);
        queue_format(client, "FINISHED tick=%d delivered=%d failed=%d distance=%lld interrupted=%d\n",
                     sim.current_time, delivered, failed, sim.total_distance, stop_requested ? 1 : 0);
    }
    long long finish_deadline = milliseconds() + 400;
    while (milliseconds() < finish_deadline) {
        int pending = 0;
        for (int i = 0; i < MAX_CLIENTS; ++i)
            if (server.clients[i].fd >= 0 && server.clients[i].output_len)
                pending = 1;
        if (!pending) break;
        network_events(&server, -1, 20);
    }
    for (int i = 0; i < MAX_CLIENTS; ++i) close_client(&server.clients[i]);
    close(listener);
    free(server.clients);
    destroy_simulation(&sim);
    scenario_destroy(&scenario);
    return 0;
}
