/* Internal interface between the core and its backends. Not part of the public API. */
#ifndef GFX_INTERNAL_H
#define GFX_INTERNAL_H

#include <stdint.h>
#include "ember_gfx.h"

typedef struct gfx_backend {
    const char *name;
    /* Copies the rectangle (x, y, w, h) of fb to the screen. fb holds 0x00RRGGBB
     * pixels, fb_width pixels per row. The rectangle is already clipped. */
    void (*present)(const uint32_t *fb, int64_t fb_width, int64_t x, int64_t y, int64_t w, int64_t h);
    /* Handles pending window events. Returns 1 once the window has been closed. */
    int (*pump)(void);
    void (*close)(void);
} gfx_backend;

/* Backend constructors. Each returns 1 on success and fills in *be. */
int gfx_null_open(gfx_backend *be, int64_t w, int64_t h);
int gfx_x11_open(gfx_backend *be, int64_t w, int64_t h);
int gfx_win32_open(gfx_backend *be, int64_t w, int64_t h);
/* Stub until tesseraOS has a framebuffer. Always returns 0 for now. */
int gfx_tesseraos_open(gfx_backend *be, int64_t w, int64_t h);

/* X11 window id, for tests that send it events. 0 when X11 is not active. */
unsigned long gfx_x11_window_id(void);

/* Input entry points. Backends call these from their event handling.
 * button: 0 = left, 1 = right, 2 = middle. Coordinates are canvas pixels. */
void gfx_input_move(int64_t x, int64_t y);
void gfx_input_button(int button, int pressed, int64_t x, int64_t y);
/* Backends call this when they lose the mouse or focus, so no button stays held. */
void gfx_input_release_all(void);

#endif
