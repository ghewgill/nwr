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
#include <string>
#include <vector>

//#define TRACE

using namespace std;

const int BUFSIZE = 65536;

struct config_t {
    string ConfigFileName;
    string Server;
    int Port;
    string Log;
    string Encoder;
    string ContentType;
    int MaxClients;
    string Name;
    string Genre;
    string URL;
    string IRC;
    string AIM;
    string ICQ;
};

config_t Config = {
    "",
    "localhost",
    8001,
    "streamer.log",
    "lame -r -m m -s 11.025 -x -b 16 -q 9 - -",
    "audio/mpeg",
    10
};

#ifdef TRACE
bool Trace;
#endif
FILE *Log;
string Title;

char Buffer[BUFSIZE];
int BufferHead;
int filterpid;
int filterin = -1;
int filterout = -1;
int monitorin = -1;

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
        execl("/bin/sh", "sh", "-c", Config.Encoder.c_str(), NULL);
        perror("execl");
        exit(127);
    }
    close(pin[0]);
    close(pout[1]);
    filterpid = child;
    filterin = pin[1];
    filterout = pout[0];

    // Make the input to the filter nonblocking, so we will never
    // deadlock due to reading and writing from the same process.
    // The worst effect of this will be short skips as filter input
    // is dropped on the floor.
    int one = 1;
    if (ioctl(filterin, FIONBIO, &one) != 0) {
        perror("ioctl");
        exit(1);
    }
}

string HttpEscape(const string &s)
{
    string r;
    for (string::const_iterator i = s.begin(); i != s.end(); i++) {
        if (isalnum(*i)) {
            r += *i;
        } else if (*i == ' ') {
            r += '+';
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", *i);
            r += buf;
        }
    }
    return r;
}

void LoadConfig(const char *fn)
{
    FILE *f = fopen(fn, "r");
    if (f == NULL) {
        fprintf(stderr, "streamer: streamer.conf not found: (%d) %s\n", errno, strerror(errno));
        exit(1);
    }
    Config.ConfigFileName = fn;
    char buf[1024];
    while (fgets(buf, sizeof(buf), f) != NULL) {
        if (buf[0] == '#') {
            continue;
        }
        const char *name = strtok(buf, " \t");
        if (name == NULL) {
            continue;
        }
        const char *value = strtok(NULL, "\n");
        if (value == NULL) {
            value = "";
        } else {
            value += strspn(value, " \t");
        }
        if (strcasecmp(name, "Server") == 0) {
            Config.Server = value;
            printf("Server: %s\n", value);
        } else if (strcasecmp(name, "Port") == 0) {
            Config.Port = atoi(value);
        } else if (strcasecmp(name, "Log") == 0) {
            Config.Log = value;
        } else if (strcasecmp(name, "Encoder") == 0) {
            Config.Encoder = value;
        } else if (strcasecmp(name, "Content-Type") == 0) {
            Config.ContentType = value;
        } else if (strcasecmp(name, "MaxClients") == 0) {
            Config.MaxClients = atoi(value);
        } else if (strcasecmp(name, "Name") == 0) {
            Config.Name = value;
        } else if (strcasecmp(name, "Genre") == 0) {
            Config.Genre = value;
        } else if (strcasecmp(name, "URL") == 0) {
            Config.URL = value;
        } else if (strcasecmp(name, "IRC") == 0) {
            Config.IRC = value;
        } else if (strcasecmp(name, "AIM") == 0) {
            Config.AIM = value;
        } else if (strcasecmp(name, "ICQ") == 0) {
            Config.ICQ = value;
        } else {
            fprintf(stderr, "streamer: unknown config option: %s\n", name);
            exit(1);
        }
    }
    fclose(f);
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

class TitleMonitor: public Stream {
public:
    TitleMonitor();
    virtual ~TitleMonitor();
    virtual int getfd() { return monitorout; }
    virtual bool notifyRead();
private:
    pid_t monitorpid;
    int monitorout;
    string title;
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
if (Trace) printf("%d RawInputStream::notifyRead\n", getpid());
#endif
    char buf[4096];
    ssize_t n = read(0, buf, sizeof(buf));
    if (n < 0) {
        perror("read");
        exit(1);
    } else if (n == 0) {
        fprintf(stderr, "streamer: eof on input\n");
        exit(0);
    }
#ifdef TRACE
if (Trace) printf("%d read raw %d\n", getpid(), n);
#endif
    if (filterin != -1) {
        ssize_t r = write(filterin, buf, n);
        if (r < n) {
            perror("filterin write");
        }
    }
#ifdef TRACE
if (Trace) printf("%d after filter write\n", getpid());
#endif
    if (monitorin != -1) {
        write(monitorin, buf, n);
    }
#ifdef TRACE
if (Trace) printf("%d after monitor write\n", getpid());
#endif
    return true;
}

TitleMonitor::TitleMonitor()
{
    int pin[2], pout[2];
    if (pipe(pin) != 0 || pipe(pout) != 0) {
        perror("pipe");
        exit(1);
    }
    monitorpid = fork();
    if (monitorpid == -1) {
        perror("fork");
        exit(1);
    }
    if (monitorpid == 0) {
        dup2(pin[0], 0);
        close(pin[1]);
        dup2(pout[1], 1);
        close(pout[0]);
        execl("/bin/sh", "sh", "-c", "./monitor -", NULL);
        perror("execl");
        exit(127);
    }
    close(pin[0]);
    close(pout[1]);
    monitorin = pin[1];
    monitorout = pout[0];
}

TitleMonitor::~TitleMonitor()
{
    close(monitorin);
    int status;
    if (waitpid(monitorpid, &status, 0) < 0) {
        perror("waitpid");
        exit(1);
    }
}

bool TitleMonitor::notifyRead()
{
    char buf[256];
    ssize_t n = read(monitorout, buf, sizeof(buf));
    if (n <= 0) {
        if (n < 0) {
            perror("read");
        }
        close(filterout);
        filterout = -1;
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            Title = title;
            title.erase();
            printf("Title: %s\n", Title.c_str());
        } else {
            title += buf[i];
        }
    }
    return true;
}

bool FilteredInputStream::notifyRead()
{
#ifdef TRACE
if (Trace) printf("%d FilteredInputStream::notifyRead\n", getpid());
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
    addr.sin_port = htons(Config.Port);
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
if (Trace) printf("%d Listener::notifyRead\n", getpid());
#endif
    sockaddr_in peer;
    socklen_t n = sizeof(peer);
    int t = accept(s, (sockaddr *)&peer, &n);
    if (t < 0) {
        perror("accept");
    } else if (TotalClients >= Config.MaxClients) {
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
    /* hmm this doesn't work
    FILE *f = fopen((Config.ConfigFileName+".id").c_str(), "r");
    if (f != NULL) {
        fscanf(f, "%d", &id);
        fclose(f);
        printf("restored id %d from %s\n", id, (Config.ConfigFileName+".id").c_str());
    }*/
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
if (Trace) printf("%d updating directory\n", getpid());
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
if (Trace) printf("%d ShoutcastDirectory::notifyRead\n", getpid());
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
                    FILE *f = fopen((Config.ConfigFileName+".id").c_str(), "w");
                    if (f != NULL) {
                        fprintf(f, "%d\n", id);
                        fclose(f);
                    } else {
                        perror("fopen id");
                    }
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
if (Trace) printf("%d connected\n", getpid());
#endif
    connected = true;
    index = 0;
    char buf[1024];
    if (id == 0) {
        snprintf(buf, sizeof(buf), "GET /addsrv?v=1&br=16&p=%d&m=%d&t=%s&g=%s&url=%s&irc=%s&aim=%s&icq=%s HTTP/1.0\r\nHost: yp.shoutcast.com\r\n\r\n",
            Config.Port,
            Config.MaxClients,
            HttpEscape(Config.Name).c_str(),
            HttpEscape(Config.Genre).c_str(),
            HttpEscape(Config.URL).c_str(),
            HttpEscape(Config.IRC).c_str(),
            HttpEscape(Config.AIM).c_str(),
            HttpEscape(Config.ICQ).c_str()
        );
    } else {
        snprintf(buf, sizeof(buf), "GET /cgi-bin/tchsrv?id=%d&p=%d&li=%d&alt=0&ct=%s HTTP/1.0\r\nHost: yp.shoutcast.com\r\n\r\n",
            id,
            Config.Port,
            TotalClients,
            HttpEscape(Title).c_str());
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
if (Trace) printf("%d Client::notifyRead\n", getpid());
#endif
    int n = read(fd, request+index, sizeof(request)-index-1);
    if (n <= 0) {
        if (n < 0) {
            perror("read");
        }
        return false;
    }
    index += n;
    //printf("header: %.*s", Clients[i].index, Clients[i].request);
    if (index > 4 && strncmp(request+index-4, "\r\n\r\n", 4) == 0 || strncmp(request+index-2, "\n\n", 2) == 0) {
        request[index] = 0;
        char *method = strtok(request, " ");
        char *url = strtok(NULL, " ");
        char response[1024];
        bool streamit = false;
        if (strcmp(url, "/") == 0) {
            snprintf(response, sizeof(response),
                "ICY 200 OK\r\n"
                "Content-Type: %s\r\n"
                //"icy-notice1: <BR>This stream requires <a href=\"http://www.winamp.com/\">Winamp</a><BR>\r\n"
                //"icy-notice2: SHOUTcast Distributed Network Audio Server/posix v1.x.x\r\n"
                "icy-name: %s\r\n"
                "icy-genre: %s\r\n"
                "icy-url: %s\r\n"
                "icy-pub: 1\r\n"
                "icy-br: 16\r\n"
                "\r\n",
                Config.ContentType.c_str(),
                Config.Name.c_str(),
                Config.Genre.c_str(),
                Config.URL.c_str()
            );
            streamit = true;
        } else if (strcmp(url, "/nwr.spx") == 0) {
            snprintf(response, sizeof(response),
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: %s\r\n"
                "ice-name: %s\r\n"
                "ice-genre: %s\r\n"
                "ice-url: %s\r\n"
                "ice-public: 1\r\n"
                "ice-bitrate: 16\r\n"
                "\r\n",
                Config.ContentType.c_str(),
                Config.Name.c_str(),
                Config.Genre.c_str(),
                Config.URL.c_str()
            );
            streamit = true;
        } else if (strcmp(url, "/nwr.ogg") == 0) {
            snprintf(response, sizeof(response),
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: %s\r\n"
                "ice-name: %s\r\n"
                "ice-genre: %s\r\n"
                "ice-url: %s\r\n"
                "ice-public: 1\r\n"
                "ice-bitrate: 16\r\n"
                "\r\n",
                Config.ContentType.c_str(),
                Config.Name.c_str(),
                Config.Genre.c_str(),
                Config.URL.c_str()
            );
            streamit = true;
        } else if (strcmp(url, "/playlist.pls") == 0) {
            snprintf(response, sizeof(response),
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: audio/x-scpls\r\n"
                "\r\n"
                "[playlist]\r\n"
                "numberofentries=1\r\n"
                "File1=http://%s:%d\r\n",
                Config.Server.c_str(),
                Config.Port);
        } else {
            snprintf(response, sizeof(response),
                "HTTP/1.0 404 Not Found\r\n"
                "\r\n");
        }
        n = write(fd, response, strlen(response));
        if (n <= 0) {
            perror("write");
            return false;
        }
        if (strcmp(method, "GET") == 0 && streamit) {
            streaming = true;
            tail = BufferHead;
        } else {
            return false;
        }
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
    string config = "streamer.conf";
    int a = 1;
    while (a < argc && argv[a][0] == '-' && argv[a][1] != 0) {
        switch (argv[a][1]) {
        case 'c':
            if (argv[a][2]) {
                config = argv[a]+2;
            } else {
                a++;
                config = argv[a];
            }
            break;
#ifdef TRACE
        case 't':
            Trace = true;
            break;
#endif
        default:
            fprintf(stderr, "%s: unknown option %c\n", argv[0], argv[a][1]);
            exit(1);
        }
        a++;
    }
    signal(SIGPIPE, SIG_IGN);
    LoadConfig(config.c_str());
    Log = fopen(Config.Log.c_str(), "a");
    Clients.push_back(new RawInputStream());
    Clients.push_back(new Listener());
    if (!Config.Name.empty()) {
        Clients.push_back(new TitleMonitor());
        Clients.push_back(new ShoutcastDirectory());
    }
    for (;;) {
        fd_set rfds;
        fd_set wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        int maxfd = -1;
#ifdef TRACE
if (Trace) printf("%d setup\n", getpid());
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
if (Trace) printf("%d select\n", getpid());
#endif
        assert(maxfd >= 0);
        int r = select(maxfd+1, &rfds, &wfds, NULL, NULL);
        if (r < 0) {
            perror("select");
            break;
        }
#ifdef TRACE
if (Trace) printf("%d identify\n", getpid());
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
if (Trace) printf("%d handle read\n", getpid());
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
if (Trace) printf("%d handle write\n", getpid());
#endif
        for (vector<Stream *>::iterator i = writables.begin(); i != writables.end(); i++) {
            if (!(*i)->notifyWrite()) {
                delete *i;
                Clients.remove(*i);
            }
        }
#ifdef TRACE
if (Trace) {
    printf("%d TotalClients=%d (%d)\n", getpid(), TotalClients, Clients.size());
    printf("filterin=%d filterout=%d\n", filterin, filterout);
}
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
#ifdef TRACE
        printf(" %d %ld %d %s\n", getpid(), time(0), TotalClients, config.c_str());
#else
        printf(" %ld %d\r", time(0), TotalClients);
#endif
        fflush(stdout);
    }
    return 0;
}
