#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
// assume the directory has been already created
// log using syslog and LOG_USER
// use syslog to write: 'Writing <string> to <file>'

void usage(void) {
    syslog(LOG_ERR, "writer <Path to file> <string to write>");
}

int main(int argc, char** argv) {
    openlog("aesd_writer", 0, LOG_USER);

    if(argc < 3) {
        usage();
        return 1;
    }

    int file_fd = open(argv[1], O_CREAT|O_RDWR, 0644);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Unable to create/open file %s", argv[1]);
        return 1;
    }

    int written = write(file_fd, argv[2], strnlen(argv[2], 500));
    if (written == -1) {
        close(file_fd);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
    close(file_fd);
    closelog();

    return 0;
}