/* Blitter-backed rectangle fills (Tom blitter at $F02200).
 *
 * Fully synchronous: wait for idle, program A1 + pattern, GO, wait again —
 * callers freely mix CPU pixel writes with blits in the same region.
 * Full-phrase-aligned fills (x and w multiples of 8) run in phrase mode,
 * 8 bytes per memory cycle; everything else uses pixel mode. Either way
 * this replaces ~14-cycle 68K byte stores with 26.6 MHz blitter writes.
 */

#include "blit.h"

#define BREG(off) (*(volatile unsigned long *)(0xF02200UL + (off)))
#define A1_BASE   0x00
#define A1_FLAGS  0x04
#define A1_CLIP   0x08
#define A1_PIXEL  0x0C
#define A1_STEP   0x10
#define A2_BASE   0x24
#define A2_FLAGS  0x28
#define A2_PIXEL  0x30
#define A2_STEP   0x34
#define B_CMD     0x38
#define B_COUNT   0x3C
#define B_PATD    0x68

#define PIXEL1    0x00000000UL
#define PIXEL8    0x00000018UL
#define WID4      0x00001000UL
#define WID8      0x00001800UL
#define WID16     0x00002000UL
#define WID32     0x00002800UL
#define WID320    0x00004200UL
#define WID1536   0x00005400UL     /* padded world-buffer row width */
#define XADDPHR   0x00000000UL
#define XADDPIX   0x00010000UL
#define PATDSEL   0x00010000UL
#define UPDA1     0x00000200UL
#define UPDA2     0x00000400UL
#define SRCEN     0x00000001UL
#define SRCENX    0x00000004UL
#define DSTEN     0x00000008UL
#define LFU_REPLACE 0x01800000UL
#define BCOMPEN   0x04000000UL
#define DCOMPEN   0x08000000UL
#define CLIP_A1   0x00000040UL     /* inhibit writes outside A1_CLIP window */

static void blit_wait(void)
{
    while (!(BREG(B_CMD) & 1))
        ;
}

void blit_fill(unsigned char *buf, int x, int y, int w, int h,
               unsigned char c)
{
    unsigned long pat = (unsigned long)c * 0x01010101UL;
    unsigned long mode = ((x | w) & 7) ? XADDPIX : XADDPHR;

    blit_wait();
    BREG(A1_BASE) = (unsigned long)buf;
    BREG(A1_FLAGS) = PIXEL8 | WID320 | mode;
    BREG(A1_CLIP) = 0;
    BREG(A1_PIXEL) = ((unsigned long)y << 16) | (unsigned short)x;
    BREG(A1_STEP) = (1UL << 16) | (unsigned short)-w;
    BREG(B_PATD) = pat;
    BREG(B_PATD + 4) = pat;
    BREG(B_COUNT) = ((unsigned long)h << 16) | (unsigned short)w;
    BREG(B_CMD) = PATDSEL | UPDA1;
    blit_wait();
}

/* NOTE: BCOMPEN (1bpp mask expansion) was tried for sprites and REMOVED:
 * BigPEmu and Virtual Jaguar implement its source addressing with
 * mutually incompatible quirks (1bpp source: mirrored on BigPEmu;
 * 8bpp source: dithered on VJ), so nothing that renders on both could
 * exist. DCOMPEN transparent-copy below works identically on both and
 * is what the SDK's own font renderer uses. */

/* Opaque full-width row copy between 320-wide 8bpp buffers, phrase mode
 * (8 bytes per memory cycle): dst rows [y, y+h) <- src rows [0, h). */
void blit_rows(unsigned char *dst, const unsigned char *src, int y, int h)
{
    blit_wait();
    BREG(A1_BASE) = (unsigned long)dst;
    BREG(A1_FLAGS) = PIXEL8 | WID320 | XADDPHR;
    BREG(A1_CLIP) = 0;
    BREG(A1_PIXEL) = (unsigned long)y << 16;
    BREG(A1_STEP) = (1UL << 16) | (unsigned short)-320;
    BREG(A2_BASE) = (unsigned long)src;
    BREG(A2_FLAGS) = PIXEL8 | WID320 | XADDPHR;
    BREG(A2_PIXEL) = 0;
    BREG(A2_STEP) = (1UL << 16) | (unsigned short)-320;
    BREG(B_COUNT) = ((unsigned long)h << 16) | 320;
    BREG(B_CMD) = SRCEN | UPDA1 | UPDA2 | LFU_REPLACE;
    blit_wait();
}

/* Opaque copy of a w x h window from a WID1536 source buffer at (sx,sy) into the
 * 320-wide dst at (dx,dy), phrase mode. Used to scroll the camera: the whole
 * forest is pre-decoded into the 1536-wide world buffer once, and each frame a
 * 320x184 window is copied out at the camera offset. x/w must be phrase-aligned
 * (multiples of 8) — they always are (cam_col*8, 320). */
static void blit_copy(unsigned char *dst, int dx, int dy,
                      const unsigned char *src, unsigned long srcwid,
                      int sx, int sy, int w, int h)
{
    blit_wait();
    BREG(A1_BASE) = (unsigned long)dst;
    BREG(A1_FLAGS) = PIXEL8 | WID320 | XADDPHR;
    BREG(A1_CLIP) = 0;
    BREG(A1_PIXEL) = ((unsigned long)dy << 16) | (unsigned short)dx;
    BREG(A1_STEP) = (1UL << 16) | (unsigned short)-w;
    BREG(A2_BASE) = (unsigned long)src;
    BREG(A2_FLAGS) = PIXEL8 | srcwid | XADDPHR;
    BREG(A2_PIXEL) = ((unsigned long)sy << 16) | (unsigned short)sx;
    BREG(A2_STEP) = (1UL << 16) | (unsigned short)-w;
    BREG(B_COUNT) = ((unsigned long)h << 16) | (unsigned short)w;
    BREG(B_CMD) = SRCEN | UPDA1 | UPDA2 | LFU_REPLACE;
    blit_wait();
}

void blit_window(unsigned char *dst, int dx, int dy,
                 const unsigned char *src, int sx, int sy, int w, int h)
{
    blit_copy(dst, dx, dy, src, WID1536, sx, sy, w, h);
}

/* Opaque copy of a w x h window between two 320-wide buffers, from
 * (sx,sy) to (dx,dy), phrase mode; x/w phrase-aligned. */
void blit_copy320(unsigned char *dst, int dx, int dy,
                  const unsigned char *src, int sx, int sy, int w, int h)
{
    blit_copy(dst, dx, dy, src, WID320, sx, sy, w, h);
}

/* Opaque copy of the w x h rect at (x,y) between two 320-wide buffers
 * (same coordinates in both), phrase mode; x/w phrase-aligned. */
void blit_rect(unsigned char *dst, const unsigned char *src,
               int x, int y, int w, int h)
{
    blit_copy(dst, x, y, src, WID320, x, y, w, h);
}

/* 8bpp sprite copy, color 0 transparent: DCOMPEN inhibits the write for
 * pixels where the source equals B_PATD (zero), per the SDK font
 * renderer. Baked sprite images carry their CLUT index per pixel, so
 * per-scanline colors cost nothing extra. imgw may be 8, 16 or 32.
 * x may be NEGATIVE (smooth-scroll edge sprites): the A1 clip window
 * inhibits the off-screen columns in hardware. */
void blit_img8(unsigned char *buf, int x, int y, int w, int h,
               const unsigned char *img, int imgw, int srcy)
{
    blit_wait();
    BREG(A1_BASE) = (unsigned long)buf;
    BREG(A1_FLAGS) = PIXEL8 | WID320 | XADDPIX;
    BREG(A1_CLIP) = (0x7FFFUL << 16) | 320;
    BREG(A1_PIXEL) = ((unsigned long)y << 16) | (unsigned short)x;
    BREG(A1_STEP) = (1UL << 16) | (unsigned short)-w;
    BREG(A2_BASE) = (unsigned long)img;
    BREG(A2_FLAGS) = PIXEL8
                   | (imgw == 32 ? WID32 : imgw == 8 ? WID8 : WID16)
                   | XADDPIX;
    BREG(A2_PIXEL) = ((unsigned long)srcy << 16);
    BREG(A2_STEP) = (1UL << 16) | (unsigned short)-w;
    BREG(B_PATD) = 0;
    BREG(B_PATD + 4) = 0;
    BREG(B_COUNT) = ((unsigned long)h << 16) | (unsigned short)w;
    BREG(B_CMD) = SRCEN | DSTEN | UPDA1 | UPDA2 | LFU_REPLACE | DCOMPEN
                | CLIP_A1;
    blit_wait();
}

