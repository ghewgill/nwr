#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s dir\n", argv[0]);
        exit(1);
    }
    char *dir = argv[1];
    FILE *log = NULL;
    int lasthour = -1;
    for (;;) {
        char buf[4096];
        int n = fread(buf, 1, sizeof(buf), stdin);
        if (n == 0) {
            fprintf(stderr, "log: eof on input\n");
            break;
        }
        int hour = (time(0) / 3600) % 24;
        if (hour != lasthour) {
            if (log != NULL) {
                fclose(log);
            }
            char fn[200];
            snprintf(fn, sizeof(fn), "%s/NWR-%02d.raw", dir, (hour+24-3)%24);
            unlink(fn);
            snprintf(fn, sizeof(fn), "%s/NWR-%02d.raw", dir, hour);
            if (lasthour != -1) {
                log = fopen(fn, "wb");
            } else {
                log = fopen(fn, "ab");
            }
            if (log == NULL) {
                perror("fopen");
                exit(1);
            }
            lasthour = hour;
        }
        fwrite(buf, 1, n, log);
    }
    return 0;
}
