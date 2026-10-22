/* Win32 backend. The framebuffer is already in the 32-bit 0x00RRGGBB layout that a
 * top-down BI_RGB DIB expects, so presenting is one StretchDIBits call on the dirty
 * rectangle with no conversion. */
#include <windows.h>
#include <string.h>

#include "gfx_internal.h"

static HWND hwnd;
static BITMAPINFO bmi;
static int closed_flag;

/* Mouse coordinates arrive as signed 16-bit values in the client area. */
static int64_t lp_x(LPARAM lp) { return (int64_t)(short)LOWORD(lp); }
static int64_t lp_y(LPARAM lp) { return (int64_t)(short)HIWORD(lp); }

static LRESULT CALLBACK gfx_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
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
        case WM_LBUTTONDOWN: gfx_input_button(0, 1, lp_x(lp), lp_y(lp)); return 0;
        case WM_LBUTTONUP:   gfx_input_button(0, 0, lp_x(lp), lp_y(lp)); return 0;
        case WM_RBUTTONDOWN: gfx_input_button(1, 1, lp_x(lp), lp_y(lp)); return 0;
        case WM_RBUTTONUP:   gfx_input_button(1, 0, lp_x(lp), lp_y(lp)); return 0;
        case WM_MBUTTONDOWN: gfx_input_button(2, 1, lp_x(lp), lp_y(lp)); return 0;
        case WM_MBUTTONUP:   gfx_input_button(2, 0, lp_x(lp), lp_y(lp)); return 0;
        default:
            return DefWindowProcA(h, msg, wp, lp);
    }
}

static void win32_present(const uint32_t *fb, int64_t fb_width, int64_t x, int64_t y,
                          int64_t w, int64_t h) {
    HDC dc = GetDC(hwnd);
    StretchDIBits(dc, (int)x, (int)y, (int)w, (int)h,
                  (int)x, (int)y, (int)w, (int)h,
                  fb, &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd, dc);
    (void)fb_width;
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

    RECT r = {0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowExA(0, "EmberGfx", "Ember graphics", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                           NULL, NULL, inst, NULL);
    if (hwnd == NULL) return 0;

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
