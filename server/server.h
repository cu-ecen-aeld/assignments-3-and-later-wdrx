#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>

#include "packet_reader.h"

#define SERVER_PORT 9000

/** Returns a listening socket bound to the given port, or -1 on error. */
int server_create_socket(uint16_t port);

/**
 * Serves clients one at a time on the listening socket until wait_readable()
 * reports that exit was requested. Owns the data file for the whole run.
 * Returns 0 on a requested exit, -1 on error.
 */
int server_run(int server_fd, wait_readable_fn wait_readable);

#endif /* SERVER_H */
