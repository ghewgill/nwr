#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <sys/soundcard.h>

int main(int argc, char *argv[])
{
    bool stereo = false;
    int a = 1;
    while (a < argc && argv[a][0] == '-' && argv[a][1] != 0) {
        switch (argv[a][1]) {
        case 's':
            stereo = true;
            break;
        default:
            fprintf(stderr, "%s: unknown option %c\n", argv[0], argv[a][1]);
            exit(1);
        }
        a++;
    }
    FILE *f;
    if (strcmp(argv[a], "-") == 0) {
        f = stdout;
    } else {
        f = fopen(argv[a], "wb");
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
    sndparam = stereo;
    if (ioctl(fd, SNDCTL_DSP_STEREO, &sndparam) == -1) {
        perror("ioctl: SNDCTL_DSP_STEREO");
        exit(1);
    }
    if (sndparam != stereo) {
        fprintf(stderr, "rec: Error, cannot set the channel number to %d\n", stereo);
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
            fprintf(stderr, "rec: eof on input\n");
            break;
        }
        fwrite(buf, 1, n, f);
    }
    close(fd);
    fclose(f);
    return 0;
}
