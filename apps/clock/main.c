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

#ifdef GB_AGON
/* The resident accessory has no CRT: c_draw reads its live window geometry. */
static unsigned char win_x, win_y, win_w, win_h;
#else
static unsigned char win_x = DEF_X, win_y = DEF_Y;
static unsigned char win_w = DEF_W, win_h = DEF_H;  /* live size (resizeable) */
#endif
static unsigned char show_sec;               /* 0 = H:M (minute refresh), 1 = +seconds */
static unsigned char ph, pm, ps, pshow, have_prev;  /* previous h/m/s + show state */
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
#if defined(GB_AGON)
#include "agon_display.h"
extern void sys_send(const void *, unsigned);
static void line(int x0, int y0, int x1, int y1, unsigned char pen)
{
    static const unsigned char inks[] = {4,15,0,9};
    unsigned char p[] = {18,0,inks[pen],25,4,x0,x0>>8,AG_PHYS_Y(y0),AG_PHYS_Y(y0)>>8,
                         25,5,x1,x1>>8,AG_PHYS_Y(y1),AG_PHYS_Y(y1)>>8};
    sys_send(p,sizeof(p));
}
#elif defined(GB_MSX2) || defined(GB_PCW)
#ifdef GB_PCW                    /* PCW: k_line Bresenham from low-RAM cells (#331) */
#define GLINE_BASE 0x0F10
#else                            /* MSX: V9938 LINE from the page-3 glue cells */
#define GLINE_BASE 0xC030
#endif
#define GLINE_X0  (*(volatile unsigned int  *)(GLINE_BASE + 0))
#define GLINE_Y0  (*(volatile unsigned int  *)(GLINE_BASE + 2))
#define GLINE_X1  (*(volatile unsigned int  *)(GLINE_BASE + 4))
#define GLINE_Y1  (*(volatile unsigned int  *)(GLINE_BASE + 6))
#define GLINE_PEN (*(volatile unsigned char *)(GLINE_BASE + 8))
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

/* the kernel hands us raw RTC registers; convert BCD -> binary unless it's binary */
static unsigned char bin(unsigned char v)
{
    return gb_binmode ? v : (unsigned char)((v >> 4) * 10 + (v & 15));
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

#ifdef GB_AGON
void agon_clock_draw(unsigned char seconds) { show_sec = seconds; c_draw(); }
void agon_clock_tick(void) { if (changed()) tick(); }
#else
/* --- Options menu + dialogs (gb_doc framework + shared form lifecycle) --------- */

/* on_event: a top-bar title click -> the framework (View / Options) (#142). */
static void clk_event(void) { gb_doc_event(); }

/* set_time_dialog: a standard form modal with shared steppers and action row. */
#define TIME_W 20
#define TIME_H 50
static unsigned char hh, mm;
static unsigned char time_x, time_y;
static const gb_form_action_t time_actions[2] = {
    { "Save", 6, 0 }, { "Cancel", 8, 0 }
};

static void draw_time_stepper(unsigned char x, unsigned char y,
                              unsigned char value)
{
    char t[3];
    put2(t, value); t[2] = 0;
    gb_stepper(x, y, 12, 10, t, 0);
}

static void time_draw(void)
{
    gb_form_label((unsigned char)(time_x + 1),
                  (unsigned char)(time_y + 13), 10, "Hr");
    gb_form_label((unsigned char)(time_x + 1),
                  (unsigned char)(time_y + 25), 10, "Min");
    draw_time_stepper((unsigned char)(time_x + 7),
                      (unsigned char)(time_y + 13), hh);
    draw_time_stepper((unsigned char)(time_x + 7),
                      (unsigned char)(time_y + 25), mm);
    gb_form_actions((unsigned char)(time_x + 2),
                    (unsigned char)(time_y + 39), 9,
                    time_actions, 2, 1);
}

static unsigned char time_click(unsigned char mx, unsigned char my)
{
    unsigned char part, action;
    part = gb_stepper_hit((unsigned char)(time_x + 7),
                          (unsigned char)(time_y + 13), 12, 10, mx, my);
    if (part == GB_STEPPER_DEC || part == GB_STEPPER_INC) {
        hh = (part == GB_STEPPER_INC) ? (unsigned char)((hh + 1) % 24)
                                      : (unsigned char)((hh + 23) % 24);
        return GB_FORM_REDRAW;
    }
    part = gb_stepper_hit((unsigned char)(time_x + 7),
                          (unsigned char)(time_y + 25), 12, 10, mx, my);
    if (part == GB_STEPPER_DEC || part == GB_STEPPER_INC) {
        mm = (part == GB_STEPPER_INC) ? (unsigned char)((mm + 1) % 60)
                                      : (unsigned char)((mm + 59) % 60);
        return GB_FORM_REDRAW;
    }
    action = gb_form_actions_hit((unsigned char)(time_x + 2),
                                 (unsigned char)(time_y + 39), 9,
                                 time_actions, 2, 1, mx, my);
    if (action == 0) return GB_FORM_ACCEPT;
    if (action == 1) return GB_FORM_CANCEL;
    return GB_FORM_STAY;
}

static unsigned char time_key(unsigned char key)
{
    if (key == 0x0D) return GB_FORM_ACCEPT;
    if (key == 0x1B) return GB_FORM_CANCEL;
    return GB_FORM_STAY;
}

static gb_form_modal_t time_modal = {
    0, 0, TIME_W, TIME_H, "Set time",
    time_draw, time_click, time_key, GB_FORM_CLICK_AWAY
};

static unsigned char set_time_dialog(void)
{
    unsigned char result;
    gb_time();
    hh = bin(gb_hour);
    mm = bin(gb_min);
    time_x = (unsigned char)(win_x + ((win_w - TIME_W) >> 1));
    time_y = (unsigned char)(win_y + 34);
    time_modal.x = time_x;
    time_modal.y = time_y;
    result = gb_form_modal_run(&time_modal);
    if (result != GB_FORM_ACCEPT) return 0;
    gb_set_time(hh, mm, 0);
    return 1;
}

/* clk_fullscreen: View > Fullscreen - the analog face rescales to the whole screen
   (relayout derives the radius from the live window rect) (#142). */
static void clk_fullscreen(unsigned char on)
{
    if (on) {
        fs_px = gb_wm_x(); fs_py = gb_wm_y(); fs_pw = gb_wm_w(); fs_ph = gb_wm_h();
        gb_wm_setpos(0, 8);
        gb_wm_setsize((unsigned char)GB_COLS,
                      (unsigned char)(GB_LINES - 8));
    } else {
        gb_wm_setpos(fs_px, fs_py); gb_wm_setsize(fs_pw, fs_ph);
    }
    have_prev = 0;                               /* face rescaled: no stale hands */
    gb_wm_damage(0, 8, (unsigned char)GB_COLS,
                 (unsigned char)(GB_LINES - 8)); /* repaint ONCE in on_frame, clipped to the toggle
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
    show_sec = 0; have_prev = 0;
    gb_wm_managed(&cmw);                         /* register (no draw yet) (#146) */
    gb_doc(&clkdoc);                             /* View > Fullscreen (#142) */
    gb_menu_add("Options", opt_items, 2, opt_action);
    gb_restore_parent();                         /* first paint: WM chrome + c_draw */
}
#endif
