/* Minimal 8x8 text renderer on the 320x200 8bpp indexed framebuffer. */
#ifndef ENGINE_TEXT_H
#define ENGINE_TEXT_H

#define SCREEN_W 320
#define SCREEN_H 200

void text_clear(unsigned char *screen, unsigned char color);
void text_draw(unsigned char *screen, int x, int y, const char *str,
               unsigned char color);
/* Centered horizontally at pixel row y. */
void text_center(unsigned char *screen, int y, const char *str,
                 unsigned char color);

#endif
