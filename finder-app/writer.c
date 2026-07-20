#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <writefile> <writestr>\n", argv[0]);
        return 1;
    }
    
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    FILE *file = fopen(argv[1], "w");
    if (file == NULL) {
        syslog(LOG_ERR, "fopen: %m");
        return 1;
    }

    int ret = fprintf(file, "%s\n", argv[2]);
    if (ret < 0) {
        syslog(LOG_ERR, "fprintf: %m");
        return 1;
    }

    

    if (fclose(file) != 0) {
        syslog(LOG_ERR, "fclose: %m");
        return 1;
    }

    return 0;
}