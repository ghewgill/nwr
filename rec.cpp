#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <sys/soundcard.h>

int main(int argc, char *argv[])
{
    FILE *f;
    if (strcmp(argv[1], "-") == 0) {
        f = stdout;
    } else {
        f = fopen(argv[1], "wb");
        if (f == NULL) {
            perror("fopen");
            exit(1);
        }
    }
    int fd = open("/dev/dsp", O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(1);
    }
    int sndparam = AFMT_S16_LE;
    if (ioctl(fd, SNDCTL_DSP_SETFMT, &sndparam) == -1) { 
        perror("ioctl: SNDCTL_DSP_SETFMT");
        exit(1);
    }
    if (sndparam != AFMT_S16_LE) {
        perror("ioctl: SNDCTL_DSP_SETFMT");
        exit(1);
    }
    sndparam = 0;
    if (ioctl(fd, SNDCTL_DSP_STEREO, &sndparam) == -1) {
        perror("ioctl: SNDCTL_DSP_STEREO");
        exit(1);
    }
    if (sndparam != 0) {
        fprintf(stderr, "gen: Error, cannot set the channel number to 0\n");
        exit(1);
    }
    int sample_rate = 11025;
    sndparam = sample_rate;
    if (ioctl(fd, SNDCTL_DSP_SPEED, &sndparam) == -1) {
        perror("ioctl: SNDCTL_DSP_SPEED");
        exit(1);
    }
    if ((10*abs(sndparam-sample_rate)) > sample_rate) {
        perror("ioctl: SNDCTL_DSP_SPEED");
        exit(1);
    }
    if (sndparam != sample_rate) {
        fprintf(stderr, "Warning: Sampling rate is %u, requested %u\n", sndparam, sample_rate);
    }
    for (;;) {
        char buf[4096];
        int n = read(fd, buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        fwrite(buf, 1, n, f);
    }
    close(fd);
    fclose(f);
    return 0;
}
