#include <math.h>

#include "eas_demod.h"

namespace eas {

inline double corr9(const float *p, const float *q)
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
                  "faddp;\n\t" :
                  "=t" (f) :
                  "r" (p),
                  "r" (q) : "memory");
    return f;
}

inline double corr(const float *p, const float *q, int n)
{
    if (n == 9) {
        return corr9(p, q);
    }
    double s = 0;
    while (n--) {
        s += (*p++) * (*q++);
    }
    return s;
}

Demodulator::Demodulator()
{
    double f0 = 2*M_PI*(3/1920e-6);
    double f1 = 2*M_PI*(4/1920e-6);
    for (int i = 0; i < CORRLEN; i++) {
        ref[0][0][i] = sin(i/11025.0*f0);
        ref[0][1][i] = cos(i/11025.0*f0);
        ref[1][0][i] = sin(i/11025.0*f1);
        ref[1][1][i] = cos(i/11025.0*f1);
    }
    overlap = 0;
    bphase = 0;
    lastbit = 0;
    bits = 0;
    samebytes = 0;
    hindex = -5;
}

void Demodulator::demod(const float *buf, int n)
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

int Demodulator::gotsamples(const float *buf, int n)
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

void Demodulator::gotbit(int b)
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

void Demodulator::gotbyte(unsigned char c)
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

void Demodulator::gotheader(const char *s)
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

} // namespace eas
