/*
 * amix.c -- the amix core: one image for core 1 with the GL server of gpu,
 * the H.264 player and decoder of h264bench, zzcrypto's public-key jobs and
 * core 1's own network (lwIP over core 0's rings, netbench's c1netif.c) for
 * the streams it fetches itself (netsrc.c).  The single cores stay as they
 * are; this file only chooses between them.
 * A posted job carrying ZZGL_JOB_MAGIC is GL work, any other is an H.264
 * clip; the player (ZZAV_CMD_OPEN, ZZAV_CMD_OPEN_URL) is h264bench's own.
 */
#include <stdint.h>
#include <stddef.h>
#include "bench.h"
#include "zzgl.h"
#include "c1netif.h"

const char hb_core_name[16] = "amix";

int gl_hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
                 void (*progress)(uint32_t frames), struct hb_result *r);
int h264_hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
                   void (*progress)(uint32_t frames), struct hb_result *r);
void zzc_service(void);

int hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
              void (*progress)(uint32_t frames), struct hb_result *r)
{
    if (size >= 4 && __builtin_bswap32(*(const uint32_t *)data) == ZZGL_JOB_MAGIC)
        return gl_hb_decode(data, size, flags, clock, progress, r);
    return h264_hb_decode(data, size, flags, clock, progress, r);
}

/* while idle and while waiting (GL server, player, netsrc): crypto jobs and
   the network (DHCP, ARP, TCP's timers, frames in) */
void hb_service(void)
{
    zzc_service();
    c1net_service();
}
