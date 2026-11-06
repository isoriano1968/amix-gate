/*
 * yuvrow16_test.c -- cores/h264bench/yuvrow.h's row_neon16 against the C
 * formula player.c uses (8.8 fixed point): every Y with a grid of U/V, each
 * channel within 1; under qemu-arm:
 *   arm-none-eabi-gcc -O2 -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard -I../../cores/h264bench \
 *     yuvrow16_test.c --specs=rdimon.specs -o yuvrow16_test; qemu-arm -cpu cortex-a9 yuvrow16_test
 */
#include <stdio.h>
#include <stdint.h>
#include "yuvrow.h"

static uint32_t c8(int v) { v >>= 8; return v < 0 ? 0 : v > 255 ? 255 : (uint32_t)v; }

static uint32_t ref(int y, int u, int v)
{
    int c = 298 * (y - 16) + 128, d = u - 128, e = v - 128;

    return c8(c + 409 * e) << 16 | c8(c - 100 * d - 208 * e) << 8 | c8(c + 516 * d);
}

int main(void)
{
    static uint8_t y[16], u[8], v[8];
    static uint32_t out[16];
    long n = 0, off1 = 0, bad = 0;
    int a, b, i, k, c;

    for (a = 0; a < 256; a++)
        for (b = 0; b < 256; b += 3) {
            for (i = 0; i < 16; i++)
                y[i] = (uint8_t)((a + i * 17) & 255);
            for (i = 0; i < 8; i++) {
                u[i] = (uint8_t)b;
                v[i] = (uint8_t)((b * 7 + a + i * 31) & 255);
            }
            row_neon16(out, y, u, v, 16);
            for (i = 0; i < 16; i++, n++) {
                const uint32_t r = ref(y[i], u[i / 2], v[i / 2]);
                int worst = 0;

                for (c = 0; c < 32; c += 8) {
                    k = (int)((out[i] >> c) & 255) - (int)((r >> c) & 255);
                    if (k < 0)
                        k = -k;
                    if (k > worst)
                        worst = k;
                }
                if (worst == 1)
                    off1++;
                if (worst > 1 && bad++ < 5)
                    printf("y %d u %d v %d: %08lx, C %08lx\n", y[i], u[i / 2], v[i / 2],
                           (unsigned long)out[i], (unsigned long)r);
            }
        }
    printf("%ld pixels: %ld off by 1, %ld off by more\n", n, off1, bad);
    return bad != 0;
}
