#define _POSIX_C_SOURCE 200809L

#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include "file_helper.h"
#include "log_helper.h"

int server_create_socket(uint16_t port)
{
    int reuse_address = 1;
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        log_errno("socket");
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) < 0) {
        log_errno("setsockopt");
        goto fail;
    }
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        log_errno("bind");
        goto fail;
    }
    if (listen(fd, SOMAXCONN) < 0) {
        log_errno("listen");
        goto fail;
    }
    return fd;

fail:
    close(fd);
    return -1;
}

/** Accepts a client and formats its IP into ip. Returns the client fd or -1. */
static int accept_client(int server_fd, char *ip, size_t ip_size)
{
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    int client_fd;

    do {
        client_fd = accept(server_fd, (struct sockaddr *)&address, &address_length);
    } while (client_fd < 0 && errno == EINTR);

    if (client_fd < 0) {
        log_errno("accept");
        return -1;
    }
    if (inet_ntop(AF_INET, &address.sin_addr, ip, (socklen_t)ip_size) == NULL) {
        log_errno("inet_ntop");
        close(client_fd);
        return -1;
    }
    return client_fd;
}

static void serve_client(int client_fd, const char *client_ip, int data_fd,
                         wait_readable_fn wait_readable)
{
    struct packet_buffer packet = {0};

    syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    // A failed connection is dropped; the server keeps serving other clients.
    // An unfinished packet is discarded when the client disconnects.
    for (;;) {
        if (packet_buffer_receive(client_fd, &packet, wait_readable) <= 0) {
            break;
        }
        if (file_helper_append(data_fd, packet.data, packet.packet_length) < 0) {
            break;
        }
        packet_buffer_consume(&packet);
        if (file_helper_send(data_fd, client_fd) < 0) {
            break;
        }
    }
    packet_buffer_free(&packet);
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
}

int server_run(int server_fd, wait_readable_fn wait_readable)
{
    int result = -1;
    int data_fd = file_helper_open();
    if (data_fd < 0) {
        return -1;
    }

    for (;;) {
        char client_ip[INET_ADDRSTRLEN];

        int ready = wait_readable(server_fd);
        if (ready <= 0) {
            result = ready;
            break;
        }
        int client_fd = accept_client(server_fd, client_ip, sizeof(client_ip));
        if (client_fd < 0) {
            break;
        }
        serve_client(client_fd, client_ip, data_fd, wait_readable);
    }

    if (result == 0) {
        syslog(LOG_INFO, "Caught signal, exiting");
    }
    file_helper_close(data_fd);
    return result;
}
