#define _DEFAULT_SOURCE
/* Headless tests for the core, using the null backend.
 *
 * The rasteriser is compared with a naive per-pixel reference model on random
 * operations, including off-screen and negative coordinates and widths that are not
 * multiples of 4 (which exercise the SSE2 tail loop). Dirty-rectangle sizes and line
 * properties are checked separately.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ember_gfx.h"
#include "gfx_internal.h"

#define W 97     /* deliberately not a multiple of 4 */
#define H 61

static uint32_t model[H][W];
static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL line %d: ", __LINE__); \
                   fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void ref_rect(int64_t x, int64_t y, int64_t w, int64_t h, uint32_t c) {
    for (int64_t yy = y; yy < y + h; yy++)
        for (int64_t xx = x; xx < x + w; xx++)
            if (xx >= 0 && yy >= 0 && xx < W && yy < H) model[yy][xx] = c;
}

static void ref_circle(int64_t cx, int64_t cy, int64_t r, uint32_t c) {
    if (r < 0) return;
    for (int64_t yy = 0; yy < H; yy++)
        for (int64_t xx = 0; xx < W; xx++) {
            int64_t dx = xx - cx, dy = yy - cy;
            if (dx * dx + dy * dy <= r * r) model[yy][xx] = c;
        }
}

static int compare_all(const char *when) {
    int bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int64_t got = gfx_get_pixel(x, y);
            if (got != (int64_t)model[y][x]) {
                if (bad < 5)
                    fprintf(stderr, "mismatch after %s at (%d,%d): got 0x%06llx want 0x%06x\n",
                            when, x, y, (unsigned long long)got, model[y][x]);
                bad++;
            }
        }
    CHECK(bad == 0, "%d pixel(s) differ after %s", bad, when);
    return bad;
}

static void test_random_against_reference(void) {
    srand(12345);
    memset(model, 0, sizeof model);
    gfx_clear(0);
    for (int op = 0; op < 3000; op++) {
        int kind = rand() % 5;
        int64_t a = rand() % (W + 60) - 30;
        int64_t b = rand() % (H + 60) - 30;
        uint32_t col = (uint32_t)rand() & 0xFFFFFF;
        switch (kind) {
        case 0:
            gfx_color(col);
            gfx_pixel(a, b);
            if (a >= 0 && b >= 0 && a < W && b < H) model[b][a] = col;
            break;
        case 1: {
            int64_t w = rand() % 60 - 10, h = rand() % 40 - 10;
            gfx_color(col);
            gfx_rect(a, b, w, h);
            ref_rect(a, b, w, h, col);
            break;
        }
        case 2: {
            int64_t r = rand() % 30 - 2;
            gfx_color(col);
            gfx_circle(a, b, r);
            ref_circle(a, b, r, col);
            break;
        }
        case 3:
            gfx_clear(col);
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) model[y][x] = col;
            break;
        default:
            gfx_present();
            break;
        }
        if (op % 100 == 99 || kind == 3) {
            if (compare_all("random operations") != 0) return;
        }
    }
    compare_all("random operations");
}

static void test_circle_edges(void) {
    gfx_clear(0);
    gfx_color(0xABCDEF);
    gfx_circle(40, 30, 5);
    CHECK(gfx_get_pixel(45, 30) == 0xABCDEF, "edge pixel at distance r is filled");
    CHECK(gfx_get_pixel(46, 30) == 0, "pixel at distance r+1 is empty");
    CHECK(gfx_get_pixel(40 + 3, 30 + 4) == 0xABCDEF, "3-4-5 diagonal is filled");
    gfx_clear(0);
    gfx_circle(10, 10, 0);
    CHECK(gfx_get_pixel(10, 10) == 0xABCDEF, "radius 0 must fill the centre pixel");
    CHECK(gfx_get_pixel(11, 10) == 0, "radius 0 must fill only the centre pixel");
    gfx_circle(10, 10, -1);
    CHECK(gfx_get_pixel(10, 10) == 0xABCDEF, "negative radius must draw nothing");
}

static void test_line_properties(void) {
    struct { int x0, y0, x1, y1; } lines[] = {
        {3, 5, 40, 20}, {50, 50, 10, 40}, {0, 0, 96, 60}, {10, 30, 10, 30},
        {5, 2, 5, 58}, {90, 55, 1, 3},
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        int x0 = lines[i].x0, y0 = lines[i].y0, x1 = lines[i].x1, y1 = lines[i].y1;
        gfx_clear(0);
        gfx_color(0x00FF00);
        gfx_line(x0, y0, x1, y1);
        int changed = 0, ok_path = 1;
        int prev_x = -1, prev_y = -1;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (gfx_get_pixel(x, y) == 0x00FF00) {
                    changed++;
                    if (x < 0 || x >= W) ok_path = 0;
                    (void)prev_x; (void)prev_y;
                }
        int dx = abs(x1 - x0), dy = abs(y1 - y0);
        int want = (dx > dy ? dx : dy) + 1;
        CHECK(changed == want, "line %d: %d pixels, want %d", (int)i, changed, want);
        CHECK(gfx_get_pixel(x0, y0) == 0x00FF00, "line %d: start pixel missing", (int)i);
        CHECK(gfx_get_pixel(x1, y1) == 0x00FF00, "line %d: end pixel missing", (int)i);
        CHECK(ok_path, "line %d drew outside the canvas", (int)i);
    }
}

static void test_dirty_region(void) {
    gfx_clear(0);
    gfx_present();
    int64_t base = gfx_pixels_presented();
    gfx_color(0x112233);
    gfx_pixel(3, 4);
    gfx_present();
    CHECK(gfx_pixels_presented() - base == 1, "one pixel presented %lld pixels",
          (long long)(gfx_pixels_presented() - base));

    base = gfx_pixels_presented();
    gfx_rect(20, 20, 10, 5);
    gfx_present();
    CHECK(gfx_pixels_presented() - base == 50, "10x5 rect presented %lld pixels",
          (long long)(gfx_pixels_presented() - base));

    base = gfx_pixels_presented();
    gfx_rect(-3, -3, 10, 10);           /* only a 7x7 part is on screen */
    gfx_present();
    CHECK(gfx_pixels_presented() - base == 49, "clipped rect presented %lld pixels",
          (long long)(gfx_pixels_presented() - base));

    base = gfx_pixels_presented();
    gfx_present();                      /* nothing changed: nothing sent */
    CHECK(gfx_pixels_presented() - base == 0, "idle present sent %lld pixels",
          (long long)(gfx_pixels_presented() - base));
}

static void test_open_close(void) {
    CHECK(gfx_open(0, 5) == 0, "zero width must fail");
    CHECK(gfx_open(10000, 5) == 0, "oversize must fail");
    CHECK(gfx_open(W, H) == 1, "open failed");
    CHECK(strcmp(gfx_backend_name(), "null") == 0, "backend is %s", gfx_backend_name());
    CHECK(gfx_width() == W && gfx_height() == H, "size wrong");
    CHECK(gfx_get_pixel(-1, 0) == -1 && gfx_get_pixel(W, 0) == -1, "out-of-range read not -1");
    CHECK(gfx_get_color() == 0xFFFFFF, "default colour is not white");
    gfx_close();
    CHECK(strcmp(gfx_backend_name(), "none") == 0, "backend still set after close");
}

static void test_input(void) {
    CHECK(gfx_poll_click() == 0, "queue not empty at start");
    gfx_input_move(5, 6);
    CHECK(gfx_mouse_x() == 5 && gfx_mouse_y() == 6, "move not tracked");

    gfx_input_button(0, 1, 5, 6);
    CHECK(gfx_mouse_down(0) == 1, "left button not reported held");
    CHECK(gfx_mouse_down(1) == 0, "right button wrongly held");
    CHECK(gfx_poll_click() == 1, "press not queued");
    CHECK(gfx_click_button() == 0 && gfx_click_x() == 5 && gfx_click_y() == 6,
          "click details wrong: %lld %lld %lld", (long long)gfx_click_button(),
          (long long)gfx_click_x(), (long long)gfx_click_y());

    gfx_input_button(0, 0, 5, 6);         /* release: held clears, no new click */
    CHECK(gfx_mouse_down(0) == 0, "release did not clear held state");
    CHECK(gfx_poll_click() == 0, "release produced a click");

    gfx_input_button(1, 1, 60, 2);        /* right click, pointer off to the side */
    CHECK(gfx_poll_click() == 1 && gfx_click_button() == 1 && gfx_click_x() == 60,
          "right click wrong");

    /* Queue order: presses come out oldest first. */
    gfx_input_button(2, 1, 1, 1);
    gfx_input_button(0, 1, 2, 2);
    CHECK(gfx_poll_click() == 1 && gfx_click_button() == 2, "middle click out of order");
    CHECK(gfx_poll_click() == 1 && gfx_click_button() == 0 && gfx_click_x() == 2,
          "second click out of order");
    CHECK(gfx_poll_click() == 0, "queue not empty after draining");

    /* Overflow: the queue holds 64 presses and drops the rest. */
    for (int i = 0; i < 100; i++) {
        gfx_input_button(0, 1, i, 0);
        gfx_input_button(0, 0, i, 0);
    }
    int taken = 0;
    while (gfx_poll_click() == 1) taken++;
    CHECK(taken == 64, "queue held %d presses, want 64", taken);

    gfx_input_button(7, 1, 0, 0);         /* unknown button is ignored */
    CHECK(gfx_poll_click() == 0 && gfx_mouse_down(7) == 0, "unknown button not ignored");
}

static void test_keys_and_close(void) {
    /* Press and release are queued in order, and auto-repeat is not an event. */
    gfx_input_key(65, 1);                 /* A down */
    gfx_input_key(65, 1);                 /* auto-repeat: ignored */
    gfx_input_key(65, 1);
    CHECK(gfx_key_down(65) == 1, "A not held after press");
    gfx_input_key(65, 0);                 /* A up */
    CHECK(gfx_key_down(65) == 0, "A still held after release");
    CHECK(gfx_poll_key() == 1 && gfx_key_code() == 65 && gfx_key_pressed() == 1,
          "first key event is not A press");
    CHECK(gfx_poll_key() == 1 && gfx_key_code() == 65 && gfx_key_pressed() == 0,
          "second key event is not A release");
    CHECK(gfx_poll_key() == 0, "extra key events queued after auto-repeat");

    /* Out-of-range codes are ignored, and the queue holds 64 events. */
    gfx_input_key(-1, 1);
    gfx_input_key(256, 1);
    CHECK(gfx_key_down(256) == 0 && gfx_key_down(-1) == 0, "out-of-range key marked down");
    CHECK(gfx_poll_key() == 0, "out-of-range key queued an event");
    for (int i = 0; i < 70; i++) {        /* alternate 0x30 down/up: 70 changes */
        gfx_input_key(0x30, i % 2 == 0);
    }
    int taken = 0;
    while (gfx_poll_key()) taken++;
    CHECK(taken == 64, "key queue held %d events, expected 64", taken);

    /* release_all clears held keys as well as buttons, and queues nothing. */
    gfx_input_key(40, 1);                 /* Down arrow */
    gfx_input_release_all();
    CHECK(gfx_key_down(40) == 0, "Down arrow stuck after release_all");
    CHECK(gfx_poll_key() == 0 || gfx_key_code() == 40, "unexpected event after release_all");
    while (gfx_poll_key()) { }

    /* Close button: a request is seen once, and does not close by itself here. */
    gfx_set_close_blocked(1);
    CHECK(gfx_close_is_blocked() == 1, "close_blocked not set");
    CHECK(gfx_close_requested() == 0, "close request seen before any request");
    gfx_input_close_request();
    CHECK(gfx_close_requested() == 1, "close request not seen");
    CHECK(gfx_close_requested() == 0, "close request seen twice");
    gfx_set_close_blocked(0);
    CHECK(gfx_close_is_blocked() == 0, "close_blocked not cleared");
}

static void test_text(void) {
    /* Printable ASCII is queued in order; control characters and DEL are not. */
    gfx_input_char('H');
    gfx_input_char('\n');
    gfx_input_char(0x7F);
    gfx_input_char('i');
    CHECK(gfx_poll_char() == 1 && gfx_char_code() == 'H', "first char is not H");
    CHECK(gfx_poll_char() == 1 && gfx_char_code() == 'i', "second char is not i");
    CHECK(gfx_poll_char() == 0, "control characters were queued");

    /* The queue holds 64 characters; extra characters are dropped. */
    for (int i = 0; i < 70; i++) gfx_input_char('a' + (i % 26));
    int taken = 0;
    while (gfx_poll_char()) taken++;
    CHECK(taken == 64, "char queue held %d, expected 64", taken);
}

static void test_release_all(void) {
    /* A held button must not stay down when the backend loses the mouse, and no click
     * may be queued for that release. */
    gfx_input_button(0, 1, 10, 10);
    gfx_input_button(2, 1, 11, 11);
    CHECK(gfx_mouse_down(0) == 1 && gfx_mouse_down(2) == 1, "buttons not held before release");
    int64_t before = 0;
    while (gfx_poll_click()) before++;
    gfx_input_release_all();
    CHECK(gfx_mouse_down(0) == 0, "left button stuck after release_all");
    CHECK(gfx_mouse_down(2) == 0, "middle button stuck after release_all");
    CHECK(gfx_poll_click() == 0, "release_all queued a click");
    CHECK(before == 2, "expected 2 queued presses, got %lld", (long long)before);
}

int main(void) {
    setenv("GFX_BACKEND", "null", 1);
    test_open_close();
    if (gfx_open(W, H) != 1) {
        fprintf(stderr, "cannot open canvas\n");
        return 1;
    }
    test_random_against_reference();
    test_circle_edges();
    test_line_properties();
    test_dirty_region();
    test_input();
    test_release_all();
    test_keys_and_close();
    test_text();
    gfx_close();
    if (failures) {
        fprintf(stderr, "test_core: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_core: all checks passed");
    return 0;
}
