#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#define SERVER_PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define IO_CHUNK_SIZE 4096

static volatile sig_atomic_t exit_requested = 0;

struct packet_buffer {
    char *data;
    size_t length;
    size_t capacity;
};

static void log_errno(const char *what)
{
    syslog(LOG_ERR, "%s: %s", what, strerror(errno));
}

/* ---- Signals ---- */

static void handle_signal(int signal_number)
{
    (void)signal_number;
    exit_requested = 1;
}

/**
 * Installs SIGINT/SIGTERM handlers and blocks both signals. They are only
 * delivered inside wait_readable(), using the saved original_mask.
 * Returns 0 on success, -1 on error.
 */
static int setup_signals(sigset_t *original_mask)
{
    struct sigaction action;
    sigset_t blocked_mask;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;  // No SA_RESTART: blocking calls must fail with EINTR
    sigemptyset(&action.sa_mask);
    sigemptyset(&blocked_mask);
    sigaddset(&blocked_mask, SIGINT);
    sigaddset(&blocked_mask, SIGTERM);

    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0 ||
        sigprocmask(SIG_BLOCK, &blocked_mask, original_mask) < 0) {
        log_errno("signal setup");
        return -1;
    }
    // A client closing early must not kill the server via SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    return 0;
}

/**
 * Waits until fd is readable. Signals are unblocked only inside pselect(),
 * so one cannot slip in between the flag check and the wait.
 * Returns 1 if readable, 0 if exit was requested, -1 on error.
 */
static int wait_readable(int fd, const sigset_t *wait_mask)
{
    for (;;) {
        if (exit_requested) {
            return 0;
        }
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        if (pselect(fd + 1, &read_set, NULL, NULL, NULL, wait_mask) < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("pselect");
            return -1;
        }
        return 1;
    }
}

/* ---- I/O helpers ---- */

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

/** Streams the whole data file to the client without loading it into memory. */
static int send_file(int data_fd, int client_fd)
{
    char buffer[IO_CHUNK_SIZE];

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

/* ---- Packet handling ---- */

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

static void packet_buffer_free(struct packet_buffer *packet)
{
    free(packet->data);
    memset(packet, 0, sizeof(*packet));
}

/** Appends the finished packet to the data file, then replies with the full file. */
static int complete_packet(struct packet_buffer *packet, int data_fd, int client_fd)
{
    if (write_all(data_fd, packet->data, packet->length) < 0) {
        return -1;
    }
    packet->length = 0;
    return send_file(data_fd, client_fd);
}

/** Splits received bytes on newlines, completing a packet at each one. */
static int process_chunk(struct packet_buffer *packet, const char *chunk, size_t length,
                         int data_fd, int client_fd)
{
    while (length > 0) {
        const char *newline = memchr(chunk, '\n', length);
        size_t segment_length = newline != NULL ? (size_t)(newline - chunk) + 1 : length;

        if (packet_buffer_append(packet, chunk, segment_length) < 0) {
            return -1;
        }
        chunk += segment_length;
        length -= segment_length;

        if (newline != NULL && complete_packet(packet, data_fd, client_fd) < 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * Serves one client until it disconnects. An unfinished packet is discarded.
 * Returns 0 when the client closes or exit is requested, -1 on error.
 */
static int receive_packets(int client_fd, int data_fd, const sigset_t *wait_mask)
{
    char buffer[IO_CHUNK_SIZE];
    struct packet_buffer packet = {0};
    int result = -1;

    for (;;) {
        int ready = wait_readable(client_fd, wait_mask);
        if (ready <= 0) {
            result = ready;
            break;
        }
        ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received < 0) {
            log_errno("recv");
            break;
        }
        if (received == 0) {
            result = 0;
            break;
        }
        if (process_chunk(&packet, buffer, (size_t)received, data_fd, client_fd) < 0) {
            break;
        }
    }
    packet_buffer_free(&packet);
    return result;
}

/* ---- Server setup and connections ---- */

/** Returns a listening socket bound to the given port, or -1 on error. */
static int create_listen_socket(uint16_t port)
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

static int open_data_file(void)
{
    // O_APPEND keeps writes at the end even after lseek() for reading
    int fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        log_errno("open");
    }
    return fd;
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
                         const sigset_t *wait_mask)
{
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    // A failed connection is dropped; the server keeps serving other clients
    receive_packets(client_fd, data_fd, wait_mask);
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
}

int main(void)
{
    int result = -1;
    int server_fd = -1;
    int data_fd = -1;
    sigset_t original_mask;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (setup_signals(&original_mask) < 0) {
        goto cleanup;
    }
    server_fd = create_listen_socket(SERVER_PORT);
    if (server_fd < 0) {
        goto cleanup;
    }
    data_fd = open_data_file();
    if (data_fd < 0) {
        goto cleanup;
    }

    for (;;) {
        char client_ip[INET_ADDRSTRLEN];

        int ready = wait_readable(server_fd, &original_mask);
        if (ready < 0) {
            goto cleanup;
        }
        if (ready == 0) {
            break;
        }
        int client_fd = accept_client(server_fd, client_ip, sizeof(client_ip));
        if (client_fd < 0) {
            goto cleanup;
        }
        serve_client(client_fd, client_ip, data_fd, &original_mask);
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    result = 0;

cleanup:
    if (data_fd >= 0) {
        close(data_fd);
        unlink(DATA_FILE);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    closelog();
    return result;
}
