/* Headless backend: keeps the canvas in memory and never touches a screen. */
#include "gfx_internal.h"

static void null_present(const uint32_t *fb, int64_t fb_width, int64_t x, int64_t y,
                         int64_t w, int64_t h) {
    (void)fb; (void)fb_width; (void)x; (void)y; (void)w; (void)h;
}

static int null_pump(void) {
    return 0;
}

static void null_close(void) {
}

int gfx_null_open(gfx_backend *be, int64_t w, int64_t h) {
    (void)w; (void)h;
    be->name = "null";
    be->present = null_present;
    be->pump = null_pump;
    be->close = null_close;
    return 1;
}
