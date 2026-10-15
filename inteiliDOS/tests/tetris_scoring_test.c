/* Host regression: compile with -ffunction-sections -fdata-sections and
 * -Wl,--gc-sections to discard the bare-metal game entry point. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../tetris/tetris.c"

void vga_set_color(vga_color_t fg, vga_color_t bg) { (void)fg; (void)bg; }
void vga_set_cursor(int row, int col) { (void)row; (void)col; }
void vga_putchar(char c) { (void)c; }
void vga_put_colored(char c, vga_color_t fg, vga_color_t bg)
{ (void)c; (void)fg; (void)bg; }
void vga_puts(const char *s) { (void)s; }
void speaker_on(uint32_t hz) { (void)hz; }
void speaker_off(void) {}
void timer_sleep(uint32_t ms) { (void)ms; }

int main(void)
{
    memset(board, 0, sizeof(board));
    g = (GS){ .piece = 1, .col = 3, .score = 123, .level = 1 };
    handle_key(KEY_DOWN);
    assert(g.row == 1 && g.score == 123 && g.dirty);
    while (!collides(g.piece, g.rot, g.row + 1, g.col)) {
        int row = g.row;
        handle_key(KEY_DOWN);
        assert(g.row == row + 1 && g.score == 123);
    }
    int bottom = g.row;
    g.dirty = 0;
    handle_key(KEY_DOWN);
    assert(g.row == bottom && g.score == 123 && !g.dirty);

    g.row = 0;
    g.paused = 1;
    handle_key(KEY_DOWN);
    assert(g.row == 0 && g.score == 123);
    g.paused = 0;
    handle_key(KEY_RIGHT);
    handle_key(KEY_LEFT);
    assert(g.score == 123);

    /* Line-clear awards and the separate hard-drop bonus are unchanged. */
    for (int lines = 1; lines <= 4; lines++) {
        static const int points[] = {0, 100, 300, 500, 800};
        g.score = 0; g.lines = 0; g.level = 2;
        add_score(lines, 0);
        assert(g.score == points[lines] * 2);
    }
    g = (GS){ .piece = 1, .col = 3, .level = 1 };
    int distance = ghost_row();
    handle_key(' ');
    assert(distance > 0 && g.score == distance * 2);
    puts("PASS: soft drop never awards points; line clears and hard drop unchanged");
    return 0;
}
