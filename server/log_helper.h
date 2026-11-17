#ifndef LOG_HELPER_H
#define LOG_HELPER_H

#include <errno.h>
#include <string.h>
#include <syslog.h>

static inline void log_errno(const char *what)
{
    syslog(LOG_ERR, "%s: %s", what, strerror(errno));
}

#endif /* LOG_HELPER_H */
