/* Win32 backend. The framebuffer is already in the 32-bit 0x00RRGGBB layout that a
 * top-down BI_RGB DIB expects, so presenting is one StretchDIBits call on the dirty
 * rectangle with no conversion. */
#include <windows.h>
#include <string.h>

#include "gfx_internal.h"

static HWND hwnd;
static BITMAPINFO bmi;
static int closed_flag;

/* The canvas is kept so that WM_PAINT can redraw the whole window. Windows asks for a
 * repaint when the window is uncovered, restored or moved, and those requests do not
 * come with a dirty rectangle. fb_ptr is the core's framebuffer, which is always up to
 * date, so repainting from it shows the current picture. */
static const uint32_t *fb_ptr;
static int64_t canvas_w, canvas_h;

/* Mouse coordinates arrive as signed 16-bit values in the client area. A drag that
 * leaves the window gives negative values, so the sign must be kept. */
static int64_t lp_x(LPARAM lp) { return (int64_t)(short)LOWORD(lp); }
static int64_t lp_y(LPARAM lp) { return (int64_t)(short)HIWORD(lp); }

/* Holds the mouse while a button is down, so that the release is still seen when the
 * pointer leaves the window. wp is the MK_* state after the event. */
static void update_capture(HWND h, WPARAM wp, int pressed) {
    if (pressed) {
        SetCapture(h);
    } else if ((wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0) {
        ReleaseCapture();
    }
}

static void paint_canvas(HDC dc) {
    if (fb_ptr == NULL) {
        PatBlt(dc, 0, 0, (int)canvas_w, (int)canvas_h, BLACKNESS);
        return;
    }
    StretchDIBits(dc, 0, 0, (int)canvas_w, (int)canvas_h,
                  0, 0, (int)canvas_w, (int)canvas_h,
                  fb_ptr, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK gfx_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            paint_canvas(dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            closed_flag = 1;
            PostQuitMessage(0);
            return 0;
        case WM_MOUSEMOVE:
            gfx_input_move(lp_x(lp), lp_y(lp));
            return 0;
        case WM_LBUTTONDOWN:
            update_capture(h, wp, 1);
            gfx_input_button(0, 1, lp_x(lp), lp_y(lp)); return 0;
        case WM_LBUTTONUP:
            gfx_input_button(0, 0, lp_x(lp), lp_y(lp));
            update_capture(h, wp, 0); return 0;
        case WM_RBUTTONDOWN:
            update_capture(h, wp, 1);
            gfx_input_button(1, 1, lp_x(lp), lp_y(lp)); return 0;
        case WM_RBUTTONUP:
            gfx_input_button(1, 0, lp_x(lp), lp_y(lp));
            update_capture(h, wp, 0); return 0;
        case WM_MBUTTONDOWN:
            update_capture(h, wp, 1);
            gfx_input_button(2, 1, lp_x(lp), lp_y(lp)); return 0;
        case WM_MBUTTONUP:
            gfx_input_button(2, 0, lp_x(lp), lp_y(lp));
            update_capture(h, wp, 0); return 0;
        case WM_CAPTURECHANGED:
        case WM_KILLFOCUS:
            /* Another window took the mouse or focus, so the release may never arrive. */
            gfx_input_release_all();
            return 0;
        default:
            return DefWindowProcA(h, msg, wp, lp);
    }
}

static void win32_present(const uint32_t *fb, int64_t fb_width, int64_t x, int64_t y,
                          int64_t w, int64_t h) {
    /* The DIB is canvas_w pixels wide, so fb_width must match it. */
    (void)fb_width;
    (void)x; (void)y; (void)w; (void)h;
    fb_ptr = fb;
    /* The whole canvas is sent, not just the dirty rectangle. StretchDIBits with a
     * non-zero source origin on this top-down DIB draws the rows at the wrong
     * vertical position (observed: a block at y=160 appears at y=150, and a block
     * that ends at the bottom edge is cut short). Using origin (0,0) avoids that. */
    HDC dc = GetDC(hwnd);
    StretchDIBits(dc, 0, 0, (int)canvas_w, (int)canvas_h,
                  0, 0, (int)canvas_w, (int)canvas_h,
                  fb, &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd, dc);
}

static int win32_pump(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) closed_flag = 1;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return closed_flag;
}

static void win32_close(void) {
    /* gfx_core frees the framebuffer after this returns, so drop the pointer first. */
    fb_ptr = NULL;
    if (hwnd != NULL) {
        DestroyWindow(hwnd);
        hwnd = NULL;
    }
}

int gfx_win32_open(gfx_backend *be, int64_t w, int64_t h) {
    HINSTANCE inst = GetModuleHandleA(NULL);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = gfx_wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = "EmberGfx";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    if (!RegisterClassA(&wc)) {
        /* Registered already by an earlier open in this process: continue. */
    }

    /* The window is not resizable. The canvas is a fixed w x h buffer and the mouse is
     * reported in client pixels, so a resized client area would leave part of the window
     * unpainted and mouse positions would not match canvas pixels. */
    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT r = {0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&r, style, FALSE);
    hwnd = CreateWindowExA(0, "EmberGfx", "Ember graphics", style | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                           NULL, NULL, inst, NULL);
    if (hwnd == NULL) return 0;

    canvas_w = w;
    canvas_h = h;
    fb_ptr = NULL;
    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)w;
    bmi.bmiHeader.biHeight = -(LONG)h;     /* top-down rows */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    ShowWindow(hwnd, SW_SHOW);
    be->name = "win32";
    be->present = win32_present;
    be->pump = win32_pump;
    be->close = win32_close;
    return 1;
}

unsigned long gfx_x11_window_id(void) {
    return 0UL;
}
