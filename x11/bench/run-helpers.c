/*
** run-helpers.c -- host check of P-5 fix 2's fillRun/copyRun/copyRunBack (x11/patches/p5-6-stores32.patch),
** extracted from the patched zz9000draw.c, against 16-bit reference loops over random lengths,
** alignments and (valid-direction) overlaps.  cc -O -w -fsanitize=address -o runs run-helpers.c && ./runs
** 2026-09-28: 200000 cases, 0 failures; with copyRunBack's tail broken, 12609.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define RUN_ODD(p)  (((uintptr_t)(p)) & 2)

/* Fill w pixels with one colour. */
static void
fillRun(dst, pixel, w)
unsigned short *dst;
unsigned short  pixel;
int             w;
{
    uint32_t two;
    uint32_t *l;
    int            n;

    if (w <= 0)
        return;
    if (RUN_ODD(dst)) {
        *dst++ = pixel;
        w--;
    }
    two = ((uint32_t)pixel << 16) | pixel;
    l = (uint32_t *)dst;
    for (n = w >> 1; n > 0; n--)
        *l++ = two;
    if (w & 1)
        *(unsigned short *)l = pixel;
}

/* Copy w pixels, left to right. */
static void
copyRun(dst, src, w)
unsigned short *dst;
unsigned short *src;
int             w;
{
    uint32_t *ld, *ls;
    int            n;

    if (w <= 0)
        return;
    if (RUN_ODD(dst) != RUN_ODD(src)) {
        while (w-- > 0)
            *dst++ = *src++;
        return;
    }
    if (RUN_ODD(dst)) {
        *dst++ = *src++;
        w--;
    }
    ld = (uint32_t *)dst;
    ls = (uint32_t *)src;
    for (n = w >> 1; n > 0; n--)
        *ld++ = *ls++;
    if (w & 1)
        *(unsigned short *)ld = *(unsigned short *)ls;
}

/* Copy w pixels, right to left (an overlapping move to the right). */
static void
copyRunBack(dst, src, w)
unsigned short *dst;
unsigned short *src;
int             w;
{
    uint32_t *ld, *ls;
    int            n;

    if (w <= 0)
        return;
    dst += w;                       /* one past the last pixel */
    src += w;
    if (RUN_ODD(dst) != RUN_ODD(src)) {
        while (w-- > 0)
            *--dst = *--src;
        return;
    }
    if (RUN_ODD(dst)) {
        *--dst = *--src;
        w--;
    }
    ld = (uint32_t *)dst;
    ls = (uint32_t *)src;
    for (n = w >> 1; n > 0; n--)
        *--ld = *--ls;
    if (w & 1)
        *((unsigned short *)ld - 1) = *((unsigned short *)ls - 1);
}

/* ---------------------------------------------------------------------- */
/* CPU rectangle helpers. Hardware blit can be added after register bring-up. */
static unsigned short A[4096] __attribute__((aligned(8))), R[4096] __attribute__((aligned(8))), T[4096] __attribute__((aligned(8)));
int main(void){ int t,f=0,i; srand(3);
 for(t=0;t<200000;t++){ int w=rand()%40, d=8+rand()%64, s=8+rand()%64, k=rand()%3; unsigned short px=rand();
  for(i=0;i<4096;i++) A[i]=R[i]=T[i]=rand();
  if(k==0){ fillRun(A+d,px,w); for(i=0;i<w;i++) R[d+i]=px; }
  else { memcpy(T,A,sizeof A); for(i=0;i<w;i++) R[d+i]=T[s+i];  /* reference: from the original */
         if(k==1) { if (d>s && d<s+w) continue; copyRun(A+d,A+s,w); } else { if (d<s && d+w>s) continue; copyRunBack(A+d,A+s,w);} }
  if(memcmp(A,R,sizeof A)){ f++; if(f<5) printf("FAIL k=%d w=%d d=%d s=%d\n",k,w,d,s);} }
 printf("run helpers: %d failures\n",f); return f!=0; }
