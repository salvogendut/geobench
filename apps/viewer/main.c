/* viewer - GEOBENCH .PIC image viewer, a KERNEL-MANAGED window (#146).
 *
 * The WM owns the chrome (frame, title, close, drag, resize): the viewer registers
 * a gb_mwin_t and provides only content (on_draw) + the gb_doc menus (File>Load,
 * View>Fullscreen). It reads its live rect with gb_wm_x/y/w/h and asks the WM to
 * repaint with gb_restore_parent. Text belongs to NOTEPAD.APP; the File Manager
 * routes only .PIC files here. A picture normally uses the kernel's fast banked
 * cache. If another Viewer owns all remaining banks, rows are read from disk in
 * bounded chunks instead of rejecting the picture. */
#include "gb.h"
#include <string.h>
#include "zoom.h"
#ifdef GB_AGON
#include "agon_viewport.h"
#endif

#define VIEW_MAX  2560    /* ten maximum-width Screen-7 rows per buffered read */
#ifdef GB_AGON
#define TITLE_PAD 15
static const unsigned char windowed=0;
#define WM_FS (&windowed)
#else
#define TITLE_PAD 12
#define WM_FS     ((volatile unsigned char *)0x130A)
#endif
#define TX_Y0     (unsigned char)(win_y + TITLE_PAD)

#define DEF_X     2
#define DEF_Y     14
#define DEF_W     76
#define DEF_H     180
#define MIN_W     30
#define MIN_H     72
#define SB_W      3       /* scrollbar width, byte cols (#166) */
#define HSB_H     7       /* horizontal scrollbar height, pixel lines */
#define ZOOM_W    4       /* four byte columns = 16 logical pixels */
#define ZOOM_H    16
#define ZOOM_STEP (ZOOM_W + 1)
#define BODY_PAD  (TITLE_PAD + 2 + ZOOM_H)
#define ZOOM_X    ((unsigned char)(win_x + win_w - GRIP_W - 2 - ZOOM_STEP - ZOOM_W))
#define ZOOM_Y    ((gb_scan_t)(win_y + win_h - 1 - ZOOM_H))

static char filebuf[VIEW_MAX];
#ifdef GB_AGON
static unsigned char rowbuf[640/2];
#else
static unsigned char rowbuf[GB_COLS * 2];
#endif    /* one visible row, even in Screen 7 */
static unsigned int filen;
static unsigned char win_x, win_y, win_w;
static gb_scan_t win_h;   /* refreshed from the WM each draw */
static char vtitle[14];                            /* "NAME.EXT" - the WM draws it */
static unsigned char opened;                       /* 0 -> the next frame sizes to a picture */
static unsigned char loaded;                       /* 0 until the file is read (blank, no msg) */
static unsigned char my_drive;                     /* source drive for streamed redraws */

/* fmt83: 11-byte 8.3 name -> "NAME.EXT". */
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

static unsigned char pic_wb;
static unsigned int  pic_h;                            /* 16-bit: tall pictures exceed 255 (#166) */
static unsigned int  pic_off;
static unsigned int  pic_stride;                       /* source bytes per row (mode-dependent) */
static unsigned char pic_mode;                         /* GBPC packing: 1=2bpp, 7=Screen-7 4bpp */
static unsigned char banked;                           /* the borrowed bank (0 = in-page), #164 */
static unsigned char banked2;                          /* optional second bank for .PIC files >16K */
#ifdef GB_MSX2
static unsigned char banked3, banked4;                 /* Screen-7 pictures can occupy four banks */
#endif
static unsigned int  scroll_y;                         /* top visible picture row (#166 scroll) */
static unsigned int scroll_x;                          /* left displayed four-pixel column */
static unsigned char zoom;
static unsigned int pic_pixels, display_w, visible_src_h;
static unsigned char tail_pixels;
static unsigned char view_x, view_y, view_w, view_vsb, view_hsb;
static unsigned char hbar_x, hbar_w;
static gb_scan_t view_h, hbar_y;
static unsigned char streamed;                         /* no bank: read visible source rows on demand */
static unsigned char invalid_pic;                      /* malformed or unsupported .PIC */
#define PIC_PAGE_K (*(volatile unsigned char *)0x130B) /* the kernel's single "active pic bank";
                                                          re-select OURS before each blit/close so
                                                          a second Viewer window can't steal it (#164) */
#define PIC_PAGE2_K (*(volatile unsigned char *)0x1348)
#ifdef GB_MSX2
#define PIC_PAGE3_K (*(volatile unsigned char *)0x1291)
#define PIC_PAGE4_K (*(volatile unsigned char *)0x1292)
#define PIC_MODE_K  (*(volatile unsigned char *)0x1293)
#define PIC_STRIDE_K (*(volatile unsigned int *)0x1294)
#endif
#define PIC_WB_K  (*(volatile unsigned char *)0x130C)  /* kernel-parsed header for the banked .PIC */
#define PIC_H_K   (*(volatile unsigned int  *)0x130D)  /* 16-bit height */
#define PIC_OFF_K (*(volatile unsigned char *)0x130F)
#define UI_MODAL_K (*(volatile unsigned char *)0x1705)
#define FS_SAVE_LEN_K (*(volatile unsigned int *)0x14FD)
#define FS_LOAD_OFS   ((volatile unsigned char *)0x144C)
#define FS_XFLAGS     (*(volatile unsigned char *)0x144F)
#ifdef GB_MSX2
#define MSX_SCRMOD    (*(volatile unsigned char *)0xFCAF)
#endif

#ifdef GB_AGON
#include "agon_bitmap.h"
#else
static void select_banked_picture(void)
{
    PIC_PAGE_K = banked;
    PIC_PAGE2_K = banked2;
#ifdef GB_MSX2
    PIC_PAGE3_K = banked3;
    PIC_PAGE4_K = banked4;
    PIC_MODE_K = pic_mode;
    PIC_STRIDE_K = pic_stride;
#endif
}

static unsigned char is_pic(void)
{
    return (unsigned char)(filen >= 6 &&
        filebuf[0] == 'G' && filebuf[1] == 'B' && filebuf[2] == 'P' && filebuf[3] == 'C');
}
static unsigned char is_pic_name(const char *n);   /* fwd: defined with the cycling code below */
static unsigned char have_pic(void) { return (unsigned char)(banked || streamed); }

static unsigned char parse_pic(void)
{
    if ((unsigned char)filebuf[4] == 2) {
        unsigned int w, h;
        if (filen < 14) return 0;
        pic_mode = (unsigned char)filebuf[5];
#ifdef GB_MSX2
        if (pic_mode == 7) {
            if (MSX_SCRMOD != 7) return 0;
        }
#else
        if (pic_mode == 7) return 0;
#endif
        if (pic_mode != 1 && pic_mode != 7) return 0;
        w = (unsigned char)filebuf[6] | ((unsigned int)(unsigned char)filebuf[7] << 8);
        h = (unsigned char)filebuf[8] | ((unsigned int)(unsigned char)filebuf[9] << 8);
        if (!w || w > 512 || !h) return 0;
        pic_pixels = w;
        pic_wb  = (unsigned char)((w + 3) >> 2);
        pic_stride = (pic_mode == 7) ? (unsigned int)((w + 1) >> 1) : pic_wb;
        pic_h   = h;
        pic_off = 14;
        if (pic_h > (unsigned int)(65521U / pic_stride)) return 0;
    } else {
        pic_mode = 1;
        pic_wb  = (unsigned char)filebuf[4];
        if (!pic_wb || !(unsigned char)filebuf[5]) return 0;
        pic_stride = pic_wb;
        pic_pixels = (unsigned int)pic_wb * 4;
        pic_h   = (unsigned char)filebuf[5];
        pic_off = 6;
    }
    return 1;
}

/* Read a file fragment through the cross-device chunk protocol. Keeping the
   offset setup here ensures every backend sees the same request and FS_XFLAGS is
   cleared even after a short/failed read. */
static unsigned int stream_read(unsigned int off, unsigned int max)
{
    unsigned int got;
    gb_set_drive(my_drive);
    FS_LOAD_OFS[0] = (unsigned char)off;
    FS_LOAD_OFS[1] = (unsigned char)(off >> 8);
    FS_LOAD_OFS[2] = 0;
    FS_XFLAGS = 1;
    got = gb_fs_load(filebuf, max);
    FS_XFLAGS = 0;
    return got;
}

/* PICEDIT copies one mapped 16K page into common RAM. Split even when a
   single scanline straddles a page boundary; never map out our destination. */
static unsigned char read_picture(unsigned int off, unsigned int len)
{
    unsigned char *dst = (unsigned char *)filebuf;
    if (!banked) return stream_read(off, len) == len;
    while (len) {
        unsigned int pos = off & 0x3fff, n = 0x4000 - pos;
        unsigned char page;
        switch (off >> 14) {
        case 0: page = banked; break;
        case 1: page = banked2; break;
#ifdef GB_MSX2
        case 2: page = banked3; break;
        case 3: page = banked4; break;
#endif
        default: return 0;
        }
        if (!page) return 0;
        if (n > len) n = len;
        PIC_PAGE_K = page;
        gb_pic_edit_buf = (unsigned int)gb_copybuf;
        gb_pic_edit_off = pos;
        FS_SAVE_LEN_K = n;
        if (!gb_pic_edit(GB_PICEDIT_CHUNK)) return 0;
        memcpy(dst, gb_copybuf, n);
        dst += n; off += n; len -= n;
    }
    return 1;
}

/* Load the opened file. Prefer the kernel's banked cache; if its app-pool pages
   are exhausted, retain only the parsed header and stream visible rows later. */
static void bank_or_parse(void)
{
    char nm[11];
    scroll_y = 0;                                  /* a fresh file starts at the top (#166) */
    scroll_x = 0;
    zoom = NORMAL;
    if (banked) {
        PIC_PAGE_K = banked; PIC_PAGE2_K = banked2;
#ifdef GB_MSX2
        PIC_PAGE3_K = banked3; PIC_PAGE4_K = banked4;
#endif
        gb_pic_close();
        banked = banked2 = 0;
#ifdef GB_MSX2
        banked3 = banked4 = 0;
#endif
    }  /* free OUR old bank only -
                                                      PIC_PAGE may hold another window's (#164) */
    banked2 = 0;
#ifdef GB_MSX2
    banked3 = banked4 = 0;
#endif
    streamed = 0;
    invalid_pic = 0;
    gb_get_name(nm);
    if (!nm[0] || nm[0] == ' ') return;            /* VIEWER.APP opened without a document */
    gb_set_name(nm);
    banked = gb_pic_open();                        /* open sets PIC_PAGE to the new bank itself */
    UI_MODAL_K = 0;                                 /* picture loaders reuse #1700; keep menus live */
    if (banked) {                                  /* in a bank: read the kernel-parsed header */
        banked2 = PIC_PAGE2_K;
#ifdef GB_MSX2
        banked3 = PIC_PAGE3_K; banked4 = PIC_PAGE4_K;
        pic_mode = PIC_MODE_K; pic_stride = PIC_STRIDE_K;
#else
        pic_mode = 1; pic_stride = PIC_WB_K;
#endif
        pic_wb = PIC_WB_K; pic_h = PIC_H_K; pic_off = PIC_OFF_K;
        pic_pixels = 0;  /* exact v2 width only needed when resampling */
        return;
    }
    filen = stream_read(0, 14);
    if (is_pic() && parse_pic()) {
        streamed = 1;
        return;
    }
    invalid_pic = 1;
    pic_wb = 28; pic_h = 32; pic_off = 0; pic_stride = 0;
}

static void size_to_pic(void)   /* size the window to the picture, clamped on screen */
{
    unsigned char x = gb_wm_x(), y = gb_wm_y(), h;
    unsigned int w;
    if ((unsigned int)pic_wb + 3 > (unsigned char)(GB_COLS - x) ||
        pic_h > GB_LINES - y - BODY_PAD) {
        x = 0; y = 8;                              /* move once to the roomy origin, then clamp
                                                       width/height independently. */
        gb_wm_setpos(x, y);
    }
    h = GB_LINES - y;
    if (pic_h <= h - BODY_PAD) h = (unsigned char)(pic_h + BODY_PAD);
    if (h < MIN_H) h = MIN_H;
    w = (unsigned int)pic_wb + 3 + (pic_h > h - BODY_PAD ? SB_W : 0);
    if (w < MIN_W) w = MIN_W;
    if (w > GB_COLS - x) w = GB_COLS - x;
    gb_wm_setsize(w, h);
}
#endif

/* Keep vertical positions in source rows: a tall picture at 4x need not
   fit a 16-bit display coordinate. Horizontal positions fit at all scales. */
static unsigned int source_height(gb_scan_t lines)
{
    if (zoom == HALF) return (unsigned int)lines * 2;
    if (zoom == QUAD) return (lines + 3u) >> 2;
    if (zoom == DOUBLE) return (lines + 1u) >> 1;
    return lines;
}
static unsigned int display_height(unsigned int rows, gb_scan_t limit)
{
    if (rows >= source_height(limit)) return limit;
    if (zoom == HALF) return (rows >> 1) + (rows & 1);
    return rows << (zoom - NORMAL);
}
static void calc_view(void)
{
    unsigned int pixels = pic_pixels ? pic_pixels : (unsigned int)pic_wb * 4;
    if (zoom == HALF) pixels = (pixels + 1) >> 1;
    else pixels <<= zoom - NORMAL;
    tail_pixels = pixels & 3;
    display_w = (pixels + 3) >> 2;
    if (*WM_FS) {
        view_vsb = view_hsb = 0;
        hbar_w = 0;
        view_x = (unsigned char)(display_w < GB_COLS ? (GB_COLS - display_w) / 2 : 0);
        view_y = (unsigned char)((GB_LINES - display_height(pic_h, GB_LINES)) / 2);
        view_w = (unsigned char)(GB_COLS - view_x);
        view_h = (unsigned char)(GB_LINES - view_y);
    } else {
        view_h = win_h - BODY_PAD;
        view_vsb = pic_h > source_height(view_h);
        view_w = win_w - 3 - (view_vsb ? SB_W : 0);
        view_hsb = display_w > view_w;
        view_x = win_x + win_w - 1 - view_w;
        view_y = TX_Y0;
        hbar_x = view_x - 1;
        hbar_y = ZOOM_Y + (ZOOM_H - HSB_H) / 2;
        hbar_w = ZOOM_X - hbar_x - 1;
    }
    visible_src_h = source_height(view_h);
    if (pic_h <= visible_src_h) scroll_y = 0;
    else if (scroll_y > pic_h - visible_src_h) scroll_y = pic_h - visible_src_h;
    if (display_w <= view_w) scroll_x = 0;
    else if (scroll_x > display_w - view_w) scroll_x = display_w - view_w;
}

static void draw_invalid(void)
{
    gb_text((unsigned char)(win_x + 2), TX_Y0, "Invalid picture");
    gb_text((unsigned char)(win_x + 2), (unsigned char)(TX_Y0 + 11),
            "Unsupported format");
}

#ifdef GB_PCW
static unsigned char native_pic_byte(unsigned char b)
{
    unsigned char i, p, out = 0;
    for (i = 0; i < 4; i++) {
        p = (unsigned char)(((b >> (7 - i)) & 1) | (((b >> (3 - i)) & 1) << 1));
        out |= (unsigned char)(p << (6 - 2 * i));
    }
    return (unsigned char)(((out & 0x55) << 1) | (((out ^ 0xFF) & 0xAA) >> 1));
}
#endif

/* Scale only the visible portion. Read consecutive rows in batches, then
   convert each source row once, including when it is displayed four times. */
#ifndef GB_AGON
static void blit_buffered(unsigned char w, unsigned char rows)
{
    unsigned int sy = scroll_y, i, batch, take, start = scroll_x, count = w;
    unsigned char dy = 0, copies, skip = zoom == HALF ? 2 : 1;
    unsigned char repeat = zoom == QUAD ? 4 : zoom == DOUBLE ? 2 : 1;
    unsigned char *src;
    if (!pic_stride || !w || !rows) return;
    batch = VIEW_MAX / pic_stride;
    if (skip == 2) batch &= ~1u;
    if (!batch) return;
    if (pic_mode == 7) { start *= 2; count *= 2; }
    while (dy < rows) {
        take = source_height(rows - dy);
        if (take > batch) take = batch;
        if (take > pic_h - sy) take = pic_h - sy;
        if (!take || !read_picture(pic_off + sy * pic_stride, take * pic_stride)) {
            if (streamed) { invalid_pic = 1; streamed = 0; }
            return;
        }
        src = (unsigned char *)filebuf;
        for (i = 0; i < take && dy < rows; i += skip) {
            sample_row(rowbuf, src, pic_stride, start, count, zoom, pic_mode);
            if (tail_pixels && scroll_x + w == display_w) {
                /* The final logical column may contain fewer than four pixels. */
                static const unsigned char masks[] = {0, 0x88, 0xcc, 0xee};
                if (pic_mode != 7) rowbuf[w - 1] &= masks[tail_pixels];
                else {
                    if (tail_pixels == 1) rowbuf[count - 2] &= 0xf0;
                    rowbuf[count - 1] &= tail_pixels == 3 ? 0xf0 : 0;
                }
            }
#ifdef GB_MSX2
            if (pic_mode != 7) {
                gb_pic_edit_buf = gb_pic_edit_off = (unsigned int)rowbuf;
                FS_SAVE_LEN_K = w;
                if (!gb_pic_edit(GB_PICEDIT_NATIVE)) return;
            }
#elif defined(GB_PCW)
            { unsigned char c; for (c = 0; c < w; ++c) rowbuf[c] = native_pic_byte(rowbuf[c]); }
#endif
            for (copies = 0; copies < repeat && dy < rows; ++copies, ++dy) {
#ifdef GB_MSX2
                if (pic_mode == 7) {
                    gb_pic_edit_buf = (unsigned int)rowbuf;
                    gb_pic_edit_off = view_x | ((unsigned int)(view_y + dy) << 8);
                    FS_SAVE_LEN_K = 0x100 | w;
                    (void)gb_pic_edit(GB_PICEDIT_NATIVE16);
                } else
#endif
                    gb_restorerect(view_x, view_y + dy, w, 1, rowbuf);
            }
            src += pic_stride * skip;
        }
        sy += take;
    }
}

static void blit_pic(unsigned char x, unsigned char y, unsigned char w,
                     unsigned char rows, unsigned int src)
{
    unsigned char r = 0;
    if (!w || !rows) return;
    if (w == pic_wb && !banked2) {
        select_banked_picture();
        gb_pic_blit(x, y, w, rows, src);
        return;
    }
    while (r < rows) {
        select_banked_picture();
        gb_pic_blit(x, (unsigned char)(y + r), w, 1, src);
        src += pic_stride;
        r++;
    }
}

#endif
static void draw_pic(void)
{
    unsigned char draw_w;
    gb_scan_t rows;
#ifndef GB_AGON
    unsigned int src;
#endif
    calc_view();
    if (!view_w || !view_h) return;
    draw_w = (display_w - scroll_x > view_w) ? view_w : (unsigned char)(display_w - scroll_x);
    rows = (gb_scan_t)display_height(pic_h - scroll_y, view_h);
#ifdef GB_AGON
    agon_view(draw_w, rows);
#else
    if (streamed || zoom != NORMAL) blit_buffered(draw_w, rows);
    else {
        src = pic_off + scroll_y * pic_stride + (pic_mode == 7 ? scroll_x * 2 : scroll_x);
        blit_pic(view_x, view_y, draw_w, rows, src);
    }
#endif
    if (view_vsb)                                      /* vertical scrollbar at the LEFT edge (#166) */
        gb_vscroll16((unsigned char)(win_x + 1), TX_Y0, SB_W, view_h,
                     scroll_y, pic_h, visible_src_h);
    if (view_hsb && hbar_w)
        gb_hscroll16(hbar_x, hbar_y, hbar_w, (unsigned char)(HSB_H - 1),
                     scroll_x, display_w, view_w);
}

/* sb_drag: while the fire is held, map the pointer Y onto scroll_y and repaint. */
static void sb_drag(void)
{
    unsigned int ny;
#ifdef GB_AGON
    unsigned int thumb=scroll_y;
    unsigned char held;
    do {
        held=gb_poll() & GB_FIRE;
        ny = gb_vscroll16_value(TX_Y0, view_h, pic_h, visible_src_h, gb_my());
        if(held && ny!=thumb) {
            thumb=ny; gb_vscroll16(win_x+1,TX_Y0,SB_W,view_h,ny,pic_h,visible_src_h);
        }
    } while (held);
    if (ny != scroll_y) { scroll_y = ny; draw_pic(); }
    else if(thumb!=ny) gb_vscroll16(win_x+1,TX_Y0,SB_W,view_h,ny,pic_h,visible_src_h);
#else
    while (gb_poll() & GB_FIRE) {
        ny = gb_vscroll16_value(TX_Y0, view_h, pic_h, visible_src_h, gb_my());
        if (ny != scroll_y) { scroll_y = ny; gb_curhide(); draw_pic(); gb_curshow(); }
    }
#endif
}

static void hsb_drag(void)
{
    unsigned int nx;
    calc_view();
    if (!view_hsb || !hbar_w || display_w <= view_w) return;
#ifdef GB_AGON
    unsigned int thumb=scroll_x;
    unsigned char held;
    do {
        held=gb_poll() & GB_FIRE;
        nx = gb_hscroll16_value(hbar_x, hbar_w, display_w, view_w, gb_mx());
        if(held && nx!=thumb) {
            thumb=nx; gb_hscroll16(hbar_x,hbar_y,hbar_w,HSB_H-1,nx,display_w,view_w);
        }
    } while (held);
    if (nx != scroll_x) { scroll_x = nx; draw_pic(); }
    else if(thumb!=nx) gb_hscroll16(hbar_x,hbar_y,hbar_w,HSB_H-1,nx,display_w,view_w);
#else
    while (gb_poll() & GB_FIRE) {
        nx = gb_hscroll16_value(hbar_x, hbar_w, display_w, view_w, gb_mx());
        if (nx != scroll_x) {
            scroll_x = nx;
            gb_curhide(); draw_pic(); gb_curshow();
        }
    }
#endif
}

static void zoom_button(unsigned char x, const char *label)
{
    gb_scan_t y = ZOOM_Y;
    gb_fill(x, y, ZOOM_W, ZOOM_H, 1);
    /* A leading 6px space centres the glyph between the byte-wide edges. */
    gb_textbw(x, y + 4, label);
    gb_frame(x, y, ZOOM_W, ZOOM_H, 2);
}

/* on_draw: the WM already drew the frame/title; paint the content. */
static void v_draw(void)
{
    win_x = gb_wm_x(); win_y = gb_wm_y(); win_w = gb_wm_w(); win_h = gb_wm_h();
    if (*WM_FS) {                               /* true fullscreen: blue backdrop, centred image,
                                                   no chrome/grip (the WM drops the frame+title) */
        gb_fill(0, 0, GB_COLS, GB_LINES, 0);    /* pen 0 = desktop blue, whole screen */
        if (loaded && have_pic()) draw_pic();
        return;
    }
    if (!loaded)           ;   /* still loading -> leave the window body blank */
    else if (have_pic()) {
        draw_pic();
        zoom_button(ZOOM_X, " -");
        zoom_button(ZOOM_X + ZOOM_STEP, " +");
    }
    else if (invalid_pic)  draw_invalid();
    gb_draw_grip(win_x, win_y, win_w, win_h);   /* resize grip (#146) */
}

#ifndef GB_AGON
static void v_fullscreen(unsigned char on);    /* defined below */

/* --- fullscreen image cycling: Space / Right = next, Left = previous (#viewer) -------
   In fullscreen there's no menu bar, so the viewer reads the keys itself. The current
   directory is enumerated for .PIC files; the chosen one is re-pointed via gb_set_name
   (the window's file arg, which gb_pic_open reads) and reloaded by bank_or_parse. */
static void copy11(char *d, const char *s) { unsigned char i; for (i = 0; i < 11; i++) d[i] = s[i]; }
static unsigned char is_pic_name(const char *n) { return (unsigned char)(n[8]=='P' && n[9]=='I' && n[10]=='C'); }
static unsigned char eq11(const char *a, const char *b)
{
    unsigned char i;
    for (i = 0; i < 11; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* read_lr: matrix state of the Left/Right cursor keys -> lr_bits (bit0 left, bit1 right).
   The firmware keys are disarmed (the joystick floods them), so read them directly with
   KM_TEST_KEY (&BB1E): key 8 = Left, key 1 = Right - same approach as Notepad's caret nav. */
static unsigned char lr_bits;
#ifdef GB_PCW
/* PCW: read Left/Right straight from the memory-mapped matrix (row 1 bit7 =
 * Left, row 0 bit6 = Right, active-high) - map block 3 into the slot-3 window
 * first, per the platform's map-before-use convention (#331). */
static void read_lr(void) __naked
{
__asm
    xor  a
    ld   (_lr_bits), a
    ld   a, #0x83          ; keyboard block -> slot-3 window
    out  (0xF3), a
    ld   a, (0xFFF1)       ; matrix row 1: bit7 = Left held
    bit  7, a
    jr   z, 1$
    ld   a, (_lr_bits)
    or   #0x01
    ld   (_lr_bits), a
1$: ld   a, (0xFFF0)       ; matrix row 0: bit6 = Right held
    bit  6, a
    jr   z, 2$
    ld   a, (_lr_bits)
    or   #0x02
    ld   (_lr_bits), a
2$: ret
__endasm;
}
#elif defined(GB_MSX2)
/* MSX: read cursor Left/Right from the BIOS NEWKEY matrix (row 8, updated by the
 * interrupt; bit4=Left, bit7=Right, active-low) - no CALSLT needed (#287). */
static void read_lr(void) __naked
{
__asm
    xor  a
    ld   (_lr_bits), a
    ld   a, (0xFBED)       ; NEWKEY row 8
    bit  4, a              ; Left held? (bit clear = pressed)
    jr   nz, 1$
    ld   a, (_lr_bits)
    or   #0x01
    ld   (_lr_bits), a
1$: ld   a, (0xFBED)
    bit  7, a              ; Right held?
    jr   nz, 2$
    ld   a, (_lr_bits)
    or   #0x02
    ld   (_lr_bits), a
2$: ret
__endasm;
}
#else
static void read_lr(void) __naked
{
__asm
    xor  a
    ld   (_lr_bits), a
    ld   a, #8             ; cursor Left = key 8
    call #0xBB1E
    jr   z, 1$
    ld   a, (_lr_bits)
    or   #0x01
    ld   (_lr_bits), a
1$: ld   a, #1             ; cursor Right = key 1
    call #0xBB1E
    jr   z, 2$
    ld   a, (_lr_bits)
    or   #0x02
    ld   (_lr_bits), a
2$: ret
__endasm;
}
#endif

/* cycle to the next (dir>0) / previous (dir<0) .PIC in the current directory, wrapping. */
static void cycle(signed char dir)
{
    char cur[11], first[11], last[11], prevn[11], nextn[11], target[11];
    unsigned char have_first = 0, have_prev = 0, have_next = 0, seen = 0;
    char *p;
    gb_get_name(cur);                          /* this window's current file (11-byte arg) */
    p = gb_dir1();
    while (p) {
        char *e = gb_entname();
        if (is_pic_name(e)) {
            if (!have_first) { copy11(first, e); have_first = 1; }
            copy11(last, e);
            if (seen) { if (!have_next) { copy11(nextn, e); have_next = 1; } }
            else if (eq11(e, cur)) seen = 1;
            else { copy11(prevn, e); have_prev = 1; }
        }
        p = gb_dirn();
    }
    if (!have_first) return;                    /* no pictures in this directory */
    if (dir > 0) copy11(target, have_next ? nextn : first);   /* next, wrap to first */
    else         copy11(target, have_prev ? prevn : last);    /* prev, wrap to last */
    if (eq11(target, cur)) return;              /* only one image -> nothing to do */
    gb_set_name(target);                        /* re-point the window arg gb_pic_open reads */
    bank_or_parse();                            /* reload the picture */
    fmt83(vtitle, target);
    gb_curhide(); v_draw(); gb_curshow();        /* repaint OUR fullscreen content directly (blue
                                                    fill + the new image) - NOT gb_restore_parent,
                                                    which would flash the desktop/windows behind us */
}

/* fullscreen key handler: F exits, Space/Right advance, Left goes back (debounced). */
static unsigned char cyc_cool;
static void fs_nav(unsigned char c)
{
    if ((c | 0x20) == 'f') { gb_doc_frame_key(c); gb_restore_parent(); return; }
    if (cyc_cool) { cyc_cool--; return; }       /* debounce a recent step */
    if (c == ' ') { cycle(1); cyc_cool = 10; return; }
    read_lr();
    if (lr_bits & 0x02)      { cycle(1);  cyc_cool = 10; }   /* Right -> next */
    else if (lr_bits & 0x01) { cycle(-1); cyc_cool = 10; }   /* Left  -> previous */
}

#endif
static void change_zoom(unsigned char increase)
{
    if (increase ? zoom == QUAD : zoom == HALF) return;
#ifndef GB_AGON
    if (!pic_pixels) {
        pic_pixels = (unsigned int)pic_wb * 4;
        if (pic_off == 14 && read_picture(6, 2)) {
            unsigned int w = (unsigned char)filebuf[0] | ((unsigned int)(unsigned char)filebuf[1] << 8);
            if (w && w <= pic_pixels && pic_pixels - w < 4) pic_pixels = w;
        }
    }
#endif
    if (increase) { ++zoom; scroll_x <<= 1; }
    else { --zoom; scroll_x >>= 1; }
    gb_curhide();
    if (!*WM_FS) {
        /* Erase the old picture and any scrollbars that just disappeared. */
        gb_fill(win_x + 1, TX_Y0, win_w - 2, win_h - 13, 0);
    }
    v_draw();
    gb_curshow();
}

/* Size the window on the first frame after File > Load; handle menus/keys. */
#ifndef GB_AGON
static void v_frame(void)
{
    unsigned char menu, c = gb_getkey();
    if ((c == '+' || c == '-') && have_pic()) {
        change_zoom(c == '+');
        return;
    }
    if (*WM_FS) { fs_nav(c); return; }           /* fullscreen: cycle images with Space / arrows */
    menu = gb_doc_frame_key(c);
    if (menu || !opened) {
        if (!opened && have_pic()) size_to_pic();
        opened = 1;
        gb_restore_parent();
    }
}

static void v_close(void)
{
    if (gb_doc_close()) {
        FS_XFLAGS = 0;
        if (banked) { select_banked_picture(); gb_pic_close(); }
        gb_wm_close();
    }
}
static void v_event(void) { gb_doc_event(); }

/* on_drag: a title-bar press - move the window (#146 slice 2). */
static void v_drag(void)
{
    unsigned char x = gb_wm_x(), y = gb_wm_y();
    if (gb_drag_window(&x, &y, gb_wm_w(), gb_wm_h())) {
        gb_wm_setpos(x, y);
        gb_restore_parent();
    }
}

#endif
/* A content press can operate a zoom button, scrollbar, or the resize grip. */
static void v_click(void)
{
    unsigned char w = gb_wm_w(); gb_scan_t h = gb_wm_h();
    win_x = gb_wm_x(); win_y = gb_wm_y(); win_w = w; win_h = h;   /* draw_pic/TX_Y0 read these */
    if (have_pic()) {
        if (*WM_FS) return;
        if ((gb_scan_t)(gb_my() - ZOOM_Y) < ZOOM_H) {
            unsigned char dx = gb_mx() - ZOOM_X;
            if (dx < ZOOM_W || (unsigned char)(dx - ZOOM_STEP) < ZOOM_W) {
                change_zoom(dx >= ZOOM_STEP);
                return;
            }
        }
        calc_view();
    }
    if (have_pic() && view_vsb && gb_mx() >= (unsigned char)(win_x + 1)
        && gb_mx() < (unsigned char)(win_x + 1 + SB_W)
        && gb_my() >= TX_Y0 && gb_my() < (gb_scan_t)(TX_Y0 + view_h)) {
        sb_drag();                                 /* drag the LEFT scrollbar (#166) */
        return;
    }
    if (have_pic() && view_hsb && hbar_w) {
        if (gb_mx() >= hbar_x && gb_mx() < (unsigned char)(hbar_x + hbar_w)
            && gb_my() >= hbar_y && gb_my() < (gb_scan_t)(hbar_y + HSB_H)) {
            hsb_drag();
            return;
        }
    }
#ifndef GB_AGON
    if (gb_in_grip(win_x, win_y, w, h, gb_mx(), gb_my()))
        if (gb_drag_resize(win_x, win_y, &w, &h, MIN_W, MIN_H)) {
            gb_wm_setsize(w, h);
            gb_restore_parent();
        }
#endif
}

/* View > Fullscreen (gb_doc) / the 'F' key: TRUE fullscreen. WM_FS (the shared low-RAM byte)
   tells the desktop to blank its top bar; we cover the WHOLE screen (so v_draw's blue fill
   paints over the WM's chrome) and centre the image. Off restores the exact prior window. */
#ifdef GB_AGON
static void v_proc(void)
{
    static const char *const menu[]={"Open","Close"};
    static const char *const pics[]={"PIC",0};
    char name[11];
    unsigned char k;
    switch(gb_msg.type) {
    case GB_MSG_DRAW: v_draw(); break;
    case GB_MSG_CLICK: v_click(); break;
    case GB_MSG_FRAME:
        k=gb_getkey();
        if(have_pic() && (k=='+' || k=='=' || k=='-')) change_zoom(k!='-');
        else agon_tick();
        break;
    case GB_MSG_MENU:
        k=gb_popup(10,8,menu,2);
        if(k==0 && gb_pickfile(name,pics)) {
            agon_open(); gb_restore_parent();
        }
        if(k!=1) break;
        /* fall through */
    case GB_MSG_CLOSE: agon_close(); gb_wm_close(); break;
    }
}
static const gb_mwin_t vmw={9,17,62,166,MIN_W,MIN_H,v_proc,vtitle};
void main(void) { gb_wm_managed(&vmw); agon_open(); loaded=1; }
#else
static void v_fullscreen(unsigned char on)
{
    static unsigned char px, py, pw, ph;
    if (on) {
        px = gb_wm_x(); py = gb_wm_y(); pw = gb_wm_w(); ph = gb_wm_h();
        gb_wm_setpos(0, 0); gb_wm_setsize(GB_COLS, GB_LINES); *WM_FS = 1;
    } else {
        *WM_FS = 0; gb_wm_setpos(px, py); gb_wm_setsize(pw, ph);
    }
    gb_wm_damage(0, 0, GB_COLS, GB_LINES); /* repaint the whole screen ONCE in the frame handler (the
                                      toggle changes the entire screen); v_draw does the paint */
}

/* on_open (File>Load): adopt the name, (re-)load via the bank-or-in-page path, re-arm resize. */
static void v_open(unsigned int len)
{
    (void)len;                        /* Viewer owns the banked/streamed load */
    my_drive = gb_get_drive();
    fmt83(vtitle, gb_doc_name());
    bank_or_parse();
    opened = 0;                       /* the next frame sizes to the new picture */
}

/* the window's single handler (#148): the kernel calls it for every message. */
static void v_proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  v_draw();  break;
        case GB_MSG_CLICK: v_click(); break;
        case GB_MSG_FRAME: v_frame(); break;
        case GB_MSG_CLOSE: v_close(); break;
        case GB_MSG_DRAG:  v_drag();  break;
        case GB_MSG_MENU:
        case GB_MSG_DROP:  v_event(); break;
    }
}

static const gb_mwin_t vmw = {            /* register SMALL (#206): main() then GROWS to the
                                            picture/reading size, so the first paint damages only
                                            that area instead of the near-fullscreen DEF rect. */
    DEF_X, DEF_Y, MIN_W, MIN_H, MIN_W, MIN_H, v_proc, vtitle
};
/* Image-only read-only doc: File offers only Load and filters for .PIC. Viewer
   owns loading because gb_doc's whole-file path cannot represent large images. */
static const char *const pic_exts[] = { "PIC", 0 };
static const gb_doc_t vdoc = {
    filebuf, VIEW_MAX, 0, v_open, 0, 0, 0, 0, 0, pic_exts, 0, 0,
    v_fullscreen, 0, 0, GB_DOC_OPEN_OWNS_LOAD
};

void main(void)
{
    gb_wm_managed(&vmw);             /* register + focus FIRST, but it does NOT draw yet (#146):
                                        now gb_fs_load/gb_doc target THIS window's launch file,
                                        and nothing is on screen while we do the slow load. The
                                        window is registered SMALL (MIN_W x MIN_H, #206) so the
                                        size below only ever GROWS it - a shrink from a big
                                        default would damage (and progressively repaint) almost
                                        the whole screen, the "weird redraw" on open. */
    my_drive = gb_get_drive();
    bank_or_parse();                 /* load the launch file: banked or demand-streamed .PIC */
    loaded = 1;
    if (have_pic()) {                /* size to the picture before the first paint (grows) */
        size_to_pic();
        opened = 1;                  /* already sized; v_frame's deferred resize is for File>Load */
    }
    else gb_wm_setsize(DEF_W, DEF_H);   /* invalid/unsupported file: readable alert body */
    gb_doc(&vdoc);                   /* menus + adopt the name for the title */
    fmt83(vtitle, gb_doc_name());
    gb_repaint_top();                /* FIRST paint: the new opaque z-top only. Lower windows are
                                        already valid and must not flash while the picture opens. */
}
#endif
