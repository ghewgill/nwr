#include <stdio.h>

int main(int argc, char *argv[])
{
    int amp = 0;
    for (;;) {
        char buf[4096];
        int n = fread(buf, 1, sizeof(buf), stdin);
        if (n == 0) {
            break;
        }
        for (int i = 0; i < n/2; i++) {
            short s = *(short *)&buf[i*2];
            amp = 99*amp/100 + abs(s)/100;
            if (amp < 4000) {
                s = 0;
            }
            *(short *)&buf[i*2] = s;
        }
        fwrite(buf, 1, n, stdout);
        //fprintf(stderr, "%d\n", amp);
    }
    return 0;
}
