/*
 * zzgl_dec.c -- core 1's side of zzgl: commands from the ring into Mesa
 * (an OSMesa context), frames out through the platform's present().  The
 * same code runs in the PC test (zzgl_host.c).
 *
 * Each command is copied out of the ring (uncached on core 1) into cached
 * memory before it is looked at; words are big-endian and swapped here.
 */
#define GL_GLEXT_PROTOTYPES
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/osmesa.h>
#include "zzgl.h"
#include "zzgl_dec.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define be32(v) __builtin_bswap32(v)
#define be16(v) __builtin_bswap16(v)
#define be64(v) __builtin_bswap64(v)
#define SWAP 1
#else
#define be32(v) (v)
#define be16(v) (v)
#define be64(v) (v)
#define SWAP 0
#endif

static const struct zzgl_dec_env *E;
static OSMesaContext ctx;
static uint8_t *fb;
static int fb_w, fb_h;
/* the zz3660 executor: softpipe */
void zzp_exec(uint32_t *words, unsigned n, void *reply);
void zz3660_exec_start(void);
void zzp_present(const void *rgba, int w, int h, int again);
void zz3660_idle(void);
void zz3660_forget(void);

static uint32_t *buf;           /* the command being decoded */
static size_t buf_cap;
static int client_unit;
static unsigned ticks;

static inline uint32_t blk_get(int w) { return be32(E->blk[w]); }
static inline void blk_put(int w, uint32_t v) { E->blk[w] = be32(v); }

#define W(k) be32(buf[k])

static inline float
F(int k)
{
   union { uint32_t u; float f; } x;

   x.u = W(k);
   return x.f;
}

static inline double
D(int k)
{
   union { uint64_t u; double d; } x;

   x.u = (uint64_t)W(k) << 32 | W(k + 1);
   return x.d;
}

/* n elements of b bytes each, in place, from big-endian */
static void
swap_elems(void *p, size_t n, unsigned b)
{
#if SWAP
   size_t k;

   if (b == 2)
      for (k = 0; k < n; k++)
         ((uint16_t *)p)[k] = be16(((uint16_t *)p)[k]);
   else if (b == 4)
      for (k = 0; k < n; k++)
         ((uint32_t *)p)[k] = be32(((uint32_t *)p)[k]);
   else if (b == 8)
      for (k = 0; k < n; k++)
         ((uint64_t *)p)[k] = be64(((uint64_t *)p)[k]);
#else
   (void)p; (void)n; (void)b;
#endif
}

static unsigned
type_bytes(GLenum type)
{
   switch (type) {
   case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
   case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
   case GL_DOUBLE: return 8;
   default: return 4;
   }
}

/* the element size a pixel type swaps by */
static unsigned
pixel_swap_bytes(GLenum type)
{
   switch (type) {
   case GL_UNSIGNED_BYTE: case GL_BYTE: case GL_UNSIGNED_BYTE_3_3_2: case GL_UNSIGNED_BYTE_2_3_3_REV:
      return 1;
   case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_UNSIGNED_SHORT_5_6_5: case GL_UNSIGNED_SHORT_4_4_4_4:
   case GL_UNSIGNED_SHORT_5_5_5_1: case GL_UNSIGNED_SHORT_4_4_4_4_REV: case GL_UNSIGNED_SHORT_1_5_5_5_REV:
   case GL_UNSIGNED_SHORT_5_6_5_REV:
      return 2;
   default:
      return 4;
   }
}

static void
open_ctx(int w, int h, int depth, int stencil)
{
   if (ctx)
      OSMesaDestroyContext(ctx);
   free(fb);
   ctx = OSMesaCreateContextExt(OSMESA_RGBA, depth, stencil, 0, NULL);
   fb = calloc((size_t)w * h, 4);
   fb_w = w;
   fb_h = h;
   client_unit = 0;
   if (!ctx || !fb || !OSMesaMakeCurrent(ctx, fb, GL_UNSIGNED_BYTE, w, h))
      blk_put(ZB_ERROR, ZZ_OPEN);
}

static void
do_sync(uint32_t n)
{
   const uint32_t seq = W(0), what = n >= 8 ? W(1) : ZQ_NONE;
   volatile uint32_t *r = E->blk + ZB_REPLY;
   int k;

   switch (what) {
   case ZQ_NONE:
      glFinish();
      break;
   case ZQ_ERROR:
      r[0] = be32(glGetError());
      break;
   case ZQ_STRING: {
      /* into the reply area (the extension list is several KB) */
      const char *s = (const char *)glGetString(W(2));
      uint32_t len = s ? (uint32_t)strlen(s) : 0;

      if (len > ZZGL_REPLY_SIZE - 1)
         len = ZZGL_REPLY_SIZE - 1;
      if (len)
         E->copy_out(E->reply, s, (len + 3) & ~3u);
      r[0] = be32(len);
      break;
   }
   case ZQ_INTEGERV: case ZQ_BOOLEANV: case ZQ_FLOATV: {
      /* up to 16 values: those Mesa wrote (the rest keep a mark) */
      union { GLint i[16]; GLfloat f[16]; uint32_t u[16]; } v;
      GLboolean b[16];
      int n = 0;

      for (k = 0; k < 16; k++)
         v.u[k] = 0x7fa5a5a5u, b[k] = 0xa5;
      if (what == ZQ_INTEGERV) {
         glGetIntegerv(W(2), v.i);
         for (n = 16; n > 1 && v.u[n - 1] == 0x7fa5a5a5u; n--)
            ;
      } else if (what == ZQ_FLOATV) {
         glGetFloatv(W(2), v.f);
         for (n = 16; n > 1 && v.u[n - 1] == 0x7fa5a5a5u; n--)
            ;
      } else {
         glGetBooleanv(W(2), b);
         for (n = 16; n > 1 && b[n - 1] == 0xa5; n--)
            ;
         for (k = 0; k < n; k++)
            v.u[k] = b[k];
      }
      r[0] = be32((uint32_t)n);
      for (k = 0; k < n; k++)
         r[1 + k] = be32(v.u[k]);
      break;
   }
   case ZQ_ISENABLED:
      r[0] = be32(glIsEnabled(W(2)));
      break;
   case ZQ_READPIXELS: {
      const GLsizei w = (GLsizei)W(4), h = (GLsizei)W(5);
      const GLenum format = W(6), type = W(7);
      size_t px = (format == GL_RGBA || format == GL_BGRA ? 4 : format == GL_RGB || format == GL_BGR ? 3 :
                   format == GL_LUMINANCE_ALPHA ? 2 : 1) * type_bytes(type);
      size_t n = (size_t)w * h * px;
      GLint align;

      if (n > ZZGL_REPLY_SIZE || w <= 0 || h <= 0) {
         r[0] = 0;
         break;
      }
      {
         uint8_t *tmp = malloc(n);

         if (!tmp) {
            r[0] = 0;
            break;
         }
         glGetIntegerv(GL_PACK_ALIGNMENT, &align);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels((GLint)W(2), (GLint)W(3), w, h, format, type, tmp);
         glPixelStorei(GL_PACK_ALIGNMENT, align);
         swap_elems(tmp, n / pixel_swap_bytes(type), pixel_swap_bytes(type));
         E->copy_out(E->reply, tmp, n);
         free(tmp);
      }
      r[0] = be32((uint32_t)n);
      break;
   }
   }
   blk_put(ZB_SYNC_ACK, seq);
}

/* a texture upload: pixels tightly packed, swapped by type */
static void
tex_image(int sub, uint32_t n)
{
   const uint32_t size = W(8);
   void *data = NULL;
   GLint align;

   if (size && size != 0xffffffffu && 36 + size <= n) {
      data = buf + 9;
      swap_elems(data, size / pixel_swap_bytes(W(7)), pixel_swap_bytes(W(7)));
   }
   glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   if (sub)
      glTexSubImage2D(W(0), (GLint)W(1), (GLint)W(2), (GLint)W(3), (GLsizei)W(4), (GLsizei)W(5),
                      W(6), W(7), data);
   else
      glTexImage2D(W(0), (GLint)W(1), (GLint)W(2), (GLsizei)W(3), (GLsizei)W(4), (GLint)W(5),
                   W(6), W(7), data);
   glPixelStorei(GL_UNPACK_ALIGNMENT, align);
}

/* ZZ_DRAW: the arrays (swapped by type), then glDrawArrays/glDrawElements */
static void
draw(uint32_t n)
{
   const GLenum mode = W(0);
   const GLsizei count = (GLsizei)W(1);
   const uint32_t flags = W(3);
   uint32_t k, pos = 4, dpos;
   uint8_t *data;

   /* the descriptors, then the data in the same order */
   dpos = 4;
   for (k = 0; k < ZA_N; k++)
      if (flags & (1u << k))
         dpos += 4;
   data = (uint8_t *)(buf + dpos);
   for (k = 0; k < ZA_N; k++) {
      GLint size;
      GLenum type;
      uint32_t elems, bytes;

      if (!(flags & (1u << k)))
         continue;
      size = (GLint)W(pos);
      type = W(pos + 1);
      elems = W(pos + 2);
      bytes = W(pos + 3);
      pos += 4;
      if ((uint32_t)(data - (uint8_t *)buf) + bytes > n)
         return;
      swap_elems(data, (size_t)elems * size, type_bytes(type));
      switch (k) {
      case ZA_VERTEX: glVertexPointer(size, type, 0, data); break;
      case ZA_COLOR: glColorPointer(size, type, 0, data); break;
      case ZA_NORMAL: glNormalPointer(type, 0, data); break;
      default:
         glClientActiveTextureARB(k == ZA_TEX0 ? GL_TEXTURE0_ARB : GL_TEXTURE1_ARB);
         glTexCoordPointer(size, type, 0, data);
         glClientActiveTextureARB(client_unit ? GL_TEXTURE1_ARB : GL_TEXTURE0_ARB);
      }
      data += bytes;
   }
   if (flags & ZD_ELEMENTS) {
      if ((uint32_t)(data - (uint8_t *)buf) + count * 4 > n)
         return;
      swap_elems(data, count, 4);
      glDrawElements(mode, count, GL_UNSIGNED_INT, data);
   } else {
      glDrawArrays(mode, 0, count);
   }
}

static void
four_floats(float f[4], int k)
{
   int i;

   for (i = 0; i < 4; i++)
      f[i] = F(k + i);
}

#ifdef ZZGL_TRACE
#include <stdio.h>
/* ZZGL_TRACE=frame: the commands of that frame on stderr (PC test) */
static void
trace(uint32_t op, uint32_t n)
{
   static int frame = -1, at;
   uint32_t k;

   if (frame < 0)
      frame = getenv("ZZGL_TRACE") ? atoi(getenv("ZZGL_TRACE")) : 0;
   if (op == ZZ_SWAP)
      at++;
   if (!frame || at != frame)
      return;
   fprintf(stderr, "op %3u n %5u:", op, n);
   for (k = 0; k < n / 4 && k < 8; k++)
      fprintf(stderr, " %08x", W(k));
   fprintf(stderr, "\n");
}
#else
#define trace(op, n) ((void)0)
#endif

/* one command (op, n payload bytes in buf): 0, or 1 at ZZ_CLOSE */
static int
command(uint32_t op, uint32_t n)
{
   float f[16];
   int k;

   trace(op, n);
   switch (op) {
   case ZZ_OPEN:
      zz3660_forget();
      blk_put(ZB_OVL_ROW, 0);
      open_ctx((int)W(0), (int)W(1), (int)W(2), (int)W(3));
      break;
   case ZZ_CLOSE:
      return 1;
   case ZZ_SYNC:
      do_sync(n);
      break;
   case ZZ_SWAP:
      glFinish();
      E->present(fb, fb_w, fb_h);
      blk_put(ZB_FRAMES, blk_get(ZB_FRAMES) + 1);
      break;
   case ZZ_EFFECT:
      if (E->effect) {
         E->effect((uint32_t *)fb, fb_w, fb_h, F(0), F(1));
         E->present(fb, fb_w, fb_h);
         if (E->tick)
            E->tick();          /* one command is a whole frame: the sound can't wait for four */
      }
      blk_put(ZB_FRAMES, blk_get(ZB_FRAMES) + 1);
      break;
   case ZZ_PIPE:
      zzp_exec(buf, n / 4, E->reply);
      break;
   case ZZ_OVERLAY:
      if (E->overlay)
         E->overlay(W(0), W(1), W(2), W(3), (int)W(4));
      break;
   case ZZ_LoadMatrixf: case ZZ_MultMatrixf:
      for (k = 0; k < 16; k++)
         f[k] = F(k);
      if (op == ZZ_LoadMatrixf)
         glLoadMatrixf(f);
      else
         glMultMatrixf(f);
      break;
   case ZZ_ClipPlane: {
      const GLdouble eq[4] = { D(1), D(3), D(5), D(7) };

      glClipPlane(W(0), eq);
      break;
   }
   case ZZ_Fogfv:
      four_floats(f, 1);
      glFogfv(W(0), f);
      break;
   case ZZ_TexEnvfv:
      four_floats(f, 2);
      glTexEnvfv(W(0), W(1), f);
      break;
   case ZZ_Lightfv:
      four_floats(f, 2);
      glLightfv(W(0), W(1), f);
      break;
   case ZZ_Materialfv:
      four_floats(f, 2);
      glMaterialfv(W(0), W(1), f);
      break;
   case ZZ_DeleteTextures:
      if (4 + W(0) * 4 <= n) {
         swap_elems(buf + 1, W(0), 4);
         glDeleteTextures((GLsizei)be32(buf[0]), (const GLuint *)(buf + 1));
      }
      break;
   case ZZ_TexImage2D:
      tex_image(0, n);
      break;
   case ZZ_TexSubImage2D:
      tex_image(1, n);
      break;
   case ZZ_DRAW:
      draw(n);
      break;
   case ZZ_ClientActiveTextureARB:
      client_unit = W(0) == GL_TEXTURE1_ARB;
      glClientActiveTextureARB(W(0));
      break;
#include "gen/zzgl_gen_dec.h"
   default:
      blk_put(ZB_ERROR, op);
   }
   return 0;
}

void
zzp_present(const void *rgba, int w, int h, int again)
{
   E->present(rgba, w, h);
   if (!again)
      blk_put(ZB_FRAMES, blk_get(ZB_FRAMES) + 1);
}

/* the commands in the ring; once: until it is empty, else until ZZ_CLOSE */
int
zzgl_dec_run(const struct zzgl_dec_env *env, int once)
{
   uint32_t rd;

   E = env;
   zz3660_exec_start();
   rd = blk_get(ZB_RD);
   (void)ticks;
   for (;;) {
      const uint32_t wr = blk_get(ZB_WR);
      uint32_t head[2], op, n;

      if (rd == wr) {
         if (once)
            return 0;
         if (env->idle)
            env->idle();
         zz3660_idle();
         continue;
      }
      if (rd + 8 > ZZGL_RING_SIZE) {         /* the very end: wrap */
         rd = 0;
         blk_put(ZB_RD, rd);
         continue;
      }
      E->copy_in(head, E->ring + rd, 8);
      op = be32(head[0]);
      n = be32(head[1]);
      if (op == ZZ_WRAP) {
         rd = 0;
         blk_put(ZB_RD, rd);
         continue;
      }
      if (n > ZZGL_RING_SIZE - rd - 8) {
         blk_put(ZB_ERROR, 0xbad00000u | op);
         return -1;
      }
      if (n > buf_cap) {
         free(buf);
         buf_cap = n + 65536;
         buf = malloc(buf_cap);
         if (!buf) {
            buf_cap = 0;
            blk_put(ZB_ERROR, 0xbad10000u | op);
            return -1;
         }
      }
      if (n)
         E->copy_in(buf, E->ring + rd + 8, n);
      rd = (rd + ((8 + n + 7) & ~7u)) % ZZGL_RING_SIZE;
      blk_put(ZB_MARK, op);
      if (command(op, n)) {
         blk_put(ZB_RD, rd);
         return 1;
      }
      blk_put(ZB_RD, rd);
      if (env->tick && (++ticks & 3) == 0)
         env->tick();
   }
}

/* a fresh server: the block's counters to 0, then the mark */
void
zzgl_dec_init(const struct zzgl_dec_env *env)
{
   int k;

   E = env;
   for (k = 1; k < ZB_REPLY; k++)
      env->blk[k] = 0;
   blk_put(ZB_MAGIC, ZZGL_MAGIC);
}

void
zzgl_dec_shutdown(void)
{
   if (E)
      E->blk[ZB_MAGIC] = 0;
   if (ctx)
      OSMesaDestroyContext(ctx);
   ctx = NULL;
   free(fb);
   fb = NULL;
}
