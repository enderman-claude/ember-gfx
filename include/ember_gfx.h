/* ember-gfx: a small immediate-mode 2D graphics stack.
 *
 * Every function takes and returns int64_t so Ember's i64 values pass straight
 * through `extern def`. Colours are 0xRRGGBB. Drawing goes to an in-memory
 * back buffer, and gfx_present() copies only the changed region to the screen.
 *
 * Backends (chosen by gfx_open):
 *   x11   Linux with an X server (DISPLAY set). Uses MIT-SHM when available.
 *   win32 Windows. Uses StretchDIBits on the dirty region.
 *   null  No screen. Used when nothing else is available, and by the tests.
 *         Force it with the environment variable GFX_BACKEND=null.
 *
 * Coordinates are in pixels, with (0,0) at the top left. Shapes are clipped to the
 * canvas, so drawing partly off-screen is safe.
 */
#ifndef EMBER_GFX_H
#define EMBER_GFX_H

#include <stdint.h>

/* Opens a w x h canvas and a window (or the null backend). Returns 1 on success. */
int64_t gfx_open(int64_t width, int64_t height);
/* Closes the window and frees the canvas. */
void gfx_close(void);

int64_t gfx_width(void);
int64_t gfx_height(void);
/* Name of the active backend: "x11-shm", "x11-xput", "win32" or "null". */
const char *gfx_backend_name(void);

/* Current drawing colour, used by the shape functions below. */
void gfx_color(int64_t rgb);
int64_t gfx_get_color(void);

/* Fills the whole canvas with rgb. */
void gfx_clear(int64_t rgb);
/* Sets one pixel to the current colour. */
void gfx_pixel(int64_t x, int64_t y);
/* Filled rectangle with top-left corner (x, y), size w x h. */
void gfx_rect(int64_t x, int64_t y, int64_t w, int64_t h);
/* Filled circle, centre (cx, cy), radius r. Pixels at distance exactly r are filled. */
void gfx_circle(int64_t cx, int64_t cy, int64_t r);
/* One-pixel line from (x0, y0) to (x1, y1), both ends included. */
void gfx_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1);

/* Reads back a pixel. Returns -1 if (x, y) is outside the canvas. */
int64_t gfx_get_pixel(int64_t x, int64_t y);

/* Shows everything drawn since the last present, and processes window events. */
void gfx_present(void);
/* 1 once the user has closed the window. */
int64_t gfx_closed(void);
/* Total pixels sent to the screen by gfx_present. Used to test the dirty-rectangle logic. */
int64_t gfx_pixels_presented(void);

void gfx_sleep_ms(int64_t ms);

#endif
