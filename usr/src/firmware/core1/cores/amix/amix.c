/*
 * amix.c -- the amix core: one image for core 1 with the GL server of gpu
 * and the H.264 player and decoder of h264bench (crypto to follow).  The
 * single cores stay as they are; this file only chooses between them.
 * A posted job carrying ZZGL_JOB_MAGIC is GL work, any other is an H.264
 * clip; the player (ZZAV_CMD_OPEN) is h264bench's own.
 */
#include <stdint.h>
#include <stddef.h>
#include "bench.h"
#include "zzgl.h"

const char hb_core_name[16] = "amix";

int gl_hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
                 void (*progress)(uint32_t frames), struct hb_result *r);
int h264_hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
                   void (*progress)(uint32_t frames), struct hb_result *r);

int hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
              void (*progress)(uint32_t frames), struct hb_result *r)
{
    if (size >= 4 && __builtin_bswap32(*(const uint32_t *)data) == ZZGL_JOB_MAGIC)
        return gl_hb_decode(data, size, flags, clock, progress, r);
    return h264_hb_decode(data, size, flags, clock, progress, r);
}
