#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>

#include <set>
#include <string>
#include <vector>

#include <pcre.h>
#include <sigc++/signal.h>

using namespace std;
using namespace SigC;

inline double corr18(const float *p, const float *q)
{
    double f;
    asm volatile ("flds (%1);\n\t"
                  "fmuls (%2);\n\t"
                  "flds 4(%1);\n\t"
                  "fmuls 4(%2);\n\t"
                  "flds 8(%1);\n\t"
                  "fmuls 8(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 12(%1);\n\t"
                  "fmuls 12(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 16(%1);\n\t"
                  "fmuls 16(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 20(%1);\n\t"
                  "fmuls 20(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 24(%1);\n\t"
                  "fmuls 24(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 28(%1);\n\t"
                  "fmuls 28(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 32(%1);\n\t"
                  "fmuls 32(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 36(%1);\n\t"
                  "fmuls 36(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 40(%1);\n\t"
                  "fmuls 40(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 44(%1);\n\t"
                  "fmuls 44(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 48(%1);\n\t"
                  "fmuls 48(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 52(%1);\n\t"
                  "fmuls 52(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 56(%1);\n\t"
                  "fmuls 56(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 60(%1);\n\t"
                  "fmuls 60(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 64(%1);\n\t"
                  "fmuls 64(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "flds 68(%1);\n\t"
                  "fmuls 68(%2);\n\t"
                  "fxch %%st(2);\n\t"
                  "faddp;\n\t"
                  "faddp;\n\t" :
                  "=t" (f) :
                  "r" (p),
                  "r" (q) : "memory");
    return f;
}

inline double corr(const float *p, const float *q, int n)
{
    if (n == 18) {
        return corr18(p, q);
    }
    double s = 0;
    while (n--) {
        s += (*p++) * (*q++);
    }
    return s;
}

class Decoder {
public:
    Decoder();
    void decode(const float *buf, int n);
    Signal1<void, const char *> activate;
    Signal0<void> deactivate;
private:
    enum {CORRLEN = 18};
    enum {BPHASESTEP = (int)(0x10000/(1920e-6*22050))};
    float ref[2][2][CORRLEN];
    float overlapbuf[CORRLEN*2];
    int overlap;
    int bphase;
    int lastbit;

    unsigned char byte;
    unsigned char lastbyte;
    int bits;
    int samebytes;

    char header[300];
    int hindex;
    int dashstate;

    string lastheader;

    int gotsamples(const float *buf, int n);
    void gotbit(int b);
    void gotbyte(unsigned char c);
    void gotheader(const char *s);
};

Decoder::Decoder()
{
    double f0 = 2*M_PI*(3/1920e-6);
    double f1 = 2*M_PI*(4/1920e-6);
    for (int i = 0; i < CORRLEN; i++) {
        ref[0][0][i] = sin(i/22050.0*f0);
        ref[0][1][i] = cos(i/22050.0*f0);
        ref[1][0][i] = sin(i/22050.0*f1);
        ref[1][1][i] = cos(i/22050.0*f1);
    }
    overlap = 0;
    bphase = 0;
    lastbit = 0;
    bits = 0;
    samebytes = 0;
    hindex = -5;
}

void Decoder::decode(const float *buf, int n)
{
    if (overlap > 0) {
        assert(overlap < CORRLEN);
        if (n >= CORRLEN) {
            memcpy(&overlapbuf[overlap], buf, (CORRLEN-1)*sizeof(float));
            int r = gotsamples(overlapbuf, overlap+CORRLEN-1);
            assert(r == overlap);
            overlap = 0;
        } else {
            memcpy(&overlapbuf[overlap], buf, n*sizeof(float));
            overlap += n;
            if (overlap >= CORRLEN) {
                int r = gotsamples(overlapbuf, overlap);
                memmove(overlapbuf, &overlapbuf[r], (overlap-r)*sizeof(float));
                overlap -= r;
            }
            return;
        }
    }
    int r = gotsamples(buf, n);
    buf += r;
    n -= r;
    if (n > 0) {
        memcpy(overlapbuf, buf, n*sizeof(float));
        overlap = n;
    }
}

int Decoder::gotsamples(const float *buf, int n)
{
    int r = 0;
    while (n >= CORRLEN) {
        double c00 = corr(buf, ref[0][0], CORRLEN);
        double c01 = corr(buf, ref[0][1], CORRLEN);
        double c10 = corr(buf, ref[1][0], CORRLEN);
        double c11 = corr(buf, ref[1][1], CORRLEN);
        double d = (c10*c10 + c11*c11) - (c00*c00 + c01*c01);
        int bit = d > 0;
        //printf("%d", bit);
        if (bit != lastbit) {
            if (bphase < 0x8000) {
                bphase += BPHASESTEP/8;
            } else {
                bphase -= BPHASESTEP/8;
            }
        }
        lastbit = bit;
        bphase += BPHASESTEP;
        if (bphase >= 0x10000) {
            bphase &= 0xffff;
            gotbit(bit);
        }
        buf++;
        n--;
        r++;
    }
    return r;
}

void Decoder::gotbit(int b)
{
    //printf(" %d ", b);
    byte >>= 1;
    if (b) {
        byte |= 0x80;
    }
    bits++;
    if (bits >= 8) {
        //printf("[%02x]", byte);
        bits = 0;
        if (byte & 0x80) {
            if (byte == 0xab) {
                //printf("(sync)");
                gotbyte(byte);
                return;
            } else if (byte == 0xae) {
                byte >>= 2;
                bits = 6;
            } else if (byte == 0xba) {
                byte >>= 4;
                bits = 4;
            } else if (byte == 0xea) {
                byte >>= 6;
                bits = 2;
            } else if (byte == 0xd5) {
                byte >>= 7;
                bits = 1;
            }
            gotbyte(0);
        } else if (byte == 0x57 && samebytes >= 4) {
            byte >>= 1;
            bits = 7;
        } else if (byte == 0x5d && samebytes >= 4) {
            byte >>= 3;
            bits = 5;
        } else if (byte == 0x75 && samebytes >= 4) {
            byte >>= 5;
            bits = 3;
        } else {
            if (byte == lastbyte) {
                samebytes++;
            } else {
                samebytes = 0;
            }
            gotbyte(byte);
            lastbyte = byte;
        }
    }
}

void Decoder::gotbyte(unsigned char c)
{
    //printf("%c", c);
    //printf("%d.%d ", hindex, dashstate);
    if (c == 0) {
        if (hindex > 0) {
            header[hindex] = 0;
            gotheader(header);
        }
        hindex = -5;
    } else if (c == 0xab) {
        if (hindex < 0) {
            hindex++;
        }
        dashstate = 0;
    } else if (hindex >= 0) {
        if (hindex+1 >= (int)sizeof(header)) {
            hindex = -5;
        } else {
            header[hindex++] = c;
            if (c == '+') {
                dashstate = 1;
            } else if (c == '-' && dashstate > 0) {
                dashstate++;
                if (dashstate >= 4) {
                    header[hindex] = 0;
                    gotheader(header);
                    hindex = -5;
                }
            }
        }
    }
}

void Decoder::gotheader(const char *s)
{
    //printf("gotheader: %s\n", s);
    if (s == lastheader) {
        return;
    }
    if (strncmp(s, "ZCZC-", 5) == 0) {
        activate(s);
    } else if (strncmp(s, "NN", 2) == 0) {
        deactivate();
    }
    lastheader = s;
}

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
        execl("/usr/local/bin/lame", "lame", "-r", "-m", "m", "-s", "22.05", "-x", "-b", "16", "-", fn, NULL);
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

void email(const char fn[])
{
    set<string> Addresses;
    //Addresses.insert("gregh@ud.com");
    //Addresses.insert("moose@ud.com");
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
            execl("/usr/sbin/sendmail", "sendmail", "-t", "-i", NULL);
            printf("EmwinProductEmailer: execl() failed: (%d) %s\n", errno, strerror(errno));
            exit(127);
        }
        close(pipeout[0]);
        FILE *f = fdopen(pipeout[1], "w");
        if (f != NULL) {
            char boundary[80];
            snprintf(boundary, sizeof(boundary), "%08x.%d", time(0), getpid());
            fprintf(f, "To: %s\n", a->c_str());
            fprintf(f, "From: nwr@hewgill.net\n");
            fprintf(f, "Subject: %s\n", fn);
            fprintf(f, "MIME-Version: 1.0\n");
            fprintf(f, "Content-Type: multipart/mixed; boundary=\"%s\"\n", boundary);
            fprintf(f, "\n");
            fprintf(f, "This is a multi-part message in MIME format.\n");
            fprintf(f, "\n");
            fprintf(f, "--%s\n", boundary);
            fprintf(f, "Content-Type: text/plain\n");
            fprintf(f, "\n");
            fprintf(f, "%s\n", fn);
            fprintf(f, "\n");
            fprintf(f, "--%s\n", boundary);
            fprintf(f, "Content-Type: audio/mp3\n");
            fprintf(f, "Content-Transfer-Encoding: base64\n");
            fprintf(f, "Content-Disposition: attachment; filename=\"%s\"\n", fn);
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

AudioWriter *rec;
string mp3name;

void eas_activate(const char *s)
{
    printf("%s\n", s);
    if (rec != NULL) {
        printf("got activate while still active\n");
        return;
    }
    const char *errptr;
    int erroffset;
    pcre *re = pcre_compile(
        "^ZCZC-(\\w+)-(\\w+)(-[^+-]+){1,31}\\+(\\d{2})(\\d{2})-(\\d{3})(\\d{2})(\\d{2})-([^-]+)-",
        //     1      2     3                 4       5        6       7       8        9
        0,
        &errptr,
        &erroffset,
        NULL);
    if (re == NULL) {
        printf("error compiling re: %s\n", errptr);
        return;
    }
    int ovector[3*10];
    int r = pcre_exec(
        re,
        NULL,
        s,
        strlen(s),
        0,
        0,
        ovector,
        sizeof(ovector)/sizeof(ovector[0]));
    pcre_free(re);
    if (r < 0) {
        printf("bad eas header: (%d) %s\n", r, s);
        return;
    }
    const char **matches;
    pcre_get_substring_list(s, ovector, r, &matches);
    int yday = atoi(matches[6]);
    time_t now = time(0);
    struct tm *tt;
    for (;;) {
        tt = gmtime(&now);
        if (1+tt->tm_yday == yday) {
            break;
        } else if (1+tt->tm_yday < yday) {
            now += 86400;
        } else if (1+tt->tm_yday > yday) {
            now -= 86400;
        }
    }
    char fn[40];
    snprintf(fn, sizeof(fn), "%04d%02d%02d%s%s-%s-%s",
        1900+tt->tm_year,
        1+tt->tm_mon,
        tt->tm_mday,
        matches[7],
        matches[8],
        matches[1],
        matches[2]);
    pcre_free_substring_list(matches);
    AudioSplitter *split = new AudioSplitter(22050, 16, 1);
    split->plug(new WavWriter((string(fn)+".wav").c_str(), 22050, 16, 1));
    mp3name = string(fn)+".mp3";
    split->plug(new Mp3Writer(mp3name.c_str(), 22050, 16, 1));
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
    if (fork() == 0) {
        email(mp3name.c_str());
        exit(0);
    }
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
    Decoder decoder;
    decoder.activate.connect(slot(eas_activate));
    decoder.deactivate.connect(slot(eas_deactivate));
    for (;;) {
        char buf[4096];
        int n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) {
            break;
        }
        float fbuf[sizeof(buf)/2];
        for (int i = 0; i < n/2; i++) {
            fbuf[i] = *(short *)&buf[i*2] * (1.0/32768.0);
        }
        decoder.decode(fbuf, n/2);
        if (rec != NULL) {
            rec->write(buf, n);
        }
    }
    fclose(f);
    return 0;
}
