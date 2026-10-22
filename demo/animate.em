# Animated scene in a window. Runs until the window is closed or 600 frames pass.
extern def gfx_open(w: i64, h: i64) -> i64
extern def gfx_close()
extern def gfx_clear(c: i64)
extern def gfx_color(c: i64)
extern def gfx_rect(x: i64, y: i64, w: i64, h: i64)
extern def gfx_circle(cx: i64, cy: i64, r: i64)
extern def gfx_line(x0: i64, y0: i64, x1: i64, y1: i64)
extern def gfx_present()
extern def gfx_sleep_ms(ms: i64)
extern def gfx_closed() -> i64

def main():
    if gfx_open(320, 240) == 0:
        print(-1)
        return
    mut frame = 0
    while gfx_closed() == 0 and frame < 600:
        gfx_clear(0x101020)
        gfx_color(0x30C0FF)
        gfx_circle((frame * 2) % 320, 120, 40)
        gfx_color(0xFFD000)
        gfx_rect(20, 20, 60, 30)
        gfx_color(0xFF4040)
        gfx_line(0, frame % 240, 319, 239 - frame % 240)
        gfx_present()
        gfx_sleep_ms(16)
        frame = frame + 1
    gfx_close()
    print(frame)
