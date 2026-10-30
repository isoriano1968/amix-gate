/*
 * yuvrow.h -- one row of YUV 4:2:0 into the overlay's BGRA words with NEON
 * (player.c; checked against the C formula by tests/yuvrow_test.c under qemu-arm).
 */
#ifndef YUVROW_H
#define YUVROW_H

#ifdef __ARM_NEON
#include <arm_neon.h>

/*
 * n pixels (n a multiple of 8) of one row into BGRA words.  Full size:
 * y[i], u/v[i / 2].  Half size: the rounded average of y[2i], y[2i+1] and
 * y2[2i], y2[2i+1] (y2 the next luma row), u/v[i] (4:2:0 chroma is at half
 * size already).  BT.601 limited range,
 * the same integer formula as the C path (8.8 fixed point, >> 8).
 */
static void row_neon(uint32_t *out, const uint8_t *y, const uint8_t *y2, const uint8_t *u, const uint8_t *v,
                     int n, int half)
{
    const int16x8_t c298 = vdupq_n_s16(298), c409 = vdupq_n_s16(409), c100 = vdupq_n_s16(100);
    const int16x8_t c208 = vdupq_n_s16(208), c516 = vdupq_n_s16(516);
    int x;

    for (x = 0; x < n; x += 8) {
        uint8x8_t y8, u8, v8;
        int16x8_t yy, uu, vv;
        int32x4_t ylo, yhi, t;
        int16x8_t r, g, b;
        uint8x8x4_t px;

        if (half) {
            /* the average of 2x2 luma samples (this row and y2, the next) */
            uint16x8_t s = vaddq_u16(vpaddlq_u8(vld1q_u8(y + x * 2)), vpaddlq_u8(vld1q_u8(y2 + x * 2)));

            y8 = vrshrn_n_u16(s, 2);
            u8 = vld1_u8(u + x);
            v8 = vld1_u8(v + x);
        } else {
            uint8x8_t u4 = vld1_u8(u + x / 2), v4 = vld1_u8(v + x / 2);

            y8 = vld1_u8(y + x);
            u8 = vzip_u8(u4, u4).val[0];            /* each chroma sample twice */
            v8 = vzip_u8(v4, v4).val[0];
        }
        yy = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(y8)), vdupq_n_s16(16));
        uu = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(u8)), vdupq_n_s16(128));
        vv = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(v8)), vdupq_n_s16(128));
        /* 298 * (Y - 16) + 128 overflows 16 bits: 32-bit lanes for the sums */
        ylo = vaddq_s32(vmull_s16(vget_low_s16(yy), vget_low_s16(c298)), vdupq_n_s32(128));
        yhi = vaddq_s32(vmull_s16(vget_high_s16(yy), vget_high_s16(c298)), vdupq_n_s32(128));
#define CH(dst, expr_lo, expr_hi) \
        dst = vcombine_s16(vqshrn_n_s32(expr_lo, 8), vqshrn_n_s32(expr_hi, 8))
        CH(r, vmlal_s16(ylo, vget_low_s16(vv), vget_low_s16(c409)),
              vmlal_s16(yhi, vget_high_s16(vv), vget_high_s16(c409)));
        t = vmlsl_s16(ylo, vget_low_s16(uu), vget_low_s16(c100));
        {
            int32x4_t t2 = vmlsl_s16(yhi, vget_high_s16(uu), vget_high_s16(c100));

            CH(g, vmlsl_s16(t, vget_low_s16(vv), vget_low_s16(c208)),
                  vmlsl_s16(t2, vget_high_s16(vv), vget_high_s16(c208)));
        }
        CH(b, vmlal_s16(ylo, vget_low_s16(uu), vget_low_s16(c516)),
              vmlal_s16(yhi, vget_high_s16(uu), vget_high_s16(c516)));
#undef CH
        px.val[0] = vqmovun_s16(b);
        px.val[1] = vqmovun_s16(g);
        px.val[2] = vqmovun_s16(r);
        px.val[3] = vdup_n_u8(0);
        vst4_u8((uint8_t *)(out + x), px);
    }
}
#endif

#endif
