#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int level = atoi(argv[1]);
    int amp = 0;
    for (;;) {
        char buf[4096];
        int n = fread(buf, 1, sizeof(buf), stdin);
        if (n == 0) {
            fprintf(stderr, "squelch: eof on input\n");
            break;
        }
        for (int i = 0; i < n/2; i++) {
            short s = *(short *)&buf[i*2];
            amp = 999*amp/1000 + abs(s)/1000;
            if (amp < level) {
                s = 0;
            }
            *(short *)&buf[i*2] = s;
        }
        fwrite(buf, 1, n, stdout);
    }
    return 0;
}
