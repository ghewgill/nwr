#include <stdio.h>
#include <time.h>
#include <sys/wait.h>

#include <set>

#include "eas_decode.h"

using namespace std;

const char *From = "nwr@hewgill.net";
const char *Notify = "notify";

void base64(char *dest, const char *src, size_t n)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *d = dest;
    while (n >= 3) {
        *d++ = table[(*src & 0xFF) >> 2];
        *d++ = table[((*src & 0xFF) << 4) & 0x3F | ((src[1] & 0xFF) >> 4)];
        src++;
        *d++ = table[((*src & 0xFF) << 2) & 0x3F | ((src[1] & 0xFF) >> 6)];
        src++;
        *d++ = table[(*src & 0xFF) & 0x3F];
        src++;
        n -= 3;
    }
    if (n == 1) {
        *d++ = table[(*src & 0xFF) >> 2];
        *d++ = table[((*src & 0xFF) << 4) & 0x3F];
    } else if (n == 2) {
        *d++ = table[(*src & 0xFF) >> 2];
        *d++ = table[((*src & 0xFF) << 4) & 0x3F | ((src[1] & 0xFF) >> 4)];
        src++;
        *d++ = table[((*src & 0xFF) << 2) & 0x3F];
    }
    while ((d-dest) % 4) {
        *d++ = '=';
    }
    *d = 0;
}

string timestr(time_t t)
{
    if (t == 0) {
        return "none";
    }
    struct tm *tm = gmtime(&t);
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", 1900+tm->tm_year, 1+tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min);
    return buf;
}

void email(const eas::Message &message, const char fn[])
{
    set<string> Addresses;
    FILE *f = fopen(Notify, "r");
    if (f == NULL) {
        return;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        if (buf[0] && buf[strlen(buf)-1] == '\n') {
            buf[strlen(buf)-1] = 0;
        }
        if (buf[0] == 0 || buf[0] == '#') {
            continue;
        }
        Addresses.insert(buf);
    }
    fclose(f);
    for (set<string>::const_iterator a = Addresses.begin(); a != Addresses.end(); a++) {
        printf("*** sending %s to %s\n", fn, a->c_str());
        int pipeout[2];
        if (pipe(pipeout) != 0) {
            printf("EmwinProductEmailer: pipe() failed: (%d) %s\n", errno, strerror(errno));
            return;
        }
        pid_t child = fork();
        if (child == -1) {
            printf("EmwinProductEmailer: fork() failed: (%d) %s\n", errno, strerror(errno));
            return;
        }
        if (child == 0) {
            dup2(pipeout[0], 0);
            close(pipeout[1]);
            if ((*a)[0] == '|') {
                execl("/bin/sh", "sh", "-c", a->c_str()+1, NULL);
            } else {
                execl("/usr/sbin/sendmail", "sendmail", "-t", "-i", a->c_str(), NULL);
            }
            printf("EmwinProductEmailer: execl() failed: (%d) %s\n", errno, strerror(errno));
            exit(127);
        }
        close(pipeout[0]);
        FILE *f = fdopen(pipeout[1], "w");
        if (f != NULL) {
            char boundary[80];
            snprintf(boundary, sizeof(boundary), "%08x.%d", time(0), getpid());
            fprintf(f, "To: %s\n", From);
            fprintf(f, "From: %s\n", From);
            string name = fn;
            string::size_type i = name.rfind('/');
            if (i != string::npos) {
                name = name.substr(i+1);
            }
            fprintf(f, "Subject: [NWR] %s\n", name.c_str());
            fprintf(f, "MIME-Version: 1.0\n");
            fprintf(f, "Content-Type: multipart/mixed; boundary=\"%s\"\n", boundary);
            fprintf(f, "\n");
            fprintf(f, "This is a multi-part message in MIME format.\n");
            fprintf(f, "\n");
            fprintf(f, "--%s\n", boundary);
            fprintf(f, "Content-Type: text/plain\n");
            fprintf(f, "\n");
            fprintf(f, "Originator: (%s) %s\n", message.originator.c_str(), message.originator_desc.c_str());
            fprintf(f, "Event: (%s) %s\n", message.event.c_str(), message.event_desc.c_str());
            fprintf(f, "Location:\n");
            for (vector<eas::Message::Area>::const_iterator a = message.areas.begin(); a != message.areas.end(); a++) {
                fprintf(f, "  (%s) %s\n", a->code.c_str(), a->desc.c_str());
            }
            fprintf(f, "Issued: %s\n", timestr(message.issued).c_str());
            fprintf(f, "Received: %s\n", timestr(message.received).c_str());
            fprintf(f, "Purge: %s\n", timestr(message.purge).c_str());
            fprintf(f, "Sender: (%s) %s\n", message.sender.c_str(), message.sender_desc.c_str());
            fprintf(f, "\n");
            fprintf(f, "--%s\n", boundary);
            fprintf(f, "Content-Type: audio/mp3\n");
            fprintf(f, "Content-Transfer-Encoding: base64\n");
            fprintf(f, "Content-Disposition: attachment; filename=\"%s\"\n", name.c_str());
            fprintf(f, "\n");
            FILE *att = fopen(fn, "rb");
            if (att != NULL) {
                for (;;) {
                    char buf[57];
                    size_t n = fread(buf, 1, sizeof(buf), att);
                    if (n == 0) {
                        break;
                    }
                    char enc[80];
                    base64(enc, buf, n);
                    fprintf(f, "%s\n", enc);
                }
                fclose(att);
            }
            fprintf(f, "\n");
            fprintf(f, "--%s--\n", boundary);
            fclose(f);
        } else {
            printf("EmwinProductEmailer: fdopen() failed: (%d) %s\n", errno, strerror(errno));
        }
        int status;
        waitpid(child, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("EmwinProductEmailer: sendmail exec error %08x\n", status);
        }
    }
}

int main(int argc, char *argv[])
{
    int a = 1;
    while (a < argc && argv[a][0] == '-' && argv[a][1] != 0) {
        switch (argv[a][1]) {
        case 'f':
            if (argv[a][2]) {
                From = argv[a]+2;
            } else {
                a++;
                From = argv[a];
            }
            break;
        case 'n':
            if (argv[a][2]) {
                Notify = argv[a]+2;
            } else {
                a++;
                Notify = argv[a];
            }
            break;
        default:
            fprintf(stderr, "%s: unknown option %c\n", argv[0], argv[a][1]);
            exit(1);
        }
        a++;
    }
    if (argc-a < 2) {
        fprintf(stderr, "usage: emailer code filename\n");
        exit(1);
    }
    eas::Message message;
    if (!eas::Decode(argv[a], message)) {
        fprintf(stderr, "emailer: bad code\n");
        exit(1);
    }
    email(message, argv[a+1]);
    return 0;
}
