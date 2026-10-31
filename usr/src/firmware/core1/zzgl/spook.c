/*
 * spook.c -- the Halloween picture, computed pixel by pixel on core 1: what a
 * fragment shader would do, in native code (softpipe interprets a shader at
 * thousands of cycles a pixel; this takes tens).  320x240 with a pumpkin that
 * turns: a sphere looked up per pixel with its ribs and carved face in a
 * texture, fire behind the face, fog from two scrolling noise layers, moon,
 * graveyard, bats, lightning and a scroller.  ZZ_EFFECT calls it, with the
 * time from the 060; the frame is RGBA, bottom row first, as OSMesa's.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "spook_font.h"

#define SW 320
#define SH 240
#define PI 3.14159265f

#define PCX 160                 /* the pumpkin */
#define PCY 150
#define PRX 66
#define PRY 54

#define TEXT_H 20
#define TEXT_ROW 214

static const char message[] =
   "*** HAPPY HALLOWEEN FROM AN AMIGA 3000 UNDER AMIX *** MUSIC HALLOWEEN BASS BY DILIK *** "
   "EVERY PIXEL COMPUTED ON THE SECOND ARM CORE OF THE Z3660 *** TRICK OR TREAT ***       ";

static uint32_t sky[SW * SH];           /* r | g << 8 | b << 16 */
static uint8_t land[SW * SH];           /* 1: graveyard */
static uint8_t halo[SW * SH];
static uint8_t fogy[SH];
static uint8_t noise[256 * 256];
static uint32_t ptex[128][256];         /* the pumpkin's skin; top byte 1: carved */
static struct { uint8_t u, v, shade, on; } sphere[PRY * 2][PRX * 2];
static uint8_t sintab[256];             /* 0..255 */
static uint8_t text[TEXT_H][1500];      /* the message, drawn once */
static int text_w;
static uint32_t pic[SW * SH];
static int ready;

static inline float
clampf(float v, float a, float b)
{
   return v < a ? a : v > b ? b : v;
}

static inline uint32_t
rgb(int r, int g, int b)
{
   r = r > 255 ? 255 : r;
   g = g > 255 ? 255 : g;
   b = b > 255 ? 255 : b;
   return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16;
}

static uint32_t
hash(uint32_t v)
{
   v ^= v << 13;
   v ^= v >> 17;
   v ^= v << 5;
   return v * 2654435761u;
}

/* smooth noise, repeating every 256: a few octaves of random lattices */
static void
make_noise(void)
{
   static uint8_t base[256 * 256];
   int o, x, y;

   for (x = 0; x < 256 * 256; x++)
      base[x] = (uint8_t)(hash((uint32_t)x + 1) >> 24);
   memset(noise, 0, sizeof(noise));
   for (o = 2; o <= 5; o++) {
      const int cell = 1 << (8 - o), m = (1 << o) - 1, amp = 128 >> (o - 2);

      for (y = 0; y < 256; y++)
         for (x = 0; x < 256; x++) {
            const int cx = x / cell, cy = y / cell, fx = (x % cell) * 256 / cell, fy = (y % cell) * 256 / cell;
            const int a = base[(cy & m) << 8 | (cx & m)], b = base[(cy & m) << 8 | ((cx + 1) & m)];
            const int c = base[((cy + 1) & m) << 8 | (cx & m)], d = base[((cy + 1) & m) << 8 | ((cx + 1) & m)];
            const int top = a + ((b - a) * fx >> 8), bot = c + ((d - c) * fx >> 8);
            const int v = noise[y << 8 | x] + ((top + ((bot - top) * fy >> 8)) * amp >> 8);

            noise[y << 8 | x] = (uint8_t)(v > 255 ? 255 : v);
         }
   }
}

static void
land_line(int x0, int y0, int x1, int y1, int w)
{
   const int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
   int i, a, b;

   for (i = 0; i <= n; i++) {
      const int x = x0 + (n ? (x1 - x0) * i / n : 0), y = y0 + (n ? (y1 - y0) * i / n : 0);

      for (b = -w; b <= w; b++)
         for (a = -w; a <= w; a++)
            if (x + a >= 0 && x + a < SW && y + b >= 0 && y + b < SH)
               land[(y + b) * SW + x + a] = 1;
   }
}

/* a bare tree: each branch splits in two */
static void
tree(int x, int y, float angle, float len, int w, uint32_t seed)
{
   const int x1 = x + (int)(cosf(angle) * len), y1 = y - (int)(sinf(angle) * len);

   land_line(x, y, x1, y1, w);
   if (len < 5)
      return;
   tree(x1, y1, angle + 0.25f + (hash(seed) >> 24) * 0.006f, len * 0.72f, w > 0 ? w - 1 : 0, seed * 3 + 1);
   tree(x1, y1, angle - 0.25f - (hash(seed + 7) >> 24) * 0.006f, len * 0.68f, w > 0 ? w - 1 : 0, seed * 5 + 2);
}

static void
tomb(int x, int y, int w, int h, int cross)
{
   int i, j;

   for (j = 0; j < h; j++) {
      const int e = j < w / 2 ? (int)sqrtf((float)(w * w / 4 - (w / 2 - j) * (w / 2 - j))) : w / 2;

      for (i = -e; i <= e; i++)
         land[(y - h + j) * SW + x + i] = 1;
   }
   if (cross) {
      land_line(x, y - h - 9, x, y - h, 1);
      land_line(x - 4, y - h - 6, x + 4, y - h - 6, 1);
   }
}

static void
make_scene(void)
{
   static const short tombs[][4] = {
      { 26, 206, 18, 26 }, { 62, 204, 14, 20 }, { 262, 205, 20, 30 }, { 298, 207, 14, 18 }, { 232, 208, 12, 16 }
   };
   int x, y, i;

   for (y = 0; y < SH; y++) {
      const float k = (float)y / SH;

      for (x = 0; x < SW; x++) {
         const float dx = (float)(x - 244), dy = (float)(y - 52), d = sqrtf(dx * dx + dy * dy);
         const float n = noise[((y * 2) & 255) << 8 | ((x * 2) & 255)] / 255.0f;
         float r = 10 + 34 * k * k, g = 4 + 10 * k, b = 34 + 44 * k;

         /* the moon, its halo and a little cloud across it */
         if (d < 24) {
            const float c = 0.82f + 0.18f * n;

            r = 245 * c;
            g = 232 * c;
            b = 190 * c;
         } else {
            const float h = 16 / (d - 12);

            r += 150 * h * h;
            g += 140 * h * h;
            b += 120 * h * h;
         }
         /* stars */
         if (y < 150 && (hash((uint32_t)(y * SW + x) + 77) >> 24) == 3 && d > 30)
            r = g = b = 120 + (hash((uint32_t)x) >> 26);
         sky[y * SW + x] = rgb((int)clampf(r, 0, 255), (int)clampf(g, 0, 255), (int)clampf(b, 0, 255));
         {
            const float hx = (x - PCX) / 150.0f, hy = (y - PCY) / 110.0f, h = 1 - sqrtf(hx * hx + hy * hy);

            halo[y * SW + x] = (uint8_t)(h > 0 ? h * h * 255 : 0);
         }
      }
      fogy[y] = (uint8_t)clampf((y - 110) * 2.6f, 0, 240);
   }
   /* the ground */
   for (x = 0; x < SW; x++) {
      const int top = 200 + (int)(4 * sinf(x * 0.03f) + 2 * sinf(x * 0.11f));

      for (y = top; y < SH; y++)
         land[y * SW + x] = 1;
   }
   for (i = 0; i < 5; i++)
      tomb(tombs[i][0], tombs[i][1], tombs[i][2], tombs[i][3], i == 2);
   tree(0 + 10, 205, 1.35f, 48, 3, 11);
   tree(308, 206, 1.75f, 34, 2, 5);
}

static void
make_pumpkin(void)
{
   int x, y;

   for (y = 0; y < 128; y++) {
      const float v = (y + 0.5f) / 128;

      for (x = 0; x < 256; x++) {
         const float a = 2 * PI * (x - 128) / 256, ax = (x - 128) / 256.0f;
         const float rib = fabsf(cosf(a * 5));
         const float dark = 0.42f + 0.58f * (float)sqrt(rib);
         float n = noise[(y * 3 & 255) << 8 | (x * 2 & 255)] / 255.0f;
         uint32_t c = rgb((int)(250 * dark * (0.85f + 0.3f * n)), (int)(112 * dark * (0.8f + 0.3f * n)),
                          (int)(14 * dark));
         int carved = 0;

         /* the eyes, triangles */
         {
            const float ex = fabsf(ax) - 0.085f, ey = v - 0.40f;

            if (ey < 0.08f && ey > -0.08f && fabsf(ex) < (ey + 0.08f) * 0.42f)
               carved = 1;
         }
         /* the nose */
         if (v > 0.47f && v < 0.54f && fabsf(ax) < (v - 0.47f) * 0.55f)
            carved = 1;
         /* the mouth, with teeth */
         {
            const float m = 0.665f - 3.2f * ax * ax, h = 0.075f - 2.0f * ax * ax;

            if (fabsf(ax) < 0.17f && v > m - h && v < m + h) {
               const int tooth = (int)((ax + 1) * 30) % 2;

               if (!(tooth && v < m - h * 0.25f) && !(!tooth && v > m + h * 0.3f && fabsf(ax) < 0.13f))
                  carved = 1;
            }
         }
         ptex[y][x] = c | (uint32_t)carved << 24;
      }
   }
   for (y = 0; y < PRY * 2; y++)
      for (x = 0; x < PRX * 2; x++) {
         const float nx = (x - PRX + 0.5f) / PRX, ny = (y - PRY + 0.5f) / PRY, r2 = nx * nx + ny * ny;

         if (r2 < 1) {
            /* a little flat on top and bottom, dented at the poles */
            const float nz = sqrtf(1 - r2);
            const float u = atan2f(nx, nz) / (2 * PI), v = 0.5f + asinf(clampf(ny, -1, 1)) / PI;
            const float l = clampf(-0.42f * nx - 0.58f * ny + 0.70f * nz, 0, 1);

            sphere[y][x].u = (uint8_t)((int)(128 + u * 256) & 255);
            sphere[y][x].v = (uint8_t)clampf(v * 128, 0, 127);
            sphere[y][x].shade = (uint8_t)clampf((0.22f + 0.95f * l) * 200, 0, 255);
            sphere[y][x].on = 1;
         }
      }
}

static void
make_text(void)
{
   const char *p;
   int x = 0;

   for (p = message; *p && x < (int)sizeof(text[0]) - 40; p++) {
      const int c = *p - 32;
      int i, wd = 0;

      if (c < 0 || c > 58)
         continue;
      if (*p == ' ') {
         x += 12;
         continue;
      }
      for (i = letter_first[c]; i < letter_first[c + 1]; i++) {
         const int x0 = letter_box[i][0], x1 = letter_box[i][2], y0 = letter_box[i][1], y1 = letter_box[i][3];
         int a, b;

         for (b = y0; b < y1; b++)
            for (a = x0; a < x1; a++) {
               const int row = TEXT_H / 2 - 1 - b;

               if (row >= 0 && row < TEXT_H) {
                  text[row][x + 16 + a] = 1;
                  if (a + 16 > wd)
                     wd = a + 16;
               }
            }
      }
      x += wd + 3;
   }
   text_w = x;
}

static void
init(void)
{
   int i;

   for (i = 0; i < 256; i++)
      sintab[i] = (uint8_t)(127.5f + 127.5f * sinf(2 * PI * i / 256));
   make_noise();
   make_scene();
   make_pumpkin();
   make_text();
   ready = 1;
}

/* sky, moon, the pumpkin's glow, fog and lightning */
static void
back(int t1, int t2, int flick, int flash)
{
   int x, y;

   for (y = 0; y < SH; y++) {
      const uint32_t *s = &sky[y * SW];
      const uint8_t *h = &halo[y * SW];
      uint32_t *o = &pic[y * SW];
      const int fy = fogy[y], r1 = ((y + t2) & 255) << 8, r2 = ((y * 2 - t2) & 255) << 8;

      for (x = 0; x < SW; x++) {
         const uint32_t c = s[x];
         const int n = (noise[r1 | ((x + t1) & 255)] + noise[r2 | ((x * 2 - t1 / 2) & 255)]) >> 1;
         const int fog = n > 96 ? (n - 96) * fy >> 7 : 0;
         const int g = h[x] * flick >> 8;
         const int r = (int)(c & 255) + (g * 3 >> 3) + (fog * 62 >> 8) + flash;
         const int gr = (int)(c >> 8 & 255) + (g >> 3) + (fog * 66 >> 8) + flash;
         const int b = (int)(c >> 16 & 255) + (fog * 86 >> 8) + flash + (flash >> 1);

         o[x] = rgb(r, gr, b);
      }
   }
}

/* the graveyard: black, with the moon's light along what the fog leaves */
static void
graveyard(int flash)
{
   int i;

   for (i = 0; i < SW * SH; i++)
      if (land[i]) {
         const uint32_t c = pic[i];

         pic[i] = rgb((int)(c & 255) >> 3, (int)(c >> 8 & 255) >> 3, ((int)(c >> 16 & 255) >> 3) + (flash >> 2));
      }
}

/* per pixel: where on the sphere, the skin there, lit; carved: the fire */
static void
pumpkin(int turn, int bob, int flick, int t)
{
   int x, y;

   for (y = 0; y < PRY * 2; y++) {
      const int py = PCY - PRY + y + bob;
      uint32_t *o = &pic[py * SW + PCX - PRX];

      if (py < 0 || py >= SH)
         continue;
      for (x = 0; x < PRX * 2; x++) {
         const int px = (y * 7 + x * 3 + t) & 255;

         if (sphere[y][x].on) {
            const uint32_t c = ptex[sphere[y][x].v][(sphere[y][x].u + turn) & 255];
            const int sh = sphere[y][x].shade;

            if (c >> 24) {
               /* fire: from yellow at the bottom of the hole to deep orange */
               const int n = noise[(((y * 2) + t * 3) & 255) << 8 | ((x * 2) & 255)];
               const int f = (n + flick) >> 1;

               o[x] = rgb(255, 130 + (f >> 1), f > 180 ? (f - 180) * 2 : 8);
            } else {
               /* the light from inside shines warm through the skin */
               const int w = sh + (flick >> 3) + (noise[px << 8 | px] >> 5);

               o[x] = rgb((int)(c & 255) * w >> 8, (int)(c >> 8 & 255) * w >> 8, (int)(c >> 16 & 255) * w >> 8);
            }
         }
      }
   }
}

static void
stem(int turn, int bob)
{
   const int top = PCY - PRY + bob;
   int y, x;

   for (y = 0; y < 15; y++) {
      const int cx = PCX + (sintab[(uint8_t)(turn + 64)] - 127) / 42 + y / 3, w = 9 - y / 3;

      for (x = -w; x <= w; x++) {
         const int py = top - y + 5;

         if (py >= 0)
            pic[py * SW + cx + x] = rgb(38 + (x & 3) * 7 + y, 74 + (x & 3) * 8 + y, 24);
      }
   }
}

static void
put(int x, int y, uint32_t c)
{
   if (x >= 0 && x < SW && y >= 0 && y < SH)
      pic[y * SW + x] = c;
}

static void
bats(float time)
{
   static const short start[6][3] = {
      { 40, 40, 90 }, { 120, 70, 130 }, { 200, 34, 70 }, { 280, 62, 110 }, { 90, 100, 150 }, { 230, 108, 120 }
   };
   int i, k;

   for (i = 0; i < 6; i++) {
      const float s = time * start[i][2] * 0.13f + i * 57;
      const int cx = (int)fmodf(start[i][0] + s, SW + 40.0f) - 20;
      const int cy = start[i][1] + (int)(9 * sinf(time * 1.7f + i * 2.1f));
      const int flap = (int)(4 * sinf(time * 16 + i));

      for (k = 0; k < 9; k++) {
         const int rise = flap * k / 8 - (k * k >> 3);

         put(cx - k, cy + rise, 0);
         put(cx + k, cy + rise, 0);
         put(cx - k, cy + rise + 1, 0);
         put(cx + k, cy + rise + 1, 0);
      }
      for (k = -1; k <= 1; k++) {
         put(cx + k, cy, 0);
         put(cx + k, cy + 1, 0);
         put(cx + k, cy + 2, 0);
      }
      put(cx - 1, cy - 1, 0);
      put(cx + 1, cy - 1, 0);
   }
}

static void
lightning(uint32_t seed)
{
   int x = 40 + (int)(hash(seed) >> 24), y, k;

   for (y = 0; y < 160; y += 6) {
      const int nx = x + (int)(hash(seed + y) >> 28) - 8;

      for (k = 0; k < 6; k++) {
         const int px = x + (nx - x) * k / 6;

         put(px, y + k, rgb(235, 235, 255));
         put(px + 1, y + k, rgb(170, 170, 230));
      }
      x = nx;
   }
}

static void
scroller(float time)
{
   const int off = (int)(time * 70), start = text_w ? off % (text_w + SW) : 0;
   int x, y;

   for (x = 0; x < SW; x++) {
      const int tx = x + start - SW;

      if (tx < 0 || tx >= text_w)
         continue;
      {
         const int wave = (sintab[(uint8_t)(x * 2 + (int)(time * 90))] - 127) >> 5;
         const int drip = ((int)(hash((uint32_t)(tx / 3)) >> 28) > 12) ? (int)(hash((uint32_t)(tx / 3) + 3) >> 29) : 0;

         for (y = 0; y < TEXT_H; y++)
            if (text[y][tx]) {
               const int py = TEXT_ROW + y + wave, shade = 255 - y * 8;
               int d;

               put(x, py, rgb(shade, y * 2, y));
               if (y + 1 == TEXT_H || !text[y + 1][tx])
                  for (d = 1; d <= drip; d++)
                     put(x, py + d, rgb(150, 0, 0));
            }
      }
   }
}

/* one frame of width w, height h (RGBA, bottom row first) at time seconds,
   inbeat of the way through the music's beat */
void
spook_frame(uint32_t *fb, int w, int h, float time, float inbeat)
{
   const int step = (int)(time * 40);
   const int flick = 150 + (noise[(step & 255) << 8 | (step * 3 & 255)] >> 1) + (int)(50 * (1 - inbeat));
   const uint32_t strike = (uint32_t)(time / 7);
   const float since = time - strike * 7.0f - (hash(strike + 1) >> 28) * 0.3f;
   const int bolt = since > 0 && since < 0.35f;
   const int flash = !bolt ? 0 : since < 0.08f ? 130 : since > 0.15f && since < 0.25f ? 90 : 25;
   const int turn = (int)(46 * sinf(time * 0.8f)), bob = (int)(3 * sinf(time * 2.2f));
   const int scale = w >= SW * 2 && h >= SH * 2 ? 2 : 1;
   const int ox = (w - SW * scale) / 2, oy = (h - SH * scale) / 2;
   int x, y;

   if (!ready)
      init();
   back(step, step / 2, flick, flash);
   if (bolt)
      lightning(strike);
   graveyard(flash);
   pumpkin(turn, bob, flick, step);
   stem(turn, bob);
   bats(time);
   scroller(time);

   for (y = 0; y < SH; y++) {
      const uint32_t *s = &pic[y * SW];
      int r;

      for (r = 0; r < scale; r++) {
         const int row = h - 1 - (oy + y * scale + r);
         uint32_t *o;

         if (row < 0 || row >= h)
            continue;
         o = fb + (size_t)row * w + ox;
         if (scale == 2) {
            for (x = 0; x < SW; x++) {
               const uint32_t c = s[x] | 0xff000000u;

               o[0] = c;
               o[1] = c;
               o += 2;
            }
         } else {
            for (x = 0; x < SW; x++)
               o[x] = s[x] | 0xff000000u;
         }
      }
   }
}
