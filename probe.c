/* Jaguar hardware probe — measures timing a Jaguar program depends on,
 * on whatever runs it (real Jaguar via JagGD, BigPEmu, Virtual Jaguar), and
 * saves the numbers to the cart EEPROM (the JagGD keeps it as a 128-byte
 * .e2p next to the .rom). Results are also shown on screen as a hex dump.
 * decode.py reads the save; README.md maps the words.
 *
 * Time base: Jerry timer 2 free-running, prescaler 1 (= 2 system clocks per
 * tick, ~75.2 ns; the divider wraps every 65536 ticks, ~4.9 ms). The 68K and
 * DSP both read its down-counting divider at $F1003C. Every duration below
 * is in these ticks.
 *
 * Load conditions (Object Processor / blitter bus pressure):
 *   0  OP list with no bitmap (nothing fetched)
 *   1  one 320x200 8bpp bitmap
 *   2  two stacked copies of it
 *   3  three stacked copies (run last: v1 used 4 and 8, and 8 full-width
 *      8bpp bitmaps is more than the OP can draw per scanline — on real
 *      hardware it never reaches STOP, owns the bus and the screen goes
 *      black; neither emulator models that limit)
 *   4  one bitmap + the 68K blitting full-screen fills nonstop
 * Under each, the DSP times every I2S interrupt and K=8 DRAM loads, and the
 * 68K counts how many iterations of a fixed loop it gets per frame.
 */

#include "text.h"
#include "blit.h"

#define VERSION 2
#define MAGIC   0x5052          /* "PR" */

#define REG16(a) (*(volatile unsigned short *)(a))
#define REG32(a) (*(volatile unsigned long *)(a))

#define HC       REG16(0xF00004)
#define VC       REG16(0xF00006)
#define OLP      REG32(0xF00020)
#define VMODE    REG16(0xF00028)
#define CONFIG   REG16(0xF14002)
#define JPIT3    REG16(0xF10004)
#define JPIT4    REG16(0xF10006)
#define TIMER    REG16(0xF1003C)
#define D_PC     REG32(0xF1A110)
#define D_CTRL   REG32(0xF1A114)
#define LEVEL0   REG32(0x100)
#define clut     ((volatile unsigned short *)0xF00400)

#define D_RAM    0xF1B000UL
#define PB(i)    REG32(0xF1B800UL + (i) * 4UL)
enum { PB_CTL, PB_COUNT, PB_PMIN, PB_PMAX, PB_DMAX, PB_DSUM, PB_LATE,
       PB_K, PB_ADDR, PB_THR };

extern volatile long ticks;
extern unsigned char vidmem[], vidmem2[];
extern unsigned char ProbeDsp[], ProbeDspEnd[];
extern void probe_vbl(void);
extern unsigned long ol_live[];
extern unsigned long *volatile ol_src;
extern volatile unsigned short ol_nlongs;
extern long probe_vdb(void), probe_vde(void), probe_height(void),
            probe_width(void);
extern int eeprom_read(int addr);
extern int eeprom_write(int addr, int val);

/* ------------------------------------------------------------ results */

static unsigned short res[64];
static int ee_fail;

static int dsp_reset(void);

static void put(int w, unsigned long v)
{
    res[w] = (unsigned short)v;
    if (eeprom_write(w, res[w]) != 0)
        ee_fail++;
}

static void put32(int w, unsigned long v)
{
    put(w, v >> 16);
    put(w + 1, v & 0xFFFF);
}

/* ------------------------------------------------------------ screen */

static int row = 2;

static void hex4(char *p, unsigned v)
{
    static const char hx[] = "0123456789ABCDEF";
    p[0] = hx[(v >> 12) & 15];
    p[1] = hx[(v >> 8) & 15];
    p[2] = hx[(v >> 4) & 15];
    p[3] = hx[v & 15];
}

static void say(const char *s)
{
    if (row >= 24)
        row = 2;
    blit_fill(vidmem, 0, row * 8, 320, 8, 0);
    text_draw(vidmem, 8, row * 8, s, 1);
    row++;
}

/* ------------------------------------------------------------ timing */

static void wait_tick(void)
{
    long t = ticks;
    while (ticks == t)
        ;
}

static unsigned tdiff(unsigned short a, unsigned short b)
{
    return (unsigned short)(a - b);      /* down counter: earlier - later */
}

/* ------------------------------------------------------------ OP lists */

static unsigned long tmpl[5][64];
static unsigned short tmpl_n[5];

/* One phrase = two longs, high long first (OP is big-endian 64-bit). */
static void phr(unsigned long *p, unsigned long hi, unsigned long lo)
{
    p[0] = hi;
    p[1] = lo;
}

static void build_list(int load, int nbmp)
{
    unsigned long *t = tmpl[load];
    unsigned long base = (unsigned long)ol_live;
    int nphr = 2 + 2 * nbmp + 1;
    unsigned long stop = base + (unsigned long)(nphr - 1) * 8;
    unsigned long vdb = probe_vdb(), vde = probe_vde();
    unsigned long ypos = ((probe_height() - 200 + vdb) & ~1UL);
    unsigned long xpos = ((probe_width() >> 2) - 320) >> 1;
    int i;

    /* branches (as startup.s InitLister): skip to STOP outside vdb..vde */
    phr(t + 0, stop >> 11,
        (((stop >> 3) & 0xFF) << 24) | (2UL << 14) | (vde << 3) | 3);
    phr(t + 2, stop >> 11,
        (((stop >> 3) & 0xFF) << 24) | (1UL << 14) | (vdb << 3) | 3);
    for (i = 0; i < nbmp; i++) {
        unsigned long next = base + (unsigned long)(2 + 2 * (i + 1)) * 8;
        unsigned long *o = t + 4 + 4 * i;
        if (i == nbmp - 1)
            next = stop;
        phr(o, (((unsigned long)vidmem >> 3) << 11) | (next >> 11),
            (((next >> 3) & 0xFF) << 24) | (200UL << 14) | (ypos << 3));
        phr(o + 2, 40UL >> 4,
            ((40UL & 0xF) << 28) | (40UL << 18) | (1UL << 15) | (3UL << 12)
            | xpos);
    }
    phr(t + 2 * (nphr - 1), 0, 4);       /* STOP */
    tmpl_n[load] = (unsigned short)(nphr * 2);
}

static void set_load(int load)
{
    wait_tick();
    ol_nlongs = 0;                       /* handler skips the copy ...   */
    ol_src = tmpl[load];
    ol_nlongs = tmpl_n[load];            /* ... until both are in place  */
    wait_tick();
    wait_tick();
}

/* ------------------------------------------------------------ DSP */

static void dsp_start(void)
{
    volatile unsigned long *dst = (volatile unsigned long *)D_RAM;
    unsigned long *src = (unsigned long *)ProbeDsp;
    long n = (ProbeDspEnd - ProbeDsp + 3) / 4;

    D_CTRL = 0;
    while (n-- > 0)
        *dst++ = *src++;
    PB(PB_CTL) = 0;
    PB(PB_K) = 8;
    PB(PB_ADDR) = 0x3000;
    PB(PB_THR) = 0xFFFF;
    D_PC = D_RAM;
    D_CTRL = 1;
}

/* Calibrate "late": 1.5 x the shortest interrupt period seen while idle
 * (the timer rate itself is one of the things being measured). */
static unsigned dsp_calibrate(void)
{
    unsigned thr;
    int i;
    set_load(0);
    wait_tick();
    dsp_reset();
    for (i = 0; i < 8; i++)
        wait_tick();
    thr = (unsigned)PB(PB_PMIN);
    if (thr == 0 || thr == 0xFFFF)
        thr = 0xFFFF;                    /* timer unreadable: no "late" */
    else
        thr += thr / 2;
    PB(PB_THR) = thr;
    return thr;
}

static int dsp_reset(void)
{
    long t0 = ticks;
    PB(PB_CTL) = 1;
    while (PB(PB_CTL) != 0)
        if (ticks - t0 > 10)
            return 0;                    /* DSP not interrupting */
    return 1;
}

/* ------------------------------------------------------------ tests */

static unsigned long loop_frame(void)
{
    unsigned long n = 0;
    long t;

    wait_tick();
    t = ticks;
    while (ticks == t)
        n++;
    return n;
}

static void run_load(int load, int w)
{
    static const char *name[5] = { "NO BITMAP", "1 BITMAP", "2 BITMAPS",
                                   "3 BITMAPS", "BLITTER" };
    char msg[24];
    unsigned long loops_min = 0xFFFFFFFFUL, count = 0, pmin = 0xFFFF,
                  pmax = 0, dmax = 0, dsum = 0, late = 0, frames = 0;
    int rep, i;

    msg[0] = 'L'; msg[1] = 'O'; msg[2] = 'A'; msg[3] = 'D'; msg[4] = ' ';
    msg[5] = (char)('0' + load);
    msg[6] = ' ';
    for (i = 0; name[load][i]; i++)
        msg[7 + i] = name[load][i];
    msg[7 + i] = 0;
    say(msg);
    set_load(load);
    for (rep = 0; rep < 3; rep++) {
        long t0;
        unsigned long l = 0;
        wait_tick();
        if (!dsp_reset())
            say("  DSP NOT RUNNING");
        t0 = ticks;
        if (load == 4) {
            while (ticks - t0 < 16) {
                blit_fill(vidmem2, 0, 0, 320, 200, (unsigned char)l);
                l++;
            }
        } else {
            unsigned long s = 0;
            for (i = 0; i < 16; i++)
                s += loop_frame();
            l = s / 16;
        }
        frames += (unsigned long)(ticks - t0);
        count += PB(PB_COUNT);
        if (PB(PB_PMIN) < pmin) pmin = PB(PB_PMIN);
        if (PB(PB_PMAX) > pmax) pmax = PB(PB_PMAX);
        if (PB(PB_DMAX) > dmax) dmax = PB(PB_DMAX);
        dsum += PB(PB_DSUM);
        late += PB(PB_LATE);
        if (l < loops_min) loops_min = l;
    }
    put(w + 0, loops_min);
    put(w + 1, count);
    put(w + 2, pmin);
    put(w + 3, pmax);
    put(w + 4, dmax);
    put(w + 5, count ? dsum / count : 0);
    put(w + 6, late);
    put(w + 7, frames);
}

static unsigned blit_time(int copy)
{
    unsigned short a, b;
    blit_fill(vidmem2, 0, 0, 8, 1, 0);   /* blitter idle */
    a = TIMER;
    if (copy)
        blit_rows(vidmem2, vidmem2 + 320 * 100, 0, 100);
    else
        blit_fill(vidmem2, 0, 0, 320, 100, 7);
    b = TIMER;
    return tdiff(a, b);
}

void __main(void)
{
    int i, boot;
    char line[41];

    /* palette: 0 black, 1 white, 2 green, 3 yellow (RGB16: R5 B5 G6) */
    clut[0] = 0;
    clut[1] = 0xFFFF;
    clut[2] = 0x003F;
    clut[3] = 0xF83F;
    blit_fill(vidmem, 0, 0, 320, 200, 0);

    /* take over the VBL handler + object list (one bitmap) */
    for (i = 0; i < 5; i++)
        build_list(i, i == 0 ? 0 : i == 2 ? 2 : i == 3 ? 3 : 1);
    /* a 68000 long write is two bus cycles: mask interrupts so a VBL can't
     * vector through a half-written LEVEL0 */
    __asm__ volatile ("move.w #0x2700,%%sr" ::: "memory");
    LEVEL0 = (unsigned long)probe_vbl;
    __asm__ volatile ("move.w #0x2000,%%sr" ::: "memory");
    ol_src = tmpl[1];
    ol_nlongs = tmpl_n[1];
    wait_tick();
    VMODE = 0;
    OLP = ((unsigned long)ol_live << 16) | ((unsigned long)ol_live >> 16);
    VMODE = 0x6C7;

    text_draw(vidmem, 8, 0, "JAGUAR HARDWARE PROBE V2", 3);

    /* EEPROM header + boot count: proves saves persist between runs */
    boot = (eeprom_read(0) == MAGIC) ? eeprom_read(2) + 1 : 1;
    for (i = 0; i < 64; i++)
        res[i] = 0;
    put(0, MAGIC);
    put(1, VERSION);
    put(2, boot);
    put(3, 1);                           /* stage */
    for (i = 4; i < 64; i++)             /* clear last run's numbers */
        put(i, 0);
    say(ee_fail ? "EEPROM WRITE FAILED" : "EEPROM OK");
    say("VIDEO + TIMER...");

    /* timer 2 free-running: prescaler 1, divider $FFFF */
    JPIT3 = 1;
    JPIT4 = 0xFFFF;

    /* environment */
    put(4, CONFIG);
    {
        unsigned vmax = 0, hmax = 0, changes = 0;
        unsigned short last = TIMER;
        long t0;
        wait_tick();
        t0 = ticks;
        while (ticks - t0 < 2) {
            unsigned v = VC & 0x7FF, h = HC & 0x3FF;
            if (v > vmax) vmax = v;
            if (h > hmax) hmax = h;
        }
        for (i = 0; i < 1000; i++) {
            unsigned short t = TIMER;
            if (t != last)
                changes++;
            last = t;
        }
        put(5, vmax);
        put(6, hmax);
        put(7, changes);
    }
    say("FRAME PERIOD...");

    /* frame period: 16 frames of timer deltas, plus per-frame spread */
    {
        unsigned long acc = 0, fmin = 0xFFFFFFFFUL, fmax = 0, fstart = 0;
        unsigned short prev;
        long t0, seen;
        wait_tick();
        prev = TIMER;
        t0 = seen = ticks;
        while (ticks - t0 < 16) {
            unsigned short t = TIMER;
            acc += tdiff(prev, t);
            prev = t;
            if (ticks != seen) {
                unsigned long f = acc - fstart;
                if (f < fmin) fmin = f;
                if (f > fmax) fmax = f;
                fstart = acc;
                seen = ticks;
            }
        }
        put32(8, acc / 16);
        put(10, fmax - fmin);
    }
    put(3, 2);
    say("BLITTER...");

    /* blitter speed under light and heavy OP load */
    put(11, blit_time(0));
    put(12, blit_time(1));
    set_load(2);
    put(13, blit_time(0));
    set_load(1);
    put(3, 3);

    /* DSP: I2S interrupt timing + DRAM latency under each load */
    dsp_start();
    put(15, dsp_calibrate());
    {
        static const int order[5] = { 0, 1, 4, 2, 3 };   /* heaviest last */
        for (i = 0; i < 5; i++) {
            run_load(order[i], 16 + 8 * order[i]);
            put(3, 4 + i);
        }
    }
    set_load(1);
    D_CTRL = 0;

    /* checksum + verify the save */
    put(14, ee_fail);
    put(3, 0xFF);
    {
        unsigned sum = 0;
        int bad = 0;
        for (i = 0; i < 63; i++)
            sum += res[i];
        put(63, sum & 0xFFFF);
        for (i = 0; i < 64; i++)
            if ((unsigned short)eeprom_read(i) != res[i])
                bad++;
        say(bad ? "EEPROM VERIFY FAILED" : "SAVED + VERIFIED");
    }

    /* hex dump: 8 rows x 8 words */
    blit_fill(vidmem, 0, 8, 320, 192, 0);
    text_draw(vidmem, 8, 16, "DONE. RESULTS SAVED TO CART EEPROM:", 2);
    for (i = 0; i < 8; i++) {
        int j;
        for (j = 0; j < 40; j++)
            line[j] = ' ';
        line[40] = 0;
        for (j = 0; j < 8; j++)
            hex4(line + j * 5, res[i * 8 + j]);
        text_draw(vidmem, 0, 32 + i * 12, line, 1);
    }
    text_draw(vidmem, 8, 136, "BOOT COUNT", 3);
    {
        char b[5];
        b[4] = 0;
        hex4(b, boot);
        text_draw(vidmem, 104, 136, b, 3);
    }
    text_draw(vidmem, 8, 160, "YOU CAN RESET / POWER OFF NOW.", 2);
    text_draw(vidmem, 8, 172, "RUN IT TWICE TO TEST SAVE PERSISTENCE.", 2);
    for (;;)
        ;
}
