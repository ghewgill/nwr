#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

const int BUFSIZE = 65536;
const int MAXCLIENTS = 100;

char Buffer[BUFSIZE];
int BufferHead;

struct Client {
    int fd;
    bool streaming;
    int tail;
    char request[1024];
    int index;
};
Client Clients[MAXCLIENTS];
int TotalClients;

void filter(int &filterin, int &filterout)
{
    int pin[2], pout[2];
    if (pipe(pin) != 0 || pipe(pout) != 0) {
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
        dup2(pout[1], 1);
        close(pout[0]);
        execl("/bin/sh", "sh", "-c", "lame -r -m m -s 22.05 -x -b 16 - -", NULL);
        perror("execl");
        exit(127);
    }
    close(pin[0]);
    close(pout[1]);
    filterin = pin[1];
    filterout = pout[0];
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        exit(1);
    }
    int one = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        perror("setsockopt");
        exit(1);
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8001);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(s);
        exit(1);
    }
    if (listen(s, 5) != 0) {
        perror("listen");
        close(s);
        exit(1);
    }
    int filterin = 0;
    int filterout = 0;
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        if (filterout != 0) {
            FD_SET(filterout, &rfds);
        }
        FD_SET(s, &rfds);
        int maxfd = s > filterout ? s : filterout;
        fd_set wfds;
        FD_ZERO(&wfds);
        for (int i = 0; i < MAXCLIENTS; i++) {
            if (Clients[i].fd != 0) {
                if (!Clients[i].streaming) {
                    FD_SET(Clients[i].fd, &rfds);
                } else if (Clients[i].tail != BufferHead) {
                    FD_SET(Clients[i].fd, &wfds);
                } else {
                    continue;
                }
                if (Clients[i].fd > maxfd) {
                    maxfd = Clients[i].fd;
                }
            }
        }
        int r = select(maxfd+1, &rfds, &wfds, NULL, NULL);
        if (r < 0) {
            perror("select");
            break;
        }
        if (FD_ISSET(0, &rfds)) {
            char buf[4096];
            ssize_t n = read(0, buf, sizeof(buf));
            if (n < 0) {
                perror("read");
                break;
            } else if (n == 0) {
                break;
            }
            if (filterin != 0) {
                write(filterin, buf, n);
            }
        }
        if (filterout != 0 && FD_ISSET(filterout, &rfds)) {
            assert(BufferHead < BUFSIZE);
            ssize_t n = read(filterout, Buffer+BufferHead, BUFSIZE-BufferHead);
            if (n > 0) {
                //printf("read %d\n", n);
                for (int i = 0; i < MAXCLIENTS; i++) {
                    if (Clients[i].fd != 0 && Clients[i].streaming && Clients[i].tail != BufferHead) {
                        int free = (Clients[i].tail + BUFSIZE - BufferHead) % BUFSIZE;
                        if (free <= n) {
                            close(Clients[i].fd);
                            Clients[i].fd = 0;
                            printf("closed client %d due to space\n", i);
                            TotalClients--;
                        }
                    }
                }
                BufferHead = (BufferHead + n) % BUFSIZE;
            }
        }
        if (FD_ISSET(s, &rfds)) {
            sockaddr_in peer;
            socklen_t n = sizeof(peer);
            int t = accept(s, (sockaddr *)&peer, &n);
            if (t < 0) {
                perror("accept");
            } else {
                bool found = false;
                for (int i = 0; i < MAXCLIENTS; i++) {
                    if (Clients[i].fd == 0) {
                        Clients[i].fd = t;
                        Clients[i].streaming = false;
                        Clients[i].index = 0;
                        printf("%s connected as client %d\n", inet_ntoa(peer.sin_addr), i);
                        if (TotalClients == 0) {
                            printf("starting filter\n");
                            filter(filterin, filterout);
                        }
                        TotalClients++;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    printf("%s turned away\n", inet_ntoa(peer.sin_addr));
                    close(t);
                }
            }
        }
        for (int i = 0; i < MAXCLIENTS; i++) {
            if (Clients[i].fd != 0) {
                if (FD_ISSET(Clients[i].fd, &rfds)) {
                    int n = read(Clients[i].fd, Clients[i].request+Clients[i].index, sizeof(Clients[i].request)-Clients[i].index);
                    if (n <= 0) {
                        if (n < 0) {
                            perror("read");
                        }
                        close(Clients[i].fd);
                        Clients[i].fd = 0;
                        TotalClients--;
                        continue;
                    }
                    Clients[i].index += n;
                    //printf("header: %.*s", Clients[i].index, Clients[i].request);
                    if (Clients[i].index > 4 && strncmp(Clients[i].request+Clients[i].index-4, "\r\n\r\n", 4) == 0) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n");
                        n = write(Clients[i].fd, buf, strlen(buf));
                        if (n <= 0) {
                            perror("write");
                            close(Clients[i].fd);
                            Clients[i].fd = 0;
                            TotalClients--;
                            continue;
                        }
                        Clients[i].streaming = true;
                        Clients[i].tail = BufferHead;
                    }
                }
                if (FD_ISSET(Clients[i].fd, &wfds)) {
                    int bytes = (BufferHead > Clients[i].tail ? BufferHead : BUFSIZE) - Clients[i].tail;
                    ssize_t n = write(Clients[i].fd, Buffer+Clients[i].tail, bytes);
                    //printf("write %d %d\n", i, n);
                    if (n < 0) {
                        if (errno != EAGAIN) {
                            perror("write");
                            close(Clients[i].fd);
                            Clients[i].fd = 0;
                            TotalClients--;
                            printf("closed client %d\n", i);
                        }
                    } else {
                        Clients[i].tail = (Clients[i].tail + n) % BUFSIZE;
                    }
                }
            }
        }
        if (filterin != 0 && TotalClients == 0) {
            printf("terminating filter\n");
            close(filterin);
            close(filterout);
            filterin = 0;
            filterout = 0;
            int status;
            if (waitpid(-1, &status, 0) < 0) {
                perror("waitpid");
                exit(1);
            }
        }
    }
    close(s);
    return 0;
}
