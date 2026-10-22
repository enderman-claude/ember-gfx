# Mouse test window. Each click leaves a coloured dot: red for left, blue for right,
# green for middle. Holding the left button paints white. The bottom band counts the
# clicks of each button as bars. Close the window to exit.
extern def gfx_open(w: i64, h: i64) -> i64
extern def gfx_close()
extern def gfx_clear(c: i64)
extern def gfx_color(c: i64)
extern def gfx_rect(x: i64, y: i64, w: i64, h: i64)
extern def gfx_circle(cx: i64, cy: i64, r: i64)
extern def gfx_present()
extern def gfx_sleep_ms(ms: i64)
extern def gfx_closed() -> i64
extern def gfx_poll_click() -> i64
extern def gfx_click_button() -> i64
extern def gfx_click_x() -> i64
extern def gfx_click_y() -> i64
extern def gfx_mouse_x() -> i64
extern def gfx_mouse_y() -> i64
extern def gfx_mouse_down(button: i64) -> i64

def draw_row(y: i64, count: i64, c: i64):
    gfx_color(0x101828)
    gfx_rect(0, y, 480, 8)
    mut w = count * 4
    if w > 460:
        w = 460
    gfx_color(c)
    gfx_rect(10, y, w, 8)

def main():
    if gfx_open(480, 360) == 0:
        return
    gfx_clear(0x202838)
    mut left = 0
    mut right = 0
    mut middle = 0
    while gfx_closed() == 0:
        while gfx_poll_click() == 1:
            b = gfx_click_button()
            x = gfx_click_x()
            y = gfx_click_y()
            if b == 0:
                left = left + 1
                gfx_color(0xFF4040)
            elif b == 1:
                right = right + 1
                gfx_color(0x4080FF)
            else:
                middle = middle + 1
                gfx_color(0x40FF80)
            gfx_circle(x, y, 12)
        if gfx_mouse_down(0) == 1:
            gfx_color(0xFFFFFF)
            gfx_rect(gfx_mouse_x() - 2, gfx_mouse_y() - 2, 5, 5)
        draw_row(324, left, 0xFF4040)
        draw_row(336, right, 0x4080FF)
        draw_row(348, middle, 0x40FF80)
        gfx_present()
        gfx_sleep_ms(10)
    gfx_close()
