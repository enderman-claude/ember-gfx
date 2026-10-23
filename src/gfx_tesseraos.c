/* tesseraOS backend: stub.
 *
 * tesseraOS does not boot yet (see enderman-claude/tesseraOS), so there is no display
 * to draw on. This file fixes the shape of the backend so the core and the build can
 * depend on it now. gfx_tesseraos_open() returns 0, which makes the core fall back to
 * the headless backend, so programs keep running and the tests stay the same.
 *
 * Planned implementation, once tessera provides a framebuffer after boot (GOP from the
 * UEFI loader, handed over in the boot info):
 *   - open:    take the framebuffer address, pitch, and pixel format from boot info,
 *              then map it. Return 1 only if the format is 32-bit 0x00RRGGBB-compatible
 *              or can be converted.
 *   - present: copy the dirty rectangle row by row into the mapped framebuffer.
 *   - pump:    read input from the kernel's input queue through gfx_input_move and
 *              gfx_input_button. Return 1 once the program asks to quit.
 *   - close:   unmap the framebuffer.
 */
#include "gfx_internal.h"

static void tesseraos_present(const uint32_t *fb, int64_t fb_width, int64_t x, int64_t y,
                              int64_t w, int64_t h) {
    (void)fb; (void)fb_width; (void)x; (void)y; (void)w; (void)h;
}

static int tesseraos_pump(void) {
    return 0;
}

static void tesseraos_close(void) {
}

int gfx_tesseraos_open(gfx_backend *be, int64_t w, int64_t h) {
    (void)w; (void)h;
    /* Stub: no framebuffer is available yet, so report failure and let the core
     * fall back to the headless backend. */
    be->name = "tesseraos";
    be->present = tesseraos_present;
    be->pump = tesseraos_pump;
    be->close = tesseraos_close;
    return 0;
}
