/*
 * zzgl_replay.c -- a stream recorded on the PC (zzgl_host.c, ZZGL_REC) through
 * the ARM's decoder, Mesa and softpipe with NEON, under qemu-arm: core 1's
 * code where a debugger can reach it, without the board.
 *
 *   qemu-arm -cpu cortex-a9 -semihosting zzgl_replay.elf rec.bin [out.ppm]
 *
 * One line per frame; the last frame as PPM.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "zzgl.h"
#include "zzgl_dec.h"

static uint32_t blk[ZB_NWORDS];
static uint8_t *ring, *reply;
static const char *out;
static int frames;

static void copy(void *dst, const void *src, size_t n) { memcpy(dst, src, n); }

static void
present(const void *rgba, int w, int h)
{
   FILE *f;
   int y, x;

   {
      const struct mallinfo mi = mallinfo();

      printf("frame %d %dx%d heap %d in use %d\n", ++frames, w, h, (int)mi.arena, (int)mi.uordblks);
   }
   fflush(stdout);
   if (out && (f = fopen(out, "wb")) != NULL) {
      const uint8_t *s = rgba;

      fprintf(f, "P6\n%d %d\n255\n", w, h);
      for (y = h - 1; y >= 0; y--)
         for (x = 0; x < w; x++)
            fwrite(s + ((size_t)y * w + x) * 4, 1, 3, f);
      fclose(f);
   }
}

int
main(int argc, char **argv)
{
   static const struct zzgl_dec_env proto = { blk, NULL, NULL, copy, copy, present, NULL, NULL, NULL };
   struct zzgl_dec_env env = proto;
   uint32_t head[2];
   long segs = 0;
   FILE *f;

   if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
      printf("usage: zzgl_replay rec.bin [out.ppm]\n");
      return 1;
   }
   out = argc > 2 ? argv[2] : NULL;
   ring = calloc(1, ZZGL_RING_SIZE);
   reply = calloc(1, ZZGL_REPLY_SIZE);
   if (!ring || !reply) {
      printf("no memory\n");
      return 1;
   }
   setenv("SOFTPIPE_FAST_WHY", "1", 1);
   env.ring = ring;
   env.reply = reply;
   zzgl_dec_init(&env);
   while (fread(head, 4, 2, f) == 2) {
      const uint32_t pos = head[0], len = head[1];

      if (len > ZZGL_RING_SIZE - pos || fread(ring + pos, 1, len, f) != len)
         break;
      blk[ZB_WR] = __builtin_bswap32((pos + len) % ZZGL_RING_SIZE);
      if (zzgl_dec_run(&env, 1) < 0) {
         printf("bad stream: %08x\n", (unsigned)__builtin_bswap32(blk[ZB_ERROR]));
         return 1;
      }
      segs++;
   }
   {
      extern unsigned sp_fast_why[16];
      int k;

      for (k = 1; k < 16; k++)
         if (sp_fast_why[k])
            printf("refused by reason %d: %u draws\n", k, sp_fast_why[k]);
   }
   printf("done: %ld segments, %d frames, error %08x\n", segs, frames, (unsigned)__builtin_bswap32(blk[ZB_ERROR]));
   return 0;
}

/* what gpu.c gives Mesa's compat layer on the board */
unsigned long long armgl_now_us(void) { static unsigned long long t; return t += 1000; }
