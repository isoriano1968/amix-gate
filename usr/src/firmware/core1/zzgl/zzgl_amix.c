/*
 * zzgl_amix.c -- zzgl on AMIX: the 060's side of the GL on the Z3660's
 * core 1.  Maps the board memory through /dev/zz9000 (a ZZ9K_MAP_BOARD
 * kernel), starts the server (a job for z3660ARM_Core_gpu, as armbench
 * posts them), and shows the frames in the RTG overlay over the window:
 * switched on the way vplay's zzav.c and glshow do it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "bench.h"
#include "zzav.h"
#include "zzgl.h"
#include "zzgl_enc.h"
#include "zzgl_platform.h"

extern int munmap();                    /* not in AMIX's sys/mman.h */

/* the AMIX zz9000 driver (zz9000.h), as zzav.c */
#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_SETREG      (ZZ9K_IOC | 7)
struct zz9k_info {
   unsigned long base, board_size, fb_offset, fb_size;
   unsigned short hw_version, fw_version, product, zorro;
   unsigned short width, height, color_mode, mode;
};
struct zz9k_reg { unsigned short offset, value; };
#define FB_PHYS             0x00200000UL
#define PAGE                4096UL

#define REG_MPEG_CMD        0x2C0
#define REG_MPEG_PARAM0     0x2C8
#define REG_OVL_YSTART      0x32C
#define REG_OVL_XSTART      0x330
#define REG_OVL_WIDTH       0x334
#define REG_OVL_HEIGHT      0x338
#define CMD_INIT            1
#define CMD_CLOSE           5
#define FIFO_OFF            0x07A00000UL
#define FIFO_SIZE           0x00010000UL
#define SHARED_OFF          0x07B00000UL

static int fd = -1, vs = 1, win_w, win_h, overlay_on, snd_div;
static volatile unsigned long *zblk;
static unsigned char *snd_stream;
static struct zz9k_info info;
static volatile unsigned long *ctl, *c1ld;
static struct zzgl_transport tr;

static void reg32(unsigned off, unsigned long v)
{
   struct zz9k_reg r;

   r.offset = off;
   r.value = (unsigned short)(v >> 16);
   ioctl(fd, ZZ9KIOC_SETREG, &r);
   r.offset = off + 2;
   r.value = (unsigned short)v;
   ioctl(fd, ZZ9KIOC_SETREG, &r);
}

static void *map(unsigned long off, unsigned long len)
{
   void *p = mmap(0, (size_t)len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(off - FB_PHYS));

   return p == (void *)-1 ? NULL : p;
}

static double now(void)
{
   struct timeval tv;

   gettimeofday(&tv, 0);
   return tv.tv_sec + tv.tv_usec / 1e6;
}

static void copy(void *dst, const void *src, size_t n)
{
   memcpy(dst, src, n);         /* lib060's (move16) when linked before libc */
}

/* while core 1 catches up: poll a little, then give the CPU away */
static void wait_core1(void)
{
   static int spins;

   if (++spins < 200)
      return;
   spins = 0;
   usleep(2000);
}

/* the picture's place in both overlay buffers to 0 (transparent) */
static void clear_overlay(int row0, int rows)
{
   const unsigned long bufs[2] = { ZZAV_OVL_OFF, ZZAV_OVL_OFF1 };
   const unsigned long pitch = (unsigned long)info.width * 4;
   int b, y;

   for (b = 0; b < 2; b++) {
      const unsigned long start = bufs[b] + (unsigned long)row0 * pitch;
      const unsigned long base = start & ~(PAGE - 1);
      const unsigned long len = (start - base + (unsigned long)rows * pitch + PAGE - 1) & ~(PAGE - 1);
      unsigned char *p = (unsigned char *)map(base, len);

      if (!p)
         continue;
      for (y = 0; y < rows; y++)
         memset(p + (start - base) + (unsigned long)y * pitch, 0, (size_t)win_w * 4);
      munmap((caddr_t)p, (size_t)len);
   }
}

static void place_regs(int x, int y)
{
   reg32(REG_OVL_XSTART, (unsigned long)x);
   reg32(REG_OVL_YSTART, (unsigned long)y);
   reg32(REG_OVL_WIDTH, (unsigned long)win_w);
   reg32(REG_OVL_HEIGHT, (unsigned long)(win_h * vs));
}

/* x, y of the window kept on the screen */
static void clamp_place(int *x, int *y)
{
   if (*x + win_w > info.width)
      *x = info.width - win_w;
   if (*y + win_h * vs > info.height)
      *y = info.height - win_h * vs;
   if (*x < 0)
      *x = 0;
   if (*y < 0)
      *y = 0;
}

/* core 1's server: running, or started as a job */
static int start_server(volatile unsigned long *blk)
{
   volatile unsigned long *data;
   char name[17];
   double t0;
   int i;

   for (i = 0; i < 4; i++) {
      const unsigned long w = ctl[W_NAME + i];

      name[i * 4] = (char)(w >> 24);
      name[i * 4 + 1] = (char)(w >> 16);
      name[i * 4 + 2] = (char)(w >> 8);
      name[i * 4 + 3] = (char)w;
   }
   name[16] = 0;
   if (ctl[W_MAGIC] != HB_MAGIC_READY || strcmp(name, "gpu")) {
      fprintf(stderr, "zzgl: core 1 does not run the GPU program (runs \"%s\"): "
              "armbench -reload z3660ARM_Core_gpu.bin\n", name);
      return -1;
   }
   if (blk[ZB_MAGIC] == ZZGL_MAGIC && ctl[W_STATE] == HB_RUNNING)
      return 0;                 /* a server left by a program that died */
   if (ctl[W_STATE] == HB_POSTED || ctl[W_STATE] == HB_RUNNING) {
      fprintf(stderr, "zzgl: core 1 is busy (state %lu)\n", ctl[W_STATE]);
      return -1;
   }
   if (!(data = (volatile unsigned long *)map(HB_DATA_OFF, PAGE)))
      return -1;
   blk[ZB_MAGIC] = 0;
   data[0] = ZZGL_JOB_MAGIC;
   ctl[W_DATA_OFF] = HB_DATA_OFF;
   ctl[W_DATA_SIZE] = 4;
   ctl[W_FLAGS] = 0;
   ctl[W_SEQ] = ctl[W_SEQ] + 1;
   ctl[W_STATE] = HB_POSTED;
   munmap((caddr_t)data, (size_t)PAGE);
   for (t0 = now(); blk[ZB_MAGIC] != ZZGL_MAGIC; usleep(10000))
      if (now() - t0 > 3.0) {
         fprintf(stderr, "zzgl: core 1's server did not start (state %lu)\n", ctl[W_STATE]);
         return -1;
      }
   return 0;
}

static int server_only;
static unsigned char *shared;
int zzgl_platform_show(int x, int y, int w, int h);

/* the server without the overlay: zz3660 for now shows frames through X */
int zzgl_platform_server(void)
{
   server_only = 1;
   return zzgl_platform_open(NULL, 0, 0, 0, 16, 16);
}

int zzgl_platform_show(int x, int y, int w, int h)
{
   /* the overlay over the window: as glshow (place before and after the
      MPEG INIT: core 0 takes a new size only once the overlay is on) */
   win_w = w;
   win_h = h;
   vs = info.width >= 1600 && !(c1ld[C1LD_W_MAGIC] == C1LD_MAGIC && (c1ld[C1LD_W_FEAT] & C1LD_FEAT_OVL_FETCH)) ? 2 : 1;
   if (w > info.width || h * vs > info.height) {
      fprintf(stderr, "zzgl: %dx%d does not fit the screen (%ux%u)\n", w, h, info.width, info.height);
      return -1;
   }
   clamp_place(&x, &y);
   clear_overlay(y / vs, h);
   place_regs(x, y);
   memset(shared, 0, 96);
   reg32(REG_MPEG_PARAM0 + 0, info.base + SHARED_OFF);
   reg32(REG_MPEG_PARAM0 + 4, info.base);
   reg32(REG_MPEG_PARAM0 + 8, FIFO_SIZE);
   reg32(REG_MPEG_PARAM0 + 12, FIFO_OFF);
   reg32(REG_MPEG_CMD, CMD_INIT);
   usleep(100000);
   place_regs(x, y);
   overlay_on = 1;
   zzgl_overlay((unsigned long)info.width * 4, (uint32_t)(y / vs), (uint32_t)w, (uint32_t)h,
                c1ld[C1LD_W_MAGIC] == C1LD_MAGIC && (c1ld[C1LD_W_FEAT] & C1LD_FEAT_OVL_FLIP));
   return 0;
}

int zzgl_platform_open(void *dpy, unsigned long win, int x, int y, int w, int h)
{
   volatile unsigned long *blk;
   (void)dpy;
   (void)win;
   if ((fd = open("/dev/zz9000", O_RDWR)) < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0) {
      perror("zzgl: /dev/zz9000");
      return -1;
   }
   ctl = (volatile unsigned long *)map(HB_CTL_OFF, PAGE);
   c1ld = (volatile unsigned long *)map(C1LD_OFF, PAGE);
   blk = (volatile unsigned long *)map(ZZGL_BLK_OFF, PAGE);
   tr.ring = (uint8_t *)map(ZZGL_RING_OFF, ZZGL_RING_SIZE);
   tr.reply = (const uint8_t *)map(ZZGL_REPLY_OFF, ZZGL_REPLY_SIZE);
   shared = (unsigned char *)map(SHARED_OFF, PAGE);
   if (!ctl || !c1ld || !blk || !tr.ring || !tr.reply || !shared) {
      perror("zzgl: mmap (needs a ZZ9K_MAP_BOARD kernel)");
      return -1;
   }
   if (start_server(blk) < 0)
      return -1;
   zblk = blk;
   tr.blk = (volatile uint32_t *)blk;
   tr.copy = copy;
   tr.wait = wait_core1;
   if (zzgl_open(&tr, w, h, 16, 0) < 0) {
      fprintf(stderr, "zzgl: no answer from core 1\n");
      return -1;
   }
   if (blk[ZB_ERROR]) {
      fprintf(stderr, "zzgl: core 1 could not make the context (%08lx)\n", blk[ZB_ERROR]);
      return -1;
   }

   return server_only ? 0 : zzgl_platform_show(x, y, w, h);
}

void zzgl_platform_frame(void)
{
}

void zzgl_platform_place(int x, int y)
{
   if (!overlay_on)
      return;
   clamp_place(&x, &y);
   clear_overlay(y / vs, win_h);
   place_regs(x, y);
   zzgl_overlay((unsigned long)info.width * 4, (uint32_t)(y / vs), (uint32_t)win_w, (uint32_t)win_h,
                c1ld[C1LD_W_MAGIC] == C1LD_MAGIC && (c1ld[C1LD_W_FEAT] & C1LD_FEAT_OVL_FLIP));
}

void zzgl_platform_close(void)
{
   double t0;

   if (fd < 0)
      return;
   zzgl_close();
   for (t0 = now(); ctl && ctl[W_STATE] == HB_RUNNING && now() - t0 < 2.0; )
      usleep(10000);
   if (overlay_on)
      reg32(REG_MPEG_CMD, CMD_CLOSE);     /* the overlay off */
   overlay_on = 0;
   close(fd);
   fd = -1;
}


/*
 * Sound: the board's HDMI through core 1 (gpu.c's snd_service).  The DMA
 * is started as vplay does it: the ring's address into core 0's audio
 * registers (it clears the ring and starts).
 */
#define REG_AUDIO_PARAM     0x1EC
#define REG_AUDIO_VAL       0x1F0

int zzgl_sound_open(int div)
{
   if (fd < 0 || !zblk || zblk[ZB_MAGIC] != ZZGL_MAGIC || (div != 1 && div != 2))
      return -1;
   if (!snd_stream && !(snd_stream = (unsigned char *)map(ZZGL_SND_OFF, ZZGL_SND_SIZE)))
      return -1;
   memset(snd_stream, 0, ZZGL_SND_SIZE);
   zblk[ZB_SND_ON] = 0;
   zblk[ZB_SND_WR] = 0;
   reg32(REG_AUDIO_PARAM, 0);
   reg32(REG_AUDIO_VAL, ZZGL_HDMI_OFF);
   usleep(100000);                      /* core 0's main loop takes it */
   snd_div = div;
   zblk[ZB_SND_POS] = 0;
   zblk[ZB_SND_ON] = (unsigned long)div;
   return 0;
}

unsigned long zzgl_sound_pos(void)
{
   return zblk && snd_div ? zblk[ZB_SND_POS] / (unsigned long)snd_div : 0;
}

void zzgl_sound_write(unsigned long at, const short *frames, unsigned long n)
{
   unsigned long off, first;

   if (!snd_stream || !n || n > ZZGL_SND_SIZE)
      return;
   off = at % ZZGL_SND_SIZE;
   first = ZZGL_SND_SIZE - off;
   if (first > n)
      first = n;
   memcpy(snd_stream + off, frames, first);
   if (n > first)
      memcpy(snd_stream, (const char *)frames + first, n - first);
   zblk[ZB_SND_WR] = at + n;
}

void zzgl_sound_close(void)
{
   unsigned char *ring;

   if (zblk)
      zblk[ZB_SND_ON] = 0;
   if (snd_div && (ring = (unsigned char *)map(ZZGL_HDMI_OFF, ZZGL_HDMI_SIZE)) != NULL) {
      /* the DMA loops the ring for ever: silence, not the last notes */
      usleep(20000);                    /* core 1 lets go of it */
      memset(ring, 0, ZZGL_HDMI_SIZE);
      munmap((caddr_t)ring, (size_t)ZZGL_HDMI_SIZE);
   }
   snd_div = 0;
}
