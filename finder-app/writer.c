#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <writefile> <writestr>\n", argv[0]);
        return 1;
    }
    
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    const char* writefile = argv[1];
    const char* writestr = argv[2];
    
    FILE *file = fopen(writefile, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "fopen: %m");
        return 1;
    }

    int ret = fprintf(file, "%s\n", writestr);
    if (ret < 0) {
        syslog(LOG_ERR, "fprintf: %m");
        return 1;
    }
    else {
        syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    }
    

    if (fclose(file) != 0) {
        syslog(LOG_ERR, "fclose: %m");
        return 1;
    }

    return 0;
}