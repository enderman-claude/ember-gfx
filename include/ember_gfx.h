/* ember-gfx: a small immediate-mode 2D graphics stack.
 *
 * Every function takes and returns int64_t so Ember's i64 values pass straight
 * through `extern def`. Colours are 0xRRGGBB. Drawing goes to an in-memory
 * back buffer, and gfx_present() copies only the changed region to the screen.
 *
 * Backends (chosen by gfx_open):
 *   x11   Linux with an X server (DISPLAY set). Uses MIT-SHM when available.
 *   win32 Windows. Uses StretchDIBits on the whole canvas.
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

/* Mouse input. Coordinates are canvas pixels, and may be negative or past the edge
 * when the pointer is outside the window. Buttons: 0 = left, 1 = right, 2 = middle.
 *
 * Presses are queued. gfx_poll_click() takes the oldest press and makes it the
 * "current click", which gfx_click_button/x/y then describe. The queue holds 64
 * presses; any further presses are dropped until the program polls. Input is read
 * during gfx_present, so poll after presenting. */
int64_t gfx_mouse_x(void);
int64_t gfx_mouse_y(void);
/* 1 while the button is held down, 0 otherwise. */
int64_t gfx_mouse_down(int64_t button);
/* 1 if a press was taken from the queue, 0 if the queue was empty. */
int64_t gfx_poll_click(void);
int64_t gfx_click_button(void);
int64_t gfx_click_x(void);
int64_t gfx_click_y(void);

/* Keyboard input. Keys use Win32 virtual-key codes, so they are the same on every
 * backend: letters and digits are their uppercase ASCII codes ('A' = 65, '0' = 48).
 * Other keys: Backspace 8, Tab 9, Enter 13, Shift 16, Ctrl 17, Alt 18, Esc 27,
 * Space 32, Left 37, Up 38, Right 39, Down 40.
 *
 * gfx_key_down(key) is 1 while the key is held. Presses and releases are queued
 * like clicks: gfx_poll_key() takes the oldest event and makes it current, then
 * gfx_key_code() and gfx_key_pressed() (1 = press, 0 = release) describe it. Holding
 * a key does not queue repeats. The queue holds 64 events. */
int64_t gfx_key_down(int64_t key);
int64_t gfx_poll_key(void);
int64_t gfx_key_code(void);
int64_t gfx_key_pressed(void);

/* Close button. Each click on the close button (or Alt+F4) is an event:
 * gfx_close_requested() returns 1 once for each such request, then clears it.
 * By default the window closes on its own, and gfx_closed() becomes 1.
 * gfx_set_close_blocked(1) stops the window closing when the button is pressed, so the
 * program can ask the user first and then call gfx_close() itself. gfx_closed() stays 0
 * until gfx_close() is called. */
int64_t gfx_close_requested(void);
void gfx_set_close_blocked(int64_t blocked);

/* Shows everything drawn since the last present, and processes window events. */
void gfx_present(void);
/* 1 once the window has been closed, by the user or by gfx_close(). */
int64_t gfx_closed(void);
/* Total pixels sent to the screen by gfx_present. Used to test the dirty-rectangle logic. */
int64_t gfx_pixels_presented(void);

void gfx_sleep_ms(int64_t ms);

#endif
