/*
** copyarea-order.c -- host check of the P-5 S2 box ordering in ZZ9000CopyArea
** (x11/patches/p5-2-copyarea-expose.patch): overlapping self-copies in all eight
** directions over banded regions with holes, against a copy through a temporary
** buffer.  walk() and copyVRAMBox() mirror the patched DDX code.
**   cc -O -fsanitize=address -o copyarea-order copyarea-order.c && ./copyarea-order
** 2026-09-27: 17778 cases, 0 failures; with the in-band direction flipped, 8934.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { short x1, y1, x2, y2; } BoxRec, *BoxPtr;
#define W 64
#define H 64
static unsigned short fb[W*H], ref[W*H], tmp[W*H];
static void copyVRAMBox(unsigned short *fbBase, int stride, int srcx, int srcy, int w, int h, int dstx, int dsty)
{ unsigned short *src,*dst; int row,col,row_start,row_end,row_step,col_start,col_end,col_step;
  if (w<=0||h<=0) return;
  if (dsty>srcy){row_start=h-1;row_end=-1;row_step=-1;}else{row_start=0;row_end=h;row_step=1;}
  if (dsty==srcy&&dstx>srcx){col_start=w-1;col_end=-1;col_step=-1;}else{col_start=0;col_end=w;col_step=1;}
  for(row=row_start;row!=row_end;row+=row_step){src=fbBase+((unsigned long)(srcy+row)*stride)+srcx;dst=fbBase+((unsigned long)(dsty+row)*stride)+dstx;
   for(col=col_start;col!=col_end;col+=col_step)dst[col]=src[col];}}
static void walk(BoxPtr pbox, int nbox, int dx, int dy)
{ BoxPtr pboxBand,pboxNext,pboxEnd=pbox+nbox; int i,bandsUp=(dy<0),boxesLeft=(dx<0);
  if(!bandsUp&&!boxesLeft){for(i=0;i<nbox;i++,pbox++)copyVRAMBox(fb,W,pbox->x1+dx,pbox->y1+dy,pbox->x2-pbox->x1,pbox->y2-pbox->y1,pbox->x1,pbox->y1);return;}
  pboxBand=bandsUp?pboxEnd:pbox;
  while(bandsUp?(pboxBand>pbox):(pboxBand<pboxEnd)){BoxPtr first,last,b;
    if(bandsUp){last=pboxBand-1;first=last;while(first>pbox&&(first-1)->y1==last->y1)first--;pboxNext=first;}
    else{first=pboxBand;last=first;while(last+1<pboxEnd&&(last+1)->y1==first->y1)last++;pboxNext=last+1;}
    for(b=boxesLeft?last:first;boxesLeft?(b>=first):(b<=last);b+=boxesLeft?-1:1)
      copyVRAMBox(fb,W,b->x1+dx,b->y1+dy,b->x2-b->x1,b->y2-b->y1,b->x1,b->y1);
    pboxBand=pboxNext;}}
int main(void){ int t,fails=0,cases=0; srand(1);
  for(t=0;t<20000;t++){ BoxRec boxes[16]; int nb=0,band,y=8,i,dx,dy;
    /* a banded region with holes, inside [8,56) so src stays in range for |d|<=8 */
    for(band=0;band<4&&y<54;band++){int h=1+rand()%10,x=8,nbx=1+rand()%3,k; if(y+h>56)h=56-y;
      for(k=0;k<nbx&&x<52;k++){int w=1+rand()%10; if(x+w>56)w=56-x; boxes[nb].x1=x;boxes[nb].x2=x+w;boxes[nb].y1=y;boxes[nb].y2=y+h;nb++; x+=w+1+rand()%4;}
      y+=h+rand()%3;}
    for(i=0;i<W*H;i++)fb[i]=(unsigned short)rand();
    dx=(t%3)-1; dy=((t/3)%3)-1; dx*=1+rand()%8; dy*=1+rand()%8; if(!dx&&!dy)continue;
    memcpy(ref,fb,sizeof fb); memcpy(tmp,fb,sizeof fb);
    for(i=0;i<nb;i++){int r,c;for(r=boxes[i].y1;r<boxes[i].y2;r++)for(c=boxes[i].x1;c<boxes[i].x2;c++)ref[r*W+c]=tmp[(r+dy)*W+c+dx];}
    walk(boxes,nb,dx,dy); cases++;
    if(memcmp(fb,ref,sizeof fb)){fails++; if(fails<4)printf("FAIL dx=%d dy=%d nb=%d\n",dx,dy,nb);}}
  printf("%d cases, %d failures\n",cases,fails); return fails!=0;}
