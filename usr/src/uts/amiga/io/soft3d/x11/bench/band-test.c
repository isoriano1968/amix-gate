/* band-test.c -- host harness for x11/bench/band-test.sh (RT-08): the blitter queue in zz9000draw.c against a direct reference */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define TRUE 1
#define FALSE 0
typedef void *pointer;
#define W 256
#define H 200
typedef struct { unsigned short *fbBase; int pitch; int fd; } rtgScreenRec, *rtgScreenPtr;
struct { int w; } mode = { W }, *ZZ9000_selected_mode = &mode;
#define ZZ9000_STRIDE(p) ((p)->pitch / 2)
#define GetZZ9000Screen(p) (p)
#define ZZ9K_BLIT_FILL 1
#define ZZ9K_BLIT_COPY 2
#define ZZ9KIOC_BLITS 1
struct zz9k_blit { unsigned short op,x,y,width,height,src_x,src_y,pad; unsigned long color; };
struct zz9k_blits { struct zz9k_blit *recs; unsigned long n; };
static unsigned short fb[W*H], ref[W*H];
static int refuse_at = -1, flushes, maxrec;           /* refuse the Nth flush */
static void ErrorF(const char *f, ...) { }
static void rawcopy(unsigned short *m, int sx,int sy,int w,int h,int dx,int dy){
    unsigned short *t=malloc(w*h*2); int i,j;
    for(j=0;j<h;j++)for(i=0;i<w;i++)t[j*w+i]=m[(sy+j)*W+sx+i];
    for(j=0;j<h;j++)for(i=0;i<w;i++)m[(dy+j)*W+dx+i]=t[j*w+i]; free(t);}
static void rawfill(unsigned short *m,int x,int y,int w,int h,unsigned short c){int i,j;for(j=0;j<h;j++)for(i=0;i<w;i++)m[(y+j)*W+x+i]=c;}
static int ioctl(int fd, int cmd, struct zz9k_blits *bs) {
    unsigned long i; if (flushes++ == refuse_at) { errno = ENXIO; return -1; }
    for (i = 0; i < bs->n; i++) { struct zz9k_blit *b = &bs->recs[i];
        if ((long)b->width*b->height > maxrec) maxrec = b->width*b->height;
        if (b->op == ZZ9K_BLIT_FILL) rawfill(fb,b->x,b->y,b->width,b->height,(unsigned short)b->color);
        else rawcopy(fb,b->src_x,b->src_y,b->width,b->height,b->x,b->y); }  /* per record, like the card */
    return (int)bs->n; }
#include "band-funcs.c"
static rtgScreenRec scr;
static void init(void){int i;for(i=0;i<W*H;i++)fb[i]=ref[i]=(unsigned short)(i*2654435761u>>16);}
int main(void){
    int fails=0,cases=0,op,dx,dy,w,h,r,mx; static int dirs[][2]={{0,1},{0,-1},{1,0},{-1,0},{3,5},{-3,-5},{5,-3},{-5,3},{0,17},{0,-17}};
    scr.fbBase=fb; scr.pitch=W*2;
    for(mx=0;mx<3;mx++) for(r=-1;r<4;r++) for(op=1;op<=2;op++) for(w=1;w<=W-40;w+=37) for(h=1;h<=H-40;h+=23){
        int d; for(d=0;d<(op==1?1:10);d++){
        int sx=20,sy=20,tx=20+(op==2?dirs[d][0]:0),ty=20+(op==2?dirs[d][1]:0);
        init(); zz9000BlitMin=1; zz9000BlitMax=(mx==0?0:mx==1?64:500); blitN=0; flushes=0; refuse_at=r; maxrec=0;
        if(op==1){ blitFill(&scr,sx,sy,sx+w-1,sy+h-1,0x1234); rawfill(ref,sx,sy,w,h,0x1234);}
        else { copyVRAMBox(&scr,sx,sy,w,h,tx,ty); rawcopy(ref,sx,sy,w,h,tx,ty);}
        BLIT_FLUSH(); cases++;
        if(memcmp(fb,ref,sizeof fb)){fails++; if(fails<6)printf("FAIL op%d w%d h%d d%d max%d refuse%d\n",op,w,h,d,zz9000BlitMax,r);}
        if(zz9000BlitMax>0 && r<0 && maxrec>zz9000BlitMax && maxrec>w){fails++; if(fails<6)printf("BAND TOO BIG %d > %d (w %d)\n",maxrec,zz9000BlitMax,w);}
    }}
    printf("band-test: %d cases, %d FAIL\n",cases,fails); return fails!=0;}
