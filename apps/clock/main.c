/* clock - the GEOBENCH analog clock (C), a co-resident window (#72).
 *
 * An xclock-style analog face: a round rim, 12 hour ticks, and hour/minute (and
 * optionally second) hands placed from a fixed-point sin/cos table, with a digital
 * read-out below. By default it shows hours+minutes and refreshes once a minute; an
 * "Options" top-bar menu toggles "Show Seconds" (per-second refresh) and offers
 * "Set time".
 *
 * Flicker-free: the hands are all shorter than the tick ring, so erasing a hand
 * (redrawing it in the background) never touches the rim/ticks - a tick just erases
 * the old hands and draws the new ones, and only lifts the pointer when it actually
 * sits over the window. The kernel has no line primitive (to stay lean); the clock
 * calls the CPC firmware graphics VDU directly for the hands.
 */
#include "gb.h"

#define DEF_X    24
#define DEF_Y    20
#define DEF_W    28           /* default size, bytes (112 px); resizeable (#81) */
#define DEF_H    122          /* px */
#define MIN_W    22
#define MIN_H    96
#define TITLE_H  14

static unsigned char win_x = DEF_X, win_y = DEF_Y;
static unsigned char win_w = DEF_W, win_h = DEF_H;  /* live size (resizeable) */
static unsigned char show_sec;               /* 0 = H:M (minute refresh), 1 = +seconds */
static unsigned char ph, pm, ps, pshow, have_prev;  /* previous h/m/s + show state */
static unsigned char modal;                  /* a self-polling dialog is up (Set time) */
static unsigned char fs_px, fs_py, fs_pw, fs_ph;    /* geometry saved across Fullscreen (#142) */

/* face geometry, recomputed from the live window rect on every full draw (#81): the
   analog face scales to whatever the window has been resized to. */
static int cx, cy;                           /* face centre, screen pixels */
static unsigned char rr, tick_in, l_hour, l_min, l_sec;  /* radius + hand lengths */
static unsigned char dig_y;                  /* digital read-out row (px) */

static void relayout(void)
{
    unsigned char top   = (unsigned char)(win_y + TITLE_H);
    unsigned char dy    = (unsigned char)(win_y + win_h - 16);   /* read-out row */
    unsigned char avail = (unsigned char)(dy - 2 - top);         /* face height band */
    unsigned char rw    = (unsigned char)(win_w * 2 - 6);        /* radius bound: width  */
    unsigned char rh    = (unsigned char)(avail / 2 - 2);        /* radius bound: height */
#ifdef GB_MSX2
    /* rr is the vertical radius; the face is stretched 9/5 horizontally (see px_at),
       so the width budget rw admits a vertical radius of only rw*5/9. Bounding by
       that (not rw) is what lets a horizontal resize grow the face on Screen 6. */
    { unsigned char rwv = (unsigned char)((unsigned int)rw * 5 / 9);
      rr = (rwv < rh) ? rwv : rh; }
#else
    rr = (rw < rh) ? rw : rh;
#endif
    cx = win_x * 4 + win_w * 2;
    cy = top + avail / 2;
    tick_in = (unsigned char)((unsigned int)rr * 37 / 44);       /* keep proportions */
    l_hour  = (unsigned char)((unsigned int)rr * 20 / 44);
    l_min   = (unsigned char)((unsigned int)rr * 30 / 44);
    l_sec   = (unsigned char)((unsigned int)rr * 36 / 44);
    dig_y = dy;
}

static const signed char SIN64[60] = {
      0,  7, 13, 20, 26, 32, 38, 43, 48, 52, 55, 58,
     61, 63, 64, 64, 64, 63, 61, 58, 55, 52, 48, 43,
     38, 32, 26, 20, 13,  7,  0, -7,-13,-20,-26,-32,
    -38,-43,-48,-52,-55,-58,-61,-63,-64,-64,-64,-63,
    -61,-58,-55,-52,-48,-43,-38,-32,-26,-20,-13, -7
};
static const signed char COS64[60] = {
     64, 64, 63, 61, 58, 55, 52, 48, 43, 38, 32, 26,
     20, 13,  7,  0, -7,-13,-20,-26,-32,-38,-43,-48,
    -52,-55,-58,-61,-63,-64,-64,-64,-63,-61,-58,-55,
    -52,-48,-43,-38,-32,-26,-20,-13, -7,  0,  7, 13,
     20, 26, 32, 38, 43, 48, 52, 55, 58, 61, 63, 64
};

/* --- line primitive ----------------------------------------------------------
 * CPC: the firmware graphics VDU (the kernel has no line primitive there).
 * MSX: GB_LINE (#8009) runs the V9938 LINE command from the GLINE_* page-3
 * glue cells - screen pixel coords straight through, no conversion (#287). */
static volatile unsigned int fx0, fy0, fx1, fy1;
static volatile unsigned char fpen;
#ifdef GB_MSX2
#define GLINE_X0  (*(volatile unsigned int  *)0xC030)
#define GLINE_Y0  (*(volatile unsigned int  *)0xC032)
#define GLINE_X1  (*(volatile unsigned int  *)0xC034)
#define GLINE_Y1  (*(volatile unsigned int  *)0xC036)
#define GLINE_PEN (*(volatile unsigned char *)0xC038)
static void fw_line(void) __naked
{
__asm
    call 0x8009          ; GB_LINE (V9938 LINE from the GLINE_* cells)
    ret
__endasm;
}
static void line(int x0, int y0, int x1, int y1, unsigned char pen)
{
    GLINE_X0 = (unsigned int)x0; GLINE_Y0 = (unsigned int)y0;
    GLINE_X1 = (unsigned int)x1; GLINE_Y1 = (unsigned int)y1;
    GLINE_PEN = pen;
    fw_line();
}
#else
static void fw_line(void) __naked
{
__asm
    ld   a, (_fpen)
    call 0xBBDE          ; GRA_SET_PEN  (A = pen)
    ld   de, (_fx0)
    ld   hl, (_fy0)
    call 0xBBC0          ; GRA_MOVE_ABS (DE = x, HL = y)
    ld   de, (_fx1)
    ld   hl, (_fy1)
    call 0xBBF6          ; GRA_LINE_ABS (DE = x, HL = y)
    ret
__endasm;
}

/* line: pixel coords (x 0-319, y 0-199 from top-left) -> firmware coords, then draw. */
static void line(int x0, int y0, int x1, int y1, unsigned char pen)
{
    fx0 = (unsigned int)(x0 * 2); fy0 = (unsigned int)((199 - y0) * 2);
    fx1 = (unsigned int)(x1 * 2); fy1 = (unsigned int)((199 - y1) * 2);
    fpen = pen;
    fw_line();
}
#endif

/* --- RTC write (Set time), straight to the Dallas registers via inline asm ----- */
static volatile unsigned char rtc_reg, rtc_val;
#ifdef GB_MSX2
static void rtc_poke(void) { (void)rtc_reg; (void)rtc_val; }  /* MSX: soft clock only (M4: BIOS RTC) */
#else
static void rtc_poke(void) __naked
{
__asm
    ld   a, (_rtc_reg)
    ld   bc, #0xFD15     ; RTC address port
    out  (c), a
    ld   a, (_rtc_val)
    ld   bc, #0xFD14     ; RTC data port
    out  (c), a
    ret
__endasm;
}
#endif
static void set_rtc(unsigned char reg, unsigned char val) { rtc_reg = reg; rtc_val = val; rtc_poke(); }

/* the kernel hands us raw RTC registers; convert BCD -> binary unless it's binary */
static unsigned char bin(unsigned char v)
{
    return gb_binmode ? v : (unsigned char)((v >> 4) * 10 + (v & 15));
}
static unsigned char tobcd(unsigned char v)
{
    return gb_binmode ? v : (unsigned char)(((v / 10) << 4) | (v % 10));
}

/* --- drawing ----------------------------------------------------------------- */

/* rr is the VERTICAL radius (in lines). On MSX2 Screen 6 a screen pixel is ~half
   as wide as a line is tall, so the horizontal radius is stretched ~9/5 to keep
   the face round; on the CPC x and y are the same (byte-identical to before). */
#ifdef GB_MSX2
static int px_at(unsigned char pos, unsigned char rad) { return cx + ((int)SIN64[pos] * rad / 64) * 9 / 5; }
#else
static int px_at(unsigned char pos, unsigned char rad) { return cx + (int)SIN64[pos] * rad / 64; }
#endif
static int py_at(unsigned char pos, unsigned char rad) { return cy - (int)COS64[pos] * rad / 64; }

static void hand(unsigned char pos, unsigned char len, unsigned char pen)
{
    line(cx, cy, px_at(pos, len), py_at(pos, len), pen);
}

static unsigned char hourpos(unsigned char h, unsigned char m)
{
    return (unsigned char)(((h % 12) * 5 + m / 12) % 60);
}

static void draw_face(void)
{
    unsigned char k, k2;
    for (k = 0; k < 60; k += 2) {                 /* rim: 30 segments */
        k2 = (unsigned char)((k + 2) % 60);
        line(px_at(k, rr), py_at(k, rr), px_at(k2, rr), py_at(k2, rr), 1);
    }
    for (k = 0; k < 60; k += 5)                    /* 12 hour ticks */
        line(px_at(k, rr), py_at(k, rr), px_at(k, tick_in), py_at(k, tick_in), 1);
}

/* draw (or erase, pen 0) the hands for h:m[:s]. withsec controls the second hand. */
static void hands(unsigned char h, unsigned char m, unsigned char s,
                  unsigned char withsec, unsigned char pen_hm, unsigned char pen_s)
{
    hand(hourpos(h, m), l_hour, pen_hm);
    hand(m, l_min, pen_hm);
    if (withsec) hand(s, l_sec, pen_s);
}

static char dig[9];
static void put2(char *p, unsigned char v) { p[0] = '0' + v / 10; p[1] = '0' + v % 10; }
static void draw_digital(unsigned char h, unsigned char m, unsigned char s)
{
    unsigned char x = (unsigned char)(win_x + (win_w - (show_sec ? 12 : 8)) / 2);
    put2(dig, h); dig[2] = ':'; put2(dig + 3, m);
    if (show_sec) { dig[5] = ':'; put2(dig + 6, s); dig[8] = 0; }
    else dig[5] = 0;
    gb_fill((unsigned char)(win_x + (win_w - 16) / 2), dig_y, 16, 8, 0);
    gb_text(x, dig_y, dig);
}

static unsigned char cursor_over(void)
{
    unsigned char mx = gb_mx(), my = gb_my();
    return (unsigned char)(mx >= win_x && mx < win_x + win_w && my >= win_y && my < win_y + win_h);
}

/* on_draw (#146): the WM already drew the frame/title/close; paint just the content
   (face + hands + digital + grip). The face rescales to the live window rect. */
static void c_draw(void)
{
    unsigned char h, m, s;
    win_x = gb_wm_x(); win_y = gb_wm_y(); win_w = gb_wm_w(); win_h = gb_wm_h();
    relayout();
    gb_time();
    h = bin(gb_hour); m = bin(gb_min); s = bin(gb_sec);
    draw_face();
    hands(h, m, s, show_sec, 1, 3);
    draw_digital(h, m, s);
    gb_draw_grip(win_x, win_y, win_w, win_h);   /* resize grip (#81) */
    ph = h; pm = m; ps = s; pshow = show_sec; have_prev = 1;
}

/* tick: move the hands without touching the face. Only lifts the pointer if it is
   actually over the window (else no per-tick cursor flicker). */
static void tick(void)
{
    unsigned char h = bin(gb_hour), m = bin(gb_min), s = bin(gb_sec);
    unsigned char co = cursor_over();
    if (co) gb_curhide();
    if (have_prev) hands(ph, pm, ps, pshow, 0, 0);  /* erase old hands (background) */
    hands(h, m, s, show_sec, 1, 3);                 /* white hands, red second */
    draw_digital(h, m, s);
    if (co) gb_curshow();
    ph = h; pm = m; ps = s; pshow = show_sec; have_prev = 1;
}

/* changed: has the displayed time advanced (minute, or second when shown)? */
static unsigned char changed(void)
{
    if (!have_prev) return 1;
    return show_sec ? (bin(gb_sec) != ps) : (bin(gb_min) != pm);
}

/* --- Options menu + dialogs (gb_doc framework + a bespoke Set-time dialog) ----- */

/* on_event: a top-bar title click -> the framework (View / Options) (#142). */
static void clk_event(void) { gb_doc_event(); }

/* set_time_dialog: +/- on hours and minutes (triangle glyphs), OK applies to the
   RTC. Returns 1 if the time was set. */
static unsigned char hh, mm;
static void draw_field(unsigned char x, unsigned char y, unsigned char v)
{
    char t[3];
    gb_textbw(x, y, GLYPH_TRI_UP);
    put2(t, v); t[2] = 0;
    gb_fill(x, y + 9, 4, 8, 1);
    gb_textbw(x, y + 9, t);
    gb_textbw(x, y + 18, GLYPH_TRI_DOWN);
}
static unsigned char set_time_dialog(void)
{
    unsigned char flags, done = 0, ok = 0;
    unsigned char x = win_x + 3, y = win_y + 34, w = 22, h = 44;
    unsigned char hx = x + 4, mx = x + 13, ay = y + 6;

    gb_time();
    hh = bin(gb_hour); mm = bin(gb_min);
    modal = 1;
    gb_curhide();
    gb_fill(x, y, w, h, 1);
    gb_frame(x, y, w, h, 2);
    gb_textbw(x + 1, y + 1, "Set time");
    gb_textbw(hx + 5, ay + 9, ":");
    draw_field(hx, ay, hh);
    draw_field(mx, ay, mm);
    gb_textbw(x + 7, y + h - 9, "OK");
    gb_curshow();

    while (!done) {
        flags = gb_poll();
        if (flags & GB_QUIT) { done = 1; break; }
        if (!(flags & GB_CLICK)) continue;
        {
            unsigned char cx = gb_mx(), cy = gb_my();
            if (cy >= ay && cy < ay + 8) {                 /* up arrows */
                if (cx >= hx && cx < hx + 3) { hh = (hh + 1) % 24; draw_field(hx, ay, hh); }
                else if (cx >= mx && cx < mx + 3) { mm = (mm + 1) % 60; draw_field(mx, ay, mm); }
            } else if (cy >= ay + 18 && cy < ay + 26) {    /* down arrows */
                if (cx >= hx && cx < hx + 3) { hh = (hh + 23) % 24; draw_field(hx, ay, hh); }
                else if (cx >= mx && cx < mx + 3) { mm = (mm + 59) % 60; draw_field(mx, ay, mm); }
            } else if (cy >= y + h - 9 && cx >= x + 7 && cx < x + 13) {  /* OK */
                ok = 1; done = 1;
            } else if (cy < y || cy >= y + h || cx < x || cx >= x + w) {
                done = 1;                                   /* clicked away -> cancel */
            }
        }
    }
    modal = 0;
    if (flags & GB_QUIT) while (gb_poll() & GB_QUIT) ;
    gb_curhide();
    gb_fill(x, y, w, h, 0);
    gb_curshow();
    if (ok) {
        gb_time();                       /* refresh binmode before writing */
        set_rtc(4, tobcd(hh));           /* hours  */
        set_rtc(2, tobcd(mm));           /* minutes */
        set_rtc(0, tobcd(0));            /* seconds */
    }
    return ok;
}

/* clk_fullscreen: View > Fullscreen - the analog face rescales to the whole screen
   (relayout derives the radius from the live window rect) (#142). */
static void clk_fullscreen(unsigned char on)
{
    if (on) {
        fs_px = gb_wm_x(); fs_py = gb_wm_y(); fs_pw = gb_wm_w(); fs_ph = gb_wm_h();
        gb_wm_setpos(0, 8); gb_wm_setsize(GB_COLS, GB_LINES - 8);
    } else {
        gb_wm_setpos(fs_px, fs_py); gb_wm_setsize(fs_pw, fs_ph);
    }
    have_prev = 0;                               /* face rescaled: no stale hands */
    gb_wm_damage(0, 8, GB_COLS, GB_LINES - 8);   /* repaint ONCE in on_frame, clipped to the toggle
                                                    area; repainting here too flickers (#153) */
}

/* opt_action: the Options menu (gb_menu_add) - Set time / toggle the seconds hand. The
   repaint happens once in on_frame after gb_doc_frame returns (#142). */
static const char *const opt_items[2] = { "Set time", "Toggle Seconds" };
static void opt_action(unsigned char sel)
{
    if (sel == 0) { if (set_time_dialog()) have_prev = 0; }
    else if (sel == 1) show_sec ^= 1;
}

/* no document (no File/Edit); gb_doc adds just View > Fullscreen, Options is added
   on top with gb_menu_add. */
static const gb_doc_t clkdoc = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, clk_fullscreen, 0, 0
};

/* --- WM callbacks ------------------------------------------------------------ */

/* on_frame (#146): the WM handled close/drag/grip routing; run the menu framework and
   tick the hands. (No QUIT / hit-testing here - the kernel does the chrome.) */
static void c_frame(void)
{
    win_x = gb_wm_x(); win_y = gb_wm_y(); win_w = gb_wm_w(); win_h = gb_wm_h();
    if (gb_doc_frame()) { gb_restore_parent(); return; }   /* a View/Options item ran (#142) */
    gb_time();
    if (changed()) tick();
}

/* on_click (#146): the only content gesture is the resize grip. */
static void c_click(void)
{
    win_x = gb_wm_x(); win_y = gb_wm_y(); win_w = gb_wm_w(); win_h = gb_wm_h();
    if (gb_in_grip(win_x, win_y, win_w, win_h, gb_mx(), gb_my()))
        if (gb_drag_resize(win_x, win_y, &win_w, &win_h, MIN_W, MIN_H)) {
            gb_wm_setsize(win_w, win_h);
            have_prev = 0;                         /* face rescaled: no stale hands */
            gb_restore_parent();
        }
}

/* on_drag (#146): a title-bar press -> move the window. */
static void c_drag(void)
{
    win_x = gb_wm_x(); win_y = gb_wm_y();
    if (gb_drag_window(&win_x, &win_y, gb_wm_w(), gb_wm_h())) {
        gb_wm_setpos(win_x, win_y);
        gb_restore_parent();
    }
}

/* the window's single handler (#148). */
static void c_proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  c_draw();      break;
        case GB_MSG_CLICK: c_click();     break;
        case GB_MSG_FRAME: c_frame();     break;
        case GB_MSG_CLOSE: gb_wm_close(); break;   /* no confirm: just close */
        case GB_MSG_DRAG:  c_drag();      break;
        case GB_MSG_MENU:
        case GB_MSG_DROP:  clk_event();   break;
    }
}

static const gb_mwin_t cmw = {
    DEF_X, DEF_Y, DEF_W, DEF_H, MIN_W, MIN_H, c_proc, "Clock"
};

void main(void)
{
    show_sec = 0; have_prev = 0; modal = 0;
    gb_wm_managed(&cmw);                         /* register (no draw yet) (#146) */
    gb_doc(&clkdoc);                             /* View > Fullscreen (#142) */
    gb_menu_add("Options", opt_items, 2, opt_action);
    gb_restore_parent();                         /* first paint: WM chrome + c_draw */
}
