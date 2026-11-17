#define _POSIX_C_SOURCE 200809L

#include "file_helper.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "log_helper.h"

#define DATA_FILE "/var/tmp/aesdsocketdata"
#define SEND_CHUNK_SIZE 4096

/** Writes all bytes, retrying on EINTR and partial writes. Returns 0 or -1. */
static int write_all(int fd, const char *data, size_t length)
{
    while (length > 0) {
        ssize_t count = write(fd, data, length);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            syslog(LOG_ERR, "write: %s", count == 0 ? "No progress" : strerror(errno));
            return -1;
        }
        data += count;
        length -= (size_t)count;
    }
    return 0;
}

int file_helper_open(void)
{
    // O_APPEND keeps writes at the end even after lseek() for reading
    int fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        log_errno("open");
    }
    return fd;
}

void file_helper_close(int data_fd)
{
    close(data_fd);
    unlink(DATA_FILE);
}

int file_helper_append(int data_fd, const char *data, size_t length)
{
    return write_all(data_fd, data, length);
}

/* Streams in chunks so the file is never loaded into memory. */
int file_helper_send(int data_fd, int client_fd)
{
    char buffer[SEND_CHUNK_SIZE];

    if (lseek(data_fd, 0, SEEK_SET) < 0) {
        log_errno("lseek");
        return -1;
    }
    for (;;) {
        ssize_t count = read(data_fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            log_errno("read");
            return -1;
        }
        if (count == 0) {
            return 0;
        }
        if (write_all(client_fd, buffer, (size_t)count) < 0) {
            return -1;
        }
    }
}
