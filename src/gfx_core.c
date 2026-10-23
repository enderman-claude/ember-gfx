/* Device-independent core: canvas, shape rasterisation, clipping and dirty tracking.
 *
 * Device-specific work stays out of this file. Backends see only the dirty rectangle
 * and the framebuffer, so one code path serves every screen.
 *
 * Optimisations here:
 *   - Dirty rectangle: gfx_present sends only the region changed since the last present.
 *   - SIMD row fill: 4 pixels per store with SSE2 on x86-64 or NEON on AArch64
 *     (scalar fallback elsewhere).
 *   - Span-based circles: each row is one horizontal fill, not a per-pixel test.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "gfx_internal.h"

#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>            /* Advanced SIMD is mandatory on AArch64 */
#endif

#define GFX_MAX_SIZE 8192
#define GFX_MAX_RADIUS (1LL << 20)   /* larger radii are clamped; see gfx_circle */
#define GFX_CLICK_QUEUE 64
#define GFX_BUTTONS 3

static struct {
    int open;
    int64_t w, h;
    uint32_t *fb;
    uint32_t color;
    int dirty;
    int64_t dx0, dy0, dx1, dy1;      /* dirty region, half-open */
    int closed;
    int64_t pixels_presented;
    gfx_backend be;

    /* input */
    int64_t mouse_x, mouse_y;
    int down[GFX_BUTTONS];
    struct { int button; int64_t x, y; } queue[GFX_CLICK_QUEUE];
    int q_head, q_count;
    struct { int button; int64_t x, y; } current;
} G;

static void mark_dirty(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    if (!G.dirty) {
        G.dirty = 1;
        G.dx0 = x0; G.dy0 = y0; G.dx1 = x1; G.dy1 = y1;
        return;
    }
    if (x0 < G.dx0) G.dx0 = x0;
    if (y0 < G.dy0) G.dy0 = y0;
    if (x1 > G.dx1) G.dx1 = x1;
    if (y1 > G.dy1) G.dy1 = y1;
}

/* Clips a rectangle to the canvas. Returns 0 when nothing is left. */
static int clip(int64_t *x, int64_t *y, int64_t *w, int64_t *h) {
    int64_t x0 = *x < 0 ? 0 : *x;
    int64_t y0 = *y < 0 ? 0 : *y;
    int64_t x1 = (*x + *w > G.w) ? G.w : *x + *w;
    int64_t y1 = (*y + *h > G.h) ? G.h : *y + *h;
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    return 1;
}

/* Fills pixels [x0, x1) of row y. The caller has already clipped. */
static void fill_span(int64_t y, int64_t x0, int64_t x1, uint32_t c) {
    uint32_t *row = G.fb + y * G.w;
    int64_t x = x0;
#if defined(__SSE2__)
    __m128i v = _mm_set1_epi32((int)c);
    for (; x + 4 <= x1; x += 4) {
        _mm_storeu_si128((__m128i *)(row + x), v);
    }
#elif defined(__aarch64__)
    uint32x4_t v = vdupq_n_u32(c);
    for (; x + 4 <= x1; x += 4) {
        vst1q_u32(row + x, v);
    }
#endif
    for (; x < x1; x++) {
        row[x] = c;
    }
}

static int64_t isqrt64(int64_t n) {
    if (n <= 0) return 0;
    int64_t r = (int64_t)sqrt((double)n);
    while (r * r > n) r--;
    while ((r + 1) * (r + 1) <= n) r++;
    return r;
}

static void plot(int64_t x, int64_t y) {
    if (x < 0 || y < 0 || x >= G.w || y >= G.h) {
        return;
    }
    G.fb[y * G.w + x] = G.color;
    mark_dirty(x, y, x + 1, y + 1);
}

/* ---- public API ---------------------------------------------------------- */

int64_t gfx_open(int64_t w, int64_t h) {
    if (G.open) {
        return 1;
    }
    if (w < 1 || h < 1 || w > GFX_MAX_SIZE || h > GFX_MAX_SIZE) {
        return 0;
    }
    G.fb = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (G.fb == NULL) {
        return 0;
    }
    G.w = w;
    G.h = h;

    int ok = 0;
    const char *force = getenv("GFX_BACKEND");
    if (force == NULL || strcmp(force, "null") != 0) {
#if defined(_WIN32)
        ok = gfx_win32_open(&G.be, w, h);
#else
#if defined(GFX_HAVE_X11)
        if (getenv("DISPLAY") != NULL) {
            ok = gfx_x11_open(&G.be, w, h);
        }
#endif
        if (!ok) {
            ok = gfx_tesseraos_open(&G.be, w, h);   /* stub: currently always 0 */
        }
#endif
    }
    if (!ok) {
        ok = gfx_null_open(&G.be, w, h);
    }

    G.open = 1;
    G.color = 0xFFFFFF;
    G.dirty = 1;                       /* the first present shows the whole canvas */
    G.dx0 = 0; G.dy0 = 0; G.dx1 = w; G.dy1 = h;
    G.closed = 0;
    G.pixels_presented = 0;
    return ok;
}

void gfx_close(void) {
    if (!G.open) {
        return;
    }
    G.be.close();
    free(G.fb);
    G.fb = NULL;
    G.open = 0;
}

int64_t gfx_width(void) { return G.w; }
int64_t gfx_height(void) { return G.h; }
const char *gfx_backend_name(void) { return G.open ? G.be.name : "none"; }

void gfx_color(int64_t rgb) { G.color = (uint32_t)(rgb & 0xFFFFFF); }
int64_t gfx_get_color(void) { return G.color; }

void gfx_clear(int64_t rgb) {
    if (!G.open) return;
    uint32_t c = (uint32_t)(rgb & 0xFFFFFF);
    for (int64_t y = 0; y < G.h; y++) {
        fill_span(y, 0, G.w, c);
    }
    mark_dirty(0, 0, G.w, G.h);
}

void gfx_pixel(int64_t x, int64_t y) {
    if (!G.open) return;
    plot(x, y);
}

void gfx_rect(int64_t x, int64_t y, int64_t w, int64_t h) {
    if (!G.open || !clip(&x, &y, &w, &h)) return;
    for (int64_t row = y; row < y + h; row++) {
        fill_span(row, x, x + w, G.color);
    }
    mark_dirty(x, y, x + w, y + h);
}

void gfx_circle(int64_t cx, int64_t cy, int64_t r) {
    if (!G.open || r < 0) return;
    /* Clamping keeps r*r from overflowing. A radius this large covers the whole
     * canvas for any centre near it, so the picture is unchanged. */
    if (r > GFX_MAX_RADIUS) r = GFX_MAX_RADIUS;

    int64_t y0 = cy - r < 0 ? 0 : cy - r;
    int64_t y1 = cy + r + 1 > G.h ? G.h : cy + r + 1;
    for (int64_t y = y0; y < y1; y++) {
        int64_t dy = y - cy;
        int64_t half = isqrt64(r * r - dy * dy);
        int64_t x0 = cx - half, x1 = cx + half + 1;
        if (x0 < 0) x0 = 0;
        if (x1 > G.w) x1 = G.w;
        if (x1 > x0) fill_span(y, x0, x1, G.color);
    }
    int64_t bx0 = cx - r < 0 ? 0 : cx - r;
    int64_t bx1 = cx + r + 1 > G.w ? G.w : cx + r + 1;
    if (y1 > y0 && bx1 > bx0) mark_dirty(bx0, y0 < 0 ? 0 : y0, bx1, y1);
}

void gfx_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    if (!G.open) return;
    /* Bresenham's algorithm in integer arithmetic, same formulation as the
     * textbook version: dy is kept negative, and err = dx + dy. */
    int64_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int64_t dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int64_t sx = x0 < x1 ? 1 : -1;
    int64_t sy = y0 < y1 ? 1 : -1;
    int64_t err = dx + dy;
    for (;;) {
        plot(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int64_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

int64_t gfx_get_pixel(int64_t x, int64_t y) {
    if (!G.open || x < 0 || y < 0 || x >= G.w || y >= G.h) return -1;
    return G.fb[y * G.w + x];
}

void gfx_present(void) {
    if (!G.open) return;
    if (G.dirty) {
        int64_t w = G.dx1 - G.dx0, h = G.dy1 - G.dy0;
        G.be.present(G.fb, G.w, G.dx0, G.dy0, w, h);
        G.pixels_presented += w * h;
        G.dirty = 0;
    }
    if (G.be.pump != NULL && G.be.pump()) {
        G.closed = 1;
    }
}

int64_t gfx_closed(void) { return G.closed; }
int64_t gfx_pixels_presented(void) { return G.pixels_presented; }

/* ---- input -------------------------------------------------------------- */

void gfx_input_move(int64_t x, int64_t y) {
    G.mouse_x = x;
    G.mouse_y = y;
}

void gfx_input_button(int button, int pressed, int64_t x, int64_t y) {
    if (button < 0 || button >= GFX_BUTTONS) return;
    G.mouse_x = x;
    G.mouse_y = y;
    G.down[button] = pressed ? 1 : 0;
    if (pressed) {
        if (G.q_count == GFX_CLICK_QUEUE) return;          /* queue full: drop the press */
        int slot = (G.q_head + G.q_count) % GFX_CLICK_QUEUE;
        G.queue[slot].button = button;
        G.queue[slot].x = x;
        G.queue[slot].y = y;
        G.q_count++;
    }
}

int64_t gfx_mouse_x(void) { return G.mouse_x; }
int64_t gfx_mouse_y(void) { return G.mouse_y; }

int64_t gfx_mouse_down(int64_t button) {
    if (button < 0 || button >= GFX_BUTTONS) return 0;
    return G.down[button];
}

int64_t gfx_poll_click(void) {
    if (G.q_count == 0) return 0;
    G.current.button = G.queue[G.q_head].button;
    G.current.x = G.queue[G.q_head].x;
    G.current.y = G.queue[G.q_head].y;
    G.q_head = (G.q_head + 1) % GFX_CLICK_QUEUE;
    G.q_count--;
    return 1;
}

int64_t gfx_click_button(void) { return G.current.button; }
int64_t gfx_click_x(void) { return G.current.x; }
int64_t gfx_click_y(void) { return G.current.y; }

void gfx_sleep_ms(int64_t ms) {
    if (ms <= 0) return;
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}
