/*
 * stretch_lin.h -- one row of BGRA words stretched from n to o pixels,
 * filtered: output pixel i is the two source pixels around its centre,
 * (i + 0.5) * n / o - 0.5, mixed by distance with 7-bit weights (player.c,
 * a picture with non-square pixels; checked against the C row by
 * tests/vplay/stretch_test.c under qemu-arm).
 */
#ifndef STRETCH_LIN_H
#define STRETCH_LIN_H

#include <stdint.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#define LIN_MAX 2048

struct lin_tab {
    int n, o;
    uint16_t x[LIN_MAX];                /* the left source pixel */
    uint8_t wa[LIN_MAX * 4];            /* its weight, 0..128, once per byte */
    uint8_t wb[LIN_MAX * 4];            /* the right one's (wa + wb = 128) */
};

static void lin_make(struct lin_tab *t, int n, int o)
{
    const int64_t max = (int64_t)(n - 1) << 16;
    int i, k;

    if (n < 2 || o < 1 || o > LIN_MAX) {
        t->n = t->o = 0;
        return;
    }
    for (i = 0; i < o; i++) {
        int64_t pos = (((int64_t)(2 * i + 1) * n << 16) / (2 * o)) - 32768;
        int x0, f;

        if (pos < 0)
            pos = 0;
        if (pos > max)
            pos = max;
        x0 = (int)(pos >> 16);
        f = (int)(((pos & 0xFFFF) + 256) >> 9);         /* 0..128 */
        if (x0 >= n - 1) {                              /* the last pixel: all of it */
            x0 = n - 2;
            f = 128;
        }
        t->x[i] = (uint16_t)x0;
        for (k = 0; k < 4; k++) {
            t->wa[i * 4 + k] = (uint8_t)(128 - f);
            t->wb[i * 4 + k] = (uint8_t)f;
        }
    }
    t->n = n;
    t->o = o;
}

/* the C row: the NEON row's remainder, and the reference */
static void lin_row_c(const struct lin_tab *t, const uint32_t *in, uint32_t *out, int from)
{
    int i, c;

    for (i = from; i < t->o; i++) {
        const uint32_t a = in[t->x[i]], b = in[t->x[i] + 1];
        const uint32_t wa = t->wa[i * 4], wb = t->wb[i * 4];
        uint32_t v = 0;

        for (c = 0; c < 32; c += 8)
            v |= ((((a >> c) & 255) * wa + ((b >> c) & 255) * wb + 64) >> 7) << c;
        out[i] = v;
    }
}

/* the row into out[]; returns o */
static int lin_row(const struct lin_tab *t, const uint32_t *in, uint32_t *out)
{
    int i = 0;

#ifdef __ARM_NEON
    for (; i + 4 <= t->o; i += 4) {
        const uint16_t *x = t->x + i;
        uint32x4_t a = vdupq_n_u32(0), b = vdupq_n_u32(0);
        uint8x16_t wa, wb, pa, pb;
        uint16x8_t lo, hi;

        a = vld1q_lane_u32(in + x[0], a, 0);
        b = vld1q_lane_u32(in + x[0] + 1, b, 0);
        a = vld1q_lane_u32(in + x[1], a, 1);
        b = vld1q_lane_u32(in + x[1] + 1, b, 1);
        a = vld1q_lane_u32(in + x[2], a, 2);
        b = vld1q_lane_u32(in + x[2] + 1, b, 2);
        a = vld1q_lane_u32(in + x[3], a, 3);
        b = vld1q_lane_u32(in + x[3] + 1, b, 3);
        wa = vld1q_u8(t->wa + i * 4);
        wb = vld1q_u8(t->wb + i * 4);
        pa = vreinterpretq_u8_u32(a);
        pb = vreinterpretq_u8_u32(b);
        lo = vmull_u8(vget_low_u8(pa), vget_low_u8(wa));
        lo = vmlal_u8(lo, vget_low_u8(pb), vget_low_u8(wb));
        hi = vmull_u8(vget_high_u8(pa), vget_high_u8(wa));
        hi = vmlal_u8(hi, vget_high_u8(pb), vget_high_u8(wb));
        vst1q_u8((uint8_t *)(out + i), vcombine_u8(vrshrn_n_u16(lo, 7), vrshrn_n_u16(hi, 7)));
    }
#endif
    lin_row_c(t, in, out, i);
    return t->o;
}

/*
 * The same filter on one 8-bit plane row (Y, or U/V at half width), before
 * the colour conversion: per 16 output pixels the source bytes lie within 16
 * of a start (an enlargement), so one load and table lookups (vtbl) fetch
 * them -- far cheaper than gathering 32-bit pixels one by one.  Only for
 * enlargements (n < o); lin8_make says 0 otherwise.
 */
#define LIN8_BLK (LIN_MAX / 16 + 1)

struct lin8_tab {
    int n, o;
    uint16_t start[LIN8_BLK];           /* per 16 outputs: the first source byte loaded */
    uint8_t ia[LIN8_BLK * 16];          /* left source byte, from start */
    uint8_t ib[LIN8_BLK * 16];          /* right one */
    uint8_t wa[LIN8_BLK * 16];          /* weights, wa + wb = 128 */
    uint8_t wb[LIN8_BLK * 16];
};

static int lin8_make(struct lin8_tab *t, struct lin_tab *l, int n, int o)
{
    int b, i;

    t->n = t->o = 0;
    if (n < 16 || n >= o || o > LIN_MAX)
        return 0;
    lin_make(l, n, o);
    for (b = 0; b * 16 < o; b++) {
        int s = l->x[b * 16];

        if (s > n - 16)
            s = n - 16;         /* the load stays in the row */
        t->start[b] = (uint16_t)s;
        for (i = 0; i < 16; i++) {
            const int k = b * 16 + i < o ? b * 16 + i : o - 1;
            const int a = l->x[k] - s;

            if (a < 0 || a + 1 > 15)    /* cannot happen when enlarging; be safe */
                return 0;
            t->ia[b * 16 + i] = (uint8_t)a;
            t->ib[b * 16 + i] = (uint8_t)(a + 1);
            t->wa[b * 16 + i] = l->wa[k * 4];
            t->wb[b * 16 + i] = l->wb[k * 4];
        }
    }
    t->n = n;
    t->o = o;
    return 1;
}

static void lin8_row_c(const struct lin8_tab *t, const uint8_t *in, uint8_t *out, int from)
{
    int i;

    for (i = from; i < t->o; i++) {
        const int b = i >> 4, k = i & 15;
        const uint8_t *p = in + t->start[b];

        out[i] = (uint8_t)((p[t->ia[b * 16 + k]] * t->wa[b * 16 + k] +
                            p[t->ib[b * 16 + k]] * t->wb[b * 16 + k] + 64) >> 7);
    }
}

static void lin8_row(const struct lin8_tab *t, const uint8_t *in, uint8_t *out)
{
    int i = 0;

#ifdef __ARM_NEON
    for (; i + 16 <= t->o; i += 16) {
        const uint8x16_t src = vld1q_u8(in + t->start[i >> 4]);
        const uint8x16_t ia = vld1q_u8(t->ia + i), ib = vld1q_u8(t->ib + i);
        const uint8x16_t wa = vld1q_u8(t->wa + i), wb = vld1q_u8(t->wb + i);
        uint8x8x2_t tb;
        uint8x8_t al, ah, bl, bh;
        uint16x8_t lo, hi;

        tb.val[0] = vget_low_u8(src);
        tb.val[1] = vget_high_u8(src);
        al = vtbl2_u8(tb, vget_low_u8(ia));
        ah = vtbl2_u8(tb, vget_high_u8(ia));
        bl = vtbl2_u8(tb, vget_low_u8(ib));
        bh = vtbl2_u8(tb, vget_high_u8(ib));
        lo = vmlal_u8(vmull_u8(al, vget_low_u8(wa)), bl, vget_low_u8(wb));
        hi = vmlal_u8(vmull_u8(ah, vget_high_u8(wa)), bh, vget_high_u8(wb));
        vst1q_u8(out + i, vcombine_u8(vrshrn_n_u16(lo, 7), vrshrn_n_u16(hi, 7)));
    }
#endif
    lin8_row_c(t, in, out, i);
}

#endif
