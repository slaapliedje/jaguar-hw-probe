/* Synchronous blitter operations on the 320-wide 8bpp framebuffers.
 * The destination buffer must be phrase (8-byte) aligned — startup.s
 * aligns _vidmem/_vidmem2 with .phrase. */
#ifndef ENGINE_BLIT_H
#define ENGINE_BLIT_H

void blit_fill(unsigned char *buf, int x, int y, int w, int h,
               unsigned char c);

/* Opaque full-width row copy between phrase-aligned 320-wide buffers:
 * dst rows [y, y+h) <- src rows [0, h). */
void blit_rows(unsigned char *dst, const unsigned char *src, int y, int h);

/* Copy a w x h window from a 1536-wide source buffer at (sx,sy) to the 320-wide
 * dst at (dx,dy). Phrase-aligned x/w only. Backs the scroll camera. */
void blit_window(unsigned char *dst, int dx, int dy,
                 const unsigned char *src, int sx, int sy, int w, int h);

/* Opaque copy of a w x h window between two 320-wide buffers, from
 * (sx,sy) to (dx,dy). Phrase-aligned x/w only. */
void blit_copy320(unsigned char *dst, int dx, int dy,
                  const unsigned char *src, int sx, int sy, int w, int h);

/* Opaque copy of the w x h rect at (x,y) between two 320-wide buffers
 * (same coordinates in both). Phrase-aligned x/w only. */
void blit_rect(unsigned char *dst, const unsigned char *src,
               int x, int y, int w, int h);

/* 8bpp image copy with color 0 transparent (DCOMPEN, PATD=0).
 * img is the top-left of a phrase-aligned image imgw (8, 16 or 32) pixels
 * wide; drawing starts at source row srcy (vertical clipping). x may be
 * negative — the hardware clip window drops off-screen columns. */
void blit_img8(unsigned char *buf, int x, int y, int w, int h,
               const unsigned char *img, int imgw, int srcy);


#endif
