#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <list>
#include <vector>

//#define TRACE

using namespace std;

const int BUFSIZE = 65536;
const int MAXCLIENTS = 10;

FILE *Log;

char Buffer[BUFSIZE];
int BufferHead;
int filterpid;
int filterin = -1;
int filterout = -1;

void filter(pid_t &filterpid, int &filterin, int &filterout)
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
        execl("/bin/sh", "sh", "-c", "lame -r -m m -s 11.025 -x -b 16 -q 9 - -", NULL);
        perror("execl");
        exit(127);
    }
    close(pin[0]);
    close(pout[1]);
    filterpid = child;
    filterin = pin[1];
    filterout = pout[0];
}

class Stream {
public:
    virtual ~Stream() {}
    virtual int getfd() = 0;
    virtual bool needWritable() { return false; }
    virtual bool notifyRead() { return true; }
    virtual bool notifyWrite() { return true; }
    virtual bool checkOverflow(int n) { return false; }
};

list<Stream *> Clients;
int TotalClients;

class RawInputStream: public Stream {
public:
    virtual int getfd() { return 0; }
    virtual bool notifyRead();
};

class FilteredInputStream: public Stream {
public:
    virtual int getfd() { return filterout; }
    virtual bool notifyRead();
};

class Listener: public Stream {
public:
    Listener();
    virtual ~Listener();
    virtual int getfd() { return s; }
    virtual bool notifyRead();
private:
    int s;
};

class ShoutcastDirectory: public Stream {
public:
    ShoutcastDirectory();
    virtual ~ShoutcastDirectory();
    virtual int getfd();
    virtual bool needWritable();
    virtual bool notifyRead();
    virtual bool notifyWrite();
private:
    int s;
    int id;
    bool connected;
    int refresh;
    time_t nextupdate;
    char buf[1024];
    int index;
};

class Client: public Stream {
public:
    Client(int f, in_addr p);
    virtual ~Client();
    virtual int getfd() { return fd; }
    virtual bool needWritable();
    virtual bool notifyRead();
    virtual bool notifyWrite();
    virtual bool checkOverflow(int n);
private:
    const int fd;
    const in_addr peer;
    const time_t start;
    bool streaming;
    int tail;
    char request[1024];
    int index;
};

bool RawInputStream::notifyRead()
{
#ifdef TRACE
printf("RawInputStream::notifyRead\n");
#endif
    char buf[4096];
    ssize_t n = read(0, buf, sizeof(buf));
    if (n < 0) {
        perror("read");
        exit(1);
    } else if (n == 0) {
        exit(0);
    }
    //printf("read raw %d\n", n);
    if (filterin != -1) {
        write(filterin, buf, n);
    }
    return true;
}

bool FilteredInputStream::notifyRead()
{
#ifdef TRACE
printf("FilteredInputStream::notifyRead\n");
#endif
    assert(BufferHead < BUFSIZE);
    ssize_t n = read(filterout, Buffer+BufferHead, BUFSIZE-BufferHead);
    if (n <= 0) {
        if (n < 0) {
            perror("read");
        }
        close(filterout);
        filterout = -1;
        return false;
    }
    //printf("read filtered %d\n", n);
    for (list<Stream *>::iterator i = Clients.begin(); i != Clients.end(); ) {
        if ((*i)->checkOverflow(n)) {
            printf("closed client due to overflow\n");
            delete *i;
            i = Clients.erase(i);
        } else {
            i++;
        }
    }
    BufferHead = (BufferHead + n) % BUFSIZE;
    return true;
}

Listener::Listener()
{
    s = socket(AF_INET, SOCK_STREAM, 0);
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
}

Listener::~Listener()
{
    close(s);
}

bool Listener::notifyRead()
{
#ifdef TRACE
printf("Listener::notifyRead\n");
#endif
    sockaddr_in peer;
    socklen_t n = sizeof(peer);
    int t = accept(s, (sockaddr *)&peer, &n);
    if (t < 0) {
        perror("accept");
    } else if (TotalClients >= MAXCLIENTS) {
        close(t);
        printf("%s turned away\n", inet_ntoa(peer.sin_addr));
        fprintf(Log, "%ld %s turned away\n", time(0), inet_ntoa(peer.sin_addr));
        fflush(Log);
    } else {
        Clients.push_back(new Client(t, peer.sin_addr));
        printf("%s connected\n", inet_ntoa(peer.sin_addr));
    }
    return true;
}

ShoutcastDirectory::ShoutcastDirectory()
 : s(-1), id(0), nextupdate(0)
{
}

ShoutcastDirectory::~ShoutcastDirectory()
{
    if (s != -1) {
        close(s);
    }
}

int ShoutcastDirectory::getfd()
{
    if (s != -1) {
        if (time(0) < nextupdate+60) {
            return s;
        }
        close(s);
        s = -1;
    }
    if (time(0) >= nextupdate) {
        nextupdate = time(0) + 60;
#ifdef TRACE
printf("updating directory\n");
#endif
        s = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        if (ioctl(s, FIONBIO, &one) != 0) {
            perror("ioctl");
            close(s);
            s = -1;
            return -1;
        }
        sockaddr_in ds;
        ds.sin_family = AF_INET;
        ds.sin_port = htons(80);
        ds.sin_addr.s_addr = inet_addr("205.188.234.56");
        connect(s, (sockaddr *)&ds, sizeof(ds));
        if (errno != EINPROGRESS) {
            perror("connect");
            close(s);
            s = -1;
            return -1;
        }
        connected = false;
        return s;
    }
    return -1;
}

bool ShoutcastDirectory::needWritable()
{
    return s != -1 && !connected;
}

bool ShoutcastDirectory::notifyRead()
{
#ifdef TRACE
printf("ShoutcastDirectory::notifyRead\n");
#endif
    ssize_t n = read(s, buf+index, sizeof(buf)-index-1);
    if (n <= 0) {
        if (n < 0) {
            perror("read");
        } else {
            assert(index < (ssize_t)sizeof(buf));
            buf[index] = 0;
            if (strstr(buf, "icy-response: ack") == NULL) {
                printf("%s\n", buf);
            }
            if (id == 0) {
                const char *p = strstr(buf, "icy-id:");
                if (p != NULL) {
                    id = atoi(p+7);
                }
                p = strstr(buf, "icy-tchfrq:");
                if (p != NULL) {
                    refresh = atoi(p+11) * 60;
                }
                if (id == 0) {
                    nextupdate = time(0) + 300;
                } else {
                    printf("stream id %d (refresh %d)\n", id, refresh);
                    nextupdate = time(0);
                }
            } else {
                nextupdate = time(0) + refresh;
            }
        }
        close(s);
        s = -1;
        return true;
    }
    index += n;
    return true;
}

bool ShoutcastDirectory::notifyWrite()
{
    int err;
    socklen_t n;
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &n) != 0) {
        perror("getsockopt");
        close(s);
        s = -1;
        return true;
    }
    if (err != 0) {
        fprintf(stderr, "connect: %s\n", strerror(err));
        close(s);
        s = -1;
        return true;
    }
#ifdef TRACE
printf("connected\n");
#endif
    connected = true;
    index = 0;
    char buf[1024];
    if (id == 0) {
        snprintf(buf, sizeof(buf), "GET /addsrv?v=1&br=16&p=8001&m=10&t=NOAA+Weather+Radio:+Austin,+TX+(WXK27+162.400+MHz)&g=Weather&url=http%3A%2F%2Fweather.hewgill.net&irc=&aim=&icq= HTTP/1.0\r\nHost: yp.shoutcast.com\r\n\r\n");
    } else {
        time_t now = time(0);
        struct tm *tt = gmtime(&now);
        snprintf(buf, sizeof(buf), "GET /cgi-bin/tchsrv?id=%d&p=8001&li=%d&alt=0&ct=Weather+Radio+%02d:%02d+UTC HTTP/1.0\r\nHost: yp.shoutcast.com\r\n\r\n",
            id,
            TotalClients,
            tt->tm_hour,
            tt->tm_min);
    }
    if (write(s, buf, strlen(buf)) < (ssize_t)strlen(buf)) {
        perror("write");
        close(s);
        s = -1;
        return true;
    }
    return true;
}

Client::Client(int f, in_addr p)
 : fd(f), peer(p), start(time(0)), streaming(false), index(0)
{
    TotalClients++;
    fprintf(Log, "%ld %s connected\n", time(0), inet_ntoa(peer));
    fflush(Log);
}

Client::~Client()
{
    close(fd);
    TotalClients--;
    fprintf(Log, "%ld %s disconnected (%ld seconds)\n", time(0), inet_ntoa(peer), time(0)-start);
    fflush(Log);
}

bool Client::needWritable()
{
    return streaming && tail != BufferHead;
}

bool Client::notifyRead()
{
#ifdef TRACE
printf("Client::notifyRead\n");
#endif
    int n = read(fd, request+index, sizeof(request)-index);
    if (n <= 0) {
        if (n < 0) {
            perror("read");
        }
        return false;
    }
    index += n;
    //printf("header: %.*s", Clients[i].index, Clients[i].request);
    if (index > 4 && strncmp(request+index-4, "\r\n\r\n", 4) == 0 || strncmp(request+index-2, "\n\n", 2) == 0) {
        char buf[1024];
        snprintf(buf, sizeof(buf),
            "ICY 200 OK\r\n"
            "Content-Type: audio/mpeg\r\n"
            "icy-notice1: <BR>This stream requires <a href=\"http://www.winamp.com/\">Winamp</a><BR>\r\n"
            "icy-notice2: SHOUTcast Distributed Network Audio Server/posix v1.x.x\r\n"
            "icy-name: NOAA Weather Radio: Austin, TX (WXK27 162.400 MHz)\r\n"
            "icy-genre: Weather\r\n"
            "icy-url: http://weather.hewgill.net\r\n"
            "icy-pub: 1\r\n"
            "icy-br: 16\r\n"
            "\r\n");
        n = write(fd, buf, strlen(buf));
        if (n <= 0) {
            perror("write");
            return false;
        }
        streaming = true;
        tail = BufferHead;
    }
    return true;
}

bool Client::notifyWrite()
{
    int bytes = (BufferHead > tail ? BufferHead : BUFSIZE) - tail;
    ssize_t n = write(fd, Buffer+tail, bytes);
    //printf("write %d\n", n);
    if (n < 0) {
        if (errno != EAGAIN) {
            perror("write");
            printf("closed client\n");
            return false;
        }
    } else {
        tail = (tail + n) % BUFSIZE;
    }
    return true;
}

bool Client::checkOverflow(int n)
{
    if (streaming && tail != BufferHead) {
        int free = (tail + BUFSIZE - BufferHead) % BUFSIZE;
        if (free <= n) {
            return true;
        }
    }
    return false;
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    Log = fopen("streamer.log", "a");
    Clients.push_back(new RawInputStream());
    Clients.push_back(new Listener());
    Clients.push_back(new ShoutcastDirectory());
    for (;;) {
        fd_set rfds;
        fd_set wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        int maxfd = -1;
#ifdef TRACE
printf("setup\n");
#endif
        for (list<Stream *>::iterator i = Clients.begin(); i != Clients.end(); i++) {
            int fd = (*i)->getfd();
            if (fd < 0) {
                continue;
            }
            FD_SET(fd, &rfds);
            if (fd > maxfd) {
                maxfd = fd;
            }
        }
        for (list<Stream *>::iterator i = Clients.begin(); i != Clients.end(); i++) {
            if ((*i)->needWritable()) {
                int fd = (*i)->getfd();
                assert(fd >= 0);
                FD_SET(fd, &wfds);
            }
        }
#ifdef TRACE
printf("select\n");
#endif
        assert(maxfd >= 0);
        int r = select(maxfd+1, &rfds, &wfds, NULL, NULL);
        if (r < 0) {
            perror("select");
            break;
        }
#ifdef TRACE
printf("identify\n");
#endif
        vector<Stream *> readables, writables;
        for (list<Stream *>::iterator i = Clients.begin(); i != Clients.end(); i++) {
            int fd = (*i)->getfd();
            if (fd < 0) {
                continue;
            }
            if (FD_ISSET(fd, &rfds)) {
                readables.push_back(*i);
            }
            if (FD_ISSET(fd, &wfds)) {
                writables.push_back(*i);
            }
        }
#ifdef TRACE
printf("handle read\n");
#endif
        for (vector<Stream *>::iterator i = readables.begin(); i != readables.end(); i++) {
            if (!(*i)->notifyRead()) {
                delete *i;
                vector<Stream *>::iterator w = writables.begin();
                while (w != writables.end()) {
                    if (*w == *i) {
                        break;
                    }
                    w++;
                }
                if (w != writables.end()) {
                    writables.erase(w);
                }
                Clients.remove(*i);
            }
        }
#ifdef TRACE
printf("handle write\n");
#endif
        for (vector<Stream *>::iterator i = writables.begin(); i != writables.end(); i++) {
            if (!(*i)->notifyWrite()) {
                delete *i;
                Clients.remove(*i);
            }
        }
#ifdef TRACE
printf("TotalClients=%d (%d)\n", TotalClients, Clients.size());
printf("filterin=%d filterout=%d\n", filterin, filterout);
#endif
        if (filterin == -1 && TotalClients > 0) {
            printf("starting filter\n");
            filter(filterpid, filterin, filterout);
            Clients.push_back(new FilteredInputStream());
        }
        if (filterin != -1 && TotalClients == 0) {
            printf("terminating filter\n");
            close(filterin);
            filterin = -1;
            kill(filterpid, SIGTERM);
            int status;
            if (waitpid(filterpid, &status, 0) < 0) {
                perror("waitpid");
                exit(1);
            }
            filterpid = 0;
        }
        printf(" %ld %d\r", time(0), TotalClients);
        fflush(stdout);
    }
    return 0;
}
