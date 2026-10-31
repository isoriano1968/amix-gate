/*
 * host_main.c -- h264bench on the PC: the reference checksum (and the PC's
 * speed) for a clip, from the same decode loop the ARM runs.
 *
 *   h264bench-host [-nodeblock] clip.h264
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libavutil/log.h>
#include "bench.h"

static uint64_t clock_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv)
{
    struct hb_result r;
    int flags = 0, i;
    const char *file = NULL;
    unsigned char *buf;
    long n;
    FILE *fp;

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-nodeblock"))
            flags |= HB_FLAG_NODEBLOCK;
        else
            file = argv[i];
    if (!file || !(fp = fopen(file, "rb"))) {
        fprintf(stderr, "usage: h264bench-host [-nodeblock] clip.h264\n");
        return 2;
    }
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    rewind(fp);
    buf = malloc((size_t)n + 64);       /* the parser may read a little past the end */
    if (!buf || fread(buf, 1, (size_t)n, fp) != (size_t)n)
        return 1;
    memset(buf + n, 0, 64);
    fclose(fp);
    av_log_set_level(AV_LOG_ERROR);
    if (hb_decode(buf, (size_t)n, flags, clock_ns, NULL, &r) < 0)
        fprintf(stderr, "decode error %d\n", r.errcode);
    printf("%s: %u frames %ux%u, checksum %08x, errors %u, %.1f fps (max %.1f ms a frame)\n", file,
           r.frames, r.width, r.height, r.checksum, r.errors,
           r.ticks ? r.frames / (r.ticks / 1e9) : 0.0, r.max_ticks / 1e6);
    return 0;
}
