# ember-gfx

A small 2D graphics stack written in C for [Ember](https://github.com/enderman-claude/ember).
It works from C directly, and from Ember programs through `extern def`.

- **Generic API**: canvas, colour, clear, pixel, rectangle, circle and line, all clipped
  to the canvas. Every function takes `int64_t`, so Ember's `i64` values pass straight through.
- **Pluggable backends**: X11 on Linux (with MIT-SHM), Win32 on Windows, and a headless
  backend for tests and servers. A tesseraOS backend exists as a stub: it compiles, but
  reports failure until tesseraOS has a framebuffer, so programs use the headless backend.
- **x86-64 and AArch64**: the same source builds for both. SSE2 and NEON fast paths are
  selected at compile time, with a scalar fallback everywhere else.
- **Device-specific fast paths**: dirty-rectangle presentation on every backend, shared
  memory and direct row copies on X11, and a single `StretchDIBits` call on Win32.

## Using it from Ember

```python
extern def gfx_open(w: i64, h: i64) -> i64
extern def gfx_clear(c: i64)
extern def gfx_color(c: i64)
extern def gfx_circle(cx: i64, cy: i64, r: i64)
extern def gfx_present()
extern def gfx_sleep_ms(ms: i64)
extern def gfx_closed() -> i64

def main():
    if gfx_open(320, 240) == 0:
        return
    mut frame = 0
    while gfx_closed() == 0 and frame < 600:
        gfx_clear(0x101020)
        gfx_color(0x30C0FF)
        gfx_circle((frame * 2) % 320, 120, 40)
        gfx_present()
        gfx_sleep_ms(16)
        frame = frame + 1
```

Build it (requires the Ember compiler, v0.5.0 or later):

```
ember build demo/animate.em -o animate \
  --link=build/libember_gfx.a --link=-lX11 --link=-lXext --link=-lm \
  --link=-Iinclude --link=-DGFX_HAVE_X11
```

The `--link=` form is needed for flags such as `-lX11`, because the argument parser would
otherwise read them as options. `make demo` does this for you.

## Mouse input

```python
extern def gfx_poll_click() -> i64
extern def gfx_click_button() -> i64
extern def gfx_click_x() -> i64
extern def gfx_click_y() -> i64
extern def gfx_mouse_x() -> i64
extern def gfx_mouse_y() -> i64
extern def gfx_mouse_down(button: i64) -> i64
```

Clicks are queued as they happen. `gfx_poll_click()` takes the oldest one, and
`gfx_click_button/x/y()` describe it: button 0 is left, 1 is right, 2 is middle. Use
`gfx_mouse_down(b)` to check whether a button is held, and `gfx_mouse_x/y()` for the
pointer position. Input is read during `gfx_present`. The queue holds 64 presses.

`demo/click_test.em` is a mouse test: each click leaves a coloured dot, holding the left
button paints white, and bars count the clicks.

```
make click-test         # Linux: build/click_test (needs Ember and the X11 libraries)
make click-test-exe     # Windows: build/click_test.exe (needs MinGW)
xvfb-run -a make e2e-click   # Linux end-to-end: real pointer clicks, checked on screen
```

## Using it from C

```c
#include "ember_gfx.h"

int main(void) {
    if (!gfx_open(320, 240)) return 1;
    gfx_clear(0x000000);
    gfx_color(0xFF0000);
    gfx_rect(10, 10, 100, 50);
    gfx_present();
    while (!gfx_closed()) {
        gfx_present();
        gfx_sleep_ms(16);
    }
    gfx_close();
}
```

## API

| Function | Description |
|---|---|
| `gfx_open(w, h)` | Opens a canvas and a window. Returns 1 on success. |
| `gfx_close()` | Closes the window and frees the canvas. |
| `gfx_width()`, `gfx_height()` | Canvas size. |
| `gfx_backend_name()` | `x11-shm`, `x11-xput`, `win32` or `null`. |
| `gfx_color(rgb)`, `gfx_get_color()` | Current drawing colour, `0xRRGGBB`. |
| `gfx_clear(rgb)` | Fills the canvas. |
| `gfx_pixel(x, y)` | Sets one pixel. |
| `gfx_rect(x, y, w, h)` | Filled rectangle. |
| `gfx_circle(cx, cy, r)` | Filled circle. Pixels at distance exactly `r` are included. |
| `gfx_line(x0, y0, x1, y1)` | One-pixel line, both ends included. |
| `gfx_get_pixel(x, y)` | Reads a pixel back, or -1 if it is off the canvas. |
| `gfx_present()` | Shows the changes since the last call and processes window events. |
| `gfx_closed()` | 1 once the user has closed the window. |
| `gfx_pixels_presented()` | Pixels sent to the screen so far. Used to test the dirty-rectangle logic. |
| `gfx_sleep_ms(ms)` | Sleeps. |

Drawing goes into a back buffer. Nothing reaches the screen until `gfx_present`, which sends
only the region that changed.

## Backends

| Backend | Selected when | Notes |
|---|---|---|
| `x11-shm` | Linux, `DISPLAY` set, MIT-SHM available | Shared-memory image. Only the dirty rectangle is sent. The server copy is waited for before the next frame writes. |
| `x11-xput` | As above, but `GFX_X11_NO_SHM=1` is set or MIT-SHM is missing | `XPutImage` of the dirty rectangle. |
| `win32` | Windows | `StretchDIBits` of the dirty rectangle. The framebuffer is already in the DIB's pixel format. |
| `null` | Anything else, or `GFX_BACKEND=null` | Canvas in memory only. Used by the tests and on headless servers. |

X11 handles any true-colour visual. The framebuffer is copied row by row when the server uses
32-bit `0x00RRGGBB` pixels, the usual case. Other layouts, such as 16-bit `RGB565` or
big-endian servers, are converted per pixel. The 16-bit path is tested.

## Optimisations

- **Dirty rectangle** (all backends): `gfx_present` sends the bounding box of everything
  drawn since the last present. A changed pixel costs one pixel, not a full frame.
- **Shared memory** (X11): the image lives in shared memory, so frames don't go through the
  socket.
- **Row copy** (X11): one `memcpy` per row in the common pixel format.
- **SIMD row fill** (core): four pixels per store, using SSE2 on x86-64 and NEON on AArch64.
  A scalar loop handles the tail and other CPUs.
- **Span circles** (core): each row is a single fill, found with an exact integer square root.

## Building and testing

```
make                 # build build/libember_gfx.a
make test            # headless tests, plus X11 tests if DISPLAY is set
xvfb-run -a make test-x11                              # X11 with 24-bit depth
xvfb-run -a -s "-screen 0 800x600x16" make test-x11-16 # X11 with 16-bit depth
make win-check       # compile the Windows backend with MinGW (does not run it)
make arm-check       # compile every source for AArch64 (cross compiler, -Werror)
make arm-test        # AArch64: headless tests and the Ember self-test (qemu-user on x86-64)
```

### AArch64 (ARM64) Linux

The library builds for AArch64 with `make arm-check` and `make arm-test`. The Ember self-test
is compiled with `--target linux-arm64` and its output is compared with `demo/selftest.expect`.

- On an x86-64 host, install `gcc-aarch64-linux-gnu` and `qemu-user`. `make arm-test` runs
  the AArch64 programs under `qemu-aarch64`.
- On an ARM64 host, no emulator is used, and `make arm-test` runs the programs natively.
- `ARM_CC` selects the compiler and `ARM_SYSROOT` the sysroot used by qemu.

`arm-check` also compiles the X11 backend for AArch64. Running the X11 tests on AArch64 needs
AArch64 X11 libraries, which the cross toolchain does not include, so the X11 backend is
compile-checked there but not run.

Requirements: a C11 compiler, `pkg-config`, `libx11-dev` and `libxext-dev` (for X11),
and the Ember compiler (for the `.em` demos; set `EMBER_REPO` if it is not at `../ember`).

What the tests cover:

- `test_core`: 3,000 random operations (pixels, rectangles, circles, clears and presents,
  including off-screen and negative coordinates) checked against a naive per-pixel model.
  Also checks circle edges, line endpoints and lengths, and dirty-rectangle sizes.
- `selftest.em`: an Ember program, run headless, whose output must match `demo/selftest.expect`.
- `test_x11`: draws on a real X server and reads the screen back through a second
  connection. Checks pixel values, partial updates, the window-close protocol, and mouse
  input sent with the XTest extension. Runs on the shared-memory path, the fallback path,
  and a 16-bit display.
- `e2e-click`: runs `click_test` on Xvfb, sends real pointer clicks, and checks the dots
  and bars on screen.

## Limits

- Immediate mode with one window. No text, images, alpha blending or input other than the
  close button.
- Coordinates and sizes are `int64_t`, but the canvas is limited to 8192 x 8192 pixels.
  Circle radii above 2^20 are clamped; the picture does not change for any centre near the canvas.
- The Win32 backend is compile-checked with MinGW but has not been run on Windows
  from this repository.
- `gfx_present` does not wait for vertical sync. Use `gfx_sleep_ms` to cap the frame rate.

## Layout

```
include/ember_gfx.h    public API
src/gfx_core.c         canvas, rasterisation, clipping, dirty tracking
src/gfx_null.c         headless backend
src/gfx_x11.c          X11 backend (MIT-SHM and XPutImage)
src/gfx_win32.c        Win32 backend
src/gfx_internal.h     backend interface
tests/                 C test suites
demo/                  Ember programs: self-test and animation
```

## Related repos

- [ember](https://github.com/enderman-claude/ember): the compiler that builds the Ember demos. The C library is used through `extern def`.
- [tesseraOS](https://github.com/enderman-claude/tesseraOS): the OS this library targets. `src/gfx_tesseraos.c` is a stub backend for it.
- [tesseraOS-teds](https://github.com/enderman-claude/tesseraOS-teds): the display server that the tesseraOS backend will use once it has a framebuffer path.

## License

MIT. See [LICENSE](LICENSE).
