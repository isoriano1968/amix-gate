/*
 * zzgl.h -- GL marshalling between the 060 (AMIX) and the Z3660's core 1.
 *
 * The 060's libGL (zzgl_enc.c) packs GL calls into a ring in board memory;
 * core 1 (z3660ARM_Core_gpu, zzgl_dec.c) unpacks them into its own Mesa and
 * shows the frames in the RTG overlay.  See docs/ARM-GL-PLAN.md.
 *
 * Everything in the ring and the block is big-endian 32-bit words (the
 * 060's order); core 1 swaps.  A command is two words, opcode and payload
 * length in bytes (a multiple of 4), then the payload.  Commands do not
 * wrap: ZZ_WRAP sends the reader back to the ring's start.
 */
#ifndef ZZGL_H
#define ZZGL_H

#include <stdint.h>

/* board memory (offsets from the RTG base, as in vplay's zzav.h): the ring
   uses the video player's packet area (the GPU program replaces the player) */
#define ZZGL_BLK_OFF        0x07AFC000UL    /* the block: one page */
#define ZZGL_RING_OFF       0x07100000UL
#define ZZGL_RING_SIZE      0x00600000UL
#define ZZGL_REPLY_OFF      0x07700000UL    /* large replies (glReadPixels) */
#define ZZGL_REPLY_SIZE     0x00180000UL
/* the sound: a ring of ZZGL_SND_FRAMES 16-bit stereo frames (big-endian),
   played like a DMA buffer: frame (time mod the ring); the 060 mixes ahead
   into it, core 1 keeps the board's HDMI ring (vplay's, 160 ms) filled from
   it 120 ms ahead of the HDMI DMA */
#define ZZGL_SND_OFF        0x07880000UL
#define ZZGL_SND_FRAMES     32768UL         /* 1.4 s at 24 kHz */
#define ZZGL_SND_SIZE       (ZZGL_SND_FRAMES * 4)
#define ZZGL_HDMI_OFF       0x07CE0000UL    /* vplay's ZZAV_AUDIO_OFF */
#define ZZGL_HDMI_SIZE      (3840UL * 8)
#define ZZGL_HDMI_RATE      48000

#define ZZGL_MAGIC          0x5A5A474CUL    /* "ZZGL": core 1's server runs */
#define ZZGL_JOB_MAGIC      0x5A474C53UL    /* "ZGLS": the job that starts it */

/* words of the block */
enum {
   ZB_MAGIC,            /* core 1: ZZGL_MAGIC while the server runs */
   ZB_WR,               /* 060: bytes written (mod ring size) */
   ZB_RD,               /* core 1: bytes taken */
   ZB_SYNC_ACK,         /* core 1: the last ZZ_SYNC done (its reply is in place) */
   ZB_FRAMES,           /* core 1: frames shown */
   ZB_ERROR,            /* core 1: a decoding error (opcode), 0 = none */
   ZB_AUDIO_POS,        /* core 1: the HDMI sound DMA's byte position in its ring */
   ZB_AUDIO_SEQ,        /* core 1: counts the updates of ZB_AUDIO_POS */
   ZB_STATS_US,         /* core 1: time of the last frame, us (swap to swap) */
   ZB_PROF = 16,        /* core 1: sp_fast_prof[0..7] of the last frame (k cycles / counts) */
   ZB_PROF_PATH = 28,   /* core 1: pixels drawn affine (NEON), perspective (NEON), */
   ZB_PROF_EVT = 30,    /* core 1: L1 data cache refills in the spans, last frame */
   ZB_PROF_PATH2 = 31,  /*   ... and on the scalar path */
   ZB_OVL = 24,         /* core 1: the overlay's pitch, row0, width, height (zzglstat -grab) */
   ZB_PROF_SEG = 40,    /* core 1: perspective segment setups: k cycles, count */
   ZB_MARK = 48,        /* core 1: the command being done */
   ZB_PROF_ROUTE = 42,  /* core 1: pixels two a turn, of those in TK_DIRECT */
   ZB_SAMPLE = 49,      /* core 1, per tick: count, pc, lr, sp, then code addresses on the stack (to 63) */
   ZB_SND_ON = 32,      /* 060: 1 or 2 = the stream's rate divides 48 kHz by this; 0 off */
   ZB_SND_WR,           /* 060: stream bytes written up to (its time) */
   ZB_SND_POS,          /* core 1: HDMI bytes played since ZB_SND_ON (48 kHz frames * 4) */
   ZB_SND_UNDER,        /* core 1: periods filled with silence */
   ZB_REPLY = 64,       /* replies: words 64..1023 */
   ZB_NWORDS = 1024
};
#define ZB_REPLY_WORDS      (ZB_NWORDS - ZB_REPLY)

/*
 * The plain calls: every argument a value, each sent as one 32-bit word
 * (doubles two, high first).  Types: e GLenum, i GLint, u GLuint, z GLsizei,
 * b GLboolean, x GLbitfield, c GLubyte, f GLfloat, d GLdouble.  The opcode
 * is ZZ_<name>; gen.py makes the encoder and decoder from this list.
 */
#define ZZGL_SIMPLE(X) \
   X(Enable, "e") X(Disable, "e") X(EnableClientState, "e") X(DisableClientState, "e") \
   X(BindTexture, "eu") X(BlendFunc, "ee") X(ClearColor, "ffff") X(Clear, "x") \
   X(ClearStencil, "i") X(ColorMask, "bbbb") X(CopyTexSubImage2D, "eiiiiizz") X(CullFace, "e") \
   X(DepthFunc, "e") X(DepthMask, "b") X(LineWidth, "f") X(PolygonOffset, "ff") \
   X(Scissor, "iizz") X(StencilFunc, "eiu") X(StencilMask, "u") X(StencilOp, "eee") \
   X(TexParameterf, "eef") X(TexParameteri, "eei") X(Viewport, "iizz") X(AlphaFunc, "ef") \
   X(Color4f, "ffff") X(Color3f, "fff") X(Color4ub, "cccc") X(LoadIdentity, "") X(MatrixMode, "e") \
   X(PopMatrix, "") X(PushMatrix, "") X(ShadeModel, "e") X(TexEnvf, "eef") X(TexEnvi, "eei") \
   X(Translatef, "fff") X(Rotatef, "ffff") X(Scalef, "fff") X(ClearDepth, "d") X(DepthRange, "dd") \
   X(DrawBuffer, "e") X(ReadBuffer, "e") X(PolygonMode, "ee") X(Begin, "e") X(End, "") \
   X(Frustum, "dddddd") X(Ortho, "dddddd") X(TexCoord2f, "ff") X(Vertex2f, "ff") \
   X(Vertex3f, "fff") X(Normal3f, "fff") X(ActiveTextureARB, "e") X(ClientActiveTextureARB, "e") \
   X(MultiTexCoord2fARB, "eff") X(Flush, "") X(PixelStorei, "ei") X(Hint, "ee") \
   X(LockArraysEXT, "iz") X(UnlockArraysEXT, "") X(FrontFace, "e") X(Fogf, "ef") X(Fogi, "ei") \
   X(LightModeli, "ei") X(Lightf, "eef") X(Materialf, "eef") X(PointSize, "f")

enum {
   ZZ_WRAP = 0,         /* back to the ring's start */
   ZZ_OPEN,             /* w, h, depth bits, stencil bits: the context */
   ZZ_CLOSE,            /* the server ends */
   ZZ_SYNC,             /* seq [, what, args]: do the reply, then ZB_SYNC_ACK = seq */
   ZZ_SWAP,             /* the frame into the overlay */
   ZZ_OVERLAY,          /* pitch, row0, maxw, maxh, double-buffered: where frames go */
   ZZ_LoadMatrixf,      /* 16 floats */
   ZZ_MultMatrixf,
   ZZ_ClipPlane,        /* plane, 4 doubles */
   ZZ_Fogfv,            /* pname, 4 floats */
   ZZ_TexEnvfv,         /* target, pname, 4 floats */
   ZZ_Lightfv,          /* light, pname, 4 floats */
   ZZ_Materialfv,       /* face, pname, 4 floats */
   ZZ_DeleteTextures,   /* n, names */
   ZZ_TexImage2D,       /* target, level, internal, w, h, border, format, type, size, data */
   ZZ_TexSubImage2D,    /* target, level, x, y, w, h, format, type, size, data */
   ZZ_DRAW,             /* mode, count, first/min, [arrays], [indices]: see zzgl_enc.c */
   ZZ_FIRST_SIMPLE
};

#define ZZ_OP(name, sig) ZZ_##name,
enum { ZZ_BEFORE_SIMPLE = ZZ_FIRST_SIMPLE - 1, ZZGL_SIMPLE(ZZ_OP) ZZ_NOPS };
#undef ZZ_OP

/* ZZ_SYNC's requests */
enum {
   ZQ_NONE,             /* only wait (glFinish) */
   ZQ_ERROR,            /* glGetError */
   ZQ_STRING,           /* glGetString(name) */
   ZQ_INTEGERV,         /* glGetIntegerv(pname): 16 words */
   ZQ_BOOLEANV,
   ZQ_FLOATV,
   ZQ_READPIXELS,       /* x, y, w, h, format, type: into ZZGL_REPLY_OFF */
   ZQ_ISENABLED
};

#define ZZ_EFFECT 1000           /* time, beat (floats): core 1 draws a frame itself (spook.c) and shows it */

/* ZZ_DRAW's arrays: a word of flags, then per array in this order
   size, type, element count, bytes (padded to 4) */
enum { ZA_VERTEX, ZA_COLOR, ZA_NORMAL, ZA_TEX0, ZA_TEX1, ZA_N };
#define ZD_ELEMENTS 0x10000     /* indices follow (type in the low bits of word 3) */

#endif
