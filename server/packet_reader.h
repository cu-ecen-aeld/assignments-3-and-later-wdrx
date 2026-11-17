#ifndef PACKET_READER_H
#define PACKET_READER_H

#include <stddef.h>

/**
 * Waits until fd is readable.
 * Returns 1 if readable, 0 if exit was requested, -1 on error.
 */
typedef int (*wait_readable_fn)(int fd);

struct packet_buffer {
    char *data;
    size_t length;
    size_t capacity;
    size_t packet_length;  // Bytes of the complete packet at the front, 0 if none
};

/**
 * Reads from the client until the buffer holds a complete newline-terminated packet.
 * Bytes after the newline stay buffered for the next call.
 * Returns 1 if a packet is ready, 0 if the client closed or exit was requested, -1 on error.
 */
int packet_buffer_receive(int client_fd, struct packet_buffer *packet,
                          wait_readable_fn wait_readable);

/** Drops the complete packet from the front of the buffer. */
void packet_buffer_consume(struct packet_buffer *packet);

void packet_buffer_free(struct packet_buffer *packet);

#endif /* PACKET_READER_H */
