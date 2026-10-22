/* X11 backend for Linux (and other Unix systems with an X server).
 *
 * Device-specific optimisations:
 *   - MIT-SHM: the image lives in shared memory and XShmPutImage sends only the
 *     dirty rectangle. This avoids copying pixels through the socket.
 *   - Direct row copy: when the server's format is 32-bit 0x00RRGGBB, little endian
 *     (the usual case), each row is one memcpy from the framebuffer.
 *   - Format conversion for any other true-colour visual (16-bit, 24-bit packed,
 *     big endian, non-standard masks).
 *
 * Set GFX_X11_NO_SHM=1 to disable MIT-SHM and use XPutImage instead.
 */
#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include "gfx_internal.h"

static Display *dpy;
static Window win;
static GC gc;
static XImage *img;
static XShmSegmentInfo shminfo;
static int use_shm;
static int identity;              /* framebuffer layout matches the server layout exactly */
static int bytes_per_pixel;
static int msb_first;
static unsigned long rmask, gmask, bmask;
static Atom wm_delete;
static int closed_flag;

/* Scales an 8-bit channel to the width of a mask and shifts it into place. */
static uint32_t channel(uint32_t c, unsigned long mask) {
    if (mask == 0) return 0;
    int shift = 0;
    while (((mask >> shift) & 1UL) == 0) shift++;
    unsigned long m = mask >> shift;
    uint32_t bits = 0;
    while (m & 1UL) { bits++; m >>= 1; }
    uint32_t maxv = (1u << bits) - 1u;
    return ((c * maxv + 127u) / 255u) << shift;
}

static uint32_t encode(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return channel(r, rmask) | channel(g, gmask) | channel(b, bmask);
}

static void store_pixel(unsigned char *p, uint32_t v) {
    for (int i = 0; i < bytes_per_pixel; i++) {
        int byte = msb_first ? (bytes_per_pixel - 1 - i) : i;
        p[byte] = (unsigned char)(v >> (8 * i));
    }
}

static void x11_present(const uint32_t *fb, int64_t fb_width, int64_t x, int64_t y,
                        int64_t w, int64_t h) {
    if (img == NULL) return;
    for (int64_t row = y; row < y + h; row++) {
        unsigned char *dst = (unsigned char *)img->data + row * img->bytes_per_line
                             + x * bytes_per_pixel;
        const uint32_t *src = fb + row * fb_width + x;
        if (identity) {
            memcpy(dst, src, (size_t)w * 4);          /* fast path: one copy per row */
        } else {
            for (int64_t i = 0; i < w; i++) {
                store_pixel(dst + i * bytes_per_pixel, encode(src[i]));
            }
        }
    }
    if (use_shm) {
        XShmPutImage(dpy, win, gc, img, (int)x, (int)y, (int)x, (int)y, (unsigned)w, (unsigned)h, False);
    } else {
        XPutImage(dpy, win, gc, img, (int)x, (int)y, (int)x, (int)y, (unsigned)w, (unsigned)h);
    }
    /* The server may read shared memory after this call returns. Wait for it to finish
     * before the next frame writes into the same memory. */
    XSync(dpy, False);
}

/* X button numbers: 1 = left, 2 = middle, 3 = right. 4-7 are the scroll wheel. */
static int x_button_to_gfx(unsigned int b) {
    switch (b) {
        case 1: return 0;
        case 2: return 2;
        case 3: return 1;
        default: return -1;
    }
}

static int x11_pump(void) {
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wm_delete) closed_flag = 1;
                break;
            case DestroyNotify:
                closed_flag = 1;
                break;
            case MotionNotify:
                gfx_input_move(ev.xmotion.x, ev.xmotion.y);
                break;
            case ButtonPress:
            case ButtonRelease: {
                int b = x_button_to_gfx(ev.xbutton.button);
                if (b >= 0) {
                    gfx_input_button(b, ev.type == ButtonPress, ev.xbutton.x, ev.xbutton.y);
                }
                break;
            }
            default:
                break;
        }
    }
    return closed_flag;
}

static void x11_close(void) {
    if (dpy == NULL) return;
    if (img != NULL) {
        if (use_shm) {
            XShmDetach(dpy, &shminfo);
            XSync(dpy, False);
            img->data = NULL;
            XDestroyImage(img);
            shmdt(shminfo.shmaddr);
        } else {
            free(img->data);
            img->data = NULL;
            XDestroyImage(img);
        }
        img = NULL;
    }
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    dpy = NULL;
}

unsigned long gfx_x11_window_id(void) {
    return dpy ? (unsigned long)win : 0UL;
}

int gfx_x11_open(gfx_backend *be, int64_t w, int64_t h) {
    dpy = XOpenDisplay(NULL);
    if (dpy == NULL) return 0;

    int scr = DefaultScreen(dpy);
    Visual *vis = DefaultVisual(dpy, scr);
    int depth = DefaultDepth(dpy, scr);
    if (vis->class != TrueColor && vis->class != DirectColor) {
        XCloseDisplay(dpy);
        dpy = NULL;
        return 0;                       /* palette visuals are not supported */
    }
    rmask = vis->red_mask;
    gmask = vis->green_mask;
    bmask = vis->blue_mask;

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, (unsigned)w, (unsigned)h, 0, 0, 0);
    XStoreName(dpy, win, "Ember graphics");
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    XSelectInput(dpy, win, StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
                           PointerMotionMask);
    gc = XCreateGC(dpy, win, 0, NULL);
    XMapWindow(dpy, win);

    use_shm = 0;
    img = NULL;
    if (getenv("GFX_X11_NO_SHM") == NULL && XShmQueryExtension(dpy)) {
        img = XShmCreateImage(dpy, vis, (unsigned)depth, ZPixmap, NULL, &shminfo,
                              (unsigned)w, (unsigned)h);
        if (img != NULL) {
            size_t size = (size_t)img->bytes_per_line * (size_t)h;
            shminfo.shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
            if (shminfo.shmid >= 0) {
                shminfo.shmaddr = (char *)shmat(shminfo.shmid, NULL, 0);
                if (shminfo.shmaddr != (char *)-1) {
                    shminfo.readOnly = False;
                    img->data = shminfo.shmaddr;
                    XShmAttach(dpy, &shminfo);
                    XSync(dpy, False);          /* the server has attached before we mark it for removal */
                    shmctl(shminfo.shmid, IPC_RMID, NULL);
                    use_shm = 1;
                } else {
                    shmctl(shminfo.shmid, IPC_RMID, NULL);
                }
            }
            if (!use_shm) {
                XDestroyImage(img);
                img = NULL;
            }
        }
    }
    if (!use_shm) {
        img = XCreateImage(dpy, vis, (unsigned)depth, ZPixmap, 0, NULL,
                           (unsigned)w, (unsigned)h, 32, 0);
        if (img == NULL) {
            x11_close();
            return 0;
        }
        img->data = malloc((size_t)img->bytes_per_line * (size_t)h);
        if (img->data == NULL) {
            x11_close();
            return 0;
        }
    }

    if (img->bits_per_pixel % 8 != 0 || img->bits_per_pixel == 0) {
        x11_close();
        return 0;
    }
    bytes_per_pixel = img->bits_per_pixel / 8;
    msb_first = (img->byte_order == MSBFirst);
    identity = (bytes_per_pixel == 4 && !msb_first &&
                rmask == 0xFF0000UL && gmask == 0xFF00UL && bmask == 0xFFUL);

    XSync(dpy, False);
    be->name = use_shm ? "x11-shm" : "x11-xput";
    be->present = x11_present;
    be->pump = x11_pump;
    be->close = x11_close;
    return 1;
}
