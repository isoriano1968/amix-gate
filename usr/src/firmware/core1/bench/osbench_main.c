/*
 * osbench_main.c -- osbench on a Unix (PC, AMIX):
 *   osbench [gears|q3|q3mt|fluid] [w h] [frames] [tris]
 * GALLIUM_NOOP=1 runs Mesa without drawing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "osbench.h"

unsigned long long osb_now_us(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

int main(int argc, char **argv)
{
    enum osb_scene scene = argc > 1 && !strcmp(argv[1], "q3") ? OSB_Q3 :
                           argc > 1 && !strcmp(argv[1], "fluid") ? OSB_FLUID :
                           argc > 1 && !strcmp(argv[1], "q3mt") ? OSB_Q3MT : OSB_GEARS;
    int w = argc > 3 ? atoi(argv[2]) : 320, h = argc > 3 ? atoi(argv[3]) : 240;
    int frames = argc > 4 ? atoi(argv[4]) : 20;
    char err[128];
    void *buf;
    double ms;

    if (argc > 5)
        osb_q3_tris = atoi(argv[5]);
    if (!(buf = calloc((size_t)w * h, 4))) {
        perror("calloc");
        return 1;
    }
    ms = osb_run(scene, buf, w, h, frames, err);
    if (ms < 0) {
        fprintf(stderr, "osbench: %s\n", err);
        return 1;
    }
    if (getenv("OSB_PPM")) {   /* OSB_PPM=file: the last frame, bottom row first in GL */
        FILE *f = fopen(getenv("OSB_PPM"), "wb");
        int y, x;

        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (y = h - 1; y >= 0; y--)
                for (x = 0; x < w; x++)
                    fwrite((unsigned char *)buf + ((long)y * w + x) * 4, 1, 3, f);
            fclose(f);
        }
    }
    printf("%s %dx%d: %.1f ms/frame (%.1f fps) over %d frames, checksum %08lx%s\n",
           scene == OSB_GEARS ? "gears" : scene == OSB_Q3 ? "q3" : scene == OSB_Q3MT ? "q3mt" : "fluid", w, h, ms, 1000.0 / ms, frames,
           osb_checksum(buf, w, h), getenv("GALLIUM_NOOP") ? " [noop]" : "");
    return 0;
}
