#include <stdio.h>

int main(int, char *[])
{
    for (;;) {
        char buf[4096];
        int n = fread(buf, 1, sizeof(buf), stdin);
        if (n == 0) {
            break;
        }
        for (int i = 0; i < n/2; i += 2) {
            *(short *)(buf+i) = *(short *)(buf+i*2);
        }
        fwrite(buf, 1, n/2, stdout);
    }
    return 0;
}
