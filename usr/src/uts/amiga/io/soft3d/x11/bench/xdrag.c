/*
** xdrag.c -- drag a window by its title bar through the XTEST extension, so an
** outline move can be reproduced without a hand on the mouse (P-5 step 0a on
** a machine whose KVM has no mouse).
**
** Finds the top-level window whose WM_NAME is <name>, walks up to the window
** manager's frame (the ancestor that is a child of the root), presses button 1
** in the middle of the title area (between the frame's top and the client's
** top), moves the pointer <dx> pixels right in <steps> steps over <ms>
** milliseconds, and releases.  Prints the frame's position before and after.
**
** Usage:   DISPLAY=:0 xdrag [-dx N] [-steps N] [-ms N] [-hold secs] name
**          defaults: -dx 300 -steps 30 -ms 2000 -hold 0
**   -hold secs   wait that long after the release (for a screenshot)
**
** Exit status: 0 moved, 1 did not move, 2 could not run.
**
** The window manager grabs the server for an outline move as soon as the
** button goes down; XTestGrabControl(dpy, True) makes this client impervious
** to that grab, or its next fake motion would never be processed and the
** release never seen (the whole server deadlocks -- A4000T, 2026-09-27).
** stdout is unbuffered so the printed lines survive a hang.
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

static Window
find(dpy, w, name)
Display *dpy;
Window   w;
char    *name;
{
    Window   root, parent, *kids = 0, hit = 0;
    unsigned int n, i;
    char    *wname = 0;

    if (XFetchName(dpy, w, &wname) && wname) {
        int same = !strcmp(wname, name);
        XFree(wname);
        if (same)
            return w;
    }
    if (!XQueryTree(dpy, w, &root, &parent, &kids, &n))
        return 0;
    for (i = 0; i < n && !hit; i++)
        hit = find(dpy, kids[i], name);
    if (kids)
        XFree((char *) kids);
    return hit;
}

/* The ancestor of w that is a direct child of the root: the WM frame. */
static Window
frame_of(dpy, w)
Display *dpy;
Window   w;
{
    Window root, parent, *kids;
    unsigned int n;

    for (;;) {
        if (!XQueryTree(dpy, w, &root, &parent, &kids, &n))
            return w;
        if (kids)
            XFree((char *) kids);
        if (parent == root || parent == 0)
            return w;
        w = parent;
    }
}

static void
origin(dpy, w, x, y)
Display *dpy;
Window   w;
int     *x, *y;
{
    Window child;

    XTranslateCoordinates(dpy, w, DefaultRootWindow(dpy), 0, 0, x, y, &child);
}

static void
pause_ms(ms)
int ms;
{
    struct timeval tv;

    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000L;
    select(0, (fd_set *) 0, (fd_set *) 0, (fd_set *) 0, &tv);
}

int
main(argc, argv)
int   argc;
char *argv[];
{
    Display     *dpy;
    Window       client, frame;
    XWindowAttributes ca;
    char        *name = 0;
    int          dx = 300, steps = 30, ms = 2000, hold = 0;
    int          ev, er, maj, min;
    int          cx, cy, fx, fy, fx2, fy2, px, py, i, scr;

    setvbuf(stdout, (char *) 0, _IONBF, 0);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-dx") && i + 1 < argc)
            dx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-steps") && i + 1 < argc)
            steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-ms") && i + 1 < argc)
            ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-hold") && i + 1 < argc)
            hold = atoi(argv[++i]);
        else if (argv[i][0] != '-' && !name)
            name = argv[i];
        else {
            fprintf(stderr,
                "usage: xdrag [-dx N] [-steps N] [-ms N] [-hold secs] name\n");
            return 2;
        }
    }
    if (!name || steps < 1) {
        fprintf(stderr,
            "usage: xdrag [-dx N] [-steps N] [-ms N] [-hold secs] name\n");
        return 2;
    }

    dpy = XOpenDisplay((char *) 0);
    if (!dpy) {
        fprintf(stderr, "xdrag: cannot open display\n");
        return 2;
    }
    scr = DefaultScreen(dpy);
    if (!XTestQueryExtension(dpy, &ev, &er, &maj, &min)) {
        fprintf(stderr, "xdrag: the server has no XTEST extension\n");
        return 2;
    }
    XTestGrabControl(dpy, True);    /* keep working through the WM's grab */

    client = find(dpy, DefaultRootWindow(dpy), name);
    if (!client) {
        fprintf(stderr, "xdrag: no window named \"%s\"\n", name);
        return 2;
    }
    frame = frame_of(dpy, client);
    XGetWindowAttributes(dpy, client, &ca);
    origin(dpy, client, &cx, &cy);
    origin(dpy, frame, &fx, &fy);
    if (frame == client || cy - fy < 4) {
        fprintf(stderr, "xdrag: \"%s\" has no title bar above it (frame %s)\n",
                name, frame == client ? "is the window itself" : "too thin");
        return 2;
    }

    px = cx + ca.width / 2;         /* middle of the title area */
    py = fy + (cy - fy) / 2;
    printf("xdrag: XTEST %d.%d; \"%s\" frame at %d,%d, client at %d,%d %dx%d\n",
           maj, min, name, fx, fy, cx, cy, ca.width, ca.height);
    printf("xdrag: press at %d,%d, move %d px right in %d steps over %d ms\n",
           px, py, dx, steps, ms);
    fflush(stdout);

    XTestFakeMotionEvent(dpy, scr, px, py, CurrentTime);
    XSync(dpy, False);
    pause_ms(200);
    XTestFakeButtonEvent(dpy, 1, True, CurrentTime);
    XSync(dpy, False);
    pause_ms(200);
    for (i = 1; i <= steps; i++) {
        XTestFakeMotionEvent(dpy, scr, px + (dx * i) / steps, py, CurrentTime);
        XSync(dpy, False);
        pause_ms(ms / steps);
    }
    XTestFakeButtonEvent(dpy, 1, False, CurrentTime);
    XSync(dpy, False);
    pause_ms(1000);                 /* let the window manager finish the move */

    origin(dpy, frame, &fx2, &fy2);
    printf("xdrag: released; frame now at %d,%d (moved %d,%d)\n",
           fx2, fy2, fx2 - fx, fy2 - fy);
    fflush(stdout);

    if (hold > 0)
        sleep(hold);
    XCloseDisplay(dpy);
    return (fx2 != fx || fy2 != fy) ? 0 : 1;
}
