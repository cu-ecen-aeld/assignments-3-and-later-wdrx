#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

static int receive_packets(int client_fd, int data_fd)
{
    char buffer[4096];
    char *packet = NULL;
    size_t packet_length = 0;
    size_t capacity = 0;
    int result = -1;

    for (;;) {
        ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received == 0) {
            result = 0;
            break;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "recv: %s", strerror(errno));
            break;
        }

        size_t offset = 0;
        while (offset < (size_t)received) {
            char *newline = memchr(buffer + offset, '\n', (size_t)received - offset);
            size_t segment_length = newline != NULL
                ? (size_t)(newline - (buffer + offset)) + 1
                : (size_t)received - offset;

            if (segment_length > SIZE_MAX - packet_length) {
                syslog(LOG_ERR, "Packet exceeds available address space");
                goto cleanup;
            }
            size_t required = packet_length + segment_length;
            if (required > capacity) {
                size_t new_capacity = capacity != 0 ? capacity : sizeof(buffer);
                while (new_capacity < required) {
                    if (new_capacity > SIZE_MAX / 2) {
                        new_capacity = required;
                        break;
                    }
                    new_capacity *= 2;
                }
                char *new_packet = realloc(packet, new_capacity);
                if (new_packet == NULL) {
                    syslog(LOG_ERR, "realloc: %s", strerror(errno));
                    goto cleanup;
                }
                packet = new_packet;
                capacity = new_capacity;
            }

            memcpy(packet + packet_length, buffer + offset, segment_length);
            packet_length = required;
            offset += segment_length;

            if (newline != NULL) {
                size_t written = 0;
                while (written < packet_length) {
                    size_t remaining = packet_length - written;
                    size_t write_length = remaining < sizeof(buffer)
                        ? remaining : sizeof(buffer);
                    ssize_t count = write(data_fd, packet + written, write_length);
                    if (count < 0 && errno == EINTR) {
                        continue;
                    }
                    if (count <= 0) {
                        syslog(LOG_ERR, "write: %s",
                               count == 0 ? "No progress" : strerror(errno));
                        goto cleanup;
                    }
                    written += (size_t)count;
                }
                packet_length = 0;
            }
        }
    }

cleanup:
    free(packet);
    return result;
}

int main(void)
{
    int result = -1;
    int client_fd = -1;
    int data_fd = -1;
    int reuse_address = 1;
    int server_fd;
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(9000),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    openlog("aesdsocket", LOG_PID, LOG_USER);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        goto cleanup;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) < 0) {
        syslog(LOG_ERR, "setsockopt: %s", strerror(errno));
        goto cleanup;
    }
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        goto cleanup;
    }
    if (listen(server_fd, SOMAXCONN) < 0) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        goto cleanup;
    }
    data_fd = open("/var/tmp/aesdsocketdata", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (data_fd < 0) {
        syslog(LOG_ERR, "open: %s", strerror(errno));
        goto cleanup;
    }

    for (;;) {
        struct sockaddr_in client_address;
        socklen_t address_length = sizeof(client_address);
        char client_ip[INET_ADDRSTRLEN];

        client_fd = accept(server_fd, (struct sockaddr *)&client_address,
                           &address_length);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "accept: %s", strerror(errno));
            goto cleanup;
        }
        if (inet_ntop(AF_INET, &client_address.sin_addr,
                      client_ip, sizeof(client_ip)) == NULL) {
            syslog(LOG_ERR, "inet_ntop: %s", strerror(errno));
            goto cleanup;
        }
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        if (receive_packets(client_fd, data_fd) < 0) {
            goto cleanup;
        }
        close(client_fd);
        client_fd = -1;
    }

cleanup:
    if (client_fd >= 0) {
        close(client_fd);
    }
    if (data_fd >= 0) {
        close(data_fd);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    closelog();
    return result;
}