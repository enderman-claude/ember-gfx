/* Tests the X11 backend against a real X server. Run under xvfb-run.
 *
 * The window is placed at (0,0) on the root window, and nothing else is on the screen.
 * A second connection reads the root back with XGetImage, so the test checks what
 * actually reached the screen, not what the library thinks it sent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include "ember_gfx.h"
#include "gfx_internal.h"

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL line %d: ", __LINE__); \
                   fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* Pixel value X should store for an 0xRRGGBB colour on a visual with these masks. */
static unsigned long expected_pixel(uint32_t rgb, unsigned long rm, unsigned long gm, unsigned long bm) {
    unsigned long out = 0;
    unsigned long masks[3] = {rm, gm, bm};
    uint32_t chans[3] = {(rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF};
    for (int i = 0; i < 3; i++) {
        unsigned long mask = masks[i];
        if (mask == 0) continue;
        int shift = 0;
        while (!((mask >> shift) & 1UL)) shift++;
        int bits = 0;
        for (unsigned long m = mask >> shift; m & 1UL; m >>= 1) bits++;
        unsigned long maxv = (1UL << bits) - 1;
        out |= ((chans[i] * maxv + 127) / 255) << shift;
    }
    return out;
}

int main(void) {
    if (getenv("DISPLAY") == NULL) {
        puts("test_x11: skipped (no DISPLAY)");
        return 0;
    }
    unsigned w = 64, h = 48;
    CHECK(gfx_open(w, h) == 1, "gfx_open failed");
    const char *name = gfx_backend_name();
    const char *want = getenv("GFX_X11_NO_SHM") ? "x11-xput" : "x11-shm";
    CHECK(strcmp(name, want) == 0, "backend is %s, expected %s", name, want);
    printf("backend: %s\n", name);
    {
        Display *probe = XOpenDisplay(NULL);
        if (probe != NULL) {
            int ps = DefaultScreen(probe);
            Visual *pv = DefaultVisual(probe, ps);
            printf("visual: depth %d, masks r=0x%lx g=0x%lx b=0x%lx\n", DefaultDepth(probe, ps),
                   pv->red_mask, pv->green_mask, pv->blue_mask);
            XCloseDisplay(probe);
        }
    }

    gfx_clear(0x000000);
    gfx_color(0xFF0000); gfx_rect(0, 0, 16, 16);
    gfx_color(0x00FF00); gfx_rect(16, 0, 16, 16);
    gfx_color(0x0000FF); gfx_rect(32, 0, 16, 16);
    gfx_color(0xFFFFFF); gfx_pixel(63, 47);
    gfx_present();
    gfx_sleep_ms(300);

    Display *reader = XOpenDisplay(NULL);
    CHECK(reader != NULL, "second display connection failed");
    if (reader == NULL) return 1;
    int scr = DefaultScreen(reader);
    Visual *vis = DefaultVisual(reader, scr);
    unsigned long rm = vis->red_mask, gm = vis->green_mask, bm = vis->blue_mask;
    XImage *shot = XGetImage(reader, RootWindow(reader, scr), 0, 0, w, h, AllPlanes, ZPixmap);
    CHECK(shot != NULL, "XGetImage failed");
    if (shot == NULL) return 1;

    struct { int x, y; uint32_t rgb; const char *what; } probes[] = {
        {8, 8, 0xFF0000, "red block"},
        {24, 8, 0x00FF00, "green block"},
        {40, 8, 0x0000FF, "blue block"},
        {60, 40, 0x000000, "background"},
        {63, 47, 0xFFFFFF, "white corner pixel"},
    };
    for (size_t i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        unsigned long got = XGetPixel(shot, probes[i].x, probes[i].y);
        unsigned long want_px = expected_pixel(probes[i].rgb, rm, gm, bm);
        CHECK(got == want_px, "%s at (%d,%d): got 0x%lx, want 0x%lx",
              probes[i].what, probes[i].x, probes[i].y, got, want_px);
    }

    /* A partial update must leave the rest of the picture alone. */
    gfx_color(0xFFFF00); gfx_rect(0, 0, 4, 4);
    gfx_present();
    gfx_sleep_ms(300);
    XImage *shot2 = XGetImage(reader, RootWindow(reader, scr), 0, 0, w, h, AllPlanes, ZPixmap);
    CHECK(shot2 != NULL, "second XGetImage failed");
    if (shot2 != NULL) {
        CHECK(XGetPixel(shot2, 1, 1) == expected_pixel(0xFFFF00, rm, gm, bm), "updated block missing");
        CHECK(XGetPixel(shot2, 24, 8) == expected_pixel(0x00FF00, rm, gm, bm), "untouched block changed");
        XDestroyImage(shot2);
    }
    CHECK(gfx_pixels_presented() == (int64_t)w * h + 16, "dirty region size %lld",
          (long long)gfx_pixels_presented());

    /* Closing the window (WM_DELETE_WINDOW from another client) must be reported. */
    CHECK(gfx_closed() == 0, "closed before the close request");
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = (Window)gfx_x11_window_id();
    ev.xclient.message_type = XInternAtom(reader, "WM_PROTOCOLS", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)XInternAtom(reader, "WM_DELETE_WINDOW", False);
    XSendEvent(reader, (Window)gfx_x11_window_id(), False, NoEventMask, &ev);
    XFlush(reader);
    gfx_sleep_ms(200);
    gfx_present();
    CHECK(gfx_closed() == 1, "WM_DELETE_WINDOW not seen");

    /* Real pointer input through the XTest extension. The window is at (0,0). */
    XTestFakeMotionEvent(reader, -1, 20, 30, CurrentTime);
    XTestFakeButtonEvent(reader, 1, True, CurrentTime);
    XTestFakeButtonEvent(reader, 1, False, CurrentTime);
    XTestFakeMotionEvent(reader, -1, 40, 10, CurrentTime);
    XTestFakeButtonEvent(reader, 3, True, CurrentTime);
    XTestFakeButtonEvent(reader, 3, False, CurrentTime);
    XTestFakeMotionEvent(reader, -1, 200, 200, CurrentTime);      /* outside the window */
    XTestFakeButtonEvent(reader, 1, True, CurrentTime);
    XTestFakeButtonEvent(reader, 1, False, CurrentTime);
    XFlush(reader);
    gfx_sleep_ms(300);
    gfx_present();
    gfx_present();

    CHECK(gfx_poll_click() == 1, "left click inside the window not received");
    CHECK(gfx_click_button() == 0 && gfx_click_x() == 20 && gfx_click_y() == 30,
          "left click at (%lld,%lld) button %lld, want (20,30) button 0",
          (long long)gfx_click_x(), (long long)gfx_click_y(), (long long)gfx_click_button());
    CHECK(gfx_poll_click() == 1, "right click not received");
    CHECK(gfx_click_button() == 1 && gfx_click_x() == 40 && gfx_click_y() == 10,
          "right click at (%lld,%lld) button %lld, want (40,10) button 1",
          (long long)gfx_click_x(), (long long)gfx_click_y(), (long long)gfx_click_button());
    CHECK(gfx_poll_click() == 0, "click outside the window was received");
    CHECK(gfx_mouse_x() == 40 && gfx_mouse_y() == 10, "mouse position %lld,%lld, want 40,10",
          (long long)gfx_mouse_x(), (long long)gfx_mouse_y());

    /* Typed text through XTest. Keystrokes go to the window under the pointer (there is
     * no window manager here), so move the pointer inside the window first. */
    XTestFakeMotionEvent(reader, -1, 20, 20, CurrentTime);
    XFlush(reader);
    gfx_sleep_ms(100);
    gfx_present();
    KeyCode kshift = XKeysymToKeycode(reader, XK_Shift_L);
    KeyCode ka = XKeysymToKeycode(reader, XK_a);
    KeyCode kb = XKeysymToKeycode(reader, XK_b);
    XTestFakeKeyEvent(reader, kshift, True, CurrentTime);
    XTestFakeKeyEvent(reader, ka, True, CurrentTime);
    XTestFakeKeyEvent(reader, ka, False, CurrentTime);
    XTestFakeKeyEvent(reader, kshift, False, CurrentTime);
    XTestFakeKeyEvent(reader, kb, True, CurrentTime);
    XTestFakeKeyEvent(reader, kb, False, CurrentTime);
    XFlush(reader);
    gfx_sleep_ms(300);
    gfx_present();
    gfx_present();
    CHECK(gfx_poll_char() == 1 && gfx_char_code() == 'A', "Shift+a did not type 'A'");
    CHECK(gfx_poll_char() == 1 && gfx_char_code() == 'b', "b did not type 'b'");
    CHECK(gfx_poll_char() == 0, "extra characters were typed");

    /* Covering the window must not lose the picture: the backend redraws on Expose.
     * The server discards the contents while another window covers them. */
    gfx_clear(0x123456);
    gfx_present();
    gfx_sleep_ms(100);
    Window cover = XCreateSimpleWindow(reader, RootWindow(reader, scr), 0, 0, w, h, 0, 0, 0);
    XMapRaised(reader, cover);
    XSync(reader, False);
    gfx_sleep_ms(100);
    gfx_present();
    XDestroyWindow(reader, cover);
    XSync(reader, False);
    gfx_sleep_ms(200);
    gfx_present();
    gfx_sleep_ms(100);
    gfx_present();
    XImage *shot3 = XGetImage(reader, RootWindow(reader, scr), 0, 0, w, h, AllPlanes, ZPixmap);
    if (shot3 != NULL) {
        unsigned long got = XGetPixel(shot3, 8, 8);
        unsigned long want_px = expected_pixel(0x123456, rm, gm, bm);
        CHECK(got == want_px, "picture lost after being covered: got 0x%lx, want 0x%lx", got, want_px);
        XDestroyImage(shot3);
    }

    XDestroyImage(shot);
    XCloseDisplay(reader);
    gfx_close();
    if (failures) {
        fprintf(stderr, "test_x11: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_x11: all checks passed");
    return 0;
}
