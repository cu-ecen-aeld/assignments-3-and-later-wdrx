#define _POSIX_C_SOURCE 200809L

#include "packet_reader.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>

#include "log_helper.h"

#define IO_CHUNK_SIZE 4096

/** Appends bytes, growing the buffer geometrically. Returns 0 or -1. */
static int packet_buffer_append(struct packet_buffer *packet, const char *data, size_t length)
{
    if (length > SIZE_MAX - packet->length) {
        syslog(LOG_ERR, "Packet exceeds available address space");
        return -1;
    }
    size_t required = packet->length + length;
    if (required > packet->capacity) {
        size_t new_capacity = packet->capacity != 0 ? packet->capacity : IO_CHUNK_SIZE;
        while (new_capacity < required) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = required;
                break;
            }
            new_capacity *= 2;
        }
        char *grown = realloc(packet->data, new_capacity);
        if (grown == NULL) {
            log_errno("realloc");
            return -1;
        }
        packet->data = grown;
        packet->capacity = new_capacity;
    }
    memcpy(packet->data + packet->length, data, length);
    packet->length = required;
    return 0;
}

/** Sets packet_length if the buffered bytes in [from, length) contain a newline. */
static void find_packet_end(struct packet_buffer *packet, size_t from)
{
    const char *newline = memchr(packet->data + from, '\n', packet->length - from);
    if (newline != NULL) {
        packet->packet_length = (size_t)(newline - packet->data) + 1;
    }
}

int packet_buffer_receive(int client_fd, struct packet_buffer *packet,
                          wait_readable_fn wait_readable)
{
    char buffer[IO_CHUNK_SIZE];

    if (packet->length > 0) {
        find_packet_end(packet, 0);
    }
    while (packet->packet_length == 0) {
        int ready = wait_readable(client_fd);
        if (ready <= 0) {
            return ready;
        }
        ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received < 0) {
            log_errno("recv");
            return -1;
        }
        if (received == 0) {
            return 0;
        }
        size_t previous_length = packet->length;
        if (packet_buffer_append(packet, buffer, (size_t)received) < 0) {
            return -1;
        }
        find_packet_end(packet, previous_length);
    }
    return 1;
}

void packet_buffer_consume(struct packet_buffer *packet)
{
    packet->length -= packet->packet_length;
    memmove(packet->data, packet->data + packet->packet_length, packet->length);
    packet->packet_length = 0;
}

void packet_buffer_free(struct packet_buffer *packet)
{
    free(packet->data);
    memset(packet, 0, sizeof(*packet));
}
