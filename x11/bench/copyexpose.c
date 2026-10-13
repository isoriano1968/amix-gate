/*
** copyexpose.c -- P-5 suspect S2: window-to-window CopyArea from a partly
** obscured source.
**
** Three windows:
**   src   green, 200x200
**   cover red, overlapping the right half of src (on top of it)
**   dst   blue, 200x200, elsewhere, graphics exposures on
**
** XCopyArea(src -> dst) of the whole of src.  The X protocol says the
** obscured part of the source is not copied: the corresponding destination
** region is tiled with dst's background (blue) and GraphicsExpose events are
** sent for it.  A server with the S2 defect copies whatever is on the screen
** there (the red cover) and sends only NoExpose.
**
** Then the same with a self-copy inside src (the xterm scroll case): src
** scrolled left by 50 while its right half is covered.
**
** Usage:   DISPLAY=:0 copyexpose [-hold secs]     (default 20)
** Exit status: 0 PASS, 1 FAIL, 2 could not run.
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SZ 200

static unsigned long
colour(dpy, name)
Display *dpy;
char    *name;
{
    XColor c, exact;

    if (!XAllocNamedColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)),
                          name, &c, &exact)) {
        fprintf(stderr, "copyexpose: cannot allocate %s\n", name);
        exit(2);
    }
    return c.pixel;
}

static Window
mkwin(dpy, x, y, w, h, bg, name)
Display      *dpy;
int           x, y, w, h;
unsigned long bg;
char         *name;
{
    Window               win;
    XSetWindowAttributes a;
    XEvent               ev;

    a.background_pixel  = bg;
    a.override_redirect = True;     /* no window manager placement or frame */
    win = XCreateWindow(dpy, DefaultRootWindow(dpy), x, y, w, h, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixel | CWOverrideRedirect, &a);
    XStoreName(dpy, win, name);
    XSelectInput(dpy, win, ExposureMask);
    XMapRaised(dpy, win);
    XWindowEvent(dpy, win, ExposureMask, &ev);
    return win;
}

/* Collect the GraphicsExpose/NoExpose events for one CopyArea. */
static long
exposures(dpy, area)
Display *dpy;
long    *area;
{
    XEvent ev;
    long   n = 0;

    *area = 0;
    XSync(dpy, False);
    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == NoExpose)
            return n;
        if (ev.type == GraphicsExpose) {
            n++;
            *area += (long) ev.xgraphicsexpose.width *
                     ev.xgraphicsexpose.height;
            if (ev.xgraphicsexpose.count == 0)
                return n;
        }
    }
}

static unsigned long
pixel(dpy, win, x, y)
Display *dpy;
Window   win;
int      x, y;
{
    XImage       *img;
    unsigned long p;

    img = XGetImage(dpy, win, x, y, 1, 1, AllPlanes, ZPixmap);
    if (!img)
        return (unsigned long) -1;
    p = XGetPixel(img, 0, 0);
    XDestroyImage(img);
    return p;
}

int
main(argc, argv)
int   argc;
char *argv[];
{
    Display      *dpy;
    Window        src, cover, dst;
    GC            gc;
    unsigned long green, red, blue, p;
    long          n, area;
    int           i, hold = 20, fails = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-hold") && i + 1 < argc)
            hold = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: copyexpose [-hold secs]\n");
            return 2;
        }
    }

    dpy = XOpenDisplay((char *) 0);
    if (!dpy) {
        fprintf(stderr, "copyexpose: cannot open display\n");
        return 2;
    }
    green = colour(dpy, "green");
    red   = colour(dpy, "red");
    blue  = colour(dpy, "blue");

    src   = mkwin(dpy,  60,  60, SZ, SZ, green, "copyexpose src");
    cover = mkwin(dpy, 160,  60, SZ, SZ, red,   "copyexpose cover");
    dst   = mkwin(dpy, 420,  60, SZ, SZ, blue,  "copyexpose dst");
    XSync(dpy, False);
    while (XPending(dpy)) {         /* drop the mapping exposures */
        XEvent ev;
        XNextEvent(dpy, &ev);
    }

    gc = XCreateGC(dpy, dst, 0, (XGCValues *) 0);
    XSetGraphicsExposures(dpy, gc, True);

    printf("copyexpose: display %s, green 0x%lx red 0x%lx blue 0x%lx\n",
           DisplayString(dpy), green, red, blue);

    /* Case 1: src -> dst; the right half of src (x >= 100) is under cover. */
    XCopyArea(dpy, src, dst, gc, 0, 0, SZ, SZ, 0, 0);
    n = exposures(dpy, &area);
    p = pixel(dpy, dst, 150, 100);
    printf("  win->win  exposures %ld (area %ld, expect %d)  dst(150,100)=0x%lx %s\n",
           n, area, (SZ / 2) * SZ, p,
           p == blue ? "blue" : p == red ? "RED (copied the cover)" :
           p == green ? "green" : "?");
    if (n == 0 || p != blue)
        fails++;
    printf("  win->win  %s\n", (n != 0 && p == blue) ? "PASS" : "FAIL");

    /* Case 2: scroll src left by 50 inside itself, right half covered. */
    XClearWindow(dpy, src);
    XSync(dpy, False);
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
    }
    XCopyArea(dpy, src, src, gc, 50, 0, SZ - 50, SZ, 0, 0);
    n = exposures(dpy, &area);
    p = pixel(dpy, src, 60, 100);   /* came from src x=110, under the cover */
    printf("  scroll    exposures %ld (area %ld)  src(60,100)=0x%lx %s\n",
           n, area, p,
           p == green ? "green" : p == red ? "RED (copied the cover)" : "?");
    if (n == 0 || p == red)
        fails++;
    printf("  scroll    %s\n", (n != 0 && p != red) ? "PASS" : "FAIL");
    fflush(stdout);

    if (hold > 0)
        sleep(hold);
    XCloseDisplay(dpy);
    return fails ? 1 : 0;
}
