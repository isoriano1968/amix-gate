/*
 * zzgl_enc.c -- the GL functions of the 060's side: each call becomes a
 * command for core 1 (see zzgl.h).  Commands collect in a local buffer
 * (cached RAM) and go to the ring in blocks: at a swap, when the buffer is
 * full, or before a call that waits for a reply.
 *
 * The functions are reached by name (zzgl_GetProcAddress), as ioquake3's
 * renderer finds them; they are not exported as gl* symbols, so a test
 * program on the PC can hold both ends and the host's Mesa.
 *
 * Kept on this side: the client arrays (pointers, enables, the client
 * texture unit: their data goes with each draw), the unpack pixel store
 * (texture data goes tightly packed), texture names (handed out here: GL
 * 1.x takes any name in glBindTexture) and cached strings.
 */
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include "zzgl.h"
#include "zzgl_enc.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define be32(v) __builtin_bswap32(v)
#else
#define be32(v) (v)
#endif

static const struct zzgl_transport *T;
static uint32_t stage_words[ZZGL_STAGE / 4];
static uint8_t *const stage = (uint8_t *)stage_words;
static size_t stage_n;
static uint32_t wr;                     /* ring position of the next block */
static uint32_t sync_seq;
static int open_w, open_h;

/* n elements of b bytes each to big-endian in place (only on a little-endian
   encoder: the PC test; the 060's memory is big-endian already) */
static void
to_be(void *p, size_t n, unsigned b)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
   size_t k;

   if (b == 2)
      for (k = 0; k < n; k++)
         ((uint16_t *)p)[k] = __builtin_bswap16(((uint16_t *)p)[k]);
   else if (b == 4)
      for (k = 0; k < n; k++)
         ((uint32_t *)p)[k] = __builtin_bswap32(((uint32_t *)p)[k]);
   else if (b == 8)
      for (k = 0; k < n; k++)
         ((uint64_t *)p)[k] = __builtin_bswap64(((uint64_t *)p)[k]);
#else
   (void)p; (void)n; (void)b;
#endif
}

static inline uint32_t
bef(float f)
{
   union { float f; uint32_t u; } x;

   x.f = f;
   return be32(x.u);
}

static inline void
put_double(uint32_t *p, double d)
{
   union { double d; uint64_t u; } x;

   x.d = d;
   p[0] = be32((uint32_t)(x.u >> 32));
   p[1] = be32((uint32_t)x.u);
}

static inline uint32_t
blk_get(int w)
{
   return be32(T->blk[w]);
}

static inline void
blk_put(int w, uint32_t v)
{
   T->blk[w] = be32(v);
}

/* ring bytes free for writing at wr (one 8-byte gap tells full from empty) */
static uint32_t
ring_free(void)
{
   const uint32_t rd = blk_get(ZB_RD);

   return (rd + ZZGL_RING_SIZE - wr - 8) % ZZGL_RING_SIZE;
}

/* n bytes (a multiple of 8) into the ring, in one piece */
static void
ring_put(const void *src, uint32_t n)
{
   for (;;) {
      const uint32_t tail = ZZGL_RING_SIZE - wr;

      if (n > tail) {
         /* not past the end: a ZZ_WRAP (or nothing, at the very end) */
         if (ring_free() < tail) {
            T->wait();
            continue;
         }
         if (tail >= 8) {
            uint32_t w[2] = { be32(ZZ_WRAP), 0 };

            T->copy(T->ring + wr, w, 8);
         }
         wr = 0;
         blk_put(ZB_WR, wr);
         continue;
      }
      if (ring_free() < n) {
         T->wait();
         continue;
      }
      T->copy(T->ring + wr, src, n);
      wr = (wr + n) % ZZGL_RING_SIZE;
      blk_put(ZB_WR, wr);
      return;
   }
}

void
zzgl_flush(void)
{
   if (stage_n) {
      ring_put(stage, (uint32_t)stage_n);
      stage_n = 0;
   }
}

/* room for a command with n payload bytes in the stage: the payload */
static uint32_t *
cmd(uint32_t op, uint32_t n)
{
   const uint32_t total = (8 + n + 7) & ~7u;
   uint32_t *p;

   if (stage_n + total > ZZGL_STAGE)
      zzgl_flush();
   p = (uint32_t *)(stage + stage_n);
   p[0] = be32(op);
   p[1] = be32(n);
   stage_n += total;
   return p + 2;
}

/* a command whose payload may be bigger than the stage: header and words,
   then raw bytes (copied as they are) */
static void
cmd_big(uint32_t op, const uint32_t *words, unsigned nw, const void *data, uint32_t nbytes)
{
   const uint32_t n = nw * 4 + nbytes, total = (8 + n + 7) & ~7u;

   if (total <= ZZGL_STAGE) {
      uint32_t *p = cmd(op, n);

      memcpy(p, words, nw * 4);
      if (nbytes)
         memcpy(p + nw, data, nbytes);
      return;
   }
   {
      uint8_t *buf = malloc(total);
      uint32_t *p = (uint32_t *)buf;

      if (!buf)
         return;
      zzgl_flush();
      memset(buf + total - 8, 0, 8);
      p[0] = be32(op);
      p[1] = be32(n);
      memcpy(p + 2, words, nw * 4);
      memcpy(p + 2 + nw, data, nbytes);
      ring_put(buf, total);
      free(buf);
   }
}

/* a request core 1 answers: the reply is in the block (or the reply area) */
static void
sync_call(uint32_t what, const uint32_t *args, unsigned nargs)
{
   uint32_t *p = cmd(ZZ_SYNC, 8 + nargs * 4);
   unsigned k;

   p[0] = be32(++sync_seq);
   p[1] = be32(what);
   for (k = 0; k < nargs; k++)
      p[2 + k] = be32(args[k]);
   zzgl_flush();
   while (blk_get(ZB_SYNC_ACK) != sync_seq)
      T->wait();
}

#include "gen/zzgl_gen_enc.h"

void
zzgl_pipe(const uint32_t *words, unsigned n)
{
   cmd_big(ZZ_PIPE, words, n, NULL, 0);
}

void
zzgl_pipe_frame(void)
{
   static uint32_t sent;

   sent++;
   while ((int32_t)(sent - blk_get(ZB_FRAMES)) > 2)
      T->wait();
}

void
zzgl_pipe_read(const uint32_t *words, unsigned n, void *reply, size_t bytes)
{
   cmd_big(ZZ_PIPE, words, n, NULL, 0);
   sync_call(ZQ_NONE, NULL, 0);
   memcpy(reply, T->reply, bytes);
}

/*
 * Client arrays: the encoder keeps them; their used part goes with a draw.
 */
struct zarray {
   GLint size;
   GLenum type;
   GLsizei stride;
   const void *ptr;
   GLboolean on;
};
static struct zarray arrays[ZA_N];
static int client_unit;                 /* 0 or 1 */

static struct zarray *
array_of(GLenum cap)
{
   switch (cap) {
   case GL_VERTEX_ARRAY: return &arrays[ZA_VERTEX];
   case GL_COLOR_ARRAY: return &arrays[ZA_COLOR];
   case GL_NORMAL_ARRAY: return &arrays[ZA_NORMAL];
   case GL_TEXTURE_COORD_ARRAY: return &arrays[ZA_TEX0 + client_unit];
   default: return NULL;
   }
}

static void GLAPIENTRY
zzgl_EnableClientState_t(GLenum cap)
{
   struct zarray *a = array_of(cap);

   if (a)
      a->on = GL_TRUE;
   zzgl_EnableClientState(cap);
}

static void GLAPIENTRY
zzgl_DisableClientState_t(GLenum cap)
{
   struct zarray *a = array_of(cap);

   if (a)
      a->on = GL_FALSE;
   zzgl_DisableClientState(cap);
}

static void GLAPIENTRY
zzgl_ClientActiveTextureARB_t(GLenum unit)
{
   client_unit = unit == GL_TEXTURE1_ARB;
   zzgl_ClientActiveTextureARB(unit);
}

static void
set_array(int k, GLint size, GLenum type, GLsizei stride, const void *ptr)
{
   arrays[k].size = size;
   arrays[k].type = type;
   arrays[k].stride = stride;
   arrays[k].ptr = ptr;
}

static void GLAPIENTRY
zzgl_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
   set_array(ZA_VERTEX, size, type, stride, ptr);
}

static void GLAPIENTRY
zzgl_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
   set_array(ZA_COLOR, size, type, stride, ptr);
}

static void GLAPIENTRY
zzgl_NormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
{
   set_array(ZA_NORMAL, 3, type, stride, ptr);
}

static void GLAPIENTRY
zzgl_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
   set_array(ZA_TEX0 + client_unit, size, type, stride, ptr);
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

/* elements [first, first + n) of the enabled arrays and the indices into
   one ZZ_DRAW: the arrays tightly packed (as they are in memory: core 1
   swaps by type) */
static void
draw(GLenum mode, GLsizei count, GLint first, GLsizei n, const GLuint *idx)
{
   uint32_t head[4 + ZA_N * 4];
   unsigned nw = 4, k, mask = 0;
   uint32_t bytes = 0, *p;
   uint8_t *d;

   for (k = 0; k < ZA_N; k++)
      if (arrays[k].on && arrays[k].ptr) {
         const uint32_t esz = arrays[k].size * type_bytes(arrays[k].type);

         mask |= 1u << k;
         head[nw++] = be32((uint32_t)arrays[k].size);
         head[nw++] = be32((uint32_t)arrays[k].type);
         head[nw++] = be32((uint32_t)n);
         head[nw++] = be32((esz * n + 3) & ~3u);
         bytes += (esz * n + 3) & ~3u;
      }
   if (idx)
      bytes += count * 4;
   head[0] = be32(mode);
   head[1] = be32((uint32_t)count);
   head[2] = be32((uint32_t)first);
   head[3] = be32(mask | (idx ? ZD_ELEMENTS : 0));

   /* small draws straight into the stage; big ones through a buffer */
   if ((8 + nw * 4 + bytes + 7) <= ZZGL_STAGE) {
      p = cmd(ZZ_DRAW, nw * 4 + bytes);
      memcpy(p, head, nw * 4);
      d = (uint8_t *)(p + nw);
   } else {
      d = NULL;
   }
   {
      uint8_t *buf = d ? NULL : malloc(bytes ? bytes : 4);
      uint8_t *o = d ? d : buf;

      if (!o)
         return;
      for (k = 0; k < ZA_N; k++)
         if (mask & (1u << k)) {
            const struct zarray *a = &arrays[k];
            const uint32_t esz = a->size * type_bytes(a->type);
            const uint32_t stride = a->stride ? (uint32_t)a->stride : esz;
            const uint8_t *s = (const uint8_t *)a->ptr + (size_t)first * stride;
            GLsizei e;

            if (stride == esz) {
               memcpy(o, s, (size_t)esz * n);
            } else {
               for (e = 0; e < n; e++)
                  memcpy(o + (size_t)e * esz, s + (size_t)e * stride, esz);
            }
            to_be(o, (size_t)a->size * n, type_bytes(a->type));
            o += (esz * n + 3) & ~3u;
         }
      if (idx) {
         GLsizei e;

         for (e = 0; e < count; e++)
            ((uint32_t *)o)[e] = be32(idx[e] - (GLuint)first);
      }
      if (buf) {
         cmd_big(ZZ_DRAW, head, nw, buf, bytes);
         free(buf);
      }
   }
}

static void GLAPIENTRY
zzgl_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
   if (count > 0)
      draw(mode, count, first, count, NULL);
}

static void GLAPIENTRY
zzgl_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
   static GLuint *tmp;
   static GLsizei tmp_n;
   GLuint lo = ~0u, hi = 0;
   GLsizei e;

   if (count <= 0)
      return;
   if (tmp_n < count) {
      free(tmp);
      tmp_n = count + 1024;
      tmp = malloc(tmp_n * sizeof(*tmp));
      if (!tmp) {
         tmp_n = 0;
         return;
      }
   }
   for (e = 0; e < count; e++) {
      const GLuint v = type == GL_UNSIGNED_INT ? ((const GLuint *)indices)[e] :
                       type == GL_UNSIGNED_SHORT ? ((const GLushort *)indices)[e] :
                       ((const GLubyte *)indices)[e];

      tmp[e] = v;
      if (v < lo)
         lo = v;
      if (v > hi)
         hi = v;
   }
   draw(mode, count, (GLint)lo, (GLsizei)(hi - lo + 1), tmp);
}

/* glArrayElement: the element's values as immediate calls */
static void GLAPIENTRY
zzgl_ArrayElement(GLint i)
{
   int k;

   for (k = ZA_N - 1; k >= 0; k--) {
      const struct zarray *a = &arrays[k];
      const uint32_t esz = a->ptr ? a->size * type_bytes(a->type) : 0;
      const uint8_t *s;

      if (!a->on || !a->ptr)
         continue;
      s = (const uint8_t *)a->ptr + (size_t)i * (a->stride ? (uint32_t)a->stride : esz);
      if (a->type == GL_UNSIGNED_BYTE && k == ZA_COLOR) {
         zzgl_Color4ub(s[0], s[1], s[2], a->size > 3 ? s[3] : 255);
      } else if (a->type == GL_FLOAT) {
         const GLfloat *f = (const GLfloat *)s;

         switch (k) {
         case ZA_VERTEX:
            if (a->size == 2)
               zzgl_Vertex2f(f[0], f[1]);
            else
               zzgl_Vertex3f(f[0], f[1], f[2]);
            break;
         case ZA_COLOR:
            zzgl_Color4f(f[0], f[1], f[2], a->size > 3 ? f[3] : 1.0f);
            break;
         case ZA_NORMAL:
            zzgl_Normal3f(f[0], f[1], f[2]);
            break;
         default:
            zzgl_MultiTexCoord2fARB(k == ZA_TEX0 ? GL_TEXTURE0_ARB : GL_TEXTURE1_ARB, f[0], f[1]);
         }
      }
   }
}

/*
 * Calls with pointers.
 */
static void
floats(uint32_t op, const uint32_t *pre, unsigned npre, const GLfloat *v, unsigned n)
{
   uint32_t *p = cmd(op, (npre + n) * 4);
   unsigned k;

   for (k = 0; k < npre; k++)
      p[k] = be32(pre[k]);
   for (k = 0; k < n; k++)
      p[npre + k] = bef(v[k]);
}

static void GLAPIENTRY zzgl_LoadMatrixf(const GLfloat *m) { floats(ZZ_LoadMatrixf, NULL, 0, m, 16); }
static void GLAPIENTRY zzgl_MultMatrixf(const GLfloat *m) { floats(ZZ_MultMatrixf, NULL, 0, m, 16); }
static void GLAPIENTRY zzgl_Color4ubv(const GLubyte *v) { zzgl_Color4ub(v[0], v[1], v[2], v[3]); }
static void GLAPIENTRY zzgl_Color4fv(const GLfloat *v) { zzgl_Color4f(v[0], v[1], v[2], v[3]); }
static void GLAPIENTRY zzgl_Color3fv(const GLfloat *v) { zzgl_Color3f(v[0], v[1], v[2]); }
static void GLAPIENTRY zzgl_TexCoord2fv(const GLfloat *v) { zzgl_TexCoord2f(v[0], v[1]); }
static void GLAPIENTRY zzgl_Vertex3fv(const GLfloat *v) { zzgl_Vertex3f(v[0], v[1], v[2]); }
static void GLAPIENTRY zzgl_Vertex2fv(const GLfloat *v) { zzgl_Vertex2f(v[0], v[1]); }

static unsigned
pname_floats(GLenum pname)
{
   return pname == GL_FOG_COLOR || pname == GL_TEXTURE_ENV_COLOR || pname == GL_AMBIENT ||
          pname == GL_DIFFUSE || pname == GL_SPECULAR || pname == GL_POSITION ||
          pname == GL_EMISSION || pname == GL_AMBIENT_AND_DIFFUSE ? 4 :
          pname == GL_SPOT_DIRECTION ? 3 : 1;
}

static void GLAPIENTRY
zzgl_Fogfv(GLenum pname, const GLfloat *v)
{
   GLfloat f[4] = { 0, 0, 0, 0 };
   uint32_t pre = pname;

   memcpy(f, v, pname_floats(pname) * sizeof(GLfloat));
   floats(ZZ_Fogfv, &pre, 1, f, 4);
}

static void GLAPIENTRY
zzgl_TexEnvfv(GLenum target, GLenum pname, const GLfloat *v)
{
   GLfloat f[4] = { 0, 0, 0, 0 };
   uint32_t pre[2] = { target, pname };

   memcpy(f, v, pname_floats(pname) * sizeof(GLfloat));
   floats(ZZ_TexEnvfv, pre, 2, f, 4);
}

static void GLAPIENTRY
zzgl_Lightfv(GLenum light, GLenum pname, const GLfloat *v)
{
   GLfloat f[4] = { 0, 0, 0, 0 };
   uint32_t pre[2] = { light, pname };

   memcpy(f, v, pname_floats(pname) * sizeof(GLfloat));
   floats(ZZ_Lightfv, pre, 2, f, 4);
}

static void GLAPIENTRY
zzgl_Materialfv(GLenum face, GLenum pname, const GLfloat *v)
{
   GLfloat f[4] = { 0, 0, 0, 0 };
   uint32_t pre[2] = { face, pname };

   memcpy(f, v, pname_floats(pname) * sizeof(GLfloat));
   floats(ZZ_Materialfv, pre, 2, f, 4);
}

static void GLAPIENTRY
zzgl_ClipPlane(GLenum plane, const GLdouble *eq)
{
   uint32_t *p = cmd(ZZ_ClipPlane, 4 + 4 * 8);
   int k;

   p[0] = be32(plane);
   for (k = 0; k < 4; k++)
      put_double(p + 1 + 2 * k, eq[k]);
}

/* texture names: handed out here */
static GLuint next_name = 1;

static void GLAPIENTRY
zzgl_GenTextures(GLsizei n, GLuint *names)
{
   GLsizei k;

   for (k = 0; k < n; k++)
      names[k] = next_name++;
}

static void GLAPIENTRY
zzgl_DeleteTextures(GLsizei n, const GLuint *names)
{
   uint32_t *p;
   GLsizei k;

   if (n <= 0 || n > 4096)
      return;
   p = cmd(ZZ_DeleteTextures, 4 + n * 4);
   p[0] = be32((uint32_t)n);
   for (k = 0; k < n; k++)
      p[1 + k] = be32(names[k]);
}

/* the unpack pixel store, as the program set it */
static GLint unpack_align = 4, unpack_row = 0, unpack_skip_rows = 0, unpack_skip_px = 0;

static void GLAPIENTRY
zzgl_PixelStorei_t(GLenum pname, GLint v)
{
   switch (pname) {
   case GL_UNPACK_ALIGNMENT: unpack_align = v; return;
   case GL_UNPACK_ROW_LENGTH: unpack_row = v; return;
   case GL_UNPACK_SKIP_ROWS: unpack_skip_rows = v; return;
   case GL_UNPACK_SKIP_PIXELS: unpack_skip_px = v; return;
   default: zzgl_PixelStorei(pname, v);       /* pack state: core 1 keeps it */
   }
}

static unsigned
format_comps(GLenum format)
{
   switch (format) {
   case GL_RGBA: case GL_BGRA: return 4;
   case GL_RGB: case GL_BGR: return 3;
   case GL_LUMINANCE_ALPHA: return 2;
   default: return 1;
   }
}

static unsigned
pixel_bytes(GLenum format, GLenum type)
{
   switch (type) {
   case GL_UNSIGNED_SHORT_5_6_5: case GL_UNSIGNED_SHORT_4_4_4_4: case GL_UNSIGNED_SHORT_5_5_5_1:
   case GL_UNSIGNED_SHORT_4_4_4_4_REV: case GL_UNSIGNED_SHORT_1_5_5_5_REV:
   case GL_UNSIGNED_SHORT_5_6_5_REV:
      return 2;
   case GL_UNSIGNED_INT_8_8_8_8: case GL_UNSIGNED_INT_8_8_8_8_REV:
      return 4;
   default:
      return format_comps(format) * type_bytes(type);
   }
}

/* w x h pixels from the program's memory, tightly packed */
static uint8_t *
pack_pixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels, uint32_t *size)
{
   const unsigned px = pixel_bytes(format, type);
   const uint32_t row = (uint32_t)w * px;
   const uint32_t src_row_px = unpack_row > 0 ? (uint32_t)unpack_row : (uint32_t)w;
   uint32_t pitch = src_row_px * px;
   uint8_t *buf;
   GLsizei y;

   if (unpack_align > 1)
      pitch = (pitch + unpack_align - 1) / unpack_align * unpack_align;
   *size = (row * h + 3) & ~3u;
   if (!pixels) {
      *size = 0;
      return NULL;
   }
   buf = malloc(*size ? *size : 4);
   if (!buf)
      return NULL;
   for (y = 0; y < h; y++)
      memcpy(buf + (size_t)y * row,
             (const uint8_t *)pixels + (size_t)(y + unpack_skip_rows) * pitch + (size_t)unpack_skip_px * px,
             row);
   {
      const unsigned sw = px == 2 ? 2 : type == GL_UNSIGNED_BYTE || type == GL_BYTE ? 1 :
                          type == GL_UNSIGNED_SHORT || type == GL_SHORT ? 2 : 4;

      if (sw > 1)
         to_be(buf, row * h / sw, sw);
   }
   return buf;
}

static void GLAPIENTRY
zzgl_TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
                GLenum format, GLenum type, const GLvoid *pixels)
{
   uint32_t size, words[9];
   uint8_t *data = pack_pixels(w, h, format, type, pixels, &size);

   words[0] = be32(target);
   words[1] = be32((uint32_t)level);
   words[2] = be32((uint32_t)internal);
   words[3] = be32((uint32_t)w);
   words[4] = be32((uint32_t)h);
   words[5] = be32((uint32_t)border);
   words[6] = be32(format);
   words[7] = be32(type);
   words[8] = be32(pixels && !data ? 0xffffffffu : size);     /* ~0: no memory here */
   cmd_big(ZZ_TexImage2D, words, 9, data, data ? size : 0);
   free(data);
}

static void GLAPIENTRY
zzgl_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                   GLenum format, GLenum type, const GLvoid *pixels)
{
   uint32_t size, words[9];
   uint8_t *data = pack_pixels(w, h, format, type, pixels, &size);

   if (!data)
      return;
   words[0] = be32(target);
   words[1] = be32((uint32_t)level);
   words[2] = be32((uint32_t)x);
   words[3] = be32((uint32_t)y);
   words[4] = be32((uint32_t)w);
   words[5] = be32((uint32_t)h);
   words[6] = be32(format);
   words[7] = be32(type);
   words[8] = be32(size);
   cmd_big(ZZ_TexSubImage2D, words, 9, data, size);
   free(data);
}

/*
 * Calls that wait for core 1.
 */
static uint32_t
reply(int k)
{
   return blk_get(ZB_REPLY + k);
}

static void GLAPIENTRY
zzgl_Finish(void)
{
   sync_call(ZQ_NONE, NULL, 0);
}

static GLenum GLAPIENTRY
zzgl_GetError(void)
{
   sync_call(ZQ_ERROR, NULL, 0);
   return (GLenum)reply(0);
}

static const GLubyte *GLAPIENTRY
zzgl_GetString(GLenum name)
{
   static char *cache[8];
   static const GLenum names[8] = { GL_VENDOR, GL_RENDERER, GL_VERSION, GL_EXTENSIONS };
   uint32_t a = name;
   int k, n;

   for (k = 0; k < 4 && names[k] != name; k++)
      ;
   if (k == 4)
      return NULL;
   if (cache[k])
      return (const GLubyte *)cache[k];
   sync_call(ZQ_STRING, &a, 1);
   n = (int)reply(0);
   if (n < 0 || n >= (int)ZZGL_REPLY_SIZE)
      return NULL;
   cache[k] = malloc(n + 4);
   if (!cache[k])
      return NULL;
   T->copy(cache[k], T->reply, (n + 3) & ~3);
   cache[k][n] = 0;
   return (const GLubyte *)cache[k];
}

static void GLAPIENTRY
zzgl_GetIntegerv(GLenum pname, GLint *v)
{
   uint32_t a = pname;
   int k, n;

   sync_call(ZQ_INTEGERV, &a, 1);
   n = (int)reply(0);
   for (k = 0; k < n && k < 16; k++)
      v[k] = (GLint)reply(1 + k);
}

static void GLAPIENTRY
zzgl_GetBooleanv(GLenum pname, GLboolean *v)
{
   uint32_t a = pname;
   int k, n;

   sync_call(ZQ_BOOLEANV, &a, 1);
   n = (int)reply(0);
   for (k = 0; k < n && k < 16; k++)
      v[k] = (GLboolean)reply(1 + k);
}

static void GLAPIENTRY
zzgl_GetFloatv(GLenum pname, GLfloat *v)
{
   uint32_t a = pname;
   int k, n;

   sync_call(ZQ_FLOATV, &a, 1);
   n = (int)reply(0);
   for (k = 0; k < n && k < 16; k++) {
      union { uint32_t u; float f; } x;

      x.u = reply(1 + k);
      v[k] = x.f;
   }
}

static GLboolean GLAPIENTRY
zzgl_IsEnabled(GLenum cap)
{
   uint32_t a = cap;

   sync_call(ZQ_ISENABLED, &a, 1);
   return (GLboolean)reply(0);
}

static void GLAPIENTRY
zzgl_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{
   const uint32_t a[6] = { (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, format, type };
   uint32_t n;

   sync_call(ZQ_READPIXELS, a, 6);
   n = reply(0);       /* tightly packed (pack alignment 1 on core 1) */
   if (n && n <= ZZGL_REPLY_SIZE) {
      T->copy(pixels, T->reply, n);
      if (type != GL_UNSIGNED_BYTE && type != GL_BYTE)
         to_be(pixels, n / 4, 4);      /* (from big-endian: the same swap) */
   }
}

/*
 * The context.
 */
int
zzgl_open(const struct zzgl_transport *t, int w, int h, int depth, int stencil)
{
   uint32_t *p;

   T = t;
   stage_n = 0;
   wr = blk_get(ZB_WR);
   if (blk_get(ZB_MAGIC) != ZZGL_MAGIC)
      return -1;
   sync_seq = blk_get(ZB_SYNC_ACK);
   memset(arrays, 0, sizeof(arrays));
   client_unit = 0;
   open_w = w;
   open_h = h;
   p = cmd(ZZ_OPEN, 16);
   p[0] = be32((uint32_t)w);
   p[1] = be32((uint32_t)h);
   p[2] = be32((uint32_t)depth);
   p[3] = be32((uint32_t)stencil);
   zzgl_Finish();
   return 0;
}

void
zzgl_overlay(uint32_t pitch, uint32_t row0, uint32_t maxw, uint32_t maxh, int dbuf)
{
   uint32_t *p = cmd(ZZ_OVERLAY, 20);

   p[0] = be32(pitch);
   p[1] = be32(row0);
   p[2] = be32(maxw);
   p[3] = be32(maxh);
   p[4] = be32((uint32_t)dbuf);
}

/* the frame: shown when core 1 gets there; waits only for the frame before */
static uint32_t sent;

/* at most one frame in hand on core 1 */
static void
frame_sent(void)
{
   zzgl_flush();
   sent++;
   while ((int32_t)(sent - blk_get(ZB_FRAMES)) > 1)
      T->wait();
}

void
zzgl_swap(void)
{
   cmd(ZZ_SWAP, 0);
   frame_sent();
}

void
zzgl_effect(float time, float beat)
{
   uint32_t *p = cmd(ZZ_EFFECT, 8);

   p[0] = bef(time);
   p[1] = bef(beat);
   frame_sent();
}

void
zzgl_close(void)
{
   if (!T)
      return;
   cmd(ZZ_CLOSE, 0);
   zzgl_flush();
   T = NULL;
}

/*
 * glLockArraysEXT: a hint only, kept here.  Each draw sends its own
 * elements rebased to its first one, so core 1's Mesa must not see a lock
 * on the program's numbering: with it, Quake 3 drawn in another surface
 * order took wrong vertices.  Core 1 gains nothing from a lock either.
 */
static void GLAPIENTRY
zzgl_LockArraysEXT_t(GLint first, GLsizei count)
{
   (void)first;
   (void)count;
}

static void GLAPIENTRY
zzgl_UnlockArraysEXT_t(void)
{
}

/*
 * By name.
 */
static const struct {
   const char *name;
   void *fn;
} procs[] = {
   /* the ones with work on this side first: they wrap the plain ones */
   { "glEnableClientState", (void *)zzgl_EnableClientState_t },
   { "glDisableClientState", (void *)zzgl_DisableClientState_t },
   { "glClientActiveTextureARB", (void *)zzgl_ClientActiveTextureARB_t },
   { "glClientActiveTexture", (void *)zzgl_ClientActiveTextureARB_t },
   { "glPixelStorei", (void *)zzgl_PixelStorei_t },
   { "glActiveTexture", (void *)zzgl_ActiveTextureARB },
   { "glMultiTexCoord2f", (void *)zzgl_MultiTexCoord2fARB },
   { "glVertexPointer", (void *)zzgl_VertexPointer },
   { "glColorPointer", (void *)zzgl_ColorPointer },
   { "glNormalPointer", (void *)zzgl_NormalPointer },
   { "glTexCoordPointer", (void *)zzgl_TexCoordPointer },
   { "glDrawArrays", (void *)zzgl_DrawArrays },
   { "glDrawElements", (void *)zzgl_DrawElements },
   { "glArrayElement", (void *)zzgl_ArrayElement },
   { "glLockArraysEXT", (void *)zzgl_LockArraysEXT_t },
   { "glUnlockArraysEXT", (void *)zzgl_UnlockArraysEXT_t },
   { "glLoadMatrixf", (void *)zzgl_LoadMatrixf },
   { "glMultMatrixf", (void *)zzgl_MultMatrixf },
   { "glColor4ubv", (void *)zzgl_Color4ubv },
   { "glColor4fv", (void *)zzgl_Color4fv },
   { "glColor3fv", (void *)zzgl_Color3fv },
   { "glTexCoord2fv", (void *)zzgl_TexCoord2fv },
   { "glVertex3fv", (void *)zzgl_Vertex3fv },
   { "glVertex2fv", (void *)zzgl_Vertex2fv },
   { "glFogfv", (void *)zzgl_Fogfv },
   { "glTexEnvfv", (void *)zzgl_TexEnvfv },
   { "glLightfv", (void *)zzgl_Lightfv },
   { "glMaterialfv", (void *)zzgl_Materialfv },
   { "glClipPlane", (void *)zzgl_ClipPlane },
   { "glGenTextures", (void *)zzgl_GenTextures },
   { "glDeleteTextures", (void *)zzgl_DeleteTextures },
   { "glTexImage2D", (void *)zzgl_TexImage2D },
   { "glTexSubImage2D", (void *)zzgl_TexSubImage2D },
   { "glFinish", (void *)zzgl_Finish },
   { "glGetError", (void *)zzgl_GetError },
   { "glGetString", (void *)zzgl_GetString },
   { "glGetIntegerv", (void *)zzgl_GetIntegerv },
   { "glGetBooleanv", (void *)zzgl_GetBooleanv },
   { "glGetFloatv", (void *)zzgl_GetFloatv },
   { "glIsEnabled", (void *)zzgl_IsEnabled },
   { "glReadPixels", (void *)zzgl_ReadPixels },
#include "gen/zzgl_gen_names.h"
};

void *
zzgl_GetProcAddress(const char *name)
{
   unsigned k;

   for (k = 0; k < sizeof(procs) / sizeof(procs[0]); k++)
      if (!strcmp(procs[k].name, name))
         return procs[k].fn;
   return NULL;
}
