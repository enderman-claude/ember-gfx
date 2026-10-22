/* End-to-end helper: sends real pointer clicks to the running click_test program,
 * then reads the screen back and checks that each click left a coloured dot. */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

static int click(Display *d, int x, int y, unsigned button) {
    XTestFakeMotionEvent(d, -1, x, y, CurrentTime);
    XTestFakeButtonEvent(d, button, True, CurrentTime);
    XTestFakeButtonEvent(d, button, False, CurrentTime);
    XFlush(d);
    usleep(150000);
    return 0;
}

static int check(Display *d, int x, int y, unsigned long want, const char *what) {
    XImage *img = XGetImage(d, DefaultRootWindow(d), x, y, 1, 1, AllPlanes, ZPixmap);
    if (!img) { printf("FAIL %s: no image\n", what); return 1; }
    unsigned long got = XGetPixel(img, 0, 0);
    XDestroyImage(img);
    if (got != want) {
        printf("FAIL %s at (%d,%d): got 0x%06lx, want 0x%06lx\n", what, x, y, got, want);
        return 1;
    }
    printf("ok   %s at (%d,%d) = 0x%06lx\n", what, x, y, got);
    return 0;
}

int main(void) {
    Display *d = XOpenDisplay(NULL);
    if (!d) return 2;
    usleep(800000);                     /* let the program open its window */
    click(d, 100, 100, 1);              /* left: red dot */
    click(d, 300, 200, 3);              /* right: blue dot */
    click(d, 400, 60, 2);               /* middle: green dot */
    usleep(400000);
    int fails = 0;
    fails += check(d, 108, 100, 0xFF4040, "left-click dot");
    fails += check(d, 308, 200, 0x4080FF, "right-click dot");
    fails += check(d, 408, 60, 0x40FF80, "middle-click dot");
    /* Bars: each click adds 4 px, starting at x=10. One left click covers x=10..13. */
    fails += check(d, 12, 328, 0xFF4040, "left-count bar");
    XCloseDisplay(d);
    return fails ? 1 : 0;
}
