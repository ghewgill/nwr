#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>

#include <set>
#include <string>
#include <vector>

#include "eas_decode.h"
#include "eas_demod.h"

using namespace std;

const char *Dir = ".";
const char *LogFile = "eas.log";
const char *Script = NULL;

class AudioWriter {
public:
    AudioWriter(int freq, int bits, int channels): Freq(freq), Bits(bits), Channels(channels) {}
    virtual ~AudioWriter() {}
    virtual void write(const void *buf, size_t n) = 0;
protected:
    const int Freq;
    const int Bits;
    const int Channels;
};

class WavWriter: public AudioWriter {
public:
    WavWriter(const char fn[], int freq, int bits, int channels);
    virtual ~WavWriter();
    virtual void write(const void *buf, size_t n);
private:
    struct Header {
        char tagRIFF[4];
        unsigned long riffsize;
        char tagWAVE[4];
        char tagfmt[4];
        unsigned long fmtsize;
        unsigned short wFormatTag;
        unsigned short nChannels;
        unsigned long nSamplesPerSec;
        unsigned long nAvgBytesPerSec;
        unsigned short nBlockAlign;
        unsigned short nBitsPerSample;
        char tagdata[4];
        unsigned long datasize;
    };
    Header header;
    FILE *f;
    unsigned long size;
};

WavWriter::WavWriter(const char fn[], int freq, int bits, int channels)
 : AudioWriter(freq, bits, channels)
{
    strncpy(header.tagRIFF, "RIFF", 4);
    header.riffsize = 0;
    strncpy(header.tagWAVE, "WAVE", 4);
    strncpy(header.tagfmt, "fmt ", 4);
    header.fmtsize = 16;
    header.wFormatTag = 1;
    header.nChannels = channels;
    header.nSamplesPerSec = freq;
    header.nAvgBytesPerSec = freq*bits/8*channels;
    header.nBlockAlign = bits/8;
    header.nBitsPerSample = bits;
    strncpy(header.tagdata, "data", 4);
    header.datasize = 0;
    f = fopen(fn, "wb");
    if (f == NULL) {
        printf("could not create %s: (%d) %s\n", fn, errno, strerror(errno));
        return;
    }
    fwrite(&header, sizeof(header), 1, f);
    size = 0;
}

WavWriter::~WavWriter()
{
    if (f == NULL) {
        return;
    }
    header.riffsize = 36 + size;
    header.datasize = size;
    fseek(f, 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, f);
    fclose(f);
}

void WavWriter::write(const void *buf, size_t n)
{
    fwrite(buf, 1, n, f);
    size += n;
}

class Mp3Writer: public AudioWriter {
public:
    Mp3Writer(const char fn[], int freq, int bits, int channels);
    virtual ~Mp3Writer();
    virtual void write(const void *buf, size_t n);
private:
    int out;
    pid_t child;
};

Mp3Writer::Mp3Writer(const char fn[], int freq, int bits, int channels)
 : AudioWriter(freq, bits, channels), out(0)
{
    int pipeout[2];
    if (pipe(pipeout) != 0) {
        perror("pipe");
        return;
    }
    child = fork();
    if (child == -1) {
        perror("fork");
        return;
    }
    if (child == 0) {
        dup2(pipeout[0], 0);
        close(pipeout[1]);
        execl("/usr/local/bin/lame", "lame", "-r", "-m", "m", "-s", "11.025", "-x", "-b", "16", "-", fn, NULL);
        perror("execl");
        exit(127);
    }
    close(pipeout[0]);
    out = pipeout[1];
}

Mp3Writer::~Mp3Writer()
{
    if (out == 0) {
        return;
    }
    close(out);
    int status;
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("program exec error %08x\n", status);
    }
}

void Mp3Writer::write(const void *buf, size_t n)
{
    if (out == 0) {
        return;
    }
    ::write(out, buf, n);
}

class AudioSplitter: public AudioWriter {
public:
    AudioSplitter(int freq, int bits, int channels): AudioWriter(freq, bits, channels) {}
    virtual ~AudioSplitter();
    virtual void write(const void *buf, size_t n);
    void plug(AudioWriter *out);
private:
    vector<AudioWriter *> writers;
};

AudioSplitter::~AudioSplitter()
{
    for (vector<AudioWriter *>::iterator i = writers.begin(); i != writers.end(); i++) {
        delete *i;
    }
}

void AudioSplitter::write(const void *buf, size_t n)
{
    for (vector<AudioWriter *>::iterator i = writers.begin(); i != writers.end(); i++) {
        (*i)->write(buf, n);
    }
}

void AudioSplitter::plug(AudioWriter *out)
{
    writers.push_back(out);
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

AudioWriter *rec;
string mp3name;
eas::Message Message;

void eas_activate(const char *s)
{
    printf("%s\n", s);
    if (rec != NULL) {
        printf("got activate while still active\n");
        return;
    }
    eas::Message message;
    if (!eas::Decode(s, message)) {
        printf("bad eas header: %s\n", s);
        return;
    }
    Message = message;
    FILE *f = fopen(LogFile, "a");
    if (f != NULL) {
        fprintf(f, "%s %s\n", timestr(time(0)).c_str(), s);
        fclose(f);
    } else {
        perror("fopen");
    }
    struct tm *tt = gmtime(&message.issued);
    char fn[40];
    snprintf(fn, sizeof(fn), "%04d%02d%02d%02d%02d-%s-%s",
        1900+tt->tm_year,
        1+tt->tm_mon,
        tt->tm_mday,
        tt->tm_hour,
        tt->tm_sec,
        message.originator.c_str(),
        message.event.c_str());
    AudioSplitter *split = new AudioSplitter(11025, 16, 1);
    split->plug(new WavWriter((string(fn)+".wav").c_str(), 11025, 16, 1));
    mp3name = string(fn)+".mp3";
    split->plug(new Mp3Writer((string(Dir)+"/"+mp3name).c_str(), 11025, 16, 1));
    rec = split;
}

void eas_deactivate()
{
    if (rec == NULL) {
        printf("got deactivate while not active\n");
        return;
    }
    delete rec;
    rec = NULL;
    printf("capture done: %s\n", mp3name.c_str());
    if (Script != NULL) {
        if (fork() == 0) {
            setenv("EAS_MESSAGE", Message.raw.c_str(), 1);
            setenv("EAS_MP3", mp3name.c_str(), 1);
            int r = system(Script);
            if (r != 0) {
                printf("capture: exit code %d from script\n", r);
            }
            exit(0);
        }
    }
}

int main(int argc, char *argv[])
{
    int a = 1;
    while (a < argc && argv[a][0] == '-' && argv[a][1] != 0) {
        switch (argv[a][1]) {
        case 'd':
            if (argv[a][2]) {
                Dir = argv[a]+2;
            } else {
                a++;
                Dir = argv[a];
            }
            break;
        case 'l':
            if (argv[a][2]) {
                LogFile = argv[a]+2;
            } else {
                a++;
                LogFile = argv[a];
            }
            break;
        case 's':
            if (argv[a][2]) {
                Script = argv[a]+2;
            } else {
                a++;
                Script = argv[a];
            }
            break;
        default:
            fprintf(stderr, "%s: unknown option %c\n", argv[0], argv[a][1]);
            exit(1);
        }
        a++;
    }
    FILE *f;
    if (strcmp(argv[a], "-") == 0) {
        f = stdin;
    } else {
        f = fopen(argv[a], "rb");
        if (f == NULL) {
            perror("fopen");
            exit(1);
        }
    }
    eas::Demodulator demodulator;
    demodulator.activate.connect(SigC::slot(eas_activate));
    demodulator.deactivate.connect(SigC::slot(eas_deactivate));
    for (;;) {
        char buf[4096];
        int n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) {
            fprintf(stderr, "capture: eof on input\n");
            break;
        }
        float fbuf[sizeof(buf)/2];
        for (int i = 0; i < n/2; i++) {
            fbuf[i] = *(short *)&buf[i*2] * (1.0/32768.0);
        }
        demodulator.demod(fbuf, n/2);
        if (rec != NULL) {
            rec->write(buf, n);
        }
    }
    fclose(f);
    return 0;
}
