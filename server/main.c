#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <syslog.h>
#include <unistd.h>

#include "log_helper.h"
#include "server.h"

static volatile sig_atomic_t exit_requested = 0;
static sigset_t original_mask;

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
static int setup_signals(void)
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
        sigprocmask(SIG_BLOCK, &blocked_mask, &original_mask) < 0) {
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
static int wait_readable(int fd)
{
    for (;;) {
        if (exit_requested) {
            return 0;
        }
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        if (pselect(fd + 1, &read_set, NULL, NULL, NULL, &original_mask) < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_errno("pselect");
            return -1;
        }
        return 1;
    }
}

/**
 * Detaches from the terminal. The parent exits on success, so only the child returns.
 * Returns 0 in the child, -1 on error.
 */
static int daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        log_errno("fork");
        return -1;
    }
    if (pid > 0) {
        _exit(0);
    }
    if (setsid() < 0) {
        log_errno("setsid");
        return -1;
    }
    if (chdir("/") < 0) {
        log_errno("chdir");
        return -1;
    }
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0) {
        log_errno("open /dev/null");
        return -1;
    }
    if (dup2(null_fd, STDIN_FILENO) < 0 || dup2(null_fd, STDOUT_FILENO) < 0 ||
        dup2(null_fd, STDERR_FILENO) < 0) {
        log_errno("dup2");
        close(null_fd);
        return -1;
    }
    if (null_fd > STDERR_FILENO) {
        close(null_fd);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int result = -1;
    int server_fd = -1;
    int run_as_daemon = 0;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (argc > 2 || (argc == 2 && strcmp(argv[1], "-d") != 0)) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        goto cleanup;
    }
    run_as_daemon = (argc == 2);

    if (setup_signals() < 0) {
        goto cleanup;
    }
    server_fd = server_create_socket(SERVER_PORT);
    if (server_fd < 0) {
        goto cleanup;
    }

    // Fork only after bind succeeded; the data file is opened by the child alone
    if (run_as_daemon && daemonize() < 0) {
        goto cleanup;
    }

    result = server_run(server_fd, wait_readable);

cleanup:
    if (server_fd >= 0) {
        close(server_fd);
    }
    closelog();
    return result;
}
