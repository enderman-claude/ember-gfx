# Keyboard and close-button test. The arrow keys move the square (polled with
# gfx_key_down). Each key press is drawn as a dot in the event row. The close button
# does not close the window straight away: it asks Y to quit or N to stay.
# Key codes: Left 37, Up 38, Right 39, Down 40, Y 89, N 78.
extern def gfx_open(w: i64, h: i64) -> i64
extern def gfx_close()
extern def gfx_clear(c: i64)
extern def gfx_color(c: i64)
extern def gfx_rect(x: i64, y: i64, w: i64, h: i64)
extern def gfx_present()
extern def gfx_sleep_ms(ms: i64)
extern def gfx_closed() -> i64
extern def gfx_poll_key() -> i64
extern def gfx_key_code() -> i64
extern def gfx_key_pressed() -> i64
extern def gfx_key_down(key: i64) -> i64
extern def gfx_close_requested() -> i64
extern def gfx_set_close_blocked(on: i64)

def main():
    if gfx_open(480, 360) == 0:
        return
    gfx_set_close_blocked(1)
    mut x = 220
    mut y = 150
    mut events = 0
    mut asking = 0
    mut quit = 0
    while gfx_closed() == 0:
        if gfx_close_requested() == 1:
            asking = 1
        while gfx_poll_key() == 1:
            if gfx_key_pressed() == 1:
                events = events + 1
                if asking == 1:
                    if gfx_key_code() == 89:
                        quit = 1
                    if gfx_key_code() == 78:
                        asking = 0
        if quit == 1:
            gfx_close()
        if gfx_key_down(37) == 1:
            x = x - 3
        if gfx_key_down(39) == 1:
            x = x + 3
        if gfx_key_down(38) == 1:
            y = y - 3
        if gfx_key_down(40) == 1:
            y = y + 3
        if x < 0:
            x = 0
        if x > 440:
            x = 440
        if y < 0:
            y = 0
        if y > 320:
            y = 320
        gfx_clear(0x202838)
        gfx_color(0x30C0FF)
        gfx_rect(x, y, 40, 40)
        mut i = 0
        while i < events and i < 40:
            gfx_color(0xFFD040)
            gfx_rect(10 + i * 11, 10, 6, 6)
            i = i + 1
        if asking == 1:
            gfx_color(0xFF4040)
            gfx_rect(10, 330, 460, 20)
        gfx_present()
        gfx_sleep_ms(16)
