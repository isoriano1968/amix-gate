/*
 * stretch8_test.c -- stretch_lin.h's 8-bit plane row (lin8_row, NEON) against
 * its C row and against the 32-bit row (lin_row_c) on grey pixels: the same
 * filter either way; under qemu-arm:
 *   arm-none-eabi-gcc -O2 -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard -I../../cores/h264bench \
 *     stretch8_test.c --specs=rdimon.specs -o stretch8_test; qemu-arm -cpu cortex-a9 stretch8_test
 */
#include <stdio.h>
#include <stdint.h>
#include "stretch_lin.h"

static struct lin_tab l, l32;
static struct lin8_tab t;
static uint8_t in[LIN_MAX + 64], out[LIN_MAX], ref[LIN_MAX];
static uint32_t in32[LIN_MAX], out32[LIN_MAX];

static uint32_t rnd(void)
{
    static uint32_t s = 2463534242u;

    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

int main(void)
{
    static const int wn[][2] = { { 672, 961 }, { 336, 481 }, { 672, 960 }, { 336, 480 }, { 720, 1024 },
                                 { 360, 512 }, { 704, 1001 }, { 480, 853 }, { 16, 17 }, { 17, 40 },
                                 { 1440, 1920 } };
    long bad = 0, n = 0;
    int k, r, i;

    for (k = 0; k < (int)(sizeof wn / sizeof wn[0]); k++) {
        if (!lin8_make(&t, &l, wn[k][0], wn[k][1])) {
            printf("w %d->%d: no table\n", wn[k][0], wn[k][1]);
            bad++;
            continue;
        }
        lin_make(&l32, wn[k][0], wn[k][1]);
        for (r = 0; r < 100; r++) {
            for (i = 0; i < wn[k][0]; i++) {
                in[i] = (uint8_t)rnd();
                in32[i] = in[i] * 0x010101u;
            }
            lin8_row(&t, in, out);
            lin8_row_c(&t, in, ref, 0);
            lin_row_c(&l32, in32, out32, 0);
            for (i = 0; i < wn[k][1]; i++, n++)
                if ((out[i] != ref[i] || out[i] != (out32[i] & 255)) && bad++ < 5)
                    printf("w %d->%d px %d: %d, C %d, 32-bit %d\n", wn[k][0], wn[k][1], i, out[i], ref[i],
                           (int)(out32[i] & 255));
        }
    }
    /* a shrink, and an enlargement too close to 1:1 for 16 bytes: the 32-bit row does those */
    if (lin8_make(&t, &l, 800, 600) || lin8_make(&t, &l, 640, 643))
        printf("800->600 or 640->643 made a table\n"), bad++;
    printf("%ld pixels, %ld wrong\n", n, bad);
    return bad != 0;
}
