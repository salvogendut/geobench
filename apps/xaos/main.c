/*
 * XAOS.APP - GEOBENCH's first "fun" app (#116): a fixed-point Mandelbrot generator,
 * inspired by XaoS (https://github.com/xaos-project/XaoS) but trimmed to what a
 * 4 MHz Z80 in Mode 1 (4 colours) can do. NOT real-time zoom (XaoS's incremental
 * approximation is out of reach here) - a render-then-explore fractal viewer.
 *
 * A window shows the current fractal; on-screen [+]/[-] buttons zoom in/out, a
 * click on the canvas recentres there, and a top-bar File > Save writes the picture
 * as a .PIC (the versioned format the Viewer displays). Uses the fractal.png icon.
 *
 * Maths is Q4.12 signed fixed point (16-bit: 4 integer bits incl. sign, 12
 * fractional). The hot op - a signed (a*b)>>12 - is a hand-written Z80 routine
 * (fixmul); the per-pixel escape loop is C around it. The canvas is a Mode-1 bitmap
 * blitted with gb_restorerect; each row shows as it finishes. The canvas lives
 * inside a .PIC buffer (header + bitmap) so Save is a single gb_fs_save.
 */
#include "gb.h"

#define DEF_X     6
#define DEF_Y     14
#define TITLE_H   14

#define CANVAS_WB 20                    /* 80 px / 4 */
#define CANVAS_W  (CANVAS_WB * 4)       /* 80 px */
#define CANVAS_H  56
#define WIN_W     (CANVAS_WB + 2)
#define WIN_H     (TITLE_H + CANVAS_H + 14)        /* title + canvas + control strip */

#define MAXITER   20

static unsigned char win_x = DEF_X, win_y = DEF_Y;
static unsigned char winw = WIN_W, winh = WIN_H;   /* live window size (Fullscreen, #142) */
static unsigned char ox = DEF_X, oy = DEF_Y;       /* content origin (centered when bigger) */
static void recalc_origin(void);                   /* defined below */
#define CVX    (unsigned char)(ox + 1)               /* canvas left, byte column */
#define CVY    (unsigned char)(oy + TITLE_H + 1)     /* canvas top, screen row   */
#define CTRL_Y (unsigned char)(CVY + CANVAS_H + 2)   /* control strip (the +/- buttons) */

/* .PIC v2 header (14 bytes) then the canvas, in one buffer so Save is one fs call.
   See apps/paint/main.c for the layout. */
#define PIC_HDR 14
#define PIC_LEN (PIC_HDR + CANVAS_WB * CANVAS_H)
static unsigned char picbuf[PIC_LEN];
#define canvas (picbuf + PIC_HDR)
static const unsigned char pic_inks[4] = { 1, 26, 0, 6 };   /* GEOBENCH palette inks */

/* view: centre (cx0,cy0) and per-pixel step, all Q4.12. Home = the whole set. */
#define HOME_CX   (-2048)               /* -0.5 */
#define HOME_CY   0
#define HOME_STEP 160                   /* ~0.039/px -> ~3.1 wide, square pixels */
static int cx0 = HOME_CX, cy0 = HOME_CY, step = HOME_STEP;

/* The document is the fractal DEFINITION - a small .TXT of the view maths (Save/Load);
   "Save as PIC" exports the rendered image separately. A fresh fractal is only generated
   on File>New, not at launch. */
#define DEFMAX   512                   /* gb_fs_load copies whole sectors -> >= 1 sector */
static unsigned char defbuf[DEFMAX];   /* the .TXT definition (the gb_doc buffer) */
static unsigned char generated;        /* 1 once a fractal is rendered (New / Load) */
static char name83[11];                /* 8.3 scratch for the PIC export name */
static char fbase[14];                 /* title: NAME.EXT from gb_doc_name() */
static char wtitle[18];                /* fbase + " *" when modified */
static char namebuf[16];               /* gb_prompt target for Save as PIC */

/* ---- Q4.12 signed multiply: fr = (fa * fb) >> 12 (hand-written, the hot op) ---- */
static int fa, fb, fr;
static void fixmul(void) __naked
{
__asm
        ld   hl,(_fa)
        ld   de,(_fb)
        ld   a,h
        xor  d
        push af                  ; result sign in bit 7
        bit  7,h                 ; |fa| -> HL
        jr   z,1$
        ld   a,h
        cpl
        ld   h,a
        ld   a,l
        cpl
        ld   l,a
        inc  hl
1$:     bit  7,d                 ; |fb| -> DE
        jr   z,2$
        ld   a,d
        cpl
        ld   d,a
        ld   a,e
        cpl
        ld   e,a
        inc  de
2$:     ld   b,h                 ; BC = multiplicand |fa|, DE = multiplier |fb|
        ld   c,l
        ld   hl,#0
        ld   a,#16
3$:     add  hl,hl               ; DE:HL <<= 1, add BC on multiplier carry-out
        rl   e
        rl   d
        jr   nc,4$
        add  hl,bc
        jr   nc,4$
        inc  de
4$:     dec  a
        jr   nz,3$               ; DE:HL = |fa| * |fb|
        ld   a,#4                 ; >>12 = shift DE:HL left 4, keep the high word DE
5$:     add  hl,hl
        rl   e
        rl   d
        dec  a
        jr   nz,5$
        ex   de,hl               ; HL = |result|
        pop  af
        bit  7,a                 ; apply the sign
        jr   z,6$
        ld   a,h
        cpl
        ld   h,a
        ld   a,l
        cpl
        ld   l,a
        inc  hl
6$:     ld   (_fr),hl
        ret
__endasm;
}
static int FMUL(int a, int b) { fa = a; fb = b; fixmul(); return fr; }

/* mand: escape iterations for c = (cx,cy) in Q4.12 (the Mandelbrot inner loop). */
static unsigned char mand(int cx, int cy)
{
    int zx = 0, zy = 0, zx2, zy2;
    unsigned char i;
    for (i = 0; i < MAXITER; i++) {
        zx2 = FMUL(zx, zx);
        zy2 = FMUL(zy, zy);
        if ((unsigned int)(zx2 + zy2) >= 0x4000u) break;   /* |z|^2 >= 4 -> escaped */
        zy = 2 * FMUL(zx, zy) + cy;
        zx = zx2 - zy2 + cx;
    }
    return i;
}

/* iteration count -> a Mode-1 pen (0 blue, 1 white, 2 black, 3 red) */
static unsigned char pen_of(unsigned char it)
{
    if (it >= MAXITER) return 2;        /* inside the set: black */
    if (it < 5)  return 0;              /* far outside: blue   */
    if (it < 11) return 3;              /* mid:         red    */
    return 1;                           /* near the edge: white */
}

/* ---- Mode-1 pixel packing (pixel i: bit0 @ 7-i, bit1 @ 3-i) ------------------ */
static unsigned char set_pixel(unsigned char b, unsigned char i, unsigned char p)
{
    b &= (unsigned char)~((1 << (7 - i)) | (1 << (3 - i)));
    if (p & 1) b |= (unsigned char)(1 << (7 - i));
    if (p & 2) b |= (unsigned char)(1 << (3 - i));
    return b;
}

static void blit_row(unsigned char row)
{
    gb_curhide();
    gb_restorerect(CVX, (unsigned char)(CVY + row), CANVAS_WB, 1,
                   canvas + (unsigned int)row * CANVAS_WB);
    gb_curshow();
}
static void blit_canvas(void)
{
    gb_curhide();
    gb_restorerect(CVX, CVY, CANVAS_WB, CANVAS_H, canvas);
    gb_curshow();
}

/* Incremental render: a full Mandelbrot is ~15-20s on a 4 MHz Z80, so don't compute
   it in one blocking call (that froze the whole desktop and let the parked pointer's
   save-under bake a hole into the canvas). Instead render_start() arms it and x_frame
   computes ONE row per frame - the pointer, window drag and close stay live, you watch
   it paint top-down, and any key aborts (handled in x_frame). */
static unsigned char rendering;        /* 1 while a render is in progress */
static unsigned char render_px;        /* next canvas pixel column to compute */
static unsigned char render_py;        /* next canvas row to compute */

static void render_start(void) { rendering = 1; render_px = 0; render_py = 0; }

/* render_step: compute a small budget of pixels per frame and blit each row as it
   finishes. A whole row is ~0.3s of Z80 fixed-point maths, so doing one per frame
   pinned the loop at ~3 Hz and felt frozen; a sub-row budget keeps the pointer
   smooth (the loop, and the k_poll cursor move, run every budget instead). */
#define RENDER_BUDGET 16               /* canvas pixels computed per frame */
static void render_step(void)
{
    unsigned char n = RENDER_BUDGET, b;
    int cx, cy;
    cy = cy0 + (int)((int)render_py - CANVAS_H / 2) * step;
    while (n--) {
        unsigned int off;
        if (render_px == 0)                         /* new row: clear it first */
            for (b = 0; b < CANVAS_WB; b++)
                canvas[(unsigned int)render_py * CANVAS_WB + b] = 0;
        off = (unsigned int)render_py * CANVAS_WB + (render_px >> 2);
        cx = cx0 + (int)((int)render_px - CANVAS_W / 2) * step;
        canvas[off] = set_pixel(canvas[off], (unsigned char)(render_px & 3), pen_of(mand(cx, cy)));
        if (++render_px >= CANVAS_W) {              /* row finished -> blit it */
            blit_row(render_py);
            render_px = 0;
            if (++render_py >= CANVAS_H) { rendering = 0; return; }
            cy = cy0 + (int)((int)render_py - CANVAS_H / 2) * step;
        }
    }
}

/* ---- window chrome ---------------------------------------------------------- */
static void fmt83(char *dst, const char *n11);   /* below */
static const char *win_title(void)
{
    /* name from gb_doc; single-index copy (the two-index form is miscompiled by SDCC
       under --fomit-frame-pointer, #142). */
    unsigned char j = 0;
    char c;
    fmt83(fbase, gb_doc_name());
    while ((c = fbase[j]) != 0) { wtitle[j] = c; j++; }
    if (gb_doc_modified()) { wtitle[j++] = ' '; wtitle[j++] = '*'; }
    wtitle[j] = 0;
    return wtitle;
}

static void btn(unsigned char bx, const char *label)
{
    gb_fill(bx, CTRL_Y, 4, 9, 1);
    gb_frame(bx, CTRL_Y, 4, 9, 2);
    gb_textbw((unsigned char)(bx + 1), (unsigned char)(CTRL_Y + 1), label);
}
static void draw_buttons(void)
{
    gb_curhide();
    btn((unsigned char)(ox + 1), "+");
    btn((unsigned char)(ox + 6), "-");
    gb_curshow();
}
static void draw_all(void)        /* content only; the WM drew the frame/title (#146) */
{
    recalc_origin();
    blit_canvas();
    draw_buttons();
}
/* sync_rect: pull the live WM-owned geometry before we use it (#146). */
static void sync_rect(void)
{
    win_x = gb_wm_x(); win_y = gb_wm_y(); winw = gb_wm_w(); winh = gb_wm_h();
}
static void x_draw(void) { sync_rect(); draw_all(); }   /* on_draw */

/* recentre the view on canvas pixel (px,py) and rescale step by num/den, re-render. */
static void rezoom(unsigned char px, unsigned char py, unsigned char num, unsigned char den)
{
    cx0 += (int)((int)px - CANVAS_W / 2) * step;
    cy0 += (int)((int)py - CANVAS_H / 2) * step;
    step = (int)((long)step * num / den);
    if (step < 1) step = 1;
    gb_doc_dirty();                       /* the view (definition) changed (#142) */
    render_start();
}

/* ---- File > Save (.PIC) ----------------------------------------------------- */
static void fmt83(char *dst, const char *n11)
{
    unsigned char i, j = 0;
    for (i = 0; i < 8 && n11[i] != ' '; i++) dst[j++] = n11[i];
    if (n11[8] != ' ') { dst[j++] = '.'; for (i = 8; i < 11 && n11[i] != ' '; i++) dst[j++] = n11[i]; }
    dst[j] = 0;
}
static void to_83(const char *s)
{
    unsigned char i = 0, j;
    for (j = 0; j < 11; j++) name83[j] = ' ';
    while (s[i] && s[i] != '.' && i < 8) { name83[i] = s[i]; i++; }
    while (s[i] && s[i] != '.') i++;
    if (s[i] == '.') { i++; for (j = 0; j < 3 && s[i]; j++, i++) name83[8 + j] = s[i]; }
}
static void ensure_pic_ext(void)
{
    unsigned char i = 0, has = 0;
    while (namebuf[i]) { if (namebuf[i] == '.') has = 1; i++; }
    if (!has && i && i <= 8) { namebuf[i++] = '.'; namebuf[i++] = 'P'; namebuf[i++] = 'I'; namebuf[i++] = 'C'; namebuf[i] = 0; }
}
static void write_pic(void)            /* v2 header + the canvas -> the open file */
{
    picbuf[0] = 'G'; picbuf[1] = 'B'; picbuf[2] = 'P'; picbuf[3] = 'C';
    picbuf[4] = 2; picbuf[5] = 1;                          /* version 2, mode 1 */
    picbuf[6] = CANVAS_W; picbuf[7] = 0;                   /* width  px */
    picbuf[8] = CANVAS_H; picbuf[9] = 0;                   /* height px */
    picbuf[10] = pic_inks[0]; picbuf[11] = pic_inks[1];
    picbuf[12] = pic_inks[2]; picbuf[13] = pic_inks[3];
    gb_fs_save((char *)picbuf, PIC_LEN);
}
/* ---- the fractal DEFINITION as a .TXT: "XAOS\n<cx0>\n<cy0>\n<step>\n" ---------- */
static char *put_int(char *p, int v)   /* signed decimal + newline */
{
    char tmp[6]; unsigned char n = 0; unsigned int u;
    if (v < 0) { *p++ = '-'; u = (unsigned int)(0 - v); } else u = (unsigned int)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) *p++ = tmp[--n];
    *p++ = '\n';
    return p;
}
static int get_int(char **pp)          /* parse a signed int, advance past its line */
{
    char *p = *pp; int v = 0; unsigned char neg = 0;
    while (*p == ' ' || *p == '\n' || *p == '\r') p++;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    while (*p && *p != '\n') p++;
    if (*p) p++;
    *pp = p;
    return neg ? -v : v;
}

/* ---- the document, via the gb_doc framework (#142) -------------------------- */
static const char *const x_exts[] = { "TXT", 0 };

/* x_new: generate a fresh fractal (the whole set) - only now, not at launch. */
static void x_new(void)
{
    cx0 = HOME_CX; cy0 = HOME_CY; step = HOME_STEP;
    generated = 1;
    render_start();
}

/* x_open: a definition .TXT was loaded into defbuf - parse the maths + render it. */
static void x_open(unsigned int len)
{
    char *p = (char *)defbuf;
    if (len >= 5 && defbuf[0] == 'X' && defbuf[1] == 'A' && defbuf[2] == 'O' && defbuf[3] == 'S') {
        while (*p && *p != '\n') p++;
        if (*p) p++;                       /* skip the "XAOS" magic line */
        cx0 = get_int(&p); cy0 = get_int(&p); step = get_int(&p);
        if (step < 1) step = 1;
        generated = 1;
        render_start();
    } else {
        generated = 0;                     /* not our format -> nothing to show */
    }
}

/* x_save: serialise the current view maths into defbuf -> the byte count to write. */
static unsigned int x_save(void)
{
    char *p = (char *)defbuf;
    *p++ = 'X'; *p++ = 'A'; *p++ = 'O'; *p++ = 'S'; *p++ = '\n';
    p = put_int(p, cx0);
    p = put_int(p, cy0);
    p = put_int(p, step);
    return (unsigned int)(p - (char *)defbuf);
}

/* x_savepic (Save as PIC): export the rendered image as a .PIC, without disturbing the
   document's current .TXT file name. */
static void x_savepic(void)
{
    char keep[11];
    if (!generated) return;
    if (!gb_prompt("Save as PIC:", namebuf, 12)) return;
    gb_get_name(keep);                      /* remember the .TXT name */
    ensure_pic_ext();
    to_83(namebuf);
    gb_set_name(name83);
    write_pic();                            /* gb_fs_save(picbuf) under the PIC name */
    gb_set_name(keep);                       /* restore the document's .TXT name */
}

/* recalc_origin: refresh the centered content origin from the live geometry. */
static void recalc_origin(void)
{
    ox = (unsigned char)(win_x + (winw - WIN_W) / 2);
    oy = (unsigned char)(win_y + (winh - WIN_H) / 2);
}

/* x_fullscreen: View > Fullscreen - cover the screen (canvas + buttons recenter) and back. */
static void x_fullscreen(unsigned char on)
{
    if (on) { gb_wm_setpos(0, 8); gb_wm_setsize(80, 192); }
    else    { gb_wm_setpos(DEF_X, DEF_Y); gb_wm_setsize(WIN_W, WIN_H); }
    gb_wm_damage(0, 8, 80, 192);  /* repaint ONCE in on_frame, clipped to the toggle area;
                                     repainting here too double-paints (the flicker, #153) */
}

static const gb_doc_t xdoc = {
    defbuf, DEFMAX, x_new, x_open, x_save, 0, 0, 0, 0, x_exts, "Save as PIC", x_savepic,
    x_fullscreen, 0, 0
};

/* on_menu: a top-bar title was clicked -> hand it to the framework. */
static void on_menu(void) { gb_doc_event(); }

/* on_frame (#146): the WM handled close/drag; run the menu framework + the 'r' reset key. */
static void x_frame(void)
{
    unsigned char c;
    sync_rect();
    win_title();                                       /* keep wtitle fresh for the WM title */
    if (rendering) {                                   /* paint a budget of pixels per frame: the
                                                          WM/pointer stay live (drag/close still work
                                                          via their own messages) instead of freezing.
                                                          No key-abort here: the click that picked
                                                          File>New buffers a char that gb_getkey
                                                          returns a frame later (once fire is
                                                          released) and that would stop the render.
                                                          Close the window or click +/-/canvas
                                                          (restarts) to interrupt. */
        render_step();
        return;
    }
    if (gb_doc_frame()) { gb_restore_parent(); return; }   /* a File menu ran (#142) */
    while ((c = gb_getkey()) != 0)                     /* 'r' = reset to the home view */
        if (c == 'r' || c == 'R') { x_new(); gb_doc_dirty(); return; }
}

/* on_click (#146): a content press - the +/- zoom buttons or a canvas recentre. */
static void x_click(void)
{
    unsigned char mx, my, cy;
    unsigned int px;
    sync_rect();
    recalc_origin();                                   /* ox/oy for the CTRL_Y/CVY hit-tests */
    if (!generated) return;                            /* zoom/pan need a fractal first (#142) */
    mx = gb_mx(); my = gb_my();
    if (my >= CTRL_Y && my < (unsigned char)(CTRL_Y + 9)) {             /* +/- buttons */
        if (mx >= (unsigned char)(ox + 1) && mx < (unsigned char)(ox + 5))
            rezoom(CANVAS_W / 2, CANVAS_H / 2, 1, 2);                   /* zoom in  */
        else if (mx >= (unsigned char)(ox + 6) && mx < (unsigned char)(ox + 10))
            rezoom(CANVAS_W / 2, CANVAS_H / 2, 2, 1);                   /* zoom out */
        return;
    }
    if (my >= CVY && (unsigned char)(my - CVY) < CANVAS_H) {            /* canvas: recentre */
        cy = (unsigned char)(my - CVY);
        px = gb_mxp();
        if (px >= (unsigned int)CVX * 4) {
            px -= (unsigned int)CVX * 4;
            if (px < CANVAS_W) rezoom((unsigned char)px, cy, 1, 1);     /* pan, no zoom */
        }
    }
}

/* on_drag (#146): a title-bar press -> move the window. */
static void x_drag(void)
{
    sync_rect();
    if (gb_drag_window(&win_x, &win_y, winw, winh)) {
        gb_wm_setpos(win_x, win_y);
        gb_restore_parent();
    }
}

/* on_close (#146): offer to save, then close (or repaint on cancel). */
static void x_close(void)
{
    if (gb_doc_close()) gb_wm_close();
    else gb_restore_parent();
}

/* the window's single handler (#148). */
static void x_proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  x_draw();  break;
        case GB_MSG_CLICK: x_click(); break;
        case GB_MSG_FRAME: x_frame(); break;
        case GB_MSG_CLOSE: x_close(); break;
        case GB_MSG_DRAG:  x_drag();  break;
        case GB_MSG_MENU:
        case GB_MSG_DROP:  on_menu(); break;
    }
}

static const gb_mwin_t xmw = {
    DEF_X, DEF_Y, WIN_W, WIN_H, 0, 0,                  /* min_w=0: not grip-resizable */
    x_proc, wtitle
};

void main(void)
{
    unsigned char n;
    unsigned int len;
    cx0 = HOME_CX; cy0 = HOME_CY; step = HOME_STEP;
    generated = 0;
    gb_wm_managed(&xmw);                                /* register FIRST (no draw): captures arg */
    gb_doc(&xdoc);                                      /* standard File menu; adopts the name */
    len = gb_fs_load(defbuf, DEFMAX);                   /* launched with a definition .TXT? */
    if (len) x_open(len);                               /* parse + render it (else stays empty) */
    win_title();                                        /* build wtitle before the first paint */
    for (n = 64; n; n--) if (!gb_getkey()) break;       /* drop buffered keys */
    gb_restore_parent();                               /* first paint: WM chrome + x_draw */
}
