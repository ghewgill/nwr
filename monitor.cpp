#include <stdio.h>
#include <time.h>

#include <list>

#include <pcre.h>

#include "eas_decode.h"
#include "eas_demod.h"

using namespace std;

list<eas::Message> Messages;

void writeTitle()
{
    string title;
    for (list<eas::Message>::const_iterator i = Messages.begin(); i != Messages.end(); i++) {
        if (!title.empty()) {
            title += " / ";
        }
        title += i->event_desc;
        struct tm *tt = localtime(&i->purge);
        char buf[20];
        snprintf(buf, sizeof(buf), " until %02d:%02d", tt->tm_hour, tt->tm_min);
        title += buf;
    }
    if (title.empty()) {
        printf("Weather Radio\n");
    } else {
        printf("%s\n", title.c_str());
    }
    fflush(stdout);
}

void eas_activate(const char *s)
{
    eas::Message message;
    if (!eas::Decode(s, message)) {
        printf("bad eas header: %s\n", s);
        return;
    }
    Messages.push_front(message);
    writeTitle();
}

int main(int argc, char *argv[])
{
    FILE *f;
    if (strcmp(argv[1], "-") == 0) {
        f = stdin;
    } else {
        f = fopen(argv[1], "rb");
        if (f == NULL) {
            perror("fopen");
            exit(1);
        }
    }
    eas::Demodulator demodulator;
    demodulator.activate.connect(SigC::slot(eas_activate));
    writeTitle();
    for (;;) {
        char buf[4096];
        int n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) {
            fprintf(stderr, "monitor: eof on input\n");
            break;
        }
        float fbuf[sizeof(buf)/2];
        for (int i = 0; i < n/2; i++) {
            fbuf[i] = *(short *)&buf[i*2] * (1.0/32768.0);
        }
        demodulator.demod(fbuf, n/2);
        time_t now = time(0);
        bool changed = false;
        for (list<eas::Message>::iterator i = Messages.begin(); i != Messages.end(); ) {
            if (now >= i->purge) {
                i = Messages.erase(i);
                changed = true;
            } else {
                i++;
            }
        }
        if (changed) {
            writeTitle();
        }
    }
    fclose(f);
    return 0;
}
