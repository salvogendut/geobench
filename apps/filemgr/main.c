/* filemgr - the GEOBENCH file manager, in C.
 *
 * Lists the active drive's directory in a co-resident window (issue #45). Two
 * views (issue #52), toggled by the top-bar "View" menu:
 *   - LIST  : one entry per row, type icon + name.
 *   - ICONS : the type icons laid out in a grid, names beneath.
 * A scrollbar at the left inner edge scrolls whichever view is active: click the
 * track above/below the thumb to page, or drag the thumb. A click selects an
 * entry (red frame); a double-click opens it (open_entry routes by file extension
 * to a co-resident app - #70). The close gadget or ESC closes the window; drag the
 * title bar to move it.
 *
 * All of this is app-level: the kernel/WM owns the window (focus, z-order, the
 * rect-clipped repaint); the contents - views, grid, scrollbar - are drawn here
 * with gb_dir*, gb_blite, gb_fill/gb_frame/gb_text. No kernel changes. */
#include "gb.h"

#define DEF_X    4            /* window position */
#define DEF_Y    26
#define DEF_W    56            /* default size; the window is resizeable (#81) */
#define DEF_H    158           /* taller default so full icons still show ~3 rows (#88) */
#define MIN_W    24            /* min size keeps the title + a couple of rows usable */
#define MIN_H    62
#define TITLE_H  14
#define DCLICK   75           /* double-click window, frames (gamepad-friendly, #153) */

/* scrollbar at the left inner edge, content to its right */
#define SB_W     3                       /* scrollbar width, byte cols */
#define SB_X     (win_x + 1)             /* just inside the left border */
#define CT_X     (win_x + 1 + SB_W)      /* content left */
#define CT_Y     (win_y + TITLE_H)       /* content top, below the title bar */
#define CT_W     (win_w - 2 - SB_W)      /* content width (runtime, resizeable) */
#define CT_H     (win_h - TITLE_H - 2)   /* content height (runtime) */
#define CT_BOT   (win_y + win_h - 2)     /* content bottom = CT_Y + CT_H, but written
                                            flat: SDCC miscompiles the summed-macro form
                                            CT_Y + CT_H (TITLE_H cancels), placing the
                                            down-button mid-window (#81) */

/* list view */
#define ROW_H    18
#define LVIS     (CT_H / ROW_H)          /* visible rows (6) */

/* icons view */
#define ICOLS    3
#define CELL_W   (CT_W / ICOLS)          /* cell width (17) */
#define CELL_H   44                      /* full icon (32) + name (8) + gap (#88) */
#define IVIS     (CT_H / CELL_H)         /* visible grid rows (4) */
#define NAME_MAX (CELL_W * 4 / 6)        /* chars that fit a cell (6px font) */

#define V_LIST   0
#define V_ICONS  1

static unsigned char win_x = DEF_X, win_y = DEF_Y;
static unsigned char win_w = DEF_W, win_h = DEF_H;
static unsigned char total;       /* number of files on the disk          */
static unsigned char top;         /* first visible LINE (row / grid row)  */
static unsigned char nsel;        /* 0 = none, else selected index + 1    */
static unsigned char dc_idx;      /* index of the last click              */
static unsigned char dc_timer;
static unsigned char view = V_ICONS;   /* default = icon view (GEOBENCH.CFG VIEW=) */
static unsigned char my_drive;         /* the drive this window browses (#65) */
static const char *const drive_title[3] = { "Disk C", "Disk A", "Disk B" };

/* GEOBENCH.CFG is loaded once at startup; the View toggle rewrites the VIEW= line
   and saves it, so the choice persists across reboots. NOTE: gb_fs_load copies in
   whole 512-byte sectors, so the buffer must be a full sector even though the file
   is tiny (a smaller buffer overflows into the globals after it). */
static char cfgbuf[512];
static unsigned int cfglen;

/* The top bar is the gb_doc View menu (#142): Fullscreen (framework) + "Icons / List".
   Going up a directory is the ".." entry in the listing (not a menu item). FM has no
   document, so it gets no File menu. */
static unsigned char fs_px, fs_py, fs_pw, fs_ph;   /* geometry saved across Fullscreen */

/* Sorted listing cache (#118): the directory is streamed once into these arrays in
   raw order, then `order` is sorted by (type, name) and BOTH views + the click/open
   path index through it. Capped at MAX_ENT (CPC dirs are small; a larger directory
   shows the first MAX_ENT sorted). Also spares the FAT a re-stream per drawn item. */
#define MAX_ENT 200
/* Flat 11-byte name records (NOT char[MAX_ENT][11]): a 2D char array indexed by an
   8-bit var makes SDCC compute the *11 offset in 8 bits, which wraps past entry 23.
   NAME_AT forces the multiply to 16-bit. */
static char          names[MAX_ENT * 11];  /* raw 8.3 names, in raw directory order */
static unsigned char icons[MAX_ENT];       /* precomputed type icon per entry        */
static unsigned char order[MAX_ENT];       /* sorted permutation -> raw index         */
#define NAME_AT(k) (&names[(unsigned int)(k) * 11])

/* dir_seek: position the dir cursor at absolute index idx; return its name (0 if
   past the end). The caller continues with gb_dirn() from there. */
static char *dir_seek(unsigned char idx)
{
    unsigned char i;
    char *p = gb_dir1();
    for (i = 0; i < idx && p; i++) p = gb_dirn();
    return p;
}

/* cfg_view_pos: index just past "VIEW=" in cfgbuf, or 0xFFFF if there's no such key. */
static unsigned int cfg_view_pos(void)
{
    unsigned int i;
    for (i = 0; i + 5 <= cfglen; i++)
        if (cfgbuf[i] == 'V' && cfgbuf[i+1] == 'I' && cfgbuf[i+2] == 'E'
            && cfgbuf[i+3] == 'W' && cfgbuf[i+4] == '=')
            return i + 5;
    return 0xFFFF;
}

/* cfg_load_view: load GEOBENCH.CFG and set the initial view from VIEW= (LIST -> list;
   DEFAULT / absent / missing file -> icons). Keeps the buffer for cfg_save_view. */
static void cfg_load_view(void)
{
    unsigned int p;
    gb_set_name("GEOBENCHCFG");
    cfglen = gb_fs_load(cfgbuf, sizeof(cfgbuf));
    view = V_ICONS;
    p = cfg_view_pos();
    if (p != 0xFFFF && p + 4 <= cfglen && cfgbuf[p] == 'L' && cfgbuf[p+1] == 'I'
        && cfgbuf[p+2] == 'S' && cfgbuf[p+3] == 'T')
        view = V_LIST;
}

/* cfg_save_view: write the current view into GEOBENCH.CFG's VIEW= line (LIST or
   DEFAULT), preserving the other keys, and save. Best-effort (no-op if the file
   can't be written - e.g. it doesn't exist). */
static void cfg_save_view(void)
{
    const char *val = (view == V_LIST) ? "LIST" : "DEFAULT";
    unsigned char vlen = (view == V_LIST) ? 4 : 7;
    unsigned int p, end, i;

    if (cfglen == 0) return;                 /* no config loaded -> nothing to update */
    p = cfg_view_pos();
    if (p == 0xFFFF) {                        /* no VIEW= key: append a line */
        if (cfglen + 7 + vlen > sizeof(cfgbuf)) return;
        cfgbuf[cfglen++] = 'V'; cfgbuf[cfglen++] = 'I'; cfgbuf[cfglen++] = 'E';
        cfgbuf[cfglen++] = 'W'; cfgbuf[cfglen++] = '=';
        for (i = 0; i < vlen; i++) cfgbuf[cfglen++] = val[i];
        cfgbuf[cfglen++] = '\r'; cfgbuf[cfglen++] = '\n';
    } else {                                  /* replace the existing value in place */
        end = p;
        while (end < cfglen && cfgbuf[end] != '\r' && cfgbuf[end] != '\n') end++;
        if (vlen > (unsigned char)(end - p)) {           /* grow: shift tail right */
            unsigned int d = vlen - (end - p);
            if (cfglen + d > sizeof(cfgbuf)) return;
            for (i = cfglen; i > end; i--) cfgbuf[i - 1 + d] = cfgbuf[i - 1];
            cfglen += d;
        } else if (vlen < (unsigned char)(end - p)) {    /* shrink: shift tail left */
            unsigned int d = (end - p) - vlen;
            for (i = end; i < cfglen; i++) cfgbuf[i - d] = cfgbuf[i];
            cfglen -= d;
        }
        for (i = 0; i < vlen; i++) cfgbuf[p + i] = val[i];
    }
    gb_set_name("GEOBENCHCFG");
    gb_fs_save(cfgbuf, cfglen);
}

/* The listing shows a synthetic ".." entry first whenever we're not at the drive
   root, so "up a directory" is a normal double-click (#142). up_avail is that 0/1
   offset; disp_total = the real entries plus it. Display position 0 is "..", the
   rest map to sorted real index (pos - up_avail). */
static unsigned char up_avail(void);     /* defined after fm_path */
static unsigned char disp_total(void) { return (unsigned char)(total + up_avail()); }

/* scroll model: lines (rows in list, grid rows in icons) and how many are visible */
static unsigned char total_lines(void)
{
    if (view == V_ICONS) return (unsigned char)((disp_total() + ICOLS - 1) / ICOLS);
    return disp_total();
}
static unsigned char vis_lines(void)
{
    return (view == V_ICONS) ? IVIS : LVIS;
}
static void clamp_top(void)
{
    unsigned char tl = total_lines(), vl = vis_lines();
    if (tl <= vl) { top = 0; return; }
    if (top > tl - vl) top = tl - vl;
}

/* thumb geometry: th = height, ty = top line (only meaningful when tl > vl). */
static unsigned char thumb_h(void)
{
    unsigned char tl = total_lines(), vl = vis_lines(), th;
    if (tl <= vl) return CT_H;
    th = (unsigned char)(((unsigned)CT_H * vl) / tl);
    return th < 6 ? 6 : th;
}
static unsigned char thumb_y(void)
{
    unsigned char tl = total_lines(), vl = vis_lines();
    if (tl <= vl) return CT_Y;
    return (unsigned char)(CT_Y + ((unsigned)(CT_H - thumb_h()) * top) / (tl - vl));
}

#define ARR_H 8                          /* up/down scroll-button height (px) */

static void draw_scrollbar(void)
{
    gb_fill(SB_X, CT_Y, SB_W, CT_H, 1);                        /* white track */
    gb_fill(SB_X + 1, thumb_y() + 1, 1, thumb_h() - 2, 3);     /* red thumb, inset 1px */
    /* up/down buttons: a white patch (covers the thumb if it parks at an end), then
       the glyph black-on-white, aligned with the thumb (SB_X+1) */
    gb_fill(SB_X, CT_Y, SB_W, ARR_H, 1);
    gb_textbw(SB_X + 1, CT_Y, GLYPH_TRI_UP);
    gb_fill(SB_X, CT_BOT - ARR_H, SB_W, ARR_H, 1);
    gb_textbw(SB_X + 1, CT_BOT - ARR_H, GLYPH_TRI_DOWN);
}

/* draw a name truncated to fit a cell (icons view) */
static void draw_name(unsigned char col, unsigned char line, char *name)
{
    static char tmp[14];
    unsigned char i;
    for (i = 0; i < NAME_MAX && i < 13 && name[i]; i++) tmp[i] = name[i];
    tmp[i] = 0;
    gb_text(col, line, tmp);
}

/* name83: format an 11-byte space-padded 8.3 name as "NAME.EXT". */
static char *name83(const char *e)
{
    static char fn[13];
    unsigned char i, j = 0;
    for (i = 0; i < 8 && e[i] != ' '; i++) fn[j++] = e[i];
    if (e[8] != ' ') {                   /* has an extension */
        fn[j++] = '.';
        for (i = 8; i < 11 && e[i] != ' '; i++) fn[j++] = e[i];
    }
    fn[j] = 0;
    return fn;
}

/* fullname: the current entry's 8.3 name as "NAME.EXT" (list view shows the
   extension; gb_dir* return name only). */
static char *fullname(void) { return name83(gb_entname()); }


/* Breadcrumb path shown in the title bar (#104), tracked app-level so it works on
   any backend: "" = root, "/GAMES", "/GAMES/RPG". path_push on descend, path_pop
   on Back; win_title builds "<drive><path>" for gb_window. */
static char fm_path[40];
static char title_buf[40];

static void path_push(const char *name)         /* append "/name" (bounds-checked) */
{
    unsigned char i = 0, n = 0;
    while (fm_path[i]) i++;
    while (name[n]) n++;
    if ((unsigned char)(i + n + 2) >= sizeof(fm_path)) return;   /* too deep -> leave */
    fm_path[i++] = '/';
    while (*name) fm_path[i++] = *name++;
    fm_path[i] = 0;
}

static void path_pop(void)                       /* drop the last "/component" */
{
    unsigned char i = 0, last = 0;
    while (fm_path[i]) { if (fm_path[i] == '/') last = i; i++; }
    fm_path[last] = 0;                            /* "/GAMES"->"", "/A/B"->"/A" */
}

/* up_avail: 1 when we're in a subdirectory (so the ".." entry is shown), 0 at the
   drive root (fm_path empty -> no parent). */
static unsigned char up_avail(void) { return (unsigned char)(fm_path[0] != 0); }

static const char *win_title(void)               /* "Disk C" + path -> title_buf */
{
    unsigned char i = 0, j = 0;
    const char *d = drive_title[my_drive];
    while (d[j]) title_buf[i++] = d[j++];
    j = 0;
    while (fm_path[j] && i < (unsigned char)(sizeof(title_buf) - 1)) title_buf[i++] = fm_path[j++];
    title_buf[i] = 0;
    return title_buf;
}

/* DEFAULT.IST slot order (matches the packicons line in tools/build_kernel.sh).
   The file -> icon mapping lives here now, not in the kernel (#103). */
#define ICON_CLOCK 2
#define ICON_GEOBENCH 4
#define ICON_BASIC 5
#define ICON_BINARY 6
#define ICON_PICTURE 7
#define ICON_TEXT 8
#define ICON_FOLDER 9
#define ICON_APP 10
#define ICON_NOTEPAD 11
#define ICON_ICONED 12
#define ICON_FNT 13
#define ICON_IST 14
#define ICON_DESKTOP 15
#define ICON_FILEMGR 16
#define ICON_PAINT 17
#define ICON_FRACTAL 18
#define ICON_VIEWER 20
#define ICON_UP 24            /* up-arrow for the ".." parent-dir entry (#142) */

/* name_is: does the name part (before '.') of "NAME.EXT" equal want? */
static unsigned char name_is(const char *name, const char *want)
{
    unsigned char i = 0;
    while (want[i]) { if (name[i] != want[i]) return 0; i++; }
    return (unsigned char)(name[i] == '.' || name[i] == 0);
}
/* ext_of: the (uppercase, <=3 char) extension of "NAME.EXT" into ext[4]. */
static void ext_of(const char *name, char *ext)
{
    const char *e = name;
    unsigned char i;
    while (*e && *e != '.') e++;
    if (*e != '.') { ext[0] = 0; return; }
    e++;
    for (i = 0; i < 3 && e[i] && e[i] != ' '; i++) ext[i] = e[i];
    ext[i] = 0;
}
static unsigned char ext_eq(const char *ext, const char *want)
{
    return (unsigned char)(ext[0] == want[0] && ext[1] == want[1] && ext[2] == want[2]);
}

/* entry_icon: the current entry (name + gb_isdir) -> its DEFAULT.IST slot. */
static unsigned char entry_icon(const char *name)
{
    char ext[4];
    if (gb_isdir()) return ICON_FOLDER;
    if (name_is(name, "GBKERN")) return ICON_GEOBENCH;   /* the kernel binary */
    ext_of(name, ext);
    if (ext_eq(ext, "BAS")) return ICON_BASIC;
    if (ext_eq(ext, "SCR") || ext_eq(ext, "PIC")) return ICON_PICTURE;
    if (ext_eq(ext, "TXT") || ext_eq(ext, "CFG")) return ICON_TEXT;
    if (ext_eq(ext, "FNT")) return ICON_FNT;
    if (ext_eq(ext, "IST")) return ICON_IST;
    if (ext_eq(ext, "APP")) {
        if (name_is(name, "NOTEPAD")) return ICON_NOTEPAD;
        if (name_is(name, "ICONED"))  return ICON_ICONED;
        if (name_is(name, "CLOCK"))   return ICON_CLOCK;
        if (name_is(name, "DESKTOP")) return ICON_DESKTOP;
        if (name_is(name, "FILEMGR")) return ICON_FILEMGR;
        if (name_is(name, "PAINT"))   return ICON_PAINT;
        if (name_is(name, "FRACTAL")) return ICON_FRACTAL;
        if (name_is(name, "XAOS"))    return ICON_FRACTAL;   /* the fractal generator (#116) */
        if (name_is(name, "VIEWER"))  return ICON_VIEWER;
        return ICON_APP;
    }
    return ICON_BINARY;
}

/* rank_of: the type group an icon sorts into - folders first, then apps, pictures,
   text, BASIC, icon sets, fonts, the kernel, binaries (#118). */
static unsigned char rank_of(unsigned char ic)
{
    switch (ic) {
        case ICON_FOLDER:   return 0;
        case ICON_DESKTOP: case ICON_FILEMGR: case ICON_NOTEPAD: case ICON_ICONED:
        case ICON_PAINT:    case ICON_VIEWER:  case ICON_CLOCK:   case ICON_FRACTAL:
        case ICON_APP:      return 1;
        case ICON_PICTURE:  return 2;
        case ICON_TEXT:     return 3;
        case ICON_BASIC:    return 4;
        case ICON_IST:      return 5;
        case ICON_FNT:      return 6;
        case ICON_GEOBENCH: return 7;
        default:            return 8;   /* binaries / unknown */
    }
}

/* entry_less: is raw entry a ordered before raw entry b? By type group, then name. */
static unsigned char entry_less(unsigned char a, unsigned char b)
{
    unsigned char ra = rank_of(icons[a]), rb = rank_of(icons[b]), i;
    char *na = NAME_AT(a), *nb = NAME_AT(b);
    if (ra != rb) return (unsigned char)(ra < rb);
    for (i = 0; i < 11; i++)
        if (na[i] != nb[i]) return (unsigned char)(na[i] < nb[i]);
    return 0;
}

/* build_list: stream the directory into the cache (raw order, with each entry's type
   icon), then insertion-sort `order` by (type, name). Sets `total`. */
static void build_list(void)
{
    unsigned char n = 0, i, j, v;
    char *p, *e;
    gb_set_drive(my_drive);
    p = gb_dir1();
    while (p && n < MAX_ENT) {
        char *d = NAME_AT(n);
        e = gb_entname();
        for (i = 0; i < 11; i++) d[i] = e[i];
        icons[n] = entry_icon(name83(e));   /* uses gb_isdir() of the current entry */
        order[n] = n;
        n++;
        p = gb_dirn();
    }
    total = n;
    for (i = 1; i < n; i++) {                /* insertion sort the permutation */
        v = order[i]; j = i;
        while (j > 0 && entry_less(v, order[j - 1])) { order[j] = order[j - 1]; j--; }
        order[j] = v;
    }
}

static void draw_list_view(void)
{
    unsigned char i, y, p, raw, up = up_avail();
    for (i = 0; i < LVIS; i++) {                   /* draw from the sorted cache (#118) */
        p = (unsigned char)(top + i);
        if (p >= disp_total()) break;
        y = CT_Y + i * ROW_H;
        gb_frame(CT_X, y, CT_W, 17, 0);            /* erase any stale selection frame first (#153) */
        if (up && p == 0) {                        /* the ".." parent-dir entry (#142) */
            gb_icon(ICON_UP, CT_X, y + 1);         /* 16px icon - fits the row at full height */
            gb_text(CT_X + 9, y + 6, "..");
        } else {
            raw = order[(unsigned char)(p - up)];
            gb_icon_half(icons[raw], CT_X, y + 1); /* half-height type icon (#103) */
            gb_text(CT_X + 9, y + 6, name83(NAME_AT(raw)));   /* NAME.EXT */
        }
        if (nsel == (unsigned char)(p + 1))        /* red frame on the selected row */
            gb_frame(CT_X, y, CT_W, 17, 3);
    }
}

static void draw_icons_view(void)
{
    unsigned char r, c, cx, cy, raw, up = up_avail();
    unsigned int idx = (unsigned int)top * ICOLS;    /* first visible item */
    unsigned int dt = disp_total();
    for (r = 0; r < IVIS; r++) {
        for (c = 0; c < ICOLS; c++) {
            if (idx >= dt) return;
            cx = CT_X + c * CELL_W;
            cy = CT_Y + r * CELL_H;
            gb_frame(cx, cy, CELL_W, CELL_H - 1, 0);             /* erase any stale frame first (#153) */
            if (up && idx == 0) {                                /* the ".." entry (#142) */
                gb_icon(ICON_UP, (unsigned char)(cx + (CELL_W - 4) / 2),  /* 16px, centered */
                        (unsigned char)(cy + 9));                        /* in the 32px band */
                gb_text((unsigned char)(cx + (CELL_W - 3) / 2), cy + 34, "..");  /* centered */
            } else {
                raw = order[(unsigned char)(idx - up)];
                gb_icon(icons[raw], cx + (CELL_W - 8) / 2, cy + 1);   /* full icon (#103) */
                draw_name(cx, cy + 34, name83(NAME_AT(raw)));         /* name below the icon */
            }
            if (nsel == (unsigned char)(idx + 1))
                gb_frame(cx, cy, CELL_W, CELL_H - 1, 3);
            idx++;
        }
    }
}

/* draw_body: the window CONTENT (scrollbar + listing + grip); the WM drew the frame (#146). */
static void draw_body(void)
{
    gb_set_drive(my_drive);              /* this window's drive (#65) - a repaint may run
                                            while another window's drive is active */
    draw_scrollbar();
    if (view == V_ICONS) draw_icons_view();
    else                 draw_list_view();
    gb_draw_grip(win_x, win_y, win_w, win_h);   /* resize grip, bottom-right (#81) */
}
/* draw: interactive content redraw (manages the cursor) - the scroll/select paths use it
   to refresh the listing without a full-stack repaint or a title change. */
static void draw(void)
{
    gb_curhide();
    draw_body();
    gb_curshow();
}
/* sync_rect: pull the live WM-owned geometry before we use it (#146). */
static void sync_rect(void)
{
    win_x = gb_wm_x(); win_y = gb_wm_y(); win_w = gb_wm_w(); win_h = gb_wm_h();
}
/* fm_draw (on_draw): the WM drew the frame/title and owns the cursor; paint the content. */
static void fm_draw(void)
{
    sync_rect();
    draw_body();
}

/* relist: re-read the current directory and redraw from the top (#54). */
static void relist(void)
{
    build_list();              /* stream + sort the directory (sets total) (#118) */
    top = 0; nsel = 0;
    clamp_top();
    win_title();               /* the path changed -> refresh the title buffer (#146) */
    gb_restore_parent();       /* full repaint: the WM redraws the frame/title + fm_draw */
}

/* go_up: ascend to the parent directory (the ".." entry / old View>Up). */
static void go_up(void) { gb_back(); path_pop(); relist(); }

/* ext_is: does the positioned entry's raw 11-byte 8.3 name end in this extension? */
static unsigned char ext_is(const char *e, char a, char b, char c)
{
    return (unsigned char)(e[8] == a && e[9] == b && e[10] == c);
}

/* open_entry: a directory descends in place (gb_chdir + re-list, same window); a
   file opens a co-resident app chosen here by extension (#70 - the routing that
   used to live in the kernel's app_for_ext is now app-level C):
     .APP             a GEOBENCH app -> run it (no file arg)
     .IST / .SPR      the icon/cursor editor (ICONED), with the file
     .TXT / .CFG / .BAS   the text editor (NOTEPAD), with the file
     anything else    the read-only VIEWER, with the file
   (Part 3 will route native .BIN/.BAS to a "run via AMSDOS" confirm instead.) */
static void open_entry(unsigned char idx)
{
    char *e;
    dir_seek(order[idx]);          /* sorted display index -> raw entry (sets attr/cluster) */
    if (gb_isdir()) { path_push(fullname()); gb_chdir(); relist(); return; }
    if (gb_wm_full()) {            /* every app bank is taken - say so, don't dead-click (#153) */
        gb_alert("Sorry, not enough RAM", "to run more apps.");
        return;
    }
    nsel = 0;
    e = gb_entname();              /* the positioned entry's 11-byte 8.3 name */
    if (ext_is(e, 'A', 'P', 'P'))
        gb_wm_open(e);                          /* run the app itself, file-less */
    else if (ext_is(e, 'I', 'S', 'T') || ext_is(e, 'S', 'P', 'R'))
        gb_wm_launch_as("ICONED  APP");
    else if (ext_is(e, 'T', 'X', 'T') || ext_is(e, 'C', 'F', 'G') ||
             ext_is(e, 'B', 'A', 'S'))
        gb_wm_launch_as("NOTEPAD APP");
    else
        gb_wm_launch_as("VIEWER  APP");         /* default: view it (incl. .PIC images,
                                                   #114) - PAINT edits via File > Load */
}

/* sb_drag: while the fire is held, map the pointer's Y to the scroll position
   (self-driven poll loop, like the modal popups - the WM loop pauses meanwhile). */
static void sb_drag(void)
{
    unsigned char tl = total_lines(), vl = vis_lines(), my, nt;
    if (tl <= vl) return;
    for (;;) {
        if (!(gb_poll() & GB_FIRE)) break;
        my = gb_my();
        if (my < CT_Y) my = CT_Y;
        if (my >= CT_BOT) my = CT_BOT - 1;
        nt = (unsigned char)(((unsigned)(my - CT_Y) * (tl - vl)) / CT_H);
        if (nt != top) { top = nt; clamp_top(); draw(); }
    }
}

/* sb_click: a click in the scrollbar - page above/below the thumb, or drag it. */
static void sb_click(unsigned char my)
{
    unsigned char ty = thumb_y(), th = thumb_h(), vl = vis_lines();
    if (total_lines() <= vl) return;
    if (my < ty)            { top = (top > vl) ? top - vl : 0; clamp_top(); draw(); }
    else if (my >= ty + th) { top += vl; clamp_top(); draw(); }
    else                    sb_drag();
}

/* fm_view: the gb_doc View menu's app items (after Fullscreen). 0 = toggle the
   list/icons layout (persisted to GEOBENCH.CFG). "Up" is no longer a menu item -
   it's the ".." entry in the listing now (#142). */
static void fm_view(unsigned char item)
{
    if (item == 0) {                      /* Icons / List: toggle + persist */
        view ^= 1;
        cfg_save_view();
        top = 0; nsel = 0;
        clamp_top();
        gb_curhide();
        gb_fill(CT_X, CT_Y, CT_W, CT_H, 0);   /* clear the old layout: icons<->list use a
                                                 different grid, so the new one must paint on
                                                 a blank content rect, not over the old (#153) */
        draw_body();
        gb_curshow();
    }
}

/* fm_fullscreen: View > Fullscreen - cover the screen and restore the prior
   geometry on exit; the listing reflows to the new size (#142). */
static void fm_fullscreen(unsigned char on)
{
    if (on) {
        fs_px = win_x; fs_py = win_y; fs_pw = win_w; fs_ph = win_h;
        win_x = 0; win_y = 8; win_w = 80; win_h = 192;
    } else {
        win_x = fs_px; win_y = fs_py; win_w = fs_pw; win_h = fs_ph;
    }
    gb_wm_setpos(win_x, win_y);
    gb_wm_setsize(win_w, win_h);
    clamp_top();                          /* a taller window shows more rows */
    gb_wm_damage(0, 8, 80, 192);          /* repaint ONCE in on_frame over the FULL toggle area (like
                                             every other app): a restore shrinks+moves the window, and
                                             setsize's clip doesn't cover the area the maximized window
                                             vacated - leaving a ghost scrollbar/listing behind (#156) */
}

static const char *const fm_view_items[] = { "Icons / List", 0 };
static const gb_doc_t fmdoc = {          /* no document -> no File menu, just View */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, fm_fullscreen, fm_view_items, fm_view
};

/* on_event: a file dropped here from another window is copied onto our drive (#65);
   a top-bar menu click goes to the framework's View handler (#142). */
static void on_event(void)
{
    sync_rect();
    if (gb_msg.type == GB_MSG_DROP) {     /* a file dropped here from another window (#65) */
        unsigned int n;                   /* copy it onto THIS window's drive (#74) */
        gb_set_drive(my_drive);
        gb_copy_begin();                  /* switch to the drag source drive/dir */
        gb_set_name(gb_dragname);
        n = gb_fs_load(gb_copybuf, GB_COPYMAX);
        gb_set_drive(my_drive);           /* back to this window's drive */
        gb_set_name(gb_dragname);
        gb_fs_save(gb_copybuf, n);        /* lands in the root */
        gb_copy_end();                    /* restore this window's drive/dir */
        relist();
        return;
    }
    gb_doc_event();                       /* the View menu (Fullscreen / Icons-List / Up) */
}

/* on_frame (#146): the WM handled close/drag/grip routing; re-assert our drive, tick the
   double-click timer, and run the View menu framework. */
static void fm_frame(void)
{
    sync_rect();
    gb_set_drive(my_drive);              /* re-assert our drive each focused frame (#65) */
    if (dc_timer) dc_timer--;
    if (gb_doc_frame()) { gb_restore_parent(); return; }   /* a View menu ran (#142) */
}

/* on_close (#146): no document to save -> just close. */
static void fm_close(void)
{
    if (gb_doc_close()) gb_wm_close();
    else gb_restore_parent();
}

/* on_drag (#146): a title-bar press -> move the window. */
static void fm_drag(void)
{
    sync_rect();
    if (gb_drag_window(&win_x, &win_y, win_w, win_h)) {
        gb_wm_setpos(win_x, win_y);
        gb_restore_parent();
    }
}

/* on_click (#146): a content press - resize grip, scrollbar, or an entry (select / open /
   drag to another window or the Trash). */
static void fm_click(void)
{
    unsigned char mx, my, idx;

    sync_rect();
    gb_set_drive(my_drive);
    mx = gb_mx();
    my = gb_my();

    /* resize grip (bottom-right corner) -> resize the window (#81) */
    if (gb_in_grip(win_x, win_y, win_w, win_h, mx, my)) {
        if (gb_drag_resize(win_x, win_y, &win_w, &win_h, MIN_W, MIN_H)) {
            gb_wm_setsize(win_w, win_h);
            clamp_top();          /* taller window shows more rows -> re-clamp the scroll,
                                     else the thumb runs past the new track bottom (#81) */
            gb_restore_parent();
        }
        return;
    }

    /* scrollbar: up/down arrow buttons (one line) at the ends, page/drag between */
    if (mx >= SB_X && mx < SB_X + SB_W && my >= CT_Y && my < CT_BOT) {
        unsigned char old = top, tl = total_lines(), vl = vis_lines();
        if (my < CT_Y + ARR_H) {                     /* up arrow */
            if (top) top--;
        } else if (my >= CT_BOT - ARR_H) {           /* down arrow */
            if (tl > vl && top < tl - vl) top++;
        } else {
            sb_click(my);
            return;
        }
        if (top != old) draw();
        return;
    }

    /* content: pick the entry under the pointer (list row or grid cell) */
    if (my >= CT_Y && my < CT_BOT && mx >= CT_X && mx < win_x + win_w) {
        if (view == V_ICONS) {
            unsigned char c = (mx - CT_X) / CELL_W;
            unsigned char r = (my - CT_Y) / CELL_H;
            if (c >= ICOLS) return;
            idx = (unsigned char)((top + r) * ICOLS + c);
        } else {
            idx = top + (my - CT_Y) / ROW_H;
        }
        if (idx >= disp_total()) return;
        if (up_avail() && idx == 0) {        /* the ".." entry: double-click ascends (#142) */
            if (dc_timer && dc_idx == idx) { go_up(); dc_timer = 0; }
            else { nsel = idx + 1; dc_idx = idx; dc_timer = DCLICK; draw(); }
            return;
        }
        idx = (unsigned char)(idx - up_avail());   /* display position -> sorted real index */
        /* press on an entry: a drag (press + move) drags it to another window /
           the Trash (#62); a plain click (no move) falls through to select/open */
        dir_seek(order[idx]);                /* sorted index -> raw entry for gb_entname */
        if (gb_drag_start(gb_entname())) {   /* dropped on a target -> it handled it */
            relist();                        /* refresh (a move changes this dir) */
            return;
        }
        if (dc_timer && dc_idx == (unsigned char)(idx + up_avail())) {   /* double-click -> open */
            open_entry(idx);
            dc_timer = 0;
        } else {                              /* single click -> select */
            nsel = (unsigned char)(idx + up_avail() + 1);
            dc_idx = (unsigned char)(idx + up_avail());
            dc_timer = DCLICK;
            draw();
        }
    }
}

/* a kernel-managed window (#146): the WM owns the frame/title/close/drag/grip; we supply
   content (fm_draw), clicks (fm_click/fm_drag), the per-frame loop (fm_frame), close
   (fm_close), and on_event (file-drop + the View menu). title is set in main once the
   path is known; relist() refreshes it. The descriptor is mutable so we can cascade x/y. */
/* the window's single handler (#148). */
static void fm_proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  fm_draw();  break;
        case GB_MSG_CLICK: fm_click(); break;
        case GB_MSG_FRAME: fm_frame(); break;
        case GB_MSG_CLOSE: fm_close(); break;
        case GB_MSG_DRAG:  fm_drag();  break;
        case GB_MSG_MENU:
        case GB_MSG_DROP:  on_event(); break;
    }
}

static gb_mwin_t fmmw = {
    DEF_X, DEF_Y, DEF_W, DEF_H, MIN_W, MIN_H, fm_proc, 0
};

void main(void)
{
    nsel = 0; dc_timer = 0;
    my_drive = gb_get_drive();   /* the drive the desktop opened us on (#65) */
    win_x = DEF_X + my_drive * 8;   /* cascade so two drive windows don't fully overlap */
    win_y = DEF_Y + my_drive * 14;
    fmmw.x = win_x;              /* register the window at the cascaded position */
    fmmw.y = win_y;
    gb_wm_managed(&fmmw);        /* register first (no draw, focus) so gb_set_name/fs_load
                                   target our window for the config read below */
    gb_doc(&fmdoc);              /* View menu (Fullscreen / Icons-List); no File (#142) */
    gb_set_drive(my_drive);
    cfg_load_view();             /* VIEW= from GEOBENCH.CFG -> view (default icons) */
    build_list();                /* stream + sort the directory (sets total) (#118) */
    top = 0;
    clamp_top();
    fmmw.title = win_title();    /* build "<drive><path>" before the first paint (#146) */
    gb_restore_parent();         /* first paint: WM chrome + fm_draw */
}
