/*
 * zzgl_direct.c -- the reference for the PC test: the program's GL calls go
 * straight into the PC's Mesa 18.3 (OSMesa), no marshalling.  Same window
 * code and same Mesa as zzgl_host.c, so a difference between the two is a
 * marshalling bug.  Replaces zzgl_enc.c, zzgl_dec.c and zzgl_host.c.
 */
#define GL_GLEXT_PROTOTYPES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "zzgl_enc.h"
#include "zzgl_platform.h"

static OSMesaContext ctx;
static uint8_t *fb;
static int fb_w, fb_h, frames;
static Display *dpy;
static Window win;
static GC gc;
static XImage *img;

void *
zzgl_GetProcAddress(const char *name)
{
   return (void *)OSMesaGetProcAddress(name);
}

int
zzgl_platform_open(void *d, unsigned long w, int x, int y, int width, int height)
{
   (void)x; (void)y;
   dpy = d;
   win = w;
   gc = XCreateGC(dpy, win, 0, NULL);
   ctx = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
   fb = calloc((size_t)width * height, 4);
   fb_w = width;
   fb_h = height;
   return ctx && fb && OSMesaMakeCurrent(ctx, fb, GL_UNSIGNED_BYTE, width, height) ? 0 : -1;
}

void
zzgl_swap(void)
{
   const char *dump = getenv("ZZGL_DUMP");
   int x, y;
   FILE *f;

   glFinish();
   if (!img)
      img = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), DefaultDepth(dpy, DefaultScreen(dpy)),
                         ZPixmap, 0, malloc((size_t)fb_w * fb_h * 4), fb_w, fb_h, 32, 0);
   for (y = 0; y < fb_h; y++)
      for (x = 0; x < fb_w; x++) {
         const uint8_t *p = fb + ((size_t)(fb_h - 1 - y) * fb_w + x) * 4;

         XPutPixel(img, x, y, (unsigned long)p[0] << 16 | p[1] << 8 | p[2]);
      }
   XPutImage(dpy, win, gc, img, 0, 0, 0, 0, fb_w, fb_h);
   XFlush(dpy);
   frames++;
   if (dump && (f = fopen(dump, "wb")) != NULL) {
      fprintf(f, "P6\n%d %d\n255\n", fb_w, fb_h);
      for (y = fb_h - 1; y >= 0; y--)
         for (x = 0; x < fb_w; x++)
            fwrite(fb + ((size_t)y * fb_w + x) * 4, 1, 3, f);
      fclose(f);
   }
   if (getenv("ZZGL_FRAMES") && frames % 50 == 0)
      fprintf(stderr, "zzgl_direct: %d frames\n", frames);
   if (getenv("ZZGL_STOP") && frames >= atoi(getenv("ZZGL_STOP")))
      exit(0);
}

void zzgl_platform_frame(void) { }
void zzgl_platform_place(int x, int y) { (void)x; (void)y; }

void
zzgl_platform_close(void)
{
   if (ctx)
      OSMesaDestroyContext(ctx);
   ctx = NULL;
}

/* no board here: the program's sound goes elsewhere */
int zzgl_sound_open(int div) { (void)div; return -1; }
unsigned long zzgl_sound_pos(void) { return 0; }
void zzgl_sound_write(unsigned long at, const short *frames, unsigned long n) { (void)at; (void)frames; (void)n; }
void zzgl_sound_close(void) { }
