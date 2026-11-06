/*
 * stretch_test.c -- cores/h264bench/stretch_lin.h's NEON row against its C
 * row, for the widths core 1 meets (DVD 704/720/672 to 4:3 and 16:9, and odd
 * ones), random pixels; under qemu-arm:
 *   arm-none-eabi-gcc -O2 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -I../../cores/h264bench \
 *     stretch_test.c --specs=rdimon.specs -o stretch_test; qemu-arm -cpu cortex-a9 stretch_test
 */
#include <stdio.h>
#include <stdint.h>
#include "stretch_lin.h"

static struct lin_tab t;
static uint32_t in[LIN_MAX], out[LIN_MAX], ref[LIN_MAX];

static uint32_t rnd(void)
{
    static uint32_t s = 12345;

    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

int main(void)
{
    static const int wn[][2] = { { 672, 960 }, { 672, 961 }, { 720, 1024 }, { 704, 1001 }, { 720, 1280 },
                                 { 480, 853 }, { 640, 643 }, { 1440, 1920 }, { 9, 13 }, { 2, 7 }, { 800, 600 } };
    long bad = 0, n = 0;
    int k, r, i, j;

    for (k = 0; k < (int)(sizeof wn / sizeof wn[0]); k++)
        for (r = 0; r < 50; r++) {
            for (i = 0; i < wn[k][0]; i++)
                in[i] = rnd() & 0x00FFFFFFu;
            lin_make(&t, wn[k][0], wn[k][1]);
            lin_row(&t, in, out);
            lin_row_c(&t, in, ref, 0);
            for (i = 0; i < t.o; i++, n++)
                if (out[i] != ref[i] && bad++ < 5)
                    printf("w %d->%d px %d: %08lx, C %08lx\n", wn[k][0], wn[k][1], i,
                           (unsigned long)out[i], (unsigned long)ref[i]);
            /* the ends: the first and last pixels as the source's */
            if (t.wa[0] != 128 && wn[k][1] > wn[k][0])
                printf("w %d->%d: first weight %d\n", wn[k][0], wn[k][1], t.wa[0]), bad++;
            for (j = 0; j < t.o; j++)
                if (t.x[j] + 1 >= wn[k][0] + 1)
                    printf("w %d->%d: px %d reads past the row\n", wn[k][0], wn[k][1], j), bad++;
        }
    /* a flat row stays flat; a step stays monotonic */
    for (i = 0; i < 672; i++)
        in[i] = i < 336 ? 0x00101010u : 0x00F0F0F0u;
    lin_make(&t, 672, 961);
    lin_row(&t, in, out);
    for (i = 1; i < t.o; i++)
        if ((out[i] & 255) < (out[i - 1] & 255))
            printf("step not monotonic at %d\n", i), bad++;
    printf("%ld pixels, %ld wrong\n", n, bad);
    return bad != 0;
}
