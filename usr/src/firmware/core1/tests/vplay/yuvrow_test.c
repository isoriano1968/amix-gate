/*
 * yuvrow_test.c -- arm/h264bench/yuvrow.h's NEON rows against the C formula
 * player.c uses, every Y/U/V combination that matters; under qemu-arm:
 *   arm-none-eabi-gcc ... --specs=rdimon.specs; qemu-arm -cpu cortex-a9 prog
 */
#include <stdio.h>
#include <stdint.h>
#include "../arm/h264bench/yuvrow.h"

static uint32_t c8(int v) { v >>= 8; return v < 0 ? 0 : v > 255 ? 255 : (uint32_t)v; }

static uint32_t ref(int y, int u, int v)
{
    int c = 298 * (y - 16) + 128, d = u - 128, e = v - 128;

    return c8(c + 409 * e) << 16 | c8(c - 100 * d - 208 * e) << 8 | c8(c + 516 * d);
}

int main(void)
{
    static uint8_t y[64], y2[64], u[64], v[64];
    static uint32_t out[32];
    long bad = 0, n = 0;
    int a, b, c, i, half;

    for (half = 0; half < 2; half++)
        for (a = 0; a < 256; a += 3)
            for (b = 0; b < 256; b += 5)
                for (c = 0; c < 256; c += 7) {
                    for (i = 0; i < 64; i++) {
                        y[i] = (uint8_t)(a + i * 13);
                        y2[i] = (uint8_t)(a * 7 + i * 17 + b);
                        u[i] = (uint8_t)(b + i * 29);
                        v[i] = (uint8_t)(c + i * 31);
                    }
                    row_neon(out, y, y2, u, v, 16, half);
                    for (i = 0; i < 16; i++) {
                        int yb = (y[i * 2] + y[i * 2 + 1] + y2[i * 2] + y2[i * 2 + 1] + 2) >> 2;
                        uint32_t r = half ? ref(yb, u[i], v[i]) : ref(y[i], u[i / 2], v[i / 2]);

                        n++;
                        if (out[i] != r && bad++ < 5)
                            printf("half %d y %d u %d v %d: neon %08x c %08x\n", half,
                                   half ? yb : y[i], half ? u[i] : u[i / 2], half ? v[i] : v[i / 2],
                                   (unsigned)out[i], (unsigned)r);
                    }
                }
    printf("%ld pixels, %ld differ\n", n, bad);
    return bad != 0;
}
