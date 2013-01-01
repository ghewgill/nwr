#include <math.h>
#include <stdio.h>
#include <stdlib.h>

class Encoder {
public:
    Encoder(FILE *f, int samplerate): File(f), SampleRate(samplerate), Samples(0), Bits(0) {}
    void encode(const char *p);
    void encodeByte(unsigned char c);
    void encodeBit(int b);
private:
    FILE *File;
    const double SampleRate;
    int Samples;
    int Bits;
};

void Encoder::encode(const char *p)
{
    while (*p) {
        encodeByte(*p);
        p++;
    }
}

void Encoder::encodeByte(unsigned char c)
{
    for (int i = 0; i < 8; i++) {
        encodeBit(c & 1);
        c >>= 1;
    }
}

void Encoder::encodeBit(int b)
{
    Bits++;
    int cycles = b ? 4 : 3;
    double f = 2*M_PI*(cycles/1920e-6);
    while (Samples < Bits*SampleRate*1920e-6) {
        short s = (short)(32767*sin(Samples/SampleRate*f));
        fwrite(&s, 2, 1, File);
        Samples++;
    }
}

int main(int argc, char *argv[])
{
    FILE *f = fopen(argv[1], "wb");
    if (f == NULL) {
        perror("fopen");
        exit(1);
    }
    Encoder encoder(f, 11025);
    encoder.encode("\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB");
    encoder.encode(argv[2]);
    fclose(f);
    return 0;
}
