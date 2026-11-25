#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef DEFAULT_ROLE
#define DEFAULT_ROLE NULL
#endif

static volatile sig_atomic_t stopped = 0;
static void on_signal(int number) {
    (void)number;
    stopped = 1;
}

static int valid_role(const char *role) {
    return !strcmp(role, "orders") || !strcmp(role, "dispatcher") ||
           !strcmp(role, "drones") || !strcmp(role, "stations") ||
           !strcmp(role, "observer");
}

static int read_port(const char *text) {
    char *end = NULL;
    errno = 0;
    long port = strtol(text, &end, 10);
    if (errno || !*text || *end || port < 1 || port > 65535) return 0;
    return (int)port;
}

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s --host IPv4 --port PORT [--role orders|dispatcher|drones|stations|observer]\n", program);
}

int main(int argc, char **argv) {
    const char *host = NULL, *role = DEFAULT_ROLE;
    int port = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--host") && ++i < argc) host = argv[i];
        else if (!strcmp(argv[i], "--port") && ++i < argc) port = read_port(argv[i]);
        else if (!strcmp(argv[i], "--role") && ++i < argc && DEFAULT_ROLE == NULL)
            role = argv[i];
        else { usage(argv[0]); return 1; }
    }
    if (!host || !port || !role || !valid_role(role)) { usage(argv[0]); return 1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in endpoint = {0};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &endpoint.sin_addr) != 1 ||
        connect(fd, (struct sockaddr *)&endpoint, sizeof(endpoint)) != 0) {
        perror("connect");
        close(fd);
        return 1;
    }
    char hello[64];
    snprintf(hello, sizeof(hello), "ROLE %s\n", role);
    if (send(fd, hello, strlen(hello), MSG_NOSIGNAL) != (ssize_t)strlen(hello)) {
        perror("send"); close(fd); return 1;
    }
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    char buffer[4096];
    while (!stopped) {
        ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            close(fd);
            return 1;
        }
        if (fwrite(buffer, 1, (size_t)count, stdout) != (size_t)count) break;
        fflush(stdout);
    }
    close(fd);
    return 0;
}
