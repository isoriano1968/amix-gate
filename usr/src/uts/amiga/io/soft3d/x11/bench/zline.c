/*
** zline.c -- is the server's zero-width line rasterisation the standard one?
**
** Draws a sparse fan of zero-width lines (Polylines, line width 0: miZeroLine on the
** RTG servers) from a centre point to every 6th point on a 128x128 square's edge,
** so every octant and every Bresenham tie occurs, and prints one checksum of
** the pixels read back.  The tie-breaks come from the screen's zero-line
** bias; a server built on miScreenInit (host Xvfb, cfb servers) uses
** DEFAULTZEROLINEBIAS, so its checksum is the reference.  An RTG server
** before the fix took the bias from an unrelated screen private and its
** checksum changed from build to build (MG10, 2026-09-29).
**
** Usage:   DISPLAY=:0 zline        prints "zline: checksum 0x........ (N set)"
** Exit status: 0 ran, 2 could not run.
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>

#define S 64

int
main()
{
    Display             *dpy;
    Window               win;
    GC                   gc;
    XImage              *img;
    XSetWindowAttributes a;
    XEvent               ev;
    unsigned long        sum = 0, n = 0, fg, bg;
    int                  scr, i, x, y, c = S + 8;

    dpy = XOpenDisplay((char *) 0);
    if (!dpy) {
        fprintf(stderr, "zline: cannot open display\n");
        return 2;
    }
    scr = DefaultScreen(dpy);
    bg = BlackPixel(dpy, scr);
    fg = WhitePixel(dpy, scr);
    a.background_pixel  = bg;
    a.override_redirect = True;
    win = XCreateWindow(dpy, RootWindow(dpy, scr), 20, 20, 2 * c, 2 * c, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixel | CWOverrideRedirect, &a);
    XSelectInput(dpy, win, ExposureMask);
    XMapRaised(dpy, win);
    do
        XNextEvent(dpy, &ev);
    while (ev.type != Expose);
    XGrabServer(dpy);
    XRaiseWindow(dpy, win);
    XClearWindow(dpy, win);
    gc = XCreateGC(dpy, win, 0, (XGCValues *) 0);
    XSetForeground(dpy, gc, fg);
    XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
    for (i = -S; i <= S; i += 6) {      /* the four edges, sparse: ties stay visible */
        XDrawLine(dpy, win, gc, c, c, c + i, c - S);
        XDrawLine(dpy, win, gc, c, c, c + i, c + S);
        XDrawLine(dpy, win, gc, c, c, c - S, c + i);
        XDrawLine(dpy, win, gc, c, c, c + S, c + i);
    }
    XSync(dpy, False);
    img = XGetImage(dpy, win, 0, 0, 2 * c, 2 * c, AllPlanes, ZPixmap);
    XUngrabServer(dpy);
    if (!img) {
        fprintf(stderr, "zline: could not read back\n");
        return 2;
    }
    for (y = 0; y < 2 * c; y++)         /* position-weighted: one value per image */
        for (x = 0; x < 2 * c; x++)
            if (XGetPixel(img, x, y) != bg) {
                n++;
                sum = (sum * 31 + (unsigned long)(y * 1024 + x)) & 0xffffffffUL;
            }
    printf("zline: checksum 0x%08lx (%lu set)\n", sum, n);
    XCloseDisplay(dpy);
    return 0;
}
