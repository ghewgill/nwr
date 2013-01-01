#include <string>

#include <sigc++/signal.h>

namespace eas {

class Demodulator {
public:
    Demodulator();
    void demod(const float *buf, int n);
    sigc::signal1<void, const char *> activate;
    sigc::signal0<void> deactivate;
private:
    enum {CORRLEN = 18};
    enum {BPHASESTEP = (int)(0x10000/(1920e-6*11025))};
    float ref[2][2][CORRLEN];
    float overlapbuf[CORRLEN*2];
    int overlap;
    int bphase;
    int bitcount;
    int lastbit;

    unsigned char byte;
    unsigned char lastbyte;
    int bits;
    int samebytes;

    char header[300];
    int hindex;
    int dashstate;

    std::string lastheader;

    int gotsamples(const float *buf, int n);
    void gotbit(int b);
    void gotbyte(unsigned char c);
    void gotheader(const char *s);
};

} // namepsace eas;
