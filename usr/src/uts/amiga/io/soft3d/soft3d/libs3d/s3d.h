/*
 * s3d.h -- libs3d: the Z3660's soft3D (Wazp3D on the board's ARM) for any
 * rasterizer front end on AMIX (2026-10-26).  Mesa's X driver uses it
 * (on by default, MESA_SOFT3D=0 off); TinyGL has its own copy of the same logic (zsoft3d.c).
 *
 * The caller transforms, lights, clips and culls; libs3d takes window-space
 * triangles, batches them, keeps soft3D's state, uploads textures, clears,
 * and presents the back buffer (off-screen VRAM) through the X server's
 * ZZ9K-SOFT3D request, which copies only the visible parts of the window.
 * One soft3D user at a time on the machine.  See s3d.c for the traps.
 */
#ifndef S3D_H
#define S3D_H

#include <X11/Xlib.h>

/* a vertex: window coordinates with y DOWN from the top of the drawing
   area, z 0 (near) .. 1, w = 1/clip w (perspective), u v normalized */
struct s3d_vertex {
    float x, y, z, w, u, v;
    unsigned char r, g, b, a;
};

/* soft3D's draw state */
struct s3d_state {
    struct s3d_tex *tex;        /* 0: untextured */
    int blend;                  /* S3D_BLEND(src, dst) or S3D_REPLACE */
    int zmode;                  /* S3D_ZMODE(update, compare) */
    int texenv;                 /* S3D_ENV_* */
};

#define S3D_ZMODE(upd, cmp) ((upd) * 8 + ((cmp) - 1))
#define S3D_Z_NEVER     1
#define S3D_Z_LESS      2
#define S3D_Z_GEQUAL    3
#define S3D_Z_LEQUAL    4
#define S3D_Z_GREATER   5
#define S3D_Z_NOTEQUAL  6
#define S3D_Z_EQUAL     7
#define S3D_Z_ALWAYS    8
#define S3D_BLEND(s, d) ((s) * 16 + (d))
#define S3D_REPLACE     S3D_BLEND(2, 1)         /* ONE, ZERO */
#define S3D_ENV_REPLACE 1
#define S3D_ENV_DECAL   2
#define S3D_ENV_MODULATE 3
#define S3D_ENV_BLEND   4

/* GL enum -> soft3D (GL's numbers; no GL header needed): 0 = none */
int s3d_gl_factor(int glenum);          /* blend factors */
int s3d_gl_depthfunc(int glenum);       /* GL_NEVER.. -> S3D_Z_* */
int s3d_gl_texenv(int glenum);          /* GL_MODULATE.. -> S3D_ENV_* */

/* who: the program's name for messages.  0 on success. */
int  s3d_init(Display *dpy, const char *who);
void s3d_verbose(int on);               /* 0: no message when unavailable */
int  s3d_active(void);
int  s3d_resize(int width, int height); /* the drawing area; 0 on success */
void s3d_set_state(const struct s3d_state *st);
int  s3d_get_state(struct s3d_state *st);  /* what the next triangle is drawn in */
void s3d_tri(const struct s3d_vertex *a, const struct s3d_vertex *b,
             const struct s3d_vertex *c);
/* the same for a triangle already clipped to the drawing area (fewer checks) */
void s3d_tri_clipped(const struct s3d_vertex *a, const struct s3d_vertex *b,
                     const struct s3d_vertex *c);
void s3d_flush(void);                   /* hand the batch to the ARM */
void s3d_finish(void);                  /* ... and have every pixel written */
/* a rectangle of the drawing area (y down) in RGB565; the whole Z buffer
   (soft3D cannot clear part of it) */
void s3d_clear_color(int x, int y, int w, int h, unsigned short rgb565);
void s3d_clear_depth(float z);
void s3d_swap(Display *dpy, Window win);
/* the back buffer for the CPU (glBitmap, glDrawPixels, glReadPixels):
   RGB565 pixels, stride in pixels; call s3d_finish() first */
unsigned short *s3d_backbuffer(int *stride_px);

/* textures: w x h (any size), 3 (RGB) or 4 (RGBA) bytes a texel, rows from
   v = 0; the texels are copied into board memory */
struct s3d_tex *s3d_tex_new(int w, int h, int bpp, const unsigned char *texels);
int  s3d_tex_update(struct s3d_tex *t, const unsigned char *texels);
void s3d_tex_free(struct s3d_tex *t);

#endif
