#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    FILE *inf = fopen(argv[1], "rb");
    if (inf == NULL) {
        perror("fopen");
        exit(1);
    }
    FILE *outf = fopen("plot.tmp", "w");
    if (outf == NULL) {
        perror("fopen");
        exit(1);
    }
    int s = 0;
    for (;;) {
        char buf[4096];
        int n = fread(buf, 1, sizeof(buf), inf);
        if (n == 0) {
            break;
        }
        for (int i = 0; i < n/2; i++) {
            fprintf(outf, "%d %d\n", s, *(short *)&buf[i*2]);
            s++;
            if (s >= 200) goto bail;
        }
    }
bail:
    fclose(outf);
    fclose(inf);
    system("gnuplot plot.gpl");
    return 0;
}
