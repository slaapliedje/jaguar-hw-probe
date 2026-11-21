/* 8x8 font text renderer (font bitmap from the jaguar-sdk jaghello example,
 * GPL). */

#include "text.h"
#include "font.h"
#include "blit.h"

void text_clear(unsigned char *screen, unsigned char color)
{
    blit_fill(screen, 0, 0, SCREEN_W, SCREEN_H, color);
}

static void draw_char(unsigned char *screen, int x, int y, char ch,
                      unsigned char color)
{
    int row, col, loc;
    int charloc = ch * F_CHARSIZE;

    for (row = 0; row < F_HEIGHT; row++) {
        loc = (y + row) * SCREEN_W + x;
        for (col = 0; col < F_WIDTH; col++) {
            if ((textfont[charloc + row] >> (7 - col)) & 1)
                screen[loc + col] = color;
        }
    }
}

void text_draw(unsigned char *screen, int x, int y, const char *str,
               unsigned char color)
{
    for (; *str; str++, x += F_WIDTH)
        draw_char(screen, x, y, *str, color);
}

void text_center(unsigned char *screen, int y, const char *str,
                 unsigned char color)
{
    int len = 0;
    const char *p;
    for (p = str; *p; p++)
        len++;
    text_draw(screen, (SCREEN_W - len * F_WIDTH) / 2, y, str, color);
}
