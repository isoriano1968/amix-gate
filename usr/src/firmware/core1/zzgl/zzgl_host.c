/*
 * zzgl_host.c -- zzgl on the PC, both sides in one process: the encoder's
 * ring, block and reply area are plain memory, core 1's decoder runs
 * whenever the encoder waits and after each frame, and frames go into the
 * X window (XPutImage).  For testing the marshalling with real programs
 * (ioquake3's PC build) before the board.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "zzgl.h"
#include "zzgl_enc.h"
#include "zzgl_dec.h"
#include "zzgl_platform.h"

static uint32_t blk[ZB_NWORDS];
static uint8_t *ring, *reply;
static Display *dpy;
static Window win;
static GC gc;
static XImage *img;

static void copy(void *dst, const void *src, size_t n) { memcpy(dst, src, n); }

static void present(const void *rgba, int w, int h);

void spook_frame(uint32_t *fb, int w, int h, float time, float beat);

static const struct zzgl_dec_env env = {
   blk, NULL, NULL, copy, copy, present, NULL, NULL, NULL, spook_frame
};
static struct zzgl_dec_env denv;

/* ZZGL_REC=file: what the decoder is about to take, as ring position, length
   and bytes, for tests/zzgl_replay.c on the ARM */
static void
rec_segment(FILE *f, uint32_t pos, uint32_t len)
{
   fwrite(&pos, 4, 1, f);
   fwrite(&len, 4, 1, f);
   fwrite(ring + pos, 1, len, f);
}

static void
dec_run(void)
{
   static FILE *rec;
   static int tried;

   if (!tried) {
      tried = 1;
      if (getenv("ZZGL_REC"))
         rec = fopen(getenv("ZZGL_REC"), "wb");
   }
   if (rec) {
      const uint32_t rd = __builtin_bswap32(blk[ZB_RD]), wr = __builtin_bswap32(blk[ZB_WR]);

      if (wr >= rd) {
         rec_segment(rec, rd, wr - rd);
      } else {
         rec_segment(rec, rd, ZZGL_RING_SIZE - rd);
         rec_segment(rec, 0, wr);
      }
      fflush(rec);
   }
   zzgl_dec_run(&denv, 1);
}

static void
wait_dec(void)
{
   dec_run();
}

static struct zzgl_transport tr;

/* the frame (RGBA bytes, bottom row first) into the window */
static void
present(const void *rgba, int w, int h)
{
   const uint8_t *s = rgba;
   int x, y;

   if (!img || img->width != w || img->height != h) {
      if (img)
         XDestroyImage(img);
      img = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), DefaultDepth(dpy, DefaultScreen(dpy)),
                         ZPixmap, 0, malloc((size_t)w * h * 4), w, h, 32, 0);
      if (!img)
         return;
   }
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++) {
         const uint8_t *p = s + ((size_t)(h - 1 - y) * w + x) * 4;

         XPutPixel(img, x, y, (unsigned long)p[0] << 16 | p[1] << 8 | p[2]);
      }
   XPutImage(dpy, win, gc, img, 0, 0, 0, 0, w, h);
   XFlush(dpy);
   {
      /* ZZGL_DUMP=file: the frame as PPM (overwritten each frame) */
      static int frames;
      const char *dump = getenv("ZZGL_DUMP");
      FILE *f;

      frames++;
      if (dump && (f = fopen(dump, "wb")) != NULL) {
         fprintf(f, "P6\n%d %d\n255\n", w, h);
         for (y = h - 1; y >= 0; y--)
            for (x = 0; x < w; x++)
               fwrite(s + ((size_t)y * w + x) * 4, 1, 3, f);
         fclose(f);
      }
      if (getenv("ZZGL_STOP")) {
         const int stop = atoi(getenv("ZZGL_STOP"));
         /* the fast spans' pixels: covered and past the depth test, as an
            average of the last 20 frames (sp_fast.c, PC builds only) */
         static uint64_t *cov, *pass, c0, p0;

         if (!cov) {
            cov = dlsym(RTLD_DEFAULT, "sp_fast_zcov");
            pass = dlsym(RTLD_DEFAULT, "sp_fast_zpass");
         }
         if (cov && pass && frames == stop - 20)
            c0 = *cov, p0 = *pass;
         if (frames >= stop) {
            if (cov && pass && stop > 20)
               fprintf(stderr, "zzgl_host: last 20 frames: %.0f pixels covered, %.0f past the depth test a frame\n",
                       (double)(*cov - c0) / 20, (double)(*pass - p0) / 20);
            exit(0);
         }
      }
      if (getenv("ZZGL_FRAMES") && frames % 50 == 0)
         fprintf(stderr, "zzgl_host: %d frames\n", frames);
   }
}

int
zzgl_platform_open(void *d, unsigned long w, int x, int y, int width, int height)
{
   (void)x; (void)y;
   dpy = d;
   win = w;
   gc = XCreateGC(dpy, win, 0, NULL);
   ring = calloc(1, ZZGL_RING_SIZE);
   reply = calloc(1, ZZGL_REPLY_SIZE);
   if (!ring || !reply)
      return -1;
   denv = env;
   denv.ring = ring;
   denv.reply = reply;
   zzgl_dec_init(&denv);
   tr.blk = blk;
   tr.ring = ring;
   tr.reply = reply;
   tr.copy = copy;
   tr.wait = wait_dec;
   if (zzgl_open(&tr, width, height, 16, 0) < 0) {
      fprintf(stderr, "zzgl_host: no server\n");
      return -1;
   }
   if (blk[ZB_ERROR]) {
      fprintf(stderr, "zzgl_host: core 1 side error %08x\n", __builtin_bswap32(blk[ZB_ERROR]));
      return -1;
   }
   return 0;
}

void
zzgl_platform_frame(void)
{
   static int reported;

   dec_run();
   if (blk[ZB_ERROR] && !reported) {
      reported = 1;
      fprintf(stderr, "zzgl_host: a command core 1 did not know: %08x\n", __builtin_bswap32(blk[ZB_ERROR]));
   }
}

void
zzgl_platform_place(int x, int y)
{
   (void)x; (void)y;
}

void
zzgl_platform_close(void)
{
   if (!ring)
      return;
   zzgl_close();
   zzgl_dec_run(&denv, 1);
   zzgl_dec_shutdown();
}

/* no board here: the program's sound goes elsewhere */
int zzgl_sound_open(int div) { (void)div; return -1; }
unsigned long zzgl_sound_pos(void) { return 0; }
void zzgl_sound_write(unsigned long at, const short *frames, unsigned long n) { (void)at; (void)frames; (void)n; }
void zzgl_sound_close(void) { }
