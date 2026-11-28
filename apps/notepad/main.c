/* notepad - the GEOBENCH text editor (C), pointer-driven (#34).
 *
 * The top bar shows a "File" menu (New / Load / Save) drawn by the kernel; click
 * it to drop the menu down. Click in the text to place the insertion point, then
 * type to insert there (Backspace deletes before it). On the CPC the pointer is
 * the arrow keys, so positioning the caret is by clicking, not by cursor keys.
 *
 *   File > New     empty buffer, name UNTITLED.TXT
 *   File > Load     pick a file from a list and open it
 *   File > Save     write the buffer (creates / grows the file via the new
 *                   AMSDOS write layer)
 *   Ctrl-Q          close the window (back to the file manager)
 *
 * #146: a kernel-MANAGED window (gb_wm_managed). The WM owns the frame/title/close/
 * drag/grip; main() registers and loads the file, then paints once via gb_restore_parent.
 * The kernel calls n_draw (content), n_frame (typing + caret), n_click (text/grip), n_drag,
 * n_close. The modal popups (menu, Load, confirm) still poll themselves. Click our window
 * to focus it; the view scrolls to keep the caret visible. */
#include "gb.h"
#ifdef GB_AGON
#include "agon_viewport.h"
#endif

#define NP_MAX    4096           /* editable text capacity - restored to 4096 (#142): the
                                    shared clipboard (gb_clip_*) replaced the local clip[512],
                                    so the file dialog + extension filter fit without trimming */
#ifdef GBDOC_BOUNDED_IO
#define NP_BUF    NP_MAX          /* chunked reads obey their exact 512-byte destination cap */
#else
#define NP_BUF    (NP_MAX + 512)  /* +1 sector of slack: gb_fs_load copies whole 512B
                                    sectors, so the buffer outruns the load cap */
#endif
#define DEF_X     2            /* initial window position (the window is draggable) */
#define DEF_Y     14
#define DEF_W     66           /* default size; the window is resizeable (#81) */
#define DEF_H     158
#define MIN_W     30
#define MIN_H     72
#define TITLE_H   14
#define LINE_H    10
#ifdef GBDOC_BOUNDED_IO
#define SB_W      3
#define SB_ARROW  8
#define SB_THUMB  8
#else
#define SB_W      0
#endif
#define MAX_LINES ((win_h - TITLE_H - 15) / LINE_H)   /* visible text rows (runtime) */
#define WRAP      ((win_w - SB_W - 4) * 2 / 3)        /* chars per wrapped line (runtime) */
#ifdef GB_AGON
#define MAXWRAP 106 /* 159 byte-columns, plus terminator */
#elif defined(GB_MSX2)
#define MAXWRAP   84           /* line[] cap = max WRAP on the 128-col MSX window */
#elif defined(GB_PCW)
#define MAXWRAP   58           /* line[] cap = max WRAP on the 90-col PCW window */
#else
#define MAXWRAP   52           /* line[] cap = max WRAP (80-col window) */
#endif
#define MAXVIS    18           /* names[] cap = max MAX_LINES */
/* text origin, relative to the live (draggable) window position */
#define SB_X      (win_x + 1)
#define TX_BG_X   (win_x + SB_W + 1)
#define TX_BG_W   (win_w - SB_W - 2)
#define TX_COL    (win_x + SB_W + 2)
#define TX_Y0     (win_y + TITLE_H + 2)   /* +2: a 2px gap so the title bar doesn't clip line 1 */
#define TX_H      ((gb_scan_t)(MAX_LINES * LINE_H))

#define K_ENTER   0x0D
#define K_BS      0x08
#define K_DEL     0x7F
#define K_TAB     0x09
#define K_QUIT    0x11           /* Ctrl-Q */


static unsigned char win_x = DEF_X;      /* live window position (draggable) */
static unsigned char win_y = DEF_Y;
static unsigned char win_w = DEF_W;
static gb_scan_t win_h = DEF_H;   /* live size (resizeable, #81) */
static char buf[NP_BUF];
#ifdef GBDOC_BOUNDED_IO
/* Reuse the executable icon after entry. The document framework uses 0x4084+
 * for its bounded-I/O state, leaving these display buffers below it. */
#ifdef GB_AGON
static char line[MAXWRAP];  /* wide VGA rows cannot share the 84-byte icon scratch */
#else
#define line   ((char *)0x4010)
#endif
#define fbase  ((char *)0x4064)
#define wtitle ((char *)0x4072)
#else
static char line[MAXWRAP];
#endif
static unsigned int len, cur;            /* text length, insertion index */
static unsigned char view_first;         /* first visible display row */
static unsigned int sel_a, sel_b;        /* selection [sel_a, sel_b) in buffer order */
static unsigned char sel_on;             /* a selection exists */
static unsigned char keycool;            /* frames to flush keys after a click or fire */
static unsigned char prev_total;         /* display-row count at the last full draw */
#ifndef GBDOC_BOUNDED_IO
static char fbase[14];                    /* "NAME.EXT" of the open file (title base) */
static char wtitle[18];                   /* fbase + " *" when dirty (window title) */
#endif
static unsigned char blink_ctr, caret_vis, cur_scr, cur_col;  /* caret blink (#86) */
#define BLINK_FRAMES 16                   /* ~3 Hz caret blink at 50 fps */

/* arrow-key caret nav (#86): the pointer PLACES the caret, the keyboard arrows
   NUDGE it. We read the cursor keys straight off the matrix (KM_TEST_KEY) rather
   than re-arming them globally - re-arming would re-flood every app on pointer
   moves (the very thing disarm_joy_keys prevents). */
#define A_UP      0
#define A_DOWN    1
#define A_LEFT    2
#define A_RIGHT   3
#define REP_DELAY 14                      /* frames held before a key auto-repeats */
#define REP_RATE  3                       /* frames between repeats */
static unsigned char arr_prev, arr_rep, arr_bits;

/* pos_of: display (row,col) of buffer index idx (wrap + newlines). */
static void pos_of(unsigned int idx, unsigned char *prow, unsigned char *pcol)
{
    unsigned int i;
    unsigned char col = 0, row = 0, ch;
    for (i = 0; i < idx; i++) {
        ch = buf[i];
        if (ch == '\n') { row++; col = 0; }
        else if (ch >= 32) { if (++col == WRAP) { row++; col = 0; } }
    }
    *prow = row; *pcol = col;
}

/* idx_of: buffer index nearest the display position (trow,tcol). */
static unsigned int idx_of(unsigned char trow, unsigned char tcol)
{
    unsigned int i;
    unsigned char col = 0, row = 0, ch;
    for (i = 0; i < len; i++) {
        if (row == trow && col >= tcol) return i;
        ch = buf[i];
        if (ch == '\n') { if (row == trow) return i; row++; col = 0; }
        else if (ch >= 32) { if (++col == WRAP) { row++; col = 0; } }
        if (row > trow) return i;
    }
    return len;
}

/* The underscore cursor sits in the inter-line gap (row +8, below the 8px glyph)
   so the blink can erase just its cell without ever nicking a character. */
static void cursor_at(unsigned char col, unsigned char row)
{
    gb_fill(TX_COL + (col * 3) / 2, TX_Y0 + row * LINE_H + 8, 2, 2, 3);
}

/* caret_erase: clear just the cursor cell (blink-off) - no line repaint, so the
   text doesn't flash every blink. Glyph-safe because the cursor is in the gap. */
static void caret_erase(void)
{
    if (cur_scr == 0xFF) return;
    gb_fill(TX_COL + (cur_col * 3) / 2, TX_Y0 + cur_scr * LINE_H + 8, 2, 2, 0);
}

/* fmt83: 11-byte space-padded 8.3 name -> "NAME.EXT" display string. */
static void fmt83(char *dst, const char *n11)
{
    unsigned char i, j = 0;
    for (i = 0; i < 8 && n11[i] != ' '; i++) dst[j++] = n11[i];
    if (n11[8] != ' ') {
        dst[j++] = '.';
        for (i = 8; i < 11 && n11[i] != ' '; i++) dst[j++] = n11[i];
    }
    dst[j] = 0;
}

/* win_title: the window title = the file name, with " *" appended when unsaved. */
static const char *win_title(void)
{
    /* NB: copy with a SINGLE index. `while (fbase[i]) wtitle[j++] = fbase[i++];`
       (two separate auto-incrementing indices in one statement) is miscompiled by
       SDCC under --fomit-frame-pointer here - it copied only the first char, so the
       title showed just "G" instead of "GBKERN.BIN" (#142). */
    unsigned char j = 0;
    char c;
    fmt83(fbase, gb_doc_name());                 /* current file name -> "NAME.EXT" */
    while ((c = fbase[j]) != 0) { wtitle[j] = c; j++; }
    if (gb_doc_modified()) { wtitle[j++] = ' '; wtitle[j++] = '*'; }
    wtitle[j] = 0;
    return wtitle;
}

static void status_line(void)
{
#ifdef GBDOC_BOUNDED_IO
    unsigned char busy = gb_doc_busy();
    if (busy == 1) { gb_text(TX_COL, win_y + win_h - 11, "Loading..."); return; }
    if (busy == 2) { gb_text(TX_COL, win_y + win_h - 11, "Saving..."); return; }
#endif
    gb_text(TX_COL, win_y + win_h - 11, "Click text to place caret. Ctrl-Q=quit");
}

/* draw_selection: overlay the selected chars [sel_a, sel_b) in reverse video
   (gb_textbw = black-on-white) over the already-drawn text, walking the wrap
   layout like pos_of. Visible rows only. Call after render() on a full draw. */
static void draw_selection(void)
{
    unsigned int i;
    unsigned char col = 0, row = 0, ch, scr;
    char one[2];
    if (!sel_on || sel_a >= sel_b) return;
    one[1] = 0;
    for (i = 0; i < len && i < sel_b; i++) {
        ch = buf[i];
        if (i >= sel_a && ch >= 32 &&
            row >= view_first && (unsigned char)(row - view_first) < MAX_LINES) {
            scr = (unsigned char)(row - view_first);
            one[0] = ch;
            gb_textbw(TX_COL + (col * 3) / 2, TX_Y0 + scr * LINE_H, one);
        }
        if (ch == '\n') { row++; col = 0; }
        else if (ch >= 32) { if (++col == WRAP) { row++; col = 0; } }
    }
}

/* count_lines: index of the last display row (newlines + wraps). */
static unsigned char count_lines(void)
{
    unsigned int i;
    unsigned char col = 0, n = 0, ch;
    for (i = 0; i < len; i++) {
        ch = buf[i];
        if (ch == '\n') { n++; col = 0; }
        else if (ch >= 32) { if (++col == WRAP) { n++; col = 0; } }
    }
    return n;
}

#ifdef GBDOC_BOUNDED_IO
static void draw_scrollbar(void)
{
    gb_scan_t ty = TX_Y0 + SB_ARROW;
    gb_fill(SB_X, TX_Y0, SB_W, TX_H, 1);
    gb_textbw((unsigned char)(SB_X + 1), TX_Y0, GLYPH_TRI_UP);
    gb_textbw((unsigned char)(SB_X + 1), (gb_scan_t)(TX_Y0 + TX_H - SB_ARROW),
              GLYPH_TRI_DOWN);
    if (view_first) {
        if ((unsigned char)(view_first + MAX_LINES) > prev_total)
            ty = (gb_scan_t)(TX_Y0 + TX_H - SB_ARROW - SB_THUMB);
        else
            ty = (gb_scan_t)(TX_Y0 + TX_H / 2 - SB_THUMB / 2);
    }
    gb_fill(SB_X, ty, SB_W, SB_THUMB, 3);
}
#endif

/* scroll_to_caret: adjust view_first so the caret's row is on screen. */
static void scroll_to_caret(void)
{
    unsigned char r, c;
    pos_of(cur, &r, &c);
    if (r < view_first) view_first = r;
    else if (r >= view_first + MAX_LINES) view_first = r - MAX_LINES + 1;
}

/* render: paint the text using the current view_first. only==0xFF -> full (frame,
   status, every visible row); otherwise repaint just that screen row (clearing it
   first) - used for a single-line edit so typing doesn't flash the whole window.
   Always (re)draws the caret. */
static void render(unsigned char only)
{
    unsigned int i;
    unsigned char col = 0, row = 0, scr, currow, curcol;

    pos_of(cur, &currow, &curcol);
    if (only == 0xFF) {                          /* the WM drew the frame/title (#146) */
        status_line();
        gb_draw_grip(win_x, win_y, win_w, win_h);   /* resize grip (#81) */
    }
    for (i = 0; i <= len; i++) {                  /* <= len: flush the last line */
        if (i == len || buf[i] == '\n' ||
            (buf[i] >= 32 && col == WRAP)) {
            if (row >= view_first && row - view_first < MAX_LINES) {
                scr = row - view_first;
                if (only >= 0xFE || scr == only) {   /* 0xFF=full, 0xFE=body, else 1 row */
                    line[col] = 0;
                    if (only < 0xFE)                 /* single-line: clear it first */
                        gb_fill(TX_BG_X, TX_Y0 + scr * LINE_H, TX_BG_W, LINE_H, 0);
                    gb_text(TX_COL, TX_Y0 + scr * LINE_H, line);
                }
            }
            if (i == len) break;
            if (buf[i] == '\n') { row++; col = 0; continue; }
            row++; col = 0;                        /* wrap: fall through to place buf[i] */
        }
        if (buf[i] >= 32) {
            if (row >= view_first && col < WRAP) line[col] = buf[i];
            col++;
        }
    }
    if (currow >= view_first && (unsigned char)(currow - view_first) < MAX_LINES) {
        scr = currow - view_first;
        cur_scr = scr; cur_col = curcol; /* remember for the blink redraw (#86) */
        cursor_at(curcol, scr);
    } else {
        cur_scr = 0xFF;                  /* manual scrollbar movement may hide the caret */
    }
}

/* sync_rect: pull the live WM-owned geometry into win_x/y/w/h before we use it (#146). */
static void sync_rect(void)
{
    win_x = gb_wm_x(); win_y = gb_wm_y(); win_w = gb_wm_w(); win_h = gb_wm_h();
}

/* draw: full content repaint - clear the text area (the WM drew the frame), then scroll
   to the caret and render everything. */
static void draw(void)
{
    caret_vis = 1; blink_ctr = 0;        /* caret solid right after any redraw (#86) */
#ifdef GBDOC_BOUNDED_IO
    if (gb_doc_busy()) {
        prev_total = 0;
        gb_fill(TX_BG_X, TX_Y0, TX_BG_W, TX_H, 0);
        draw_scrollbar();
        status_line();
        gb_draw_grip(win_x, win_y, win_w, win_h);
        return;
    }
#endif
    scroll_to_caret();
    prev_total = count_lines();
#ifdef GB_AGON
    /* Keep the last page full when widening the wrap or adding visible rows. */
    if (view_first && view_first + MAX_LINES > prev_total + 1u)
        view_first = prev_total + 1u > MAX_LINES ? prev_total + 1u - MAX_LINES : 0;
#endif
    gb_fill(TX_BG_X, TX_Y0, TX_BG_W, TX_H, 0);  /* clear text (#146) */
#ifdef GBDOC_BOUNDED_IO
    draw_scrollbar();
#endif
    render(0xFF);
    draw_selection();                    /* reverse-video highlight over the text (#99) */
}

/* redraw_body: repaint just the text area + selection (no window frame) - used during
   the selection drag so the title bar doesn't flash on every step (#99). */
static void redraw_body(void)
{
    caret_vis = 1; blink_ctr = 0;
    scroll_to_caret();
    gb_fill(TX_BG_X, TX_Y0, TX_BG_W, TX_H, 0);
#ifdef GBDOC_BOUNDED_IO
    draw_scrollbar();
#endif
    render(0xFE);
    draw_selection();
}

#ifdef GBDOC_BOUNDED_IO
/* Repaint after scrollbar movement without snapping the view back to the caret. */
static void redraw_scrolled(void)
{
    caret_vis = 1; blink_ctr = 0;
    gb_fill(TX_BG_X, TX_Y0, TX_BG_W, TX_H, 0);
    draw_scrollbar();
    render(0xFE);
    draw_selection();
}
#endif

/* insert one char at the caret; grow the buffer right. */
static void ins(char c)
{
    unsigned int i;
    if (len >= NP_MAX) return;
    for (i = len; i > cur; i--) buf[i] = buf[i - 1];
    buf[cur] = c;
    len++; cur++;
    gb_doc_dirty();
}

static void del_back(void)
{
    unsigned int i;
    if (cur == 0) return;
    for (i = cur; i < len; i++) buf[i - 1] = buf[i];
    len--; cur--;
    gb_doc_dirty();
}

/* on_menu: a top-bar title (File or our Edit) was clicked -> hand it to the
   document framework, which drops the dropdown on the next on_frame (#142). */
static void on_menu(void)
{
    gb_doc_event();
}

/* is_bas: is the current file a BASIC program? .BAS needs CR+LF on the CPC (we
   otherwise edit plain \n); read the framework's current 8.3 name. */
static unsigned char is_bas(void)
{
    const char *n = gb_doc_name();
    return (unsigned char)(n[8] == 'B' && n[9] == 'A' && n[10] == 'S');
}

static unsigned char is_geobench_cfg(void)
{
    const char *n = gb_doc_name();
    return (unsigned char)(n[0] == 'G' && n[1] == 'E' && n[2] == 'O' &&
                           n[3] == 'B' && n[4] == 'E' && n[5] == 'N' &&
                           n[6] == 'C' && n[7] == 'H' && n[8] == 'C' &&
                           n[9] == 'F' && n[10] == 'G');
}

/* strip_cr: drop every \r from buf[0..n) so a CR+LF file edits as plain \n lines;
   returns the new length. */
static unsigned int strip_cr(unsigned int n)
{
    unsigned int i, j = 0;
    for (i = 0; i < n; i++) if (buf[i] != '\r') buf[j++] = buf[i];
    return j;
}

/* to_crlf: rewrite buf in place as CR+LF (each \n -> \r\n) with a trailing CR+LF, for
   a .BAS save (CPC BASIC needs every line, including the last, terminated). Returns
   the new length, or 'len' unchanged if it would not fit NP_BUF (caller saves as-is).
   Expands back-to-front so the destination never overruns the unread source. */
static unsigned int to_crlf(void)
{
    unsigned int nl = 0, i, w, newlen;
    unsigned char trail, ch;
    for (i = 0; i < len; i++) if (buf[i] == '\n') nl++;
    trail = (unsigned char)(len == 0 || buf[len - 1] != '\n');   /* need a final \n? */
    newlen = len + nl + (trail ? 2 : 0);       /* each \n gains a \r; trailing gains \r\n */
    if (newlen > NP_BUF) return len;           /* won't fit -> save as-is */
    w = newlen;
    if (trail) { buf[--w] = '\n'; buf[--w] = '\r'; }
    i = len;
    while (i) {
        ch = (unsigned char)buf[--i];
        buf[--w] = (char)ch;
        if (ch == '\n') buf[--w] = '\r';
    }
    return newlen;
}

/* --- document-framework hooks (#142): the standard File menu (New/Load/Save/Save
   As) is the system's; we provide only how to clear, read in, and write out our
   text buffer. The .BAS CR+LF round-trip rides on_save (expand) + on_saved (undo). */
static unsigned int sv_orig, sv_wl;            /* .BAS save round-trip state */

static void np_new(void)
{
    len = 0; cur = 0; view_first = 0; sel_on = 0;
}
static void np_open(unsigned int n)
{
    len = n;
    if (is_bas()) len = strip_cr(len);         /* .BAS arrives CR+LF -> edit as \n */
    cur = 0; view_first = 0; sel_on = 0;
}
static unsigned int np_save(void)
{
    if (!is_bas()) return len;
    sv_orig = len; sv_wl = to_crlf();          /* expand to CR+LF for CPC BASIC */
    return sv_wl;
}
static void np_saved(void)
{
    if (is_bas()) { strip_cr(sv_wl); len = sv_orig; }   /* collapse back to \n */
    else if (is_geobench_cfg() && len <= 512) {
        char *cfg = (char *)0x1000;
        unsigned int i;
        for (i = 0; i < len; i++) cfg[i] = buf[i];
        *(volatile unsigned int *)0x1200 = len;
    }
}

/* the file types Notepad opens - the file dialog shows only these (+ folders) */
#ifdef GB_AGON
static const char *const np_exts[] = { "TXT", 0 };
#else
static const char *const np_exts[] = { "TXT", "BAS", "CFG", 0 };
#endif
static void select_all(void);            /* the Edit-menu hooks (defined below) */
static void copy_sel(void);
static void paste_clip(void);

/* np_fullscreen: View > Fullscreen - resize our window to cover the screen (below the
 * top bar) and back. Notepad is resizable (#81), so the text just reflows. Restoring
 * repaints the desktop we vacated. */
#ifndef GB_AGON
static void np_fullscreen(unsigned char on)
{
    if (on) { gb_wm_setpos(0, 8); gb_wm_setsize(GB_COLS, GB_LINES - 8); }
    else    { gb_wm_setpos(DEF_X, DEF_Y); gb_wm_setsize(DEF_W, DEF_H); }
    gb_wm_damage(0, 8, GB_COLS, GB_LINES - 8); /* repaint ONCE in on_frame, clipped to the toggle area;
                                            repainting here too double-paints (the flicker, #153) */
}

#endif

static const gb_doc_t npdoc = {
    buf, NP_MAX, np_new, np_open, np_save, np_saved, copy_sel, paste_clip, select_all,
    np_exts, 0, 0,
#ifdef GB_AGON
    0, /* moving/resizing is owned by the Agon window manager */
#else
    np_fullscreen,
#endif
    0, 0
};

/* --- Edit menu: copy / paste over a selection (#99) ----------------------- */

static void delete_range(unsigned int a, unsigned int b)
{
    unsigned int i;
    if (a >= b || b > len) return;
    for (i = b; i < len; i++) buf[i - (b - a)] = buf[i];
    len -= (b - a);
    cur = a;
    gb_doc_dirty();
}

static void select_all(void)
{
    if (!len) { sel_on = 0; return; }
    sel_a = 0; sel_b = len; sel_on = 1; cur = len;
}

/* copy_sel / paste_clip / select_all are the framework's Edit-menu hooks (#142): the
 * standard Edit menu (Select All / Copy / Paste) is added by gb_doc, and copy/paste go
 * through the SHARED clipboard (gb_clip_*) so they work across apps. */
static void copy_sel(void)
{
    if (!sel_on || sel_a >= sel_b) return;
    gb_clip_set(buf + sel_a, sel_b - sel_a);
}

/* paste_clip: replace any selection with the clipboard, inserted at the caret. No local
 * buffer - open the gap, then read the clipboard straight into it. */
static void paste_clip(void)
{
    unsigned int n;
    char *s, *d, *lo;
    if (sel_on && sel_a < sel_b) delete_range(sel_a, sel_b);   /* caret -> sel_a */
    sel_on = 0;
    n = gb_clip_len();
    if (!n || len >= NP_MAX) return;
    if (n > NP_MAX - len) n = NP_MAX - len;                    /* clamp (avoids len+n wrap) */
    /* open a gap of n at the caret, copying end-first. Pointer walk, NOT the index
       form buf[i-1+n]=buf[i-1]: under --fomit-frame-pointer SDCC spilled that loop's
       dst pointer and clobbered the loop counter, looping forever -> crash (#142). */
    lo = buf + cur;
    s  = buf + len;
    d  = s + n;
    while (s > lo) *--d = *--s;
    gb_clip_get(buf + cur, n);                                 /* fill it from the clipboard */
    len += n; cur += n;
    gb_doc_dirty();
}

/* try_close: the close gadget was hit -> let the framework offer to save first
   (it owns the "Save changes?" dialog). 1 = close, 0 = stay open. */
static unsigned char try_close(void)
{
    return gb_doc_close();
}

/* cursor_over: is the pointer inside our window? (so the blink only lifts the
   pointer when it actually sits over the text - no per-blink pointer flicker.) */
static unsigned char cursor_over(void)
{
    unsigned char mx = gb_mx(); gb_scan_t my = gb_my();
    return (unsigned char)(mx >= win_x && mx < win_x + win_w &&
                           my >= win_y && my < win_y + win_h);
}

/* read_arrows: matrix state of the four cursor keys -> arr_bits (bit0 up, 1 down,
   2 left, 3 right). KM_TEST_KEY (&BB1E): A = key number, returns NZ when pressed.
   The CPC firmware key numbers for the cursor keys are 0=Up, 1=Right, 2=Down,
   8=Left (NOT 72-75, which are Joystick 0 - reading those nudged the caret with the
   gamepad d-pad but never with the actual arrow keys). We talk to the matrix
   directly so the keys stay globally disarmed (no char-buffer flood) yet still reach
   us. Only A + arr_bits cross each call, so a KM_TEST_KEY register clobber can't
   corrupt the accumulator. */
#if defined(GB_MSX2) || defined(GB_PCW) || defined(GB_AGON)
/* MSX2/PCW have no CPC firmware: #BB1E is not KM_TEST_KEY there, and calling it wrote an
   invalid i8255 PPI mode that HUNG the machine the instant a text app tried to read
   the cursor keys (#287). On MSX the cursor keys ARE the pointer (GTSTCK 0) anyway,
   so arrow-key caret nav doesn't belong here - the caret is placed by click (see the
   "Click text to place caret" status line). Report no arrows. */
static void read_arrows(void) { arr_bits = 0; }
#else
static void read_arrows(void) __naked
{
__asm
    xor  a
    ld   (_arr_bits), a
    ld   a, #0             ; cursor up
    call #0xBB1E
    jr   z, 1$
    ld   a, (_arr_bits)
    or   #0x01
    ld   (_arr_bits), a
1$: ld   a, #2             ; cursor down
    call #0xBB1E
    jr   z, 2$
    ld   a, (_arr_bits)
    or   #0x02
    ld   (_arr_bits), a
2$: ld   a, #8             ; cursor left
    call #0xBB1E
    jr   z, 3$
    ld   a, (_arr_bits)
    or   #0x04
    ld   (_arr_bits), a
3$: ld   a, #1             ; cursor right
    call #0xBB1E
    jr   z, 4$
    ld   a, (_arr_bits)
    or   #0x08
    ld   (_arr_bits), a
4$: ret
__endasm;
}
#endif

/* move_caret: step the insertion point one cell in a direction. Up/Down keep the
   display column via the wrap-aware pos_of/idx_of; Left/Right walk the buffer. */
static void move_caret(unsigned char dir)
{
    unsigned char r, c;
    if (dir == A_LEFT)  { if (cur) cur--; }
    else if (dir == A_RIGHT) { if (cur < len) cur++; }
    else {
        pos_of(cur, &r, &c);
        if (dir == A_UP) { if (r) cur = idx_of((unsigned char)(r - 1), c); }
        else             cur = idx_of((unsigned char)(r + 1), c);   /* A_DOWN (clamps) */
    }
}

/* move_redraw: move the caret, then repaint just the old + new caret lines (or the
   whole window if the view scrolled), keeping the cursor solid + the blink reset. */
static void move_redraw(unsigned char dir)
{
    unsigned char oldscr = cur_scr, oldvf = view_first;
    move_caret(dir);
    caret_vis = 1; blink_ctr = 0;
    gb_curhide();
    scroll_to_caret();
    /* render() always (re)draws the caret at its current position, so repainting the
       OLD caret line both erases the old underscore and paints the new one (which sits
       on whatever line the caret moved to). A scroll needs the whole window. */
    if (view_first != oldvf) render(0xFF);
    else                     render(oldscr);
    gb_curshow();
}

/* handle_arrows: edge-detect + auto-repeat the cursor keys. Returns 1 if an arrow
   is engaged (caret moved or key held), so the caller skips typing/blink this frame. */
static unsigned char handle_arrows(void)
{
    unsigned char now, fresh, act = 0, dir;
    read_arrows();
    now = arr_bits;
    fresh = (unsigned char)(now & ~arr_prev);
    if (fresh) { arr_rep = REP_DELAY; act = fresh; }      /* new press -> move now */
    else if (now && now == arr_prev) {                    /* held -> maybe repeat */
        if (--arr_rep == 0) { arr_rep = REP_RATE; act = now; }
    }
    arr_prev = now;
    if (!act) return (unsigned char)(now ? 1 : 0);        /* held, not yet repeating */
    if (act & 1) dir = A_UP;
    else if (act & 2) dir = A_DOWN;
    else if (act & 4) dir = A_LEFT;
    else dir = A_RIGHT;
    move_redraw(dir);
    return 1;
}

/* text_press: a press in the text area. A plain click places the caret (clearing
   any selection); a drag (fire held + the pointer moved) selects the dragged range,
   highlighted in reverse video. anchor = idx under the press, extent follows the
   pointer (wrap-aware idx_of). (#99) */
static void text_press(unsigned char mx, gb_scan_t my)
{
    unsigned int anchor, ext, last;
    unsigned char moved = 0, r, fl;
    r = (unsigned char)(view_first + (my - TX_Y0) / LINE_H);
    anchor = idx_of(r, (unsigned char)(((mx - TX_COL) * 2) / 3));
    ext = anchor; last = anchor;
    for (;;) {
        fl = gb_poll();
        if (!(fl & GB_FIRE)) break;
        { unsigned char qx = gb_mx(); gb_scan_t qy = gb_my();
          r = (qy < TX_Y0) ? view_first
                           : (unsigned char)(view_first + (qy - TX_Y0) / LINE_H);
          ext = idx_of(r, (qx > TX_COL) ? (unsigned char)(((qx - TX_COL) * 2) / 3) : 0); }
        if (ext != anchor) moved = 1;
        if (ext != last) {                       /* selection changed -> repaint the body */
            last = ext;
            cur = ext;
            if (anchor <= ext) { sel_a = anchor; sel_b = ext; }
            else               { sel_a = ext;    sel_b = anchor; }
            sel_on = (unsigned char)(sel_a != sel_b);
            gb_curhide(); redraw_body(); gb_curshow();
        }
    }
    if (!moved && (anchor != cur || sel_on)) {   /* plain click -> caret, no selection */
        unsigned char oldscr = cur_scr, oldvf = view_first, hadsel = sel_on;
        cur = anchor; sel_on = 0;
        caret_vis = 1; blink_ctr = 0;
        gb_curhide();
        scroll_to_caret();
        if (hadsel || view_first != oldvf) redraw_body();
        else render(oldscr == 0xFF ? 0xFD : oldscr); /* erase only the old caret line */
        gb_curshow();
    }
    keycool = 4;
}

/* on_draw (#146): the WM drew the frame/title; paint the content (status + text + sel). */
static void n_draw(void)
{
    sync_rect();
    draw();
}

/* on_click (#146): a content-area press. The chrome (close/title/drag) was handled by the
   WM; here only the resize grip and a text click matter. */
static void n_click(void)
{
    unsigned char mx = gb_mx(); gb_scan_t my = gb_my();
    unsigned char vis; gb_scan_t txbot;
    sync_rect();
    keycool = 4;                                 /* flush the click's buffered keys */
#ifndef GB_AGON
    if (gb_in_grip(win_x, win_y, win_w, win_h, mx, my)) {  /* resize grip (#81) */
        if (gb_drag_resize(win_x, win_y, &win_w, &win_h, MIN_W, MIN_H)) {
            gb_wm_setsize(win_w, win_h);         /* commit size + damage clip */
            gb_restore_parent();                 /* WM repaints frame + n_draw */
        }
        return;
    }
#endif
#ifdef GBDOC_BOUNDED_IO
    if (mx >= SB_X && mx < SB_X + SB_W && my >= TX_Y0 && my < TX_Y0 + TX_H) {
        unsigned char old = view_first, max = 0;
        if (prev_total >= MAX_LINES) max = (unsigned char)(prev_total - MAX_LINES + 1);
        if (my < TX_Y0 + TX_H / 2) {
            view_first = view_first > 3 ? (unsigned char)(view_first - 3) : 0;
        } else {
            view_first = (unsigned char)(view_first + 3);
        }
        if (view_first > max) view_first = max;
        if (view_first != old) {
            gb_curhide(); redraw_scrolled(); gb_curshow();
        }
        return;
    }
#endif
    /* text area bottom, precomputed flat: the summed (win_y+TITLE_H)+(win_h-...) form
       SDCC miscompiles - cf. the FM CT_BOT fix (#81). TX_Y0 + a runtime value is safe. */
    vis = MAX_LINES;
    txbot = (gb_scan_t)(TX_Y0 + vis * LINE_H);
    if (my >= TX_Y0 && my < txbot && mx >= TX_COL)
        text_press(mx, my);                      /* click = caret, drag = select (#99) */
}

/* on_drag (#146): a title-bar press -> move the window. */
static void n_drag(void)
{
    sync_rect();
    if (gb_drag_window(&win_x, &win_y, win_w, win_h)) {
        gb_wm_setpos(win_x, win_y);
        gb_restore_parent();
    }
}

/* on_close (#146): the close gadget/ESC -> let the framework offer to save first; repaint
   over the dialog if the user cancelled. */
static void n_close(void)
{
    if (try_close()) gb_wm_close();
    else gb_restore_parent();
}

/* on_frame (#146): the WM handled close/drag/grip routing; run the menu framework, then
   the keyboard (typing + arrows) and the caret blink. */
static void n_frame(void)
{
    unsigned char c, n, edited;

    sync_rect();
    win_title();                                 /* keep wtitle fresh for the WM title draw */

    {
#ifdef GBDOC_BOUNDED_IO
        unsigned char was_busy = gb_doc_busy();
#endif
        unsigned char r = gb_doc_frame();         /* a File/Edit menu/dialog ran (#142) */
        if (r) {
            keycool = 4;                          /* flush the dialog click's buffered keys */
#ifdef GBDOC_BOUNDED_IO
            if (was_busy) {                       /* completed a background load/save: only our
                                                     opaque top window changed */
                win_title();
                gb_wm_damage(win_x, win_y, win_w, win_h);
                gb_repaint_top();
            } else
#endif
            if (r == 2) {                         /* #191: Edit changed the text (Paste/Select All) -
                                                     the menu saved-under, so just redraw the body,
                                                     smoothly, with the frame untouched (no flash) */
                gb_curhide(); draw(); gb_curshow();
            } else {                              /* File/View action (or a fallback picker): full repaint */
#ifndef GBDOC_BOUNDED_IO
                win_title();                     /* synchronous File actions already changed the name/dirty flag */
#endif
                gb_restore_parent();
            }
            return;
        }
#ifdef GBDOC_BOUNDED_IO
        if (gb_doc_busy()) return;                 /* one storage chunk ran; no edits until complete */
#endif
    }

    if (gb_flags() & GB_FIRE) keycool = 4;        /* fire held -> flush its 'Z'/'X' */

    if (keycool) {                                /* after a click/fire: drop the */
        keycool--;                                 /* buffered fire/direction chars */
        while (gb_getkey()) ;
        return;
    }

    if (handle_arrows()) return;                  /* arrow nudged the caret (#86 item 6) */

    edited = 0;                                   /* drain typed keys */
#ifdef GBDOC_BOUNDED_IO
    n = 2;                                        /* cap worst-case 4 KiB insert/delete shifts */
#else
    n = 8;
#endif
    while (n--) {
        c = gb_getkey();
        if (!c) break;
        if (c == K_QUIT) {
#ifdef GB_AGON
            n_close();
#else
            gb_wm_close();
#endif
            return;
        }
        if (c == K_BS || c == K_DEL) { del_back(); edited = 1; }
        else if (c == K_ENTER) { ins('\n'); edited = 1; }
        else if (c == K_TAB) { ins(' '); ins(' '); ins(' '); ins(' '); edited = 1; }
        else if (c >= 32 && c < 127) { ins((char)c); edited = 1; }
        /* other keys (arrows etc.) are ignored - no redraw */
    }
    if (edited) {
        unsigned char t = count_lines(), oldvf = view_first, cr, cc;
        unsigned char had_sel = sel_on;           /* any edit collapses the selection (#99) */
        sel_on = 0;
        caret_vis = 1; blink_ctr = 0;             /* caret solid while typing (#86) */
        gb_curhide();
        scroll_to_caret();
        pos_of(cur, &cr, &cc);
        if (had_sel || t != prev_total || view_first != oldvf) {
            prev_total = t;
            redraw_body();                        /* clear the text area + redraw + highlight */
        } else {
            render(cr - view_first);              /* just the caret's line */
        }
        gb_curshow();
    } else if (++blink_ctr >= BLINK_FRAMES) {     /* idle -> blink the caret (#86) */
        unsigned char co = cursor_over();
        blink_ctr = 0;
        caret_vis ^= 1;
        if (cur_scr == 0xFF) return;               /* caret is above/below a manually scrolled view */
        if (co) gb_curhide();
        if (caret_vis) cursor_at(cur_col, cur_scr);
        else caret_erase();                       /* erase just the cell - no line flash */
        if (co) gb_curshow();
    }
}

/* a kernel-managed window (#146): the WM owns the frame/title/close/drag/grip; we supply
   the content (n_draw), clicks (n_click/n_drag), the per-frame loop (n_frame), the save
   prompt (n_close), and the bar-menu handler (on_menu). title = wtitle (kept fresh). */
/* the window's single handler (#148). */
static void n_proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  n_draw();  break;
        case GB_MSG_CLICK: n_click(); break;
        case GB_MSG_FRAME: n_frame(); break;
        case GB_MSG_CLOSE: n_close(); break;
        case GB_MSG_DRAG:  n_drag();  break;
        case GB_MSG_MENU:
        case GB_MSG_DROP:  on_menu(); break;
    }
}

static const gb_mwin_t npmw = {
    DEF_X, DEF_Y, DEF_W, DEF_H, MIN_W, MIN_H, n_proc, wtitle
};

void main(void)
{
    unsigned char n;

    caret_vis = 1; blink_ctr = 0; arr_prev = 0; arr_rep = 0;
    gb_wm_managed(&npmw);                        /* register FIRST (no draw): captures our arg */
    gb_doc(&npdoc);                              /* standard File + Edit menus; adopts the name */
#ifdef GBDOC_BOUNDED_IO
    len = 0; cur = 0; view_first = 0;
    gb_doc_startup_load();                       /* one 512-byte read per focused frame */
#else
    len = gb_fs_load(buf, NP_MAX);              /* load the launch file (0 if none) */
#ifdef GB_AGON
    if (len==0xffff) { gb_wm_close(); return; }
#endif
    if (is_bas()) len = strip_cr(len);           /* .BAS in CR+LF -> edit as plain \n */
    cur = len; view_first = 0;
#endif
    sel_on = 0;                                  /* selection state (clipboard is shared now) */
    win_title();                                 /* build wtitle before the first paint */
    for (n = 64; n; n--) if (!gb_getkey()) break;   /* drop buffered keys */

#ifdef GBDOC_BOUNDED_IO
    gb_wm_damage(win_x, win_y, win_w, win_h);
    gb_repaint_top();                            /* first paint: the new opaque window only */
#else
    gb_restore_parent();
#endif
}
