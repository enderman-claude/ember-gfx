# Deterministic check of the graphics stack, run headless. Prints pixel readbacks.
extern def gfx_open(w: i64, h: i64) -> i64
extern def gfx_close()
extern def gfx_clear(c: i64)
extern def gfx_color(c: i64)
extern def gfx_rect(x: i64, y: i64, w: i64, h: i64)
extern def gfx_circle(cx: i64, cy: i64, r: i64)
extern def gfx_line(x0: i64, y0: i64, x1: i64, y1: i64)
extern def gfx_pixel(x: i64, y: i64)
extern def gfx_get_pixel(x: i64, y: i64) -> i64
extern def gfx_present()
extern def gfx_pixels_presented() -> i64

def main():
    if gfx_open(64, 48) == 0:
        print(-1)
        return
    gfx_clear(0x000000)
    gfx_color(0xFF0000)
    gfx_rect(10, 10, 20, 20)
    print(gfx_get_pixel(15, 15))      # inside the rectangle: 16711680
    print(gfx_get_pixel(9, 15))       # just left of it: 0
    print(gfx_get_pixel(30, 10))      # right edge is exclusive: 0
    gfx_color(0x00FF00)
    gfx_circle(50, 40, 5)
    print(gfx_get_pixel(50, 40))      # centre: 65280
    print(gfx_get_pixel(55, 40))      # distance exactly 5: 65280
    print(gfx_get_pixel(56, 40))      # distance 6: 0
    gfx_color(0x0000FF)
    gfx_line(0, 0, 63, 47)
    print(gfx_get_pixel(63, 47))      # line end: 255
    gfx_color(0xFFFFFF)
    gfx_rect(-5, -5, 10, 10)          # partly off-screen
    print(gfx_get_pixel(0, 0))        # covered by the rectangle: 16777215
    print(gfx_get_pixel(5, 5))        # outside the clipped rectangle: 0
    gfx_present()
    print(gfx_pixels_presented())     # first present sends the whole canvas: 3072
    gfx_pixel(1, 1)
    gfx_present()
    print(gfx_pixels_presented())     # one more pixel: 3073
    gfx_close()
