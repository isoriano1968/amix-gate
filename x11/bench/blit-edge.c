/*
** blit-edge.c -- P-5 fix 3: do the card-blitter paths draw exactly what the
** CPU paths draw?
**
** A full-screen override-redirect window gets a position-coded pattern
** (XPutImage, which is always a CPU path), and the client keeps the same
** picture in memory.  Each case then draws with one X request that the
** server may hand to the blitter and does the same to the memory copy, and
** the screen area around the case (the rectangles plus a 2-pixel margin,
** clipped to the screen) is read back and compared pixel for pixel:
**
**   fill    XFillRectangle, GXcopy        -> SolidRect
**   clear   XClearArea                    -> PaintWindow (background)
**   copy    XCopyArea, window to itself   -> CopyArea
**   move    XMoveWindow of a child window -> CopyWindow + parent PaintWindow
**
** Every left edge 0-3 mod 4 (so every pixel pair and long alignment), widths
** of 1-5, 7, 8, 31-33 and 100 pixels, heights of 1, 2 and 17, rectangles on
** all four screen edges, and copies overlapping in all eight directions,
** including by a single pixel.  The server is grabbed for the whole run and
** the window raised under the grab, so nothing but this client draws and no
** other window covers the cases.
**
** Run it on the server under test twice: with -blitmin 1 (everything that
** qualifies goes to the blitter; the server log says "card blitter in use")
** and without -blitmin (the CPU paths, the control).  Both must PASS.
**
** -stress secs is the metal load for the interrupt hold-off measurement
** instead: for that long, alternate a full-screen fill and a full-screen
** copy (one pixel down-right) with an XSync after each, then print the
** count and the mean milliseconds per operation.  With the blitter each
** one is a single stalled register write on the card, so the mean is the
** longest stretch the CPU takes no interrupt (zz9k_blit_maxarea 0).
**
** Usage:   DISPLAY=:0 blit-edge [-v] [-hold secs] [-stress secs]
**   -v            print every case, not just failures
**   -hold secs    keep the window up that long at the end (default 0)
**   -stress secs  the load loop above instead of the cases
**
** Exit status: 0 all cases PASS, 1 any FAIL, 2 could not run.
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

static Display        *dpy;
static Window          win;
static GC              gc;
static int             W, H;
static unsigned short *ref;             /* the picture as it must be */
static int             verbose, ncase, nfail;
static unsigned long   bgpix = 0x1234;  /* the window's background */

#define REF(x, y)  ref[(long)(y) * W + (x)]

static unsigned short
pattern(x, y)
int x, y;
{
    return (unsigned short)(x * 31 + y * 1021 + (x >> 5) * 7);
}

/* Paint the pattern over the screen box and the reference, by XPutImage. */
static void
paint(x, y, w, h)
int x, y, w, h;
{
    XImage *img;
    char   *data;
    int     i, j;

    data = malloc((size_t) w * h * 2);
    img = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), 16,
                       ZPixmap, 0, data, w, h, 16, w * 2);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            XPutPixel(img, i, j, pattern(x + i, y + j));
            REF(x + i, y + j) = pattern(x + i, y + j);
        }
    XPutImage(dpy, win, gc, img, 0, 0, x, y, w, h);
    XDestroyImage(img);
}

/* Compare the screen box with the reference; count and report a case. */
static void
check(name, x1, y1, x2, y2)
char *name;
int   x1, y1, x2, y2;             /* inclusive-exclusive, will be grown */
{
    XImage *img;
    long    bad = 0;
    int     x, y, fx = -1, fy = -1;
    unsigned long got = 0, want = 0;

    x1 -= 2; y1 -= 2; x2 += 2; y2 += 2;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > W) x2 = W;
    if (y2 > H) y2 = H;
    XSync(dpy, False);
    img = XGetImage(dpy, win, x1, y1, x2 - x1, y2 - y1, AllPlanes, ZPixmap);
    ncase++;
    if (!img) {
        printf("  FAIL %s: could not read back\n", name);
        nfail++;
        return;
    }
    for (y = y1; y < y2; y++)
        for (x = x1; x < x2; x++)
            if (XGetPixel(img, x - x1, y - y1) != REF(x, y)) {
                if (!bad++) {
                    fx = x; fy = y;
                    got = XGetPixel(img, x - x1, y - y1);
                    want = REF(x, y);
                }
            }
    XDestroyImage(img);
    if (bad) {
        nfail++;
        printf("  FAIL %s: %ld pixels, first (%d,%d) 0x%04lx want 0x%04lx\n",
               name, bad, fx, fy, got, want);
    } else if (verbose)
        printf("  PASS %s\n", name);
    fflush(stdout);
}

static void
do_fill(x, y, w, h)
int x, y, w, h;
{
    char name[80];
    unsigned short pix = (unsigned short)(0x8000 | (x * 3 + y * 5 + w));
    int  i, j;

    XSetForeground(dpy, gc, pix);
    XFillRectangle(dpy, win, gc, x, y, w, h);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            REF(x + i, y + j) = pix;
    sprintf(name, "fill %dx%d at %d,%d", w, h, x, y);
    check(name, x, y, x + w, y + h);
}

static void
do_clear(x, y, w, h)
int x, y, w, h;
{
    char name[80];
    int  i, j;

    XClearArea(dpy, win, x, y, w, h, False);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            REF(x + i, y + j) = (unsigned short) bgpix;
    sprintf(name, "clear %dx%d at %d,%d", w, h, x, y);
    check(name, x, y, x + w, y + h);
    paint(x, y, w, h);                  /* back to the pattern */
}

static void
do_copy(sx, sy, w, h, dx, dy)
int sx, sy, w, h, dx, dy;
{
    char            name[80];
    unsigned short *tmp;
    int             i, j;

    XCopyArea(dpy, win, win, gc, sx, sy, w, h, dx, dy);
    tmp = malloc((size_t) w * h * 2);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            tmp[j * w + i] = REF(sx + i, sy + j);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            REF(dx + i, dy + j) = tmp[j * w + i];
    free(tmp);
    sprintf(name, "copy %dx%d %d,%d -> %d,%d", w, h, sx, sy, dx, dy);
    check(name, sx < dx ? sx : dx, sy < dy ? sy : dy,
          (sx > dx ? sx : dx) + w, (sy > dy ? sy : dy) + h);
    /* restore the pattern so later cases start from known pixels */
    paint(sx < dx ? sx : dx, sy < dy ? sy : dy,
          (sx > dx ? sx - dx : dx - sx) + w, (sy > dy ? sy - dy : dy - sy) + h);
}

/* A child window with its own pattern, moved by (mx,my): CopyWindow. */
static void
do_move(x, y, w, h, mx, my)
int x, y, w, h, mx, my;
{
    char                 name[80];
    XSetWindowAttributes a;
    Window               c;
    unsigned short      *tmp;
    int                  i, j;

    a.background_pixmap = None;         /* the server paints nothing into it */
    a.override_redirect = True;
    c = XCreateWindow(dpy, win, x, y, w, h, 0, CopyFromParent, InputOutput,
                      CopyFromParent, CWBackPixmap | CWOverrideRedirect, &a);
    XMapWindow(dpy, c);
    XSync(dpy, False);
    /* the child shows the parent's pixels (background None); give it its own */
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            REF(x + i, y + j) = (unsigned short)(0x4000 ^ pattern(i * 3, j * 5));
    {
        XImage *img;
        char   *data = malloc((size_t) w * h * 2);

        img = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), 16,
                           ZPixmap, 0, data, w, h, 16, w * 2);
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                XPutPixel(img, i, j, REF(x + i, y + j));
        XPutImage(dpy, c, gc, img, 0, 0, 0, 0, w, h);
        XDestroyImage(img);
    }
    XMoveWindow(dpy, c, x + mx, y + my);
    tmp = malloc((size_t) w * h * 2);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            tmp[j * w + i] = REF(x + i, y + j);
    for (j = 0; j < h; j++)             /* uncovered parent: its background */
        for (i = 0; i < w; i++)
            REF(x + i, y + j) = (unsigned short) bgpix;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            REF(x + mx + i, y + my + j) = tmp[j * w + i];
    free(tmp);
    sprintf(name, "move %dx%d at %d,%d by %d,%d", w, h, x, y, mx, my);
    check(name, mx < 0 ? x + mx : x, my < 0 ? y + my : y,
          (mx > 0 ? x + mx : x) + w, (my > 0 ? y + my : y) + h);
    XDestroyWindow(dpy, c);
    XSync(dpy, False);
    /* destroying it exposes the parent: background there, then the pattern */
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            REF(x + mx + i, y + my + j) = (unsigned short) bgpix;
    paint(mx < 0 ? x + mx : x, my < 0 ? y + my : y,
          w + (mx < 0 ? -mx : mx), h + (my < 0 ? -my : my));
}

static double
now()
{
    struct timeval tv;

    gettimeofday(&tv, (struct timezone *) 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* Full-screen fill, full-screen copy, alternately, for secs seconds. */
static void
stress(secs)
int secs;
{
    double t0, t;
    long   fills = 0, copies = 0;

    t0 = now();
    do {
        XSetForeground(dpy, gc, (unsigned long)((fills & 1) ? 0xf800 : 0x001f));
        XFillRectangle(dpy, win, gc, 0, 0, W, H);
        XSync(dpy, False);
        fills++;
        XCopyArea(dpy, win, win, gc, 0, 0, W - 1, H - 1, 1, 1);
        XSync(dpy, False);
        copies++;
        t = now();
    } while (t - t0 < secs);
    printf("blit-edge: stress %dx%d, %.1f s: %ld fills + %ld copies, %.2f ms per op\n",
           W, H, t - t0, fills, copies, (t - t0) * 1000.0 / (fills + copies));
    fflush(stdout);
}

int
main(argc, argv)
int   argc;
char *argv[];
{
    XSetWindowAttributes a;
    XEvent  ev;
    int     scr, i, hold = 0, a4, wi, hi, d, x, y, secs = 0;
    static int widths[]  = { 1, 2, 3, 4, 5, 7, 8, 31, 32, 33, 100 };
    static int heights[] = { 1, 2, 17 };
    static int dirs[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1},
                              {5,3}, {-5,-3}, {3,-5}, {-3,5} };

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            verbose = 1;
        else if (!strcmp(argv[i], "-hold") && i + 1 < argc)
            hold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-stress") && i + 1 < argc)
            secs = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: blit-edge [-v] [-hold secs] [-stress secs]\n");
            return 2;
        }
    }
    dpy = XOpenDisplay((char *) 0);
    if (!dpy) {
        fprintf(stderr, "blit-edge: cannot open display\n");
        return 2;
    }
    scr = DefaultScreen(dpy);
    if (DefaultDepth(dpy, scr) != 16) {
        fprintf(stderr, "blit-edge: needs a 16-bit screen, this one is %d\n",
                DefaultDepth(dpy, scr));
        return 2;
    }
    W = DisplayWidth(dpy, scr);
    H = DisplayHeight(dpy, scr);
    if (W < 1024 || H < 768) {
        fprintf(stderr, "blit-edge: the cases need at least 1024x768, this is %dx%d\n",
                W, H);
        return 2;
    }
    ref = malloc((size_t) W * H * 2);
    if (!ref) {
        fprintf(stderr, "blit-edge: no memory for a %dx%d reference\n", W, H);
        return 2;
    }

    a.background_pixel  = bgpix;
    a.override_redirect = True;
    win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, W, H, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixel | CWOverrideRedirect, &a);
    XSelectInput(dpy, win, ExposureMask);
    XMapRaised(dpy, win);
    do
        XNextEvent(dpy, &ev);
    while (ev.type != Expose);
    XSelectInput(dpy, win, 0);          /* the cases' own exposures are not read */
    gc = XCreateGC(dpy, win, 0, (XGCValues *) 0);
    XSetGraphicsExposures(dpy, gc, False);

    if (secs > 0) {
        stress(secs);
        XCloseDisplay(dpy);
        return 0;
    }

    /*
    ** Raise again under the grab: a client that mapped a window after ours
    ** (the session's clock and workspace manager still starting, the rig
    ** 2026-09-29) would otherwise stay above it, and every case under it
    ** would compare some other window's pixels.  Nobody can map or raise
    ** anything while the server is grabbed.
    */
    XGrabServer(dpy);
    XRaiseWindow(dpy, win);
    XSync(dpy, False);
    printf("blit-edge: display %s, %dx%d depth 16\n", DisplayString(dpy), W, H);
    paint(0, 0, W, H);
    check("pattern", 0, 0, W, H);

    /* fills and clears: every alignment, the widths and heights, all edges */
    for (a4 = 0; a4 < 4; a4++)
        for (wi = 0; wi < sizeof widths / sizeof widths[0]; wi++)
            for (hi = 0; hi < sizeof heights / sizeof heights[0]; hi++) {
                int w = widths[wi], h = heights[hi];

                do_fill(200 + a4 + wi * 4, 100 + hi * 40, w, h);
                do_clear(600 + a4 + wi * 4, 100 + hi * 40, w, h);
            }
    for (wi = 0; wi < sizeof widths / sizeof widths[0]; wi++) {
        int w = widths[wi];

        do_fill(0, 300, w, 9);                  /* left edge */
        do_fill(W - w, 320, w, 9);              /* right edge */
        do_fill(100 + wi * 80, 0, w, 3);         /* top edge */
        do_fill(100 + wi * 80, H - 3, w, 3);     /* bottom edge */
        do_clear(W - w, 340, w, 9);
    }
    do_fill(0, 0, W, H);                        /* the whole screen */
    paint(0, 0, W, H);
    check("pattern again", 0, 0, W, H);

    /* copies: all alignments, overlapping in eight directions, edges */
    for (a4 = 0; a4 < 4; a4++)
        for (wi = 0; wi < sizeof widths / sizeof widths[0]; wi++)
            for (d = 0; d < 8; d++) {
                int w = widths[wi], h = 17;

                x = 150 + a4 + (wi % 6) * 120;
                y = 420 + (wi / 6) * 60;
                do_copy(x, y, w, h, x + dirs[d][0], y + dirs[d][1]);
                do_copy(x, y, w, h, x + dirs[d][0] * 20 + a4, y + dirs[d][1] * 9);
            }
    do_copy(0, 0, 100, 50, 1, 1);               /* from the corner */
    do_copy(W - 100, H - 50, 100, 50, W - 101, H - 51);
    do_copy(1, 1, W - 1, H - 1, 0, 0);          /* the whole screen, up-left */
    paint(0, 0, W, H);
    do_copy(0, 0, W - 1, H - 1, 1, 1);          /* and down-right */
    paint(0, 0, W, H);
    do_copy(0, 0, W, H / 2, 0, H / 2);          /* a scroll's worth */
    paint(0, 0, W, H);

    /* window moves: CopyWindow and the parent's PaintWindow */
    for (d = 0; d < 8; d++) {
        do_move(400, 500, 101, 37, dirs[d][0], dirs[d][1]);
        do_move(401, 500, 200, 60, dirs[d][0] * 13, dirs[d][1] * 7);
    }
    do_move(0, 0, 150, 80, 3, 2);               /* from the corner */
    do_move(W - 150, H - 80, 150, 80, -3, -2);

    paint(0, 0, W, H);
    check("pattern at the end", 0, 0, W, H);
    XUngrabServer(dpy);
    XSync(dpy, False);

    printf("blit-edge: %d cases, %d FAIL -- %s\n", ncase, nfail,
           nfail ? "FAIL" : "PASS");
    fflush(stdout);
    if (hold > 0)
        sleep(hold);
    XCloseDisplay(dpy);
    return nfail ? 1 : 0;
}
