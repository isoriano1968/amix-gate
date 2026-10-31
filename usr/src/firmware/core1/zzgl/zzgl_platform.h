/*
 * zzgl_platform.h -- what a program's window code (ioquake3's
 * amix_glimp_zzgl.c) calls: the GL on core 1 (zzgl_amix.c) or, for tests on
 * the PC, in the same process (zzgl_host.c).  The GL functions themselves
 * come from zzgl_GetProcAddress (zzgl_enc.h).
 */
#ifndef ZZGL_PLATFORM_H
#define ZZGL_PLATFORM_H

/* the context for an X window (display, window) of w x h at x, y on the
   screen; 0, or -1 with the reason printed */
int zzgl_platform_open(void *dpy, unsigned long win, int x, int y, int w, int h);
/* after the frame's zzgl_swap() */
void zzgl_platform_frame(void);
/* the window moved: its top left on the screen */
void zzgl_platform_place(int x, int y);
void zzgl_platform_close(void);

/*
 * Sound through the board's HDMI (AMIX, with core 1's server running): a
 * ring of ZZGL_SND_FRAMES 16-bit stereo frames that core 1 plays like a DMA
 * buffer; the program writes frames at (its time mod the ring) * 4.
 * div: 1 for 48000 Hz, 2 for 24000 Hz.  -1: not here.
 */
int zzgl_sound_open(int div);
/* stream bytes (at the stream's rate) the HDMI has played since the open */
unsigned long zzgl_sound_pos(void);
/* n bytes of frames (native 16-bit stereo) at byte offset at (mod the ring) */
void zzgl_sound_write(unsigned long at, const short *frames, unsigned long n);
void zzgl_sound_close(void);

#endif
