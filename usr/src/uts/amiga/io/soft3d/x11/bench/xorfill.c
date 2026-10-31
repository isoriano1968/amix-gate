/*
** xorfill.c -- P-5 suspect S1: does the server honour GXxor in PolyFillRect?
**
** Draws the same shape twice with GXxor, which must leave the window exactly
** as it was, and reads the pixels back.  Three cases, each in its own window
** column so a screenshot shows which one left a mark:
**
**   fill   XFillRectangle, GXxor, twice         -> PolyFillRect (the suspect)
**   wide   XDrawRectangle, line width 3, twice  -> mi turns it into PolyFillRect
**   thin   XDrawRectangle, line width 0, twice  -> zero-width lines (FillSpans);
**                                                  the control case
**
** A server with the S1 defect paints solid twice instead of XORing twice, so
** fill and wide stay visible in the foreground colour and read back FAIL,
** while thin reads back PASS.
**
** Two more cases repeat olwm's own move outline exactly (USL olwm Window.c:
** XFillRectangles on the ROOT window, four OUTLINE_WIDTH-wide rectangles, the
** GC switched to GXinvert, subwindow_mode IncludeInferiors -- Display.c:372,
** 387; drawn once to show and again to erase):
**
**   root2  that outline, 2 px wide (olwm's OUTLINE_WIDTH), around this window
**   root1  the same, 1 px wide
**
** They are judged by reading the root area back before and after (the root
** holds other clients' pixels, so "unchanged" rather than "background" is the
** test).  The server is grabbed across each case: without that, olwm redrawing
** a frame inside the compared area between the two reads (s6 on the rig,
** 2026-09-28: root2 "26064 pixels left", root1 clean) reads as a failure.
**
** Usage:   DISPLAY=:0 xorfill [-once] [-hold secs]
**   -once       draw each shape once only (shows what the shapes look like)
**   -hold secs  keep the window up that long for a screenshot (default 20)
**
** Exit status: 0 all cases PASS, 1 any FAIL, 2 could not run.
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIN_W   480
#define WIN_H   200
#define COL_W   160

static const char *names[3] = { "fill", "wide", "thin" };

/* olwm's move outline: four thin rectangles on the root, GXinvert, IncludeInferiors */
static void
outline(dpy, root, gc, x, y, w, h, t)
Display *dpy;
Window   root;
GC       gc;
int      x, y, w, h, t;
{
    XRectangle r[4];

    r[0].x = x;         r[0].y = y;         r[0].width = w; r[0].height = t;
    r[1].x = x;         r[1].y = y + h - t; r[1].width = w; r[1].height = t;
    r[2].x = x;         r[2].y = y + t;     r[2].width = t; r[2].height = h - 2 * t;
    r[3].x = x + w - t; r[3].y = y + t;     r[3].width = t; r[3].height = h - 2 * t;
    XFillRectangles(dpy, root, gc, r, 4);
}

/* Pixels that differ between two images of the same size. */
static long
differ(a, b, w, h)
XImage *a, *b;
int     w, h;
{
    long n = 0;
    int  x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (XGetPixel(a, x, y) != XGetPixel(b, x, y))
                n++;
    return n;
}

static void
draw(dpy, win, gc, which)
Display *dpy;
Window   win;
GC       gc;
int      which;
{
    int x = which * COL_W + 30;

    switch (which) {
    case 0:
        XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
        XFillRectangle(dpy, win, gc, x, 50, 100, 100);
        break;
    case 1:
        XSetLineAttributes(dpy, gc, 3, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, win, gc, x, 50, 100, 100);
        break;
    default:
        XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, win, gc, x, 50, 100, 100);
        break;
    }
}

/* Count pixels in one column that differ from the background. */
static long
marked(dpy, win, which, bg)
Display      *dpy;
Window        win;
int           which;
unsigned long bg;
{
    XImage *img;
    long    n = 0;
    int     x, y;

    img = XGetImage(dpy, win, which * COL_W, 0, COL_W, WIN_H,
                    AllPlanes, ZPixmap);
    if (!img)
        return -1;
    for (y = 0; y < WIN_H; y++)
        for (x = 0; x < COL_W; x++)
            if (XGetPixel(img, x, y) != bg)
                n++;
    XDestroyImage(img);
    return n;
}

int
main(argc, argv)
int   argc;
char *argv[];
{
    Display      *dpy;
    Window        win;
    GC            gc;
    XGCValues     gcv;
    XEvent        ev;
    unsigned long bg, fg;
    int           scr, i, once = 0, hold = 20, fails = 0;
    long          n;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-once"))
            once = 1;
        else if (!strcmp(argv[i], "-hold") && i + 1 < argc)
            hold = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: xorfill [-once] [-hold secs]\n");
            return 2;
        }
    }

    dpy = XOpenDisplay((char *) 0);
    if (!dpy) {
        fprintf(stderr, "xorfill: cannot open display\n");
        return 2;
    }
    scr = DefaultScreen(dpy);
    bg  = BlackPixel(dpy, scr);
    fg  = WhitePixel(dpy, scr) ^ bg;    /* XOR mask that flips to white */

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 40, 40,
                              WIN_W, WIN_H, 2, WhitePixel(dpy, scr), bg);
    XStoreName(dpy, win, "xorfill");
    XSelectInput(dpy, win, ExposureMask);
    XMapWindow(dpy, win);
    do
        XNextEvent(dpy, &ev);
    while (ev.type != Expose);
    XClearWindow(dpy, win);
    XSync(dpy, False);

    gcv.function   = GXxor;
    gcv.foreground = fg;
    gc = XCreateGC(dpy, win, GCFunction | GCForeground, &gcv);

    printf("xorfill: display %s, %dx%d depth %d, bg 0x%lx xor 0x%lx%s\n",
           DisplayString(dpy), DisplayWidth(dpy, scr), DisplayHeight(dpy, scr),
           DefaultDepth(dpy, scr), bg, fg, once ? ", -once" : "");

    for (i = 0; i < 3; i++) {
        draw(dpy, win, gc, i);
        if (!once)
            draw(dpy, win, gc, i);
    }
    XSync(dpy, False);

    for (i = 0; i < 3; i++) {
        n = marked(dpy, win, i, bg);
        if (once)
            printf("  %-4s %ld pixels drawn\n", names[i], n);
        else {
            printf("  %-4s %s (%ld pixels left after two XORs)\n", names[i],
                   n == 0 ? "PASS" : "FAIL", n);
            if (n != 0)
                fails++;
        }
    }
    fflush(stdout);

    /* olwm's outline on the root, around (and across) this window */
    {
        Window        root = RootWindow(dpy, scr), child;
        XWindowAttributes wa;
        XGCValues     rv;
        GC            rgc;
        XImage       *before, *after;
        int           wx, wy, ox, oy, ow, oh, t;
        static const char *rn[2] = { "root2", "root1" };

        XGetWindowAttributes(dpy, win, &wa);
        XTranslateCoordinates(dpy, win, root, 0, 0, &wx, &wy, &child);
        ox = wx - 20; oy = wy - 20; ow = wa.width + 40; oh = wa.height + 40;
        if (ox < 0) ox = 0;
        if (oy < 0) oy = 0;
        rv.function       = GXinvert;
        rv.subwindow_mode = IncludeInferiors;
        rv.foreground     = WhitePixel(dpy, scr);
        rgc = XCreateGC(dpy, root, GCFunction | GCSubwindowMode | GCForeground, &rv);
        for (t = 2; t >= 1; t--) {
            XGrabServer(dpy);           /* nobody else draws between the reads */
            XSync(dpy, False);
            before = XGetImage(dpy, root, ox, oy, ow, oh, AllPlanes, ZPixmap);
            outline(dpy, root, rgc, ox, oy, ow, oh, t);
            if (!once)
                outline(dpy, root, rgc, ox, oy, ow, oh, t);
            XSync(dpy, False);
            after = XGetImage(dpy, root, ox, oy, ow, oh, AllPlanes, ZPixmap);
            XUngrabServer(dpy);
            XSync(dpy, False);
            if (!before || !after) {
                printf("  %-5s could not read the root back\n", rn[2 - t]);
                fails++;
                continue;
            }
            n = differ(before, after, ow, oh);
            if (once)
                printf("  %-5s %ld pixels drawn\n", rn[2 - t], n);
            else {
                printf("  %-5s %s (%ld pixels left after two GXinvert passes)\n",
                       rn[2 - t], n == 0 ? "PASS" : "FAIL", n);
                if (n != 0)
                    fails++;
            }
            if (once)       /* leave the root as it was */
                outline(dpy, root, rgc, ox, oy, ow, oh, t);
            XDestroyImage(before);
            XDestroyImage(after);
        }
        XSync(dpy, False);
    }
    fflush(stdout);

    if (hold > 0)
        sleep(hold);
    XCloseDisplay(dpy);
    return fails ? 1 : 0;
}
