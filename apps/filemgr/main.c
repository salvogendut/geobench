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
#define CASCADE_X 4
#define CASCADE_Y 4
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
#define FSV_DIAG (*(volatile unsigned char *)0x170E)  /* FLOPPYSV.MOD diagnostic byte */
/* Chunked-copy transfer cells (the #144C..#144F free low-RAM gap; the fs backend reads them).
   FS_LOAD_OFS = 24-bit read offset; FS_XFLAGS bit0 = chunk-read, bit1 = append-write,
   bit2 = chunk-save (backend may clamp stale full-file lengths to the staging chunk). */
#define FS_LOAD_OFS ((volatile unsigned char *)0x144C)
#define FS_XFLAGS   (*(volatile unsigned char *)0x144F)
#define FS_SAVE_LEN_K (*(volatile unsigned int *)0x14FD)
#ifdef GB_PREEMPTIVE
#define COPY_IDLE      0
#define COPY_DRAW      1
#define COPY_TRANSFER  2
#define COPY_DONE      3
#define COPY_FAILED    4
#define COPY_CANCELLED 5
#endif

/* Optional GBAP executable preamble (#426/#428). V1 has one portable icon; V2
   has a resource directory and may add a native MSX Screen-7 icon. */
#define APPICON_UNKNOWN  0x80
#define APPICON_EMBEDDED 0x40
#define APPICON_PENDING  0x20
#define APPICON_SLOTMASK 0x1F
#define APPICON_OFF      16
#define APPICON_WB       8
#define APPICON_H        32
#define APPICON_LEN      256
#define APPICON7_WB      16
#define APPICON7_LEN     512
#define APPICON_PROBE    1024
#define APPICON_MODE1    1
#define APPICON_MODE7    7
#if !defined(GB_MSX2) && !defined(GB_PCW)
extern unsigned char gb_app_probe(char *dst);
#endif
#ifdef GB_MSX2
#define MSX_SCRMOD (*(volatile unsigned char *)0xFCAF)
#endif

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
#ifdef GB_MSX2
static char msx_drive_title[7] = "Disk A";
#endif
static unsigned char free_known;
static unsigned int free_kib;
#ifndef GB_PREEMPTIVE
static unsigned int appicon_off;
static unsigned char appicon_codec;
#endif
#ifdef GB_PREEMPTIVE
static unsigned char copy_state;
static unsigned char copy_created;
static unsigned char copy_ofs_hi;
static unsigned int copy_ofs_lo;
static unsigned char list_state;
#endif

/* Reuse the kernel's fixed 512-byte configuration text area. It already has this
   lifetime, stays visible under app paging, and leaves File Manager enough bank
   room for the dual-icon parser. */
#define cfgbuf ((char *)0x1000)
#define CFG_BUF_SIZE 512
static unsigned int cfglen;

/* The top bar is the gb_doc View menu (#142): Fullscreen (framework) + "Icons / List".
   Going up a directory is the ".." entry in the listing (not a menu item). FM has no
   document, so it gets no File menu. */
static unsigned char fs_px, fs_py, fs_pw, fs_ph;   /* geometry saved across Fullscreen */

/* Sorted listing cache (#118): the directory is streamed once into these arrays in
   raw order, then `order` is sorted by (type, name) and BOTH views + the click/open
   path index through it. Capped at MAX_ENT (the shipped floppy/card directories are
   small; a larger directory shows the first MAX_ENT sorted). Also spares the FAT a
   re-stream per drawn item. */
#ifdef GB_PCW
#define MAX_ENT 96   /* 180K media has 64 directory slots; leave room for its RAM icon cache */
#else
#define MAX_ENT 104
#endif
/* Flat 11-byte name records (NOT char[MAX_ENT][11]): a 2D char array indexed by an
   8-bit var makes SDCC compute the *11 offset in 8 bits, which wraps past entry 23.
   NAME_AT forces the multiply to 16-bit. */
static char          names[MAX_ENT * 11];  /* raw 8.3 names, in raw directory order */
static unsigned char icons[MAX_ENT];       /* precomputed type icon per entry        */
static unsigned char order[MAX_ENT];       /* sorted permutation -> raw index         */
#define NAME_AT(k) (&names[(unsigned int)(k) * 11])

#ifdef GB_PREEMPTIVE
static const char appicon_modname[11] = {
    'G','B','A','P','I','C','K',' ','M','O','D'
};
static unsigned char icon_req_raw, icon_req_x, icon_req_y;
static unsigned char icon_scan_pos, icon_scan_col;
#ifdef GB_PCW
#define APPICON_FIRST_DELAY 25        /* publish the window before a short icon batch */
static unsigned char icon_probe_wait;
static unsigned char icon_cache_page;
#endif

/* Marshal the queued icon request entirely in assembly. Besides being smaller
   than SDCC's four-argument loop, this makes the low-RAM handoff explicit. */
static unsigned char module_draw_icon(void) __naked
{
__asm
    ld a,(_icon_req_raw)
    ld l,a
    ld h,#0
    ld e,l
    ld d,h
    add hl,hl
    add hl,hl
    add hl,de
    add hl,hl
    add hl,de
    ld de,#_names
    add hl,de
    ld de,#0x1708
    ld bc,#11
    ldir
    ld hl,#_appicon_modname
    ld de,#0x3914
    ld bc,#11
    ldir
    ld a,#1
    ld (#0x1700),a
    ld a,(_icon_req_x)
    ld (#0x1701),a
    ld a,(_icon_req_y)
    ld (#0x1702),a
    ld a,(_view)
    xor a,#1
    ld (#0x1703),a
#ifdef GB_PCW
    ld a,(_icon_cache_page)
    ld (#0x1706),a
    ld a,(_icon_req_raw)
    ld (#0x1707),a
#endif
    ld a,#0x80
    call #0x80AE
#ifdef GB_PCW
    ld a,(#0x1706)
    ld (_icon_cache_page),a
#endif
    ld a,c
    ret
__endasm;
}

#ifdef GB_PCW
static void icon_cache_release(void) __naked
{
__asm
    ld a,(_icon_cache_page)
    or a
    ret z
    ld (#0x130B),a
    xor a
    ld (#0x1348),a
    call #0x8069
    xor a
    ld (_icon_cache_page),a
    ret
__endasm;
}

static void icon_cache_draw(void) __naked
{
__asm
    ld a,(_icon_cache_page)
    ld (#0x130B),a
    xor a
    ld (#0x1348),a
    ld a,(_icon_req_raw)
    ld h,a
    ld l,#0
    ld a,(_view)
    or a
    jr nz,001$
    ld l,#64
    ld e,#16
    jr 002$
001$:
    ld e,#32
002$:
    ld a,(_icon_req_x)
    ld b,a
    ld a,(_icon_req_y)
    ld c,a
    ld d,#8
    call #0x8054
    ret
__endasm;
}
#endif
#endif

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
    cfglen = gb_fs_load(cfgbuf, CFG_BUF_SIZE);
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
        if (cfglen + 7 + vlen > CFG_BUF_SIZE) return;
        cfgbuf[cfglen++] = 'V'; cfgbuf[cfglen++] = 'I'; cfgbuf[cfglen++] = 'E';
        cfgbuf[cfglen++] = 'W'; cfgbuf[cfglen++] = '=';
        for (i = 0; i < vlen; i++) cfgbuf[cfglen++] = val[i];
        cfgbuf[cfglen++] = '\r'; cfgbuf[cfglen++] = '\n';
    } else {                                  /* replace the existing value in place */
        end = p;
        while (end < cfglen && cfgbuf[end] != '\r' && cfgbuf[end] != '\n') end++;
        if (vlen > (unsigned char)(end - p)) {           /* grow: shift tail right */
            unsigned int d = vlen - (end - p);
            if (cfglen + d > CFG_BUF_SIZE) return;
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
static unsigned char disp_total(void)
{
#ifdef GB_PREEMPTIVE
    /* Directory entries are built in-place over several frames. Do not expose a
       half-built list if unrelated WM damage repaints us before publication. */
    if (list_state) return 0;
#endif
    return (unsigned char)(total + up_avail());
}

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

static void draw_scrollbar(void)
{
    gb_vscroll(SB_X, CT_Y, SB_W, CT_H, top, total_lines(), vis_lines(),
               GB_WIDGET_ARROWS);
}

/* Draw a name truncated to the current icon cell. Calculate the dynamic limit
   once: expanding NAME_MAX in the loop condition makes SDCC run two signed
   divisions per character and corrupts the longest CPC label. */
static void draw_name(unsigned char col, unsigned char line, char *name)
{
    static char tmp[14];
    unsigned char i, limit = NAME_MAX;
    if (limit > 13) limit = 13;
    for (i = 0; i < limit && name[i]; i++) tmp[i] = name[i];
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
#define TITLE_MAX 23                    /* kernel title scratch is 24 bytes incl. NUL */
static char title_buf[TITLE_MAX + 1];
static char free_suffix[15];             /* " 178KiB free", " 32MiB free", or " >63MiB free" */

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

static void append_ch(char *dst, unsigned char *i, char ch)
{
    if (*i < TITLE_MAX) dst[(*i)++] = ch;
}

static void append_small(char *dst, unsigned char *i, unsigned int v)
{
    unsigned char d, started = 0;
    if (v >= 1000) { append_ch(dst, i, '1'); v -= 1000; started = 1; }
    d = 0; while (v >= 100) { v -= 100; d++; }
    if (started || d) { append_ch(dst, i, (char)('0' + d)); started = 1; }
    d = 0; while (v >= 10) { v -= 10; d++; }
    if (started || d) append_ch(dst, i, (char)('0' + d));
    append_ch(dst, i, (char)('0' + (unsigned char)v));
}

static unsigned char make_free_suffix(void)
{
    unsigned char i = 0;
    unsigned int v;
    if (!free_known) { free_suffix[0] = 0; return 0; }
    append_ch(free_suffix, &i, ' ');
    if (free_kib == 0xFFFF) {
        append_ch(free_suffix, &i, '>');
        append_ch(free_suffix, &i, '6');
        append_ch(free_suffix, &i, '3');
        append_ch(free_suffix, &i, 'M');
        append_ch(free_suffix, &i, 'i');
        append_ch(free_suffix, &i, 'B');
    } else if (free_kib >= 1024) {
        v = free_kib >> 10;
        append_small(free_suffix, &i, v);
        append_ch(free_suffix, &i, 'M');
        append_ch(free_suffix, &i, 'i');
        append_ch(free_suffix, &i, 'B');
    } else {
        append_small(free_suffix, &i, free_kib);
        append_ch(free_suffix, &i, 'K');
        append_ch(free_suffix, &i, 'i');
        append_ch(free_suffix, &i, 'B');
    }
    append_ch(free_suffix, &i, ' ');
    append_ch(free_suffix, &i, 'f');
    append_ch(free_suffix, &i, 'r');
    append_ch(free_suffix, &i, 'e');
    append_ch(free_suffix, &i, 'e');
    free_suffix[i] = 0;
    return i;
}

static const char *win_title(void)               /* "Disk C/path 32MiB free" -> title_buf */
{
    unsigned char i = 0, j = 0, slen, body_max;
#ifdef GB_MSX2
    const char *d;
    msx_drive_title[5] = (char)gb_msx_drive_letter(my_drive);
    d = msx_drive_title;
#else
    const char *d = drive_title[my_drive];
#endif
    slen = make_free_suffix();
    body_max = (slen < TITLE_MAX) ? (unsigned char)(TITLE_MAX - slen) : TITLE_MAX;
    while (d[j] && i < body_max) title_buf[i++] = d[j++];
    j = 0;
    while (fm_path[j] && i < body_max) title_buf[i++] = fm_path[j++];
    j = 0;
    while (free_suffix[j] && i < TITLE_MAX) title_buf[i++] = free_suffix[j++];
    title_buf[i] = 0;
    return title_buf;
}

/* DEFAULT.IST slot order (matches the packicons line in tools/build_kernel.sh).
   The file -> icon mapping lives here now, not in the kernel (#103). */
#define ICON_FLOPPY 0         /* floppy drive icon (first packicons entry); DISKUTIL.APP reuses it */
#define ICON_CLOCK 1
#define ICON_GEOBENCH 3
#define ICON_BASIC 4
#define ICON_BINARY 5
#define ICON_PICTURE 6
#define ICON_TEXT 7
#define ICON_FOLDER 8
#define ICON_APP 9            /* generic .APP icon (a custom document glyph in REFINED/DEFAULT) */
#define ICON_FNT 10
#define ICON_DESKTOP 11
#define ICON_FILEMGR 12
#define ICON_SD 13
#define ICON_UP 14            /* up-arrow for the ".." parent-dir entry (#142) */
#define ICON_SCREENSAVER 15   /* #221: reused the freed gear slot for the screensaver (.SAV) icon */
#define ICON_CALCULATOR 20    /* #437: shared themed CALC.APP icon */

/* name_is: does the name part (before '.') of "NAME.EXT" equal want? */
static unsigned char name_is(const char *name, const char *want)
{
    unsigned char i = 0;
    while (want[i]) { if (name[i] != want[i]) return 0; i++; }
    return (unsigned char)(name[i] == '.' || name[i] == 0);
}

/* Slot byte followed by a NUL-terminated basename; 0xFF ends the table. */
static const unsigned char app_icon_map[] = {
    ICON_CLOCK,    'C','L','O','C','K',0,
    ICON_DESKTOP,  'D','E','S','K','T','O','P',0,
    ICON_FILEMGR,  'F','I','L','E','M','G','R',0,
    ICON_FLOPPY,   'D','I','S','K','U','T','I','L',0,
    ICON_CALCULATOR, 'C','A','L','C',0,
    0xFF
};

static unsigned char app_icon(const char *name)
{
    const unsigned char *entry = app_icon_map;
    unsigned char icon;
    while (*entry != 0xFF) {
        icon = *entry++;
        if (name_is(name, (const char *)entry)) return icon;
        while (*entry) entry++;
        entry++;
    }
    return ICON_APP;
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
    unsigned char icon;
    if (gb_isdir()) return ICON_FOLDER;
    if (name_is(name, "GBKERN")) return ICON_GEOBENCH;   /* the kernel binary */
    ext_of(name, ext);
    if (ext_eq(ext, "BAS")) return ICON_BASIC;
    if (ext_eq(ext, "SCR") || ext_eq(ext, "PIC")) return ICON_PICTURE;
    if (ext_eq(ext, "TXT") || ext_eq(ext, "CFG") || ext_eq(ext, "HTM")) return ICON_TEXT;
    if (ext_eq(ext, "FNT")) return ICON_FNT;
    if (ext_eq(ext, "SAV")) return ICON_SCREENSAVER;   /* #221: screensaver modules */
    if (ext_eq(ext, "MOD")) return ICON_GEOBENCH;      /* #234: kernel modules = the lollipop icon */
    if (ext_eq(ext, "IST")) return ICON_BINARY;   /* #221: apps/data share the binary icon */
    if (ext_eq(ext, "APP")) {
        icon = app_icon(name);
        return (icon == ICON_APP) ? (unsigned char)(APPICON_UNKNOWN | ICON_APP) : icon;
    }
    return ICON_BINARY;
}

/* rank_of: the type group an icon sorts into - folders first, then apps, pictures,
   text, BASIC, icon sets, fonts, the kernel, binaries (#118). */
static unsigned char rank_of(unsigned char ic)
{
    ic &= APPICON_SLOTMASK;
    switch (ic) {
        case ICON_FOLDER:   return 0;
        case ICON_DESKTOP: case ICON_FILEMGR: case ICON_CLOCK:
        case ICON_SCREENSAVER: case ICON_CALCULATOR:
        case ICON_APP:      return 1;
        case ICON_PICTURE:  return 2;
        case ICON_TEXT:     return 3;
        case ICON_BASIC:    return 4;
        case ICON_FNT:      return 6;
        case ICON_GEOBENCH: return 7;
        default:            return 8;   /* binaries / unknown */
    }
}

#if defined(GB_PCW) && !defined(GB_PREEMPTIVE)
/* Canonical Mode-1 byte -> the raw PCW hardware byte expected by
   gb_restorerect. This is the same fallback conversion used by VIEWER.APP. */
static unsigned char appicon_native(unsigned char value)
{
    unsigned char i, pen, native = 0;
    for (i = 0; i < 4; i++) {
        pen = (unsigned char)(((value >> (7 - i)) & 1)
                              | (((value >> (3 - i)) & 1) << 1));
        native |= (unsigned char)(pen << (6 - 2 * i));
    }
    return (unsigned char)(((native & 0x55) << 1)
                           | (((native ^ 0xFF) & 0xAA) >> 1));
}
#endif

#ifndef GB_PREEMPTIVE
/* appicon_load: read and validate the GBAP resources into gb_copybuf. The File
   Manager's launch name is parked beyond the probe while gb_fs_load temporarily
   targets the APP. Mode 7 selects codec 7 when present; every other target/mode
   uses the required codec-1 fallback. */
static unsigned char appicon_load(unsigned char raw)
{
    unsigned char *data = (unsigned char *)gb_copybuf;
    unsigned char *entry;
    unsigned int got, len, total;
#ifdef GB_MSX2
    unsigned int off;
#endif
#ifdef GB_PCW
    unsigned int p;
#endif

    gb_get_name((char *)gb_copybuf + APPICON_PROBE);
    gb_set_name(NAME_AT(raw));
#if !defined(GB_MSX2) && !defined(GB_PCW)
    got = gb_app_probe((char *)data) ? APPICON_PROBE : 0;
#else
    FS_LOAD_OFS[0] = 0; FS_LOAD_OFS[1] = 0; FS_LOAD_OFS[2] = 0;
    FS_XFLAGS = 0x01;
    got = gb_fs_load((char *)data, APPICON_PROBE);
    FS_XFLAGS = 0;
#endif
    gb_set_name((char *)gb_copybuf + APPICON_PROBE);
    if (got < APPICON_OFF || data[0] != 0xC3 || data[3] != 'G'
        || data[4] != 'B' || data[5] != 'A' || data[6] != 'P')
        return 0;
    if (data[7] == 1) {
        if (data[8] != APPICON_MODE1 || data[9] != APPICON_WB
            || data[10] != APPICON_H || data[11] != 0 || data[12] != 1
            || data[13] != APPICON_OFF || data[14] != 0)
            return 0;
        appicon_codec = APPICON_MODE1;
        appicon_off = APPICON_OFF;
    } else if (data[7] == 2) {
        total = (unsigned int)data[10] | ((unsigned int)data[11] << 8);
        if (!data[8] || data[8] > 2 || data[9] != 8
            || data[12] != APPICON_OFF || data[13] != 0 || total > got)
            return 0;
        entry = data + APPICON_OFF;              /* required portable fallback */
        appicon_codec = entry[0];
        appicon_off = (unsigned int)entry[6] | ((unsigned int)entry[7] << 8);
        len = (unsigned int)entry[4] | ((unsigned int)entry[5] << 8);
        if (appicon_codec != APPICON_MODE1 || entry[1] != APPICON_WB
            || entry[2] != APPICON_H || entry[3] != 0 || len != APPICON_LEN
            || appicon_off + len > total)
            return 0;
#ifdef GB_MSX2
        if (MSX_SCRMOD == 7 && data[8] == 2) {
            entry += 8;                          /* optional native variant */
            off = (unsigned int)entry[6] | ((unsigned int)entry[7] << 8);
            len = (unsigned int)entry[4] | ((unsigned int)entry[5] << 8);
            if (entry[0] == APPICON_MODE7 && entry[1] == APPICON7_WB
                && entry[2] == APPICON_H && entry[3] == 0
                && len == APPICON7_LEN && off + len <= total) {
                appicon_codec = APPICON_MODE7;
                appicon_off = off;
            }
        }
#endif
    } else return 0;

#ifdef GB_MSX2
    if (appicon_codec == APPICON_MODE1) {
        gb_pic_edit_buf = (unsigned int)(data + appicon_off);
        gb_pic_edit_off = (unsigned int)(data + appicon_off);
        FS_SAVE_LEN_K = APPICON_LEN;
        if (!gb_pic_edit(GB_PICEDIT_NATIVE)) return 0;
    }
#elif defined(GB_PCW)
    for (p = appicon_off; p < appicon_off + APPICON_LEN; p++)
        data[p] = appicon_native(data[p]);
#endif
    return 1;
}
#endif

/* draw_entry_type: regular IST slot or an APP-owned bitmap. */
static void draw_entry_type(unsigned char raw, unsigned char x, unsigned char y,
                            unsigned char half)
{
    unsigned char state = icons[raw];
    unsigned char slot = (unsigned char)(state & APPICON_SLOTMASK);
#ifdef GB_PREEMPTIVE
    /* Repaints are storage-free. The frame job reloads and draws visible embedded
       icons after this generic placeholder has been painted. */
#ifdef GB_PCW
    if (slot == ICON_APP && (state & APPICON_EMBEDDED)) {
        if (icon_cache_page && raw < 64) {
            icon_req_raw = raw;
            icon_req_x = x;
            icon_req_y = y;
            icon_cache_draw();
            return;
        }
        icons[raw] = (unsigned char)(state | APPICON_PENDING);
    }
#else
    if (slot == ICON_APP && (state & APPICON_EMBEDDED))
        icons[raw] = (unsigned char)(state | APPICON_PENDING);
#endif
    if (half) gb_icon_half(slot, x, y);
    else      gb_icon(slot, x, y);
#else
    unsigned char *bitmap;
    if (slot == ICON_APP && (state & (APPICON_UNKNOWN | APPICON_EMBEDDED))) {
        if (appicon_load(raw)) {
            icons[raw] = (unsigned char)(APPICON_EMBEDDED | ICON_APP);
            bitmap = (unsigned char *)gb_copybuf + appicon_off;
#ifdef GB_MSX2
            if (appicon_codec == APPICON_MODE7) {
                unsigned char rows = half ? 16 : APPICON_H;
                if (half) bitmap += APPICON7_WB * 8;
                gb_pic_edit_buf = (unsigned int)bitmap;
                gb_pic_edit_off = (unsigned int)x | ((unsigned int)y << 8);
                FS_SAVE_LEN_K = APPICON_WB | ((unsigned int)rows << 8);
                if (gb_pic_edit(GB_PICEDIT_NATIVE16)) return;
                icons[raw] = ICON_APP;
                gb_icon(slot, x, y);
                return;
            }
#endif
            if (half) {
                bitmap += APPICON_WB * 8;       /* middle 16 rows, like gb_icon_half */
                gb_restorerect(x, y, APPICON_WB, 16, bitmap);
            } else {
                gb_restorerect(x, y, APPICON_WB, APPICON_H, bitmap);
            }
            return;
        }
        if (state & APPICON_UNKNOWN) icons[raw] = ICON_APP;
    }
    if (half) gb_icon_half(slot, x, y);
    else      gb_icon(slot, x, y);
#endif
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

static void draw(void);

#ifdef GB_PREEMPTIVE
#define LIST_IDLE  0
#define LIST_WAIT  1
#define LIST_FIRST 2
#define LIST_NEXT  3
#define LIST_FREE  4
#define LIST_BATCH 4

/* list_start/list_step keep filesystem work on the root task, but process only a
   small directory batch per frame. LIST_WAIT serialises File Manager storage jobs
   without stealing another window's claim. Each new entry is inserted directly
   into the sorted permutation, eliminating the old second full sort pass. The
   collected list is published only when complete. The existing desktop/window is
   left intact while scanning, avoiding an empty Reading paint followed by another
   full repaint and preventing entries moving visibly as sorting progresses. */
static void list_start(void)
{
#ifdef GB_PCW
    icon_cache_release();
#endif
    total = 0; top = 0; nsel = 0; free_known = 0;
    title_buf[0] = 'R'; title_buf[1] = 'e'; title_buf[2] = 'a'; title_buf[3] = 'd';
    title_buf[4] = 'i'; title_buf[5] = 'n'; title_buf[6] = 'g'; title_buf[7] = 0;
    list_state = LIST_WAIT;
}

static void list_step(void)
{
    unsigned char n = 0, i, j, raw;
    char *p, *e, *d;

    if (list_state == LIST_WAIT) {
        if (gb_drop_claimed()) return;
        gb_drop_claim();
        list_state = LIST_FIRST;
    }
    if (list_state == LIST_FREE) {
        gb_set_drive(my_drive);
        free_known = gb_fs_free_kib(&free_kib);
        gb_drop_release();
        list_state = LIST_IDLE;
        win_title();
        /* Publish the completed title and listing together in one repaint. */
        gb_wm_damage(win_x, win_y, win_w, win_h);
        gb_repaint_top();             /* publish the new opaque window without redrawing Desktop */
        return;
    }

    gb_set_drive(my_drive);
    p = (list_state == LIST_FIRST) ? gb_dir1() : gb_dirn();
    while (p && total < MAX_ENT && n < LIST_BATCH) {
        raw = total;
        d = NAME_AT(raw); e = gb_entname();
        for (i = 0; i < 11; i++) d[i] = e[i];
        icons[raw] = entry_icon(name83(e));
        j = raw;
        while (j && entry_less(raw, order[j - 1])) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = raw;
        total++; n++;
        if (n < LIST_BATCH && total < MAX_ENT) p = gb_dirn();
    }
    list_state = (!p || total == MAX_ENT) ? LIST_FREE : LIST_NEXT;
}
#else
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
#endif

static void draw_list_row(unsigned char i)
{
    unsigned char y, p, raw, up = up_avail();
    unsigned char dt = disp_total();
    p = (unsigned char)(top + i);
    y = CT_Y + i * ROW_H;
    gb_fill(CT_X, y, CT_W, ROW_H, 0);          /* clear stale rows/icons after scroll/repaint */
    if (p >= dt) return;
    if (up && p == 0) {                        /* the ".." parent-dir entry (#142) */
        gb_icon(ICON_UP, CT_X, y + 1);         /* 16px icon - fits the row at full height */
        gb_text(CT_X + 9, y + 6, "..");
    } else {
        raw = order[(unsigned char)(p - up)];
        draw_entry_type(raw, CT_X, y + 1, 1); /* half-height type icon (#103/#426) */
        gb_text(CT_X + 9, y + 6, name83(NAME_AT(raw)));   /* NAME.EXT */
    }
    if (nsel == (unsigned char)(p + 1))        /* red frame on the selected row */
        gb_frame(CT_X, y, CT_W, 17, 3);
}

static void draw_list_view(void)
{
    unsigned char i;
    for (i = 0; i < LVIS; i++) draw_list_row(i);   /* draw from the sorted cache (#118) */
}

static void draw_icons_row(unsigned char r)
{
    unsigned char c, cx, cy, raw, cell_w = CELL_W, up = up_avail();
    unsigned int idx = (unsigned int)(top + r) * ICOLS;
    unsigned int dt = disp_total();
    cy = (unsigned char)(CT_Y + r * CELL_H);
    cx = CT_X;
    for (c = 0; c < ICOLS; c++) {
        gb_fill(cx, cy, cell_w, CELL_H - 1, 0);             /* clear whole cell before repaint */
        if (idx < dt) {
            if (up && idx == 0) {                            /* the ".." entry (#142) */
                gb_icon(ICON_UP, (unsigned char)(cx + (cell_w - 4) / 2),  /* 16px, centered */
                        (unsigned char)(cy + 9));                    /* in the 32px band */
                gb_text((unsigned char)(cx + (cell_w - 3) / 2), cy + 34, "..");  /* centered */
            } else {
                raw = order[(unsigned char)(idx - up)];
                draw_entry_type(raw, cx + (cell_w - 8) / 2, cy + 1, 0); /* full icon (#103/#426) */
                draw_name(cx, cy + 34, name83(NAME_AT(raw)));         /* name below the icon */
            }
            if (nsel == (unsigned char)(idx + 1))
                gb_frame(cx, cy, cell_w, CELL_H - 1, 3);
        }
        idx++;
        cx = (unsigned char)(cx + cell_w);
    }
}

static void draw_icons_view(void)
{
    unsigned char r;
    for (r = 0; r < IVIS; r++) draw_icons_row(r);
}

#ifdef GB_PREEMPTIVE
static void icon_scan_reset(void)
{
    icon_scan_pos = 0;
    icon_scan_col = 0;
    icon_req_x = (view == V_ICONS)
        ? (unsigned char)(CT_X + (CELL_W - 8) / 2) : CT_X;
    icon_req_y = CT_Y + 1;
#ifdef GB_PCW
    icon_probe_wait = APPICON_FIRST_DELAY;
#endif
}
#endif

static void sel_frame(unsigned char pos, unsigned char pen)
{
    unsigned char line = 0, x, y;
    if (!pos || view != V_ICONS) return;
    pos--;
    while (pos >= ICOLS) { pos -= ICOLS; line++; }
    if (line < top || line >= top + IVIS) return;
    x = (unsigned char)(CT_X + pos * CELL_W);
    y = (unsigned char)(CT_Y + (line - top) * CELL_H);
    gb_frame(x, y, CELL_W, CELL_H - 1, pen);
}

static void select_entry(unsigned char pos)
{
    if (nsel == pos) return;
    if (view != V_ICONS) { nsel = pos; draw(); return; }
    gb_curhide();
    sel_frame(nsel, 0);
    nsel = pos;
    sel_frame(nsel, 3);
    gb_curshow();
}

/* draw_body: the window CONTENT (scrollbar + listing + grip); the WM drew the frame (#146). */
static void draw_body(void)
{
    gb_set_drive(my_drive);              /* this window's drive (#65) - a repaint may run
                                            while another window's drive is active */
#ifdef GB_PREEMPTIVE
    icon_scan_reset();
#endif
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

/* scroll_draw: retain the rows which remain visible after a short scroll and
   draw only the newly exposed rows. Besides reducing screen traffic, this keeps
   already-rendered APP-owned icons on screen instead of reloading their headers
   from slow media after every scrollbar click. gb_copybuf is scratch here; no
   storage job can run while the focused File Manager handles this click. */
static void scroll_draw(unsigned char old_top)
{
    unsigned char delta, row_h, rows, shift, copy_h, y, r;

    rows = vis_lines();
    row_h = (view == V_ICONS) ? CELL_H : ROW_H;
    delta = (top > old_top) ? (unsigned char)(top - old_top)
                            : (unsigned char)(old_top - top);
    gb_curhide();
#ifdef GB_PREEMPTIVE
    icon_scan_reset();
#endif
    draw_scrollbar();
    if (delta >= rows) {
        if (view == V_ICONS) draw_icons_view();
        else                 draw_list_view();
        gb_curshow();
        return;
    }

    shift = (unsigned char)(delta * row_h);
    copy_h = (unsigned char)(rows * row_h - shift);
    if (top > old_top) {
        for (y = 0; y < copy_h; y++) {
            gb_saverect(CT_X, (unsigned char)(CT_Y + shift + y), CT_W, 1, gb_copybuf);
            gb_restorerect(CT_X, (unsigned char)(CT_Y + y), CT_W, 1, gb_copybuf);
        }
        r = (unsigned char)(rows - delta);
    } else {
        y = copy_h;
        while (y) {
            y--;
            gb_saverect(CT_X, (unsigned char)(CT_Y + y), CT_W, 1, gb_copybuf);
            gb_restorerect(CT_X, (unsigned char)(CT_Y + shift + y), CT_W, 1, gb_copybuf);
        }
        r = 0;
        rows = delta;
    }
    for (; r < rows; r++) {
        if (view == V_ICONS) draw_icons_row(r);
        else                 draw_list_row(r);
    }
    gb_curshow();
}

#ifdef GB_PREEMPTIVE
/* Probe at most one visible APP per frame. A normal repaint leaves its generic
   placeholder and marks a known custom icon pending, so no filesystem operation
   ever runs from fm_draw. */
static unsigned char appicon_step(void)
{
    unsigned char p, raw, state, up = up_avail();
    unsigned char limit = (view == V_ICONS)
        ? (unsigned char)(IVIS * ICOLS) : LVIS;

    if (icon_scan_pos >= limit) return 0;
    p = (view == V_ICONS)
        ? (unsigned char)(top * ICOLS + icon_scan_pos)
        : (unsigned char)(top + icon_scan_pos);
    if (p >= disp_total()) { icon_scan_pos = limit; return 0; }

    if (!(up && p == 0)) {
        raw = order[(unsigned char)(p - up)];
        state = icons[raw];
        if ((state & APPICON_SLOTMASK) == ICON_APP
            && (state & (APPICON_UNKNOWN | APPICON_PENDING))) {
            icon_req_raw = raw;
            gb_set_drive(my_drive);
            gb_curhide();
            icons[raw] = module_draw_icon()
                ? (unsigned char)(APPICON_EMBEDDED | ICON_APP) : ICON_APP;
            gb_curshow();
        }
    }

    icon_scan_pos++;
    if (view == V_ICONS) {
        icon_scan_col++;
        if (icon_scan_col == ICOLS) {
            icon_scan_col = 0;
            icon_req_x = (unsigned char)(CT_X + (CELL_W - 8) / 2);
            icon_req_y = (unsigned char)(icon_req_y + CELL_H);
        } else icon_req_x = (unsigned char)(icon_req_x + CELL_W);
    } else icon_req_y = (unsigned char)(icon_req_y + ROW_H);
    return 1;
}
#endif
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
#ifdef GB_PREEMPTIVE
    list_start();
#else
    build_list();              /* stream + sort the directory (sets total) (#118) */
    free_known = gb_fs_free_kib(&free_kib);
    top = 0; nsel = 0;
    clamp_top();
    win_title();               /* the path changed -> refresh the title buffer (#146) */
    gb_restore_parent();       /* full repaint: the WM redraws the frame/title + fm_draw */
#endif
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
     .APP / .SAV      a GEOBENCH app/screensaver -> run it
     .IST / .SPR      the icon/cursor editor (ICONED), with the file
     .TXT / .CFG      the text editor (NOTEPAD), with the file
     .PIC             the image-only VIEWER
     .HTM             an offline page -> BROWSER.APP
     .BAS             a GB-BASIC program -> opens in BASIC.APP
     .BIN             a native binary -> an info note (exec unimplemented, #236)
     anything else    no associated GEOBENCH application */
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
    if (ext_is(e, 'A', 'P', 'P') || ext_is(e, 'S', 'A', 'V'))
        gb_wm_open(e);                          /* #234: run the app/screensaver image itself */
    else if (ext_is(e, 'I', 'S', 'T') || ext_is(e, 'S', 'P', 'R'))
        gb_wm_launch_as("ICONED  APP");
    else if (ext_is(e, 'T', 'X', 'T') || ext_is(e, 'C', 'F', 'G'))
        gb_wm_launch_as("NOTEPAD APP");
    else if (ext_is(e, 'P', 'I', 'C'))
        gb_wm_launch_as("VIEWER  APP");
    else if (ext_is(e, 'H', 'T', 'M'))
        gb_wm_launch_as("BROWSER APP");
    else if (ext_is(e, 'B', 'A', 'S'))          /* GB-BASIC programs open in BASIC.APP */
        gb_wm_launch_as("BASIC   APP");
    else if (ext_is(e, 'B', 'I', 'N'))          /* #236: native binaries aren't runnable
                                                   from GEOBENCH (exec unimplemented) - say so */
        gb_alert("Binary programs cannot", "be run from GEOBENCH.");
    else
        gb_alert("No application for", "this file type.");
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
        if (nt != top) {
            unsigned char old = top;
            top = nt; clamp_top(); scroll_draw(old);
        }
    }
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
        gb_fill(CT_X, CT_Y, CT_W, CT_H, 0);   /* icons<->list must repaint over a blank content rect */
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
        win_x = 0; win_y = 8; win_w = GB_COLS; win_h = GB_LINES - 8;
    } else {
        win_x = fs_px; win_y = fs_py; win_w = fs_pw; win_h = fs_ph;
    }
    gb_wm_setpos(win_x, win_y);
    gb_wm_setsize(win_w, win_h);
    clamp_top();                          /* a taller window shows more rows */
    gb_wm_damage(0, 8, GB_COLS, GB_LINES - 8); /* repaint ONCE in on_frame over the FULL toggle area (like
                                             every other app): a restore shrinks+moves the window, and
                                             setsize's clip doesn't cover the area the maximized window
                                             vacated - leaving a ghost scrollbar/listing behind (#156) */
}

static const char *const fm_view_items[] = { "Icons / List", 0 };
static const gb_doc_t fmdoc = {          /* no document -> no File menu, just View */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, fm_fullscreen, fm_view_items, fm_view
};

#ifdef GB_PREEMPTIVE
/* A copy stays on the root task because every filesystem operation is kernel-owned.
   The kernel already captured its source context at drag start; one complete
   read/write chunk runs per focused frame, leaving input, the clock, and compute
   workers live between chunks. The WM raises the claimed target, and other File
   Managers suspend storage actions until release; a chunk never leaves live data
   in the shared transfer buffer across frames. */
static void copy_start(void)
{
    copy_ofs_lo = 0;
    copy_ofs_hi = 0;
    copy_created = 0;
    copy_state = COPY_DRAW;
    title_buf[0] = 'C'; title_buf[1] = 'o'; title_buf[2] = 'p'; title_buf[3] = 'y';
    title_buf[4] = 'i'; title_buf[5] = 'n'; title_buf[6] = 'g'; title_buf[7] = 0;
    gb_drop_claim();
}

static void copy_remove_partial(void)
{
    if (!copy_created) return;
    gb_set_drive(my_drive);
    gb_file_delete(gb_dragname);
}

static void copy_error(void)
{
#ifdef GB_MSX2
    if (gb_msx_drive_media(my_drive) == GB_MSX_MEDIA_FLOPPY) {
#else
    if (my_drive == GB_DRIVE_A || my_drive == GB_DRIVE_B) {
#endif
        gb_alert("Copy failed", "floppy write error");
    } else {
        gb_alert("Copy failed", "too big or disk full");
    }
}

static void copy_finish(void)
{
    unsigned char result = copy_state;
    FS_XFLAGS = 0;
    gb_drop_release();
    if (result != COPY_DONE) copy_remove_partial();
    copy_created = 0;
    copy_state = COPY_IDLE;
    relist();
    if (result == COPY_FAILED) copy_error();
}

static void copy_step(void)
{
    unsigned int got;

    if (copy_state == COPY_DRAW) {
        copy_state = COPY_TRANSFER;
        gb_wm_damage(win_x, win_y, win_w, win_h);
        gb_restore_parent();
        return;
    }
    if (copy_state >= COPY_DONE) {
        copy_finish();
        return;
    }
    if (copy_state != COPY_TRANSFER) return;

    gb_set_drive(my_drive);
    gb_copy_begin();
    gb_set_name(gb_dragname);
    FS_LOAD_OFS[0] = (unsigned char)copy_ofs_lo;
    FS_LOAD_OFS[1] = (unsigned char)(copy_ofs_lo >> 8);
    FS_LOAD_OFS[2] = copy_ofs_hi;
    FS_XFLAGS = 0x01;
    got = gb_fs_load(gb_copybuf, GB_COPYMAX);
    gb_copy_end();
    if (got == 0) {
        FS_XFLAGS = 0;
        copy_state = (copy_ofs_lo || copy_ofs_hi) ? COPY_DONE : COPY_FAILED;
        return;
    }
    if (got > GB_COPYMAX) got = GB_COPYMAX;

    gb_set_drive(my_drive);
    gb_set_name(gb_dragname);
    FS_XFLAGS = (copy_ofs_lo || copy_ofs_hi) ? 0x06 : 0x04;
    if (!copy_created) copy_created = 1;
    FSV_DIAG = 0xEE;
    if (!gb_fs_save(gb_copybuf, got)) {
        FS_XFLAGS = 0;
        copy_state = COPY_FAILED;
        return;
    }
    FS_XFLAGS = 0;
    {
        unsigned int n = (unsigned int)(copy_ofs_lo + got);
        if (n < copy_ofs_lo) copy_ofs_hi++;
        copy_ofs_lo = n;
    }
    if (got < GB_COPYMAX) copy_state = COPY_DONE;
}
#else
/* copy_file: copy gb_dragname from the drag-source drive/dir onto my_drive in
   <=GB_COPYMAX chunks. Each pass reads a chunk at the running offset (FS_XFLAGS
   chunk-read) and appends it to the dest (FS_XFLAGS append after the first),
   looping until a short read marks EOF. gb_copy_begin/end swap the drive+dir
   context per chunk, so same- and cross-drive both work. The app offset is 24-bit;
   CPC floppy files are still capped by the headed AMSDOS format/backing module.
   Returns 1 ok, 0 on any failure. */
static unsigned char copy_file(void)
{
    unsigned int   ofs_lo = 0;                  /* 24-bit offset as 16-bit low + 8-bit high */
    unsigned char  ofs_hi = 0;                  /* (cheaper codegen than a 32-bit long)      */
    unsigned int   got;
    unsigned char  first = 1;

    for (;;) {
        gb_set_drive(my_drive);                 /* so copy_end restores to our drive */
        gb_copy_begin();                        /* -> drag source drive/dir */
        gb_set_name(gb_dragname);
        FS_LOAD_OFS[0] = (unsigned char)ofs_lo;
        FS_LOAD_OFS[1] = (unsigned char)(ofs_lo >> 8);
        FS_LOAD_OFS[2] = ofs_hi;
        FS_XFLAGS = 0x01;                       /* chunk-read from the offset */
        got = gb_fs_load(gb_copybuf, GB_COPYMAX);
        gb_copy_end();                          /* -> our drive/dir */
        if (got == 0) { FS_XFLAGS = 0; return (unsigned char)!first; }  /* EOF ok / empty fail */
        if (got > GB_COPYMAX) got = GB_COPYMAX;  /* defensive: never save past the staging chunk */
        gb_set_drive(my_drive);
        gb_set_name(gb_dragname);
        FS_XFLAGS = first ? 0x04 : 0x06;        /* chunk-save create first, append after */
        FSV_DIAG = 0xEE;                         /* floppy diag: unchanged => writer didn't run */
        if (!gb_fs_save(gb_copybuf, got)) { FS_XFLAGS = 0; return 0; }
        {
            unsigned int n = (unsigned int)(ofs_lo + got);
            if (n < ofs_lo) ofs_hi++;           /* 16-bit carry into bits 16-23 */
            ofs_lo = n;
        }
        first = 0;
        if (got < GB_COPYMAX) break;            /* short read = EOF */
    }
    FS_XFLAGS = 0;
    return 1;
}
#endif

/* on_event: a file dropped here from another window is copied onto our drive (#65);
   a top-bar menu click goes to the framework's View handler (#142). */
static void on_event(void)
{
    sync_rect();
    if (gb_msg.type == GB_MSG_DROP) {     /* a file dropped here from another window (#65) */
#ifdef GB_PREEMPTIVE
        if (copy_state == COPY_IDLE && list_state == LIST_IDLE
            && !gb_drop_claimed()) copy_start();
#else
        if (!copy_file()) {               /* copy it onto THIS window's drive, any size (#74) */
#ifdef GB_MSX2
            if (gb_msx_drive_media(my_drive) == GB_MSX_MEDIA_FLOPPY) {
#else
            if (my_drive == GB_DRIVE_A || my_drive == GB_DRIVE_B) {
#endif
                gb_alert("Copy failed", "floppy write error");
            } else {
                gb_alert("Copy failed", "too big or disk full");
            }
        }
        relist();
#endif
        return;
    }
#ifdef GB_PREEMPTIVE
    if (copy_state != COPY_IDLE || gb_drop_claimed()) return;
#endif
    gb_doc_event();                       /* the View menu (Fullscreen / Icons-List / Up) */
}

/* on_frame (#146): the WM handled close/drag/grip routing; re-assert our drive, tick the
   double-click timer, and run the View menu framework. */
static void fm_frame(void)
{
    sync_rect();
    gb_set_drive(my_drive);              /* re-assert our drive each focused frame (#65) */
#ifdef GB_PREEMPTIVE
    if (copy_state != COPY_IDLE) { copy_step(); return; }
    if (list_state != LIST_IDLE) { list_step(); return; }
    if (gb_drop_claimed()) return;        /* another File Manager owns the storage job */
#ifdef GB_PCW
    if (icon_probe_wait) icon_probe_wait--;
    else if (appicon_step()) return;
#else
    if (appicon_step()) return;
#endif
#endif
    if (dc_timer) dc_timer--;
    if (gb_doc_frame()) { gb_restore_parent(); return; }   /* a View menu ran (#142) */
}

/* on_close (#146): no document to save -> just close. */
static void fm_close(void)
{
#ifdef GB_PREEMPTIVE
    if (copy_state != COPY_IDLE) {
        FS_XFLAGS = 0;
        gb_drop_release();
        copy_remove_partial();
        copy_created = 0;
        copy_state = COPY_IDLE;
#ifdef GB_PCW
        icon_cache_release();
#endif
        gb_wm_close();
        return;
    }
    if (list_state != LIST_IDLE) {
        if (list_state != LIST_WAIT) gb_drop_release();
        list_state = LIST_IDLE;
    }
#endif
    if (gb_doc_close()) {
#ifdef GB_PCW
        icon_cache_release();
#endif
        gb_wm_close();
    }
    else gb_restore_parent();
}

/* on_drag (#146): a title-bar press -> move the window. */
static void fm_drag(void)
{
    sync_rect();
#ifdef GB_PREEMPTIVE
    if (copy_state != COPY_IDLE || list_state != LIST_IDLE || gb_drop_claimed()) return;
#endif
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

#ifdef GB_PREEMPTIVE
    if (copy_state != COPY_IDLE || list_state != LIST_IDLE || gb_drop_claimed()) return;
#endif

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

    /* scrollbar: shared arrows/page regions plus the app-owned thumb drag loop */
    if (mx >= SB_X && mx < SB_X + SB_W && my >= CT_Y && my < CT_BOT) {
        unsigned char old = top, tl = total_lines(), vl = vis_lines();
        unsigned char part = gb_vscroll_hit(
            SB_X, CT_Y, SB_W, CT_H, top, tl, vl, mx, my, GB_WIDGET_ARROWS);
        if (part == GB_SCROLL_UP) {
            if (top) top--;
        } else if (part == GB_SCROLL_DOWN) {
            if (tl > vl && top < tl - vl) top++;
        } else if (part == GB_SCROLL_PAGE_UP) {
            top = (top > vl) ? (unsigned char)(top - vl) : 0;
            clamp_top();
        } else if (part == GB_SCROLL_PAGE_DOWN) {
            top = (unsigned char)(top + vl);
            clamp_top();
        } else if (part == GB_SCROLL_THUMB) {
            sb_drag();
            return;
        }
        if (top != old) scroll_draw(old);
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
            else { select_entry(idx + 1); dc_idx = idx; dc_timer = DCLICK; }
            return;
        }
        idx = (unsigned char)(idx - up_avail());   /* display position -> sorted real index */
        /* press on an entry: a drag (press + move) drags it to another window /
           the Trash (#62); a plain click (no move) falls through to select/open */
        dir_seek(order[idx]);                /* sorted index -> raw entry for gb_entname */
        if (gb_drag_start(gb_entname())) {   /* dropped on a target -> it handled it */
#ifdef GB_PREEMPTIVE
            if (!gb_drop_claimed()) relist(); /* an async target owns focus + its own repaint */
#else
            relist();                         /* refresh (a move changes this dir) */
#endif
            return;
        }
        if (dc_timer && dc_idx == (unsigned char)(idx + up_avail())) {   /* double-click -> open */
            open_entry(idx);
            dc_timer = 0;
        } else {                              /* single click -> select */
            select_entry((unsigned char)(idx + up_avail() + 1));
            dc_idx = (unsigned char)(idx + up_avail());
            dc_timer = DCLICK;
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
    win_x = DEF_X + my_drive * CASCADE_X;   /* cascade drive windows, but keep Disk B on-screen */
    win_y = DEF_Y + my_drive * CASCADE_Y;
    fmmw.x = win_x;              /* register the window at the cascaded position */
    fmmw.y = win_y;
    gb_wm_managed(&fmmw);        /* register first (no draw, focus) so gb_set_name/fs_load
                                   target our window for the config read below */
    gb_doc(&fmdoc);              /* View menu (Fullscreen / Icons-List); no File (#142) */
    gb_set_drive(my_drive);
    /* Open at the drive ROOT. The kernel's directory position is a single global
       (fs_dir_clus / dir stack); a previously-open window may have left it in a
       subdir, so reopening this drive would list that subdir - and with fm_path
       empty, with no ".." to climb out. Pop back to the top (gb_back is a no-op at
       root on both backends; DIRSTACK is 4 deep) so the listing matches fm_path. */
    { unsigned char k; for (k = 0; k < 4; k++) gb_back(); }
    cfg_load_view();             /* VIEW= from GEOBENCH.CFG -> view (default icons) */
#ifdef GB_PREEMPTIVE
    fmmw.title = title_buf;
    list_start();                /* scan first; list_step publishes one complete paint */
#else
    build_list();                /* stream + sort the directory (sets total) (#118) */
    free_known = gb_fs_free_kib(&free_kib);
    top = 0;
    clamp_top();
    fmmw.title = win_title();    /* build "<drive><path>" before the first paint (#146) */
#endif
#ifdef GB_PREEMPTIVE
    /* k_wm_managed already clipped the unpublished window. Keep the desktop as-is
       until list_step can paint the completed window once. */
#else
    gb_wm_damage(0, 8, GB_COLS, GB_LINES - 8); /* opening one FM can expose older stacked FM
                                                  windows outside the new window's damage rect */
    gb_restore_parent();         /* first paint: WM chrome + fm_draw */
#endif
}
