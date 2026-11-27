/* saver - SQUARES: the GEOBENCH screensaver (#281, replaces the banding CIRCLE).
 *
 * Random rectangles popping up all over the screen - random size, position and
 * colour - with a periodic full clear. gb_fill IS the square primitive, so every
 * square is exact by construction (no curve rasterising to go wrong, none of the
 * overlap artifacts that made the circle saver degenerate into solid bands).
 *
 * A screensaver is just an app (built like any other, shipped with a .SAV extension
 * instead of .APP). The desktop's idle timer launches it via gb_wm_open after the
 * SAVER timeout; it runs as a normal co-resident window. What makes it a "saver":
 *   - it sets WM_FS so the desktop's top bar steps aside and it owns all 200 lines;
 *   - its per-frame handler animates, and closes the moment it sees ANY input
 *     (a typed key, a click/fire/ESC, or a pointer move), handing back to the desktop. */
#include "gb.h"

/* WM_FS (#130A): the app-side full-screen flag the desktop's bar_draw reads each
 * frame. 1 = a borderless window owns lines 0-7, so the bar suppresses itself. */
#define WM_FS (*(volatile unsigned char *)0x130A)

#define CLEAR_EVERY 120    /* squares between full clears (~2.4 s at one per frame) */

static unsigned char lmx, lmy;   /* pointer position at launch - a change = wake */
static unsigned char armed;      /* 0 until one clean frame: swallows the launch click */
#ifdef GB_MSX2
static unsigned char settle;     /* MSX: frames left to keep draining the launch Space */
#endif
static unsigned int  rng;        /* PRNG state (16-bit LCG) */
static unsigned char tick;       /* squares since the last clear */

/* rnd: 16-bit LCG (full 65536 period). rr(n) reduces its well-distributed HIGH byte
 * uniformly onto 0..n-1 - never the weak low bits (rnd() % n). rr is a MACRO: as a
 * uchar-arg function SDCC (--fomit-frame-pointer) miscompiled it to a constant, so
 * every shape landed on the same spot (see the SDCC gotchas in the README). */
static unsigned int rnd(void) { rng = (unsigned int)(rng * 25173u + 13849u); return rng; }
#define rr(n) ((unsigned char)(((rnd() >> 8) * (unsigned int)(n)) >> 8))

/* step: one square per frame, then the periodic clear. Width is in byte columns
 * (1 byte = 4 Mode-1 pixels); the height is scaled ~x3.3 so the rectangle LOOKS
 * square on the 320x200 display. Position is clamped so the square fits. */
static void step(void)
{
    unsigned char w   = (unsigned char)(2 + rr(9));            /* 2..10 bytes = 8..40 px wide */
#ifdef GB_AGON
    unsigned char h   = w * 4;
#else
    unsigned char h   = (unsigned char)((w * 10) / 3);         /* visually square height */
#endif
    unsigned char x   = rr((unsigned char)(GB_COLS - w));       /* fits horizontally */
    unsigned char y   = rr((unsigned char)(GB_LINES - h));      /* fits vertically */
    unsigned char pen = (unsigned char)(1 + rr(3));            /* pens 1..3 (not the bg) */
    gb_fill(x, y, w, h, pen);
    if (++tick >= CLEAR_EVERY) { tick = 0; gb_fill(0, 0, GB_COLS, GB_LINES, 0); }
}

static void ss_paint(void)
{
    gb_fill(0, 0, GB_COLS, GB_LINES, 0);   /* blank to pen 0; step() fills it in */
    tick = 0;
}

#ifdef GB_AGON
void agon_squares_start(unsigned int seed) { rng = seed; ss_paint(); }
void agon_squares_tick(void) { step(); }
#else
static void ss_frame(void)
{
    unsigned char f = gb_flags();
    /* The click/key that launched us is still settling on the first frame(s) - a held
       fire reads as GB_FIRE and would close us instantly. Wait for one clean frame
       before we start watching for a wake; keep animating meanwhile. */
    if (!armed) {
        while (gb_getkey()) ;                            /* drain residual keystrokes */
        if (!(f & (GB_CLICK | GB_FIRE | GB_QUIT))) {
            lmx = gb_mx(); lmy = gb_my();                /* re-baseline the pointer */
            armed = 1;
#ifdef GB_MSX2
            settle = 8;                                  /* on MSX the fire IS the Space bar,
                              which also feeds a char into the BIOS keyboard buffer AFTER this
                              drain - keep draining it for a few frames or it wakes us at once. */
#endif
        }
    }
#ifdef GB_MSX2
    else if (settle) {                                   /* still swallowing the launch Space */
        settle--;
        while (gb_getkey()) ;
    }
#endif
    else if (gb_getkey() ||                              /* a typed key */
               (f & (GB_CLICK | GB_FIRE | GB_QUIT)) ||   /* a click / fire / ESC */
               gb_mx() != lmx || gb_my() != lmy) {       /* the pointer moved */
        WM_FS = 0;                                       /* let the top bar redraw */
        gb_wm_close();                                   /* hand control to the desktop */
        return;
    }
    step();
}

static const gb_win_t sswin = { 0, 0, GB_COLS, GB_LINES, ss_frame, ss_paint, 0, 0 };

void main(void)
{
    unsigned char n;
    lmx = gb_mx(); lmy = gb_my();   /* baseline: wake when the pointer leaves here */
    armed = 0;
    gb_time();                      /* seed the PRNG from the clock (differs each launch) */
    rng = (unsigned int)((gb_sec << 8) ^ (gb_min << 3) ^ gb_hour ^ 0xACE1u);
    if (!rng) rng = 0xACE1u;
    WM_FS = 1;                      /* full screen: the desktop bar steps aside */
    gb_curhide();                   /* no pointer over the blank screen */
    for (n = 64; n; n--) if (!gb_getkey()) break;  /* drain any launch keystrokes */
    ss_paint();                     /* clear now (don't wait for the first repaint) */
    gb_wm_add(&sswin);              /* register the window, return to the opener */
}
#endif
