#ifndef OSBENCH_H
#define OSBENCH_H

enum osb_scene { OSB_GEARS, OSB_Q3, OSB_FLUID, OSB_Q3MT };

extern int osb_q3_tris;     /* triangles per pass in the q3 scene */

/* called after each frame (glFinish), when set: the RGBA buffer, bottom row first */
extern void (*osb_frame_hook)(const void *buffer, int w, int h);

/* from the platform */
unsigned long long osb_now_us(void);

double osb_run(enum osb_scene scene, void *buffer, int w, int h, int frames, char *err);
unsigned long osb_checksum(const void *buffer, int w, int h);

#endif
