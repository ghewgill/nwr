#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int fds[2];
int nfds;

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: demux leftcmd rightcmd\n");
        exit(1);
    }
    for (int a = 1; a < 3; a++) {
        int pin[2];
        if (pipe(pin) != 0) {
            perror("pipe");
            exit(1);
        }
        pid_t child = fork();
        if (child == -1) {
            perror("fork");
            exit(1);
        }
        if (child == 0) {
            dup2(pin[0], 0);
            close(pin[1]);
            execl("/bin/sh", "sh", "-c", argv[a], NULL);
            perror("execl");
            exit(127);
        }
        close(pin[0]);
        fds[nfds++] = pin[1];
    }
    for (;;) {
        char buf[4096];
        ssize_t n = read(0, buf, sizeof(buf));
        if (n < 0) {
            perror("read");
            break;
        } else if (n == 0) {
            fprintf(stderr, "demux: eof on input\n");
            break;
        }
        assert(n % (nfds*2) == 0);
        char out[2][sizeof(buf)/2];
        for (ssize_t i = 0; i < n; ) {
            for (int j = 0; j < nfds; j++) {
                *(short *)&out[j][i/(nfds*2)*2] = *(short *)&buf[i];
                i += 2;
            }
        }
        for (int i = 0; i < nfds; i++) {
            ssize_t w = write(fds[i], out[i], n/nfds);
            if (w < 0) {
                perror("write");
                exit(1);
            }
            if (w < n/nfds) {
                fprintf(stderr, "short write: %d < %d\n", w, n/nfds);
                exit(1);
            }
        }
    }
    for (int i = 0; i < nfds; i++) {
        close(fds[i]);
    }
    return 0;
}
