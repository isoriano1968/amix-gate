/*
** xorcopy.c -- P-5 S4/S5: do image writes and copies honour GXxor?
**
** Three cases, each twice with GXxor into its own column of a black window,
** which must leave the column black; then the pixels are read back:
**
**   image    XPutImage (ZPixmap)                  -> mi PutImage -> SetSpans   (S4)
**   pixmap   XCopyArea pixmap -> window           -> mi CopyArea -> SetSpans   (S4)
**   window   XCopyArea window -> window           -> the DDX fast path, which
**                                                    could only copy          (S5)
**
** The source is a 100x100 pattern with no zero pixels.  A server that draws
** these as GXcopy leaves the pattern in the column: FAIL.
**
** Both windows are override-redirect at fixed, non-overlapping places.  With
** a window-manager-placed target, olwm's cascading put its frame over a corner
** of the source window on one rig run, and a restack between the two copies
** left 165 pixels of the first copy -- a test artefact, not a server fault.
**
** Usage:   DISPLAY=:0 xorcopy [-once] [-hold secs]
**   -once       one XOR per case (the pattern should show: reference frame)
**   -hold secs  keep the windows up that long for a screenshot (default 20)
**
** Exit status: 0 all PASS, 1 any FAIL, 2 could not run.
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
#define PAT     100

static const char *names[3] = { "image", "pixmap", "window" };

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

static void
wait_expose(dpy, win)
Display *dpy;
Window   win;
{
    XEvent ev;

    XSelectInput(dpy, win, ExposureMask);
    XMapRaised(dpy, win);
    XWindowEvent(dpy, win, ExposureMask, &ev);
}

int
main(argc, argv)
int   argc;
char *argv[];
{
    Display             *dpy;
    Window               win, src;
    XSetWindowAttributes a;
    Pixmap               pix;
    XImage              *img;
    GC                   gcx, gcc;
    XGCValues            gcv;
    unsigned long        bg;
    char                *data;
    int                  scr, depth, i, x, y, n, once = 0, hold = 20, fails = 0;
    long                 m;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-once"))
            once = 1;
        else if (!strcmp(argv[i], "-hold") && i + 1 < argc)
            hold = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: xorcopy [-once] [-hold secs]\n");
            return 2;
        }
    }

    dpy = XOpenDisplay((char *) 0);
    if (!dpy) {
        fprintf(stderr, "xorcopy: cannot open display\n");
        return 2;
    }
    scr   = DefaultScreen(dpy);
    depth = DefaultDepth(dpy, scr);
    bg    = BlackPixel(dpy, scr);

    /* The pattern, as a client-side image */
    img = XCreateImage(dpy, DefaultVisual(dpy, scr), depth, ZPixmap, 0,
                       (char *) 0, PAT, PAT, 32, 0);
    if (!img) {
        fprintf(stderr, "xorcopy: XCreateImage failed\n");
        return 2;
    }
    data = (char *) malloc(img->bytes_per_line * PAT);
    if (!data) {
        fprintf(stderr, "xorcopy: out of memory\n");
        return 2;
    }
    img->data = data;
    for (y = 0; y < PAT; y++)
        for (x = 0; x < PAT; x++)
            XPutPixel(img, x, y, (unsigned long) (((x * 97 + y * 31) | 1) & 0xFFFF));

    /* The target window and a visible source window for case 3, both
       unmanaged so that no window manager can place or restack them */
    a.background_pixel  = bg;
    a.override_redirect = True;
    win = XCreateWindow(dpy, RootWindow(dpy, scr), 40, 300, WIN_W, WIN_H, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixel | CWOverrideRedirect, &a);
    XStoreName(dpy, win, "xorcopy");
    wait_expose(dpy, win);

    src = XCreateWindow(dpy, RootWindow(dpy, scr), 40 + WIN_W + 40, 300, PAT, PAT, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixel | CWOverrideRedirect, &a);
    XStoreName(dpy, src, "xorcopy src");
    wait_expose(dpy, src);

    gcv.function = GXcopy;
    gcc = XCreateGC(dpy, win, GCFunction, &gcv);
    gcv.function = GXxor;
    gcx = XCreateGC(dpy, win, GCFunction, &gcv);

    pix = XCreatePixmap(dpy, win, PAT, PAT, depth);
    XPutImage(dpy, pix, gcc, img, 0, 0, 0, 0, PAT, PAT);
    XPutImage(dpy, src, gcc, img, 0, 0, 0, 0, PAT, PAT);
    XClearWindow(dpy, win);
    XSync(dpy, False);

    printf("xorcopy: display %s, %dx%d depth %d%s\n", DisplayString(dpy),
           DisplayWidth(dpy, scr), DisplayHeight(dpy, scr), depth,
           once ? ", -once" : "");

    for (n = 0; n < (once ? 1 : 2); n++) {
        XPutImage(dpy, win, gcx, img, 0, 0, 0 * COL_W + 30, 50, PAT, PAT);
        XCopyArea(dpy, pix, win, gcx, 0, 0, PAT, PAT, 1 * COL_W + 30, 50);
        XCopyArea(dpy, src, win, gcx, 0, 0, PAT, PAT, 2 * COL_W + 30, 50);
    }
    XSync(dpy, False);

    for (i = 0; i < 3; i++) {
        m = marked(dpy, win, i, bg);
        if (once)
            printf("  %-6s %ld pixels drawn\n", names[i], m);
        else {
            printf("  %-6s %s (%ld pixels left after two XORs)\n", names[i],
                   m == 0 ? "PASS" : "FAIL", m);
            if (m != 0)
                fails++;
        }
    }
    fflush(stdout);

    if (hold > 0)
        sleep(hold);
    XCloseDisplay(dpy);
    return fails ? 1 : 0;
}
