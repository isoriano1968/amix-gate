/*
 * zzgl_dec.h -- core 1's side of zzgl (zzgl_dec.c) and what it needs from
 * the platform: the shared memory, copies out of it (uncached on core 1),
 * the frame's way to the screen and something to do while idle.
 */
#ifndef ZZGL_DEC_H
#define ZZGL_DEC_H

#include <stddef.h>
#include <stdint.h>

struct zzgl_dec_env {
   volatile uint32_t *blk;
   const uint8_t *ring;
   uint8_t *reply;
   void (*copy_in)(void *dst, const void *src, size_t n);      /* from the ring */
   void (*copy_out)(void *dst, const void *src, size_t n);     /* to the reply area */
   void (*present)(const void *rgba, int w, int h);            /* bottom row first */
   void (*overlay)(uint32_t pitch, uint32_t row0, uint32_t maxw, uint32_t maxh, int dbuf);
   void (*idle)(void);          /* the ring is empty */
   void (*tick)(void);          /* every 4 commands, or NULL */
   void (*effect)(uint32_t *fb, int w, int h, float time, float beat);     /* ZZ_EFFECT, or NULL */
};

void zzgl_dec_init(const struct zzgl_dec_env *env);
/* once: until the ring is empty, else until ZZ_CLOSE (1); -1: a bad stream */
int zzgl_dec_run(const struct zzgl_dec_env *env, int once);
void zzgl_dec_shutdown(void);

#endif
