#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

const int NFDS = 10;

int fds[NFDS];
int nfds;

int main(int argc, char *argv[])
{
    for (int a = 1; a < argc; a++) {
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
            fprintf(stderr, "splitter: eof on input\n");
            break;
        }
        for (int i = 0; i < nfds; i++) {
            ssize_t w = write(fds[i], buf, n);
            if (w < 0) {
                perror("write");
                exit(1);
            }
            if (w < n) {
                fprintf(stderr, "short write: %d < %d\n", w, n);
                exit(1);
            }
        }
    }
    for (int i = 0; i < nfds; i++) {
        close(fds[i]);
    }
    return 0;
}
