/*
 * zzgl_enc.h -- the 060's side of zzgl (zzgl_enc.c) and what it needs from
 * the platform: where the block, the ring and the reply area are, and how
 * to copy into board memory (move16 on AMIX) and to wait.
 */
#ifndef ZZGL_ENC_H
#define ZZGL_ENC_H

#include <stddef.h>
#include <stdint.h>

#define ZZGL_STAGE          (256 * 1024)    /* commands collected before the ring */

struct zzgl_transport {
   volatile uint32_t *blk;
   uint8_t *ring;
   const uint8_t *reply;
   void (*copy)(void *dst, const void *src, size_t n);
   void (*wait)(void);          /* while core 1 catches up */
};

/* the context: w x h with depth and stencil bits; -1: no server on core 1 */
int zzgl_open(const struct zzgl_transport *t, int w, int h, int depth, int stencil);
/* where core 1 puts the frames (the overlay buffer's row pitch, the row of
   the picture's top, its size, double buffering by core 0) */
void zzgl_overlay(uint32_t pitch, uint32_t row0, uint32_t maxw, uint32_t maxh, int dbuf);
void zzgl_swap(void);
/* instead of drawing and swapping: core 1 computes the frame for this time;
   beat: 0 to 1 within the music's beat */
void zzgl_effect(float time, float beat);
void zzgl_flush(void);
/* words of zz3660 commands; _read: and wait for its n bytes */
void zzgl_pipe(const uint32_t *words, unsigned n);
/* a frame sent: wait while core 1 is more than two behind */
void zzgl_pipe_frame(void);
void zzgl_pipe_read(const uint32_t *words, unsigned n, void *reply, size_t bytes);
void zzgl_close(void);
void *zzgl_GetProcAddress(const char *name);

#endif
