#ifndef FILE_HELPER_H
#define FILE_HELPER_H

#include <stddef.h>

/** Opens (creating if needed) the data file for appending and reading. Returns fd or -1. */
int file_helper_open(void);

/** Closes the data file and removes it from disk. */
void file_helper_close(int data_fd);

/** Appends the bytes to the data file. Returns 0 or -1. */
int file_helper_append(int data_fd, const char *data, size_t length);

/** Sends the whole data file to the client. Returns 0 or -1. */
int file_helper_send(int data_fd, int client_fd);

#endif /* FILE_HELPER_H */
