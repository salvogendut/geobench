/* desktop - the GEOBENCH desktop, in C (the last app to leave assembly).
 *
 * Boots into PAGE_APP0. Draws the backdrop (below the kernel's top bar) and the
 * Disk / Clock / Trash icons, lets you DRAG icons (hold fire, a red outline
 * follows, release to drop), and opens an icon on double-click: Disk -> the file
 * manager, Clock -> the C demo.
 *
 * Issue #45: the desktop no longer owns a for(;;) loop - it registers a window
 * (gb_wm_run) and the KERNEL drives the master loop, calling on_frame each frame.
 * It is the single, permanent root window; app windows layer on top of it.
 *
 * A press over an icon both arms a double-click and starts a drag; releasing
 * drops the icon at its new spot (no movement => it was just a click). The
 * backdrop is a solid colour, so the drag outline erases by redrawing in the
 * backdrop pen - no save-under needed. */
#include "gb.h"
#include "gbtitle.h"

#define IC_W    8             /* icon width  (byte cols) = 32 px */
#define IC_H    32            /* icon height (lines)             */
#define BOX_H   44            /* icon + label box (hit-test/lift) */
#define XMAX    (GB_COLS - IC_W)   /* drag clamps */
#define YMIN    9
#define YMAX    (GB_LINES - BOX_H)
#define DCLICK  75            /* double-click window, frames (gamepad-friendly, #153) */
#define NONE    0xFF
#define DRAGTH  2             /* press must move this far before it lifts (#153) */
#define WM_OPEN_STRICT (*(volatile unsigned char *)0x123D)

/* The desktop icons: three storage slots + Clock + Trash (#65). CPC/PCW retain
   the historical C/A/B slots; MSX resolves each slot to the letter and media
   type DOS assigned at boot. Positions are mutable (drag updates them). */
#define N_ICONS  5
#define IDX_C    0            /* Disk C = IDE */
#define IDX_A    1            /* Disk A = floppy A */
#define IDX_B    2            /* Disk B = floppy B */
#define IDX_CLOCK 3
#define IDX_TRASH 4
/* Settings has no desktop icon (#221): reach it from the top-bar System menu. */

#define ICON_DISK_C 13        /* shared card icon for current Disk C backends */
#define ICON_CF     16
#define ICON_IDE    17
#define IC_RCOL (GB_COLS - 14)  /* right icon column: 66 on the CPC, 114 on the MSX (#287) */
#define IC_BOTY (GB_LINES - 50) /* bottom row: label clears the border (150 CPC / 162 MSX) */
static unsigned char ic_x[N_ICONS]     = {  0,  0,  0, IC_RCOL, IC_RCOL };
static unsigned char ic_y[N_ICONS]     = { 35, 80, 125, 35, IC_BOTY };
static unsigned char ic_slot[N_ICONS] = { ICON_DISK_C, 0, 0, 1, 2 };  /* C, flp, flp, clock, trash */
static const char *ic_lbl[N_ICONS] = { "Disk C","Disk A","Disk B","Clock","Trash" };
#ifdef GB_MSX2
static char drive_lbl[3][7] = { "Disk A", "Disk B", "Disk C" };
#endif
static const unsigned char ic_drive[N_ICONS] = { 1, 1, 1, 0, 0 };      /* opens the file mgr */
static unsigned char ic_present[N_ICONS] = { 1, 0, 0, 1, 1 };          /* drives set by poll */

static unsigned char drag_active, drag_idx, out_x, out_y, grab_dx, grab_dy;
static unsigned char drag_armed, arm_mx, arm_my;   /* pressed on an icon, not yet lifted (#153) */
static unsigned char sel_idx = NONE;               /* selected icon (red frame), NONE = none (#153) */
static unsigned char desk_active;                  /* set by on_frame -> the desktop is focused (#153) */
static unsigned char dc_timer, dc_idx, held_prev;
static unsigned char show_ram;               /* System menu footprint toggle (#74) */
static unsigned char menu_inited;            /* gb_doc/System registered on the 1st frame (#142) */
static unsigned char menu_refresh;           /* refocus after a child window closes -> rebuild System */
static unsigned char want_settings;          /* System>Settings: open AFTER the menu repaint (#129) */
static unsigned char want_saver;             /* System>Activate screensaver: open after repaint (#219) */
static unsigned char want_about;             /* System>About: 1=menu selected, 2=open next frame (#409) */
#if !defined(GB_MSX2) && !defined(GB_PCW)
static unsigned char first_paint;             /* defer the definitive paint until WM registration */
#endif
#ifdef GB_PCW
static unsigned char want_timesync;          /* boot time helper enabled when TIMESYNC=true */
static unsigned int timesync_delay;          /* let real PerryNet hardware finish booting first */
static unsigned char timesync_tries;         /* one visible boot sync attempt */
#endif

#define DRIVE_TOP  20         /* drive icons stack down the left column, packed */
#define DRIVE_STEP 46         /* (BOX_H 44 + 2 gap), in detection order */

/* drive_poll: probe the drives, show an icon per present one and pack them down
   the left column in detection order (C, A, B) - no gaps for absent drives (#65). */
static void drive_poll(void)
{
    unsigned char d = gb_drives(), i, n = 0;
#ifdef GB_MSX2
    (void)d;
    for (i = 0; i < 3; i++) {
        unsigned char media;
        ic_present[i] = (unsigned char)(i < GB_MSX_DRIVE_COUNT);
        if (!ic_present[i]) continue;
        drive_lbl[i][5] = (char)gb_msx_drive_letter(i);
        ic_lbl[i] = drive_lbl[i];
        media = gb_msx_drive_media(i);
        if (media == GB_MSX_MEDIA_FLOPPY) ic_slot[i] = 0;
        else if (media == GB_MSX_MEDIA_SD) ic_slot[i] = ICON_DISK_C;
        else if (media == GB_MSX_MEDIA_IDE) ic_slot[i] = ICON_IDE;
        else ic_slot[i] = ICON_CF;
    }
#else
    ic_present[IDX_C] = (d & GB_DRV_C) ? 1 : 0;
    ic_present[IDX_A] = (d & GB_DRV_A) ? 1 : 0;
    ic_present[IDX_B] = (d & GB_DRV_B) ? 1 : 0;
    ic_slot[IDX_C] = ICON_DISK_C;
#endif
    for (i = 0; i < 3; i++)               /* the three drive icons are indices 0..2 */
        if (ic_present[i]) {
            ic_x[i] = 0;
            ic_y[i] = DRIVE_TOP + n * DRIVE_STEP;
            n++;
        }
}

static void draw_icon(unsigned char i)
{
    const char *l = ic_lbl[i];
    unsigned char n = 0;
    while (l[n]) n++;                            /* label length, in chars */
    gb_icon(ic_slot[i], ic_x[i], ic_y[i]);      /* pen-0 transparent: backdrop shows through (#128) */
    gb_fill(ic_x[i], (unsigned char)(ic_y[i] + 34),  /* a solid plate so the name stays readable */
            (unsigned char)((n * 3 + 1) / 2), 8, 0); /* over any backdrop pattern (n chars * 1.5 cols) */
    gb_text(ic_x[i], (unsigned char)(ic_y[i] + 34), l);
}

/* ---- centred .PIC wallpaper (#212, experiment) -------------------------------
 * If a wallpaper picture loads into a borrowed app bank, every backdrop-erase goes
 * through wp_backdrop: solid-fill the rect, then blit the overlapping part of the
 * centred picture from the bank (reusing the Viewer's gb_pic_open/blit services).
 * No bank (128K / missing file) -> plain gb_backdrop (tile or solid). */
#define PIC_PAGE_K (*(volatile unsigned char *)0x130B)
#define PIC_PAGE2_K (*(volatile unsigned char *)0x1348)
#ifdef GB_MSX2
#define PIC_PAGE3_K (*(volatile unsigned char *)0x1291)
#define PIC_PAGE4_K (*(volatile unsigned char *)0x1292)
#define PIC_MODE_K  (*(volatile unsigned char *)0x1293)
#define PIC_STRIDE_K (*(volatile unsigned int *)0x1294)
#endif
#define PIC_WB_K   (*(volatile unsigned char *)0x130C)
#define PIC_H_K    (*(volatile unsigned int  *)0x130D)
#define PIC_OFF_K  (*(volatile unsigned char *)0x130F)
#define CLIP_X_K   (*(volatile unsigned char *)0x1338)
#define CLIP_Y_K   (*(volatile unsigned char *)0x1339)
#define CLIP_W_K   (*(volatile unsigned char *)0x133A)
#define CLIP_H_K   (*(volatile unsigned char *)0x133B)
#define UI_OP_K    (*(volatile unsigned char *)0x1700)
#define UI_COL_K   (*(volatile unsigned char *)0x1701)
#define UI_LINE_K  (*(volatile unsigned char *)0x1702)
#define UI_MODAL_K (*(volatile unsigned char *)0x1705)
#define BD_NAME_K  ((const char *)0x1231)
#define BD_DRIVE_K (*(volatile unsigned char *)0x123C)
#define BD_SOLID_K (*(volatile unsigned char *)0x1290)
extern unsigned char gb_ui(void);
static unsigned char wp_bank;                 /* borrowed bank (0 = none) */
static unsigned char wp_bank2;                /* optional second wallpaper picture bank */
#ifdef GB_MSX2
static unsigned char wp_bank3, wp_bank4;       /* Screen-7 pictures can occupy four banks */
static unsigned char wp_mode;                  /* GBPC packing: 1=2bpp, 7=Screen-7 4bpp */
static unsigned int  wp_stride;                /* source bytes per picture row */
#endif
static unsigned char wp_wb, wp_x, wp_y;       /* picture width + centred top-left */
static unsigned int  wp_h;                    /* picture height; large .PIC files exceed 255 rows */
static unsigned int  wp_srcy;                 /* first picture row shown (centre a too-tall pic) */
static unsigned int  wp_off;
static unsigned char wp_drive;
static unsigned char wp_cfg_drive;
static unsigned char wp_cfg_valid;            /* last parsed WALLPAPER= was a usable PIC path */
static char wp_name[11];                      /* last parsed 8.3 wallpaper name */

/* The kernel keeps the raw GEOBENCH.CFG it loaded at boot here (for GB_RELOAD). We parse
   WALLPAPER= straight out of it - NOT via a fixed transfer cell: every free low-RAM cell
   collides (the dir scratch #12xx, the floppy cursor sector-overread #1500..#16FF, and the
   GBUI dialog block #1700 - that one broke the System menu). #1000 is the one buffer that
   stays put boot->desktop. */
#define KCFG_TEXT ((const char *)0x1000)
#define KCFG_LEN  (*(volatile unsigned int *)0x1200)

static unsigned char boot_drive(void)
{
#ifdef GB_MSX2
    return gb_boot_drive;
#else
    return (gb_drives() & GB_DRV_C) ? GB_DRIVE_C : GB_DRIVE_A;
#endif
}

static unsigned char drive_present(unsigned char d)
{
#ifdef GB_MSX2
    return (unsigned char)(d < GB_MSX_DRIVE_COUNT);
#else
    unsigned char m = gb_drives();
    if (d == GB_DRIVE_A) return (unsigned char)((m & GB_DRV_A) != 0);
    if (d == GB_DRIVE_B) return (unsigned char)((m & GB_DRV_B) != 0);
    return (unsigned char)((m & GB_DRV_C) != 0);
#endif
}

static unsigned char parse_drive(const char *t, unsigned int len, unsigned int *p)
{
    if (*p + 1 < len && t[*p + 1] == ':') {
#ifdef GB_MSX2
        unsigned char i, letter = (unsigned char)t[*p];
        for (i = 0; i < GB_MSX_DRIVE_COUNT; i++)
            if (gb_msx_drive_letter(i) == letter) { *p += 2; return i; }
#else
        if (t[*p] == 'A') { *p += 2; return GB_DRIVE_A; }
        if (t[*p] == 'B') { *p += 2; return GB_DRIVE_B; }
        if (t[*p] == 'C') { *p += 2; return GB_DRIVE_C; }
#endif
    }
    return boot_drive();
}

/* wp_cfg_name: find WALLPAPER=[D:]<stem>[.PIC] at a line start in the config text
   and build the 11-byte 8.3 "STEM    PIC" into out. Returns 1 for a usable name;
   0 if the key is absent or NONE. */
static unsigned char wp_cfg_name(char *out)
{
    const char *t = KCFG_TEXT;
    unsigned int len = KCFG_LEN, i, p;
    unsigned char j;
    for (i = 0; i + 10 <= len; i++) {
        if (i && t[i-1] != '\r' && t[i-1] != '\n') continue;   /* line start only */
        if (t[i]!='W'||t[i+1]!='A'||t[i+2]!='L'||t[i+3]!='L'||t[i+4]!='P'||
            t[i+5]!='A'||t[i+6]!='P'||t[i+7]!='E'||t[i+8]!='R'||t[i+9]!='=') continue;
        p = i + 10;
        wp_drive = parse_drive(t, len, &p);
        for (j = 0; j < 8; j++) out[j] = ' ';
        for (j = 0; j < 8 && p < len && t[p] != '\r' && t[p] != '\n' && t[p] != '.'; j++, p++)
            out[j] = t[p];
        out[8] = 'P'; out[9] = 'I'; out[10] = 'C';
        if (out[0] == ' ' ||                                    /* empty value */
            (out[0]=='N' && out[1]=='O' && out[2]=='N' && out[3]=='E' && out[4]==' '))
            return 0;                                           /* NONE -> no wallpaper */
        return 1;
    }
    return 0;                                                   /* absent */
}

static void wp_release(void)
{
    if (!wp_bank) return;
    PIC_PAGE_K = wp_bank;
    PIC_PAGE2_K = wp_bank2;
#ifdef GB_MSX2
    PIC_PAGE3_K = wp_bank3;
    PIC_PAGE4_K = wp_bank4;
    PIC_MODE_K = wp_mode;
    PIC_STRIDE_K = wp_stride;
#endif
    gb_pic_close();
    wp_bank = wp_bank2 = 0;
#ifdef GB_MSX2
    wp_bank3 = wp_bank4 = 0;
    wp_mode = 1;
    wp_stride = 0;
#endif
}

static unsigned char same11(const char *a, const char *b)
{
    unsigned char i;
    for (i = 0; i < 11; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static void copy11(char *dst, const char *src)
{
    unsigned char i;
    for (i = 0; i < 11; i++) dst[i] = src[i];
}

static unsigned char wp_changed(void)
{
    char nm[11];
    unsigned char valid = wp_cfg_name(nm);
    if (!valid) return wp_cfg_valid;
    if (!wp_cfg_valid) return 1;
    if (wp_drive != wp_cfg_drive) return 1;
    return (unsigned char)!same11(wp_name, nm);
}

/* enter_pics: descend into the root-level /PICS gallery if present, so gb_pic_open
   finds the wallpaper on card/MSX media. Flat CPC/PCW floppies have no directories
   and keep LOGO.PIC at root, so they fall through without descending. */
static unsigned char enter_pics(void)
{
    char *p = gb_dir1();
    while (p) {
        if (gb_isdir()) {
            const char *r = gb_entname();
            if (r[0]=='P' && r[1]=='I' && r[2]=='C' && r[3]=='S' && r[4]==' ') {
                gb_chdir();
                return 1;
            }
        }
        p = gb_dirn();
    }
    return 0;
}

/* System assets live in /GBENCH on hierarchical media and at the root of flat
   CPC/PCW floppies. */
static unsigned char enter_system(void)
{
    char *p = gb_dir1();
    while (p) {
        if (gb_isdir()) {
            const char *r = gb_entname();
            if (r[0]=='G' && r[1]=='B' && r[2]=='E' && r[3]=='N' &&
                r[4]=='C' && r[5]=='H' && r[6]==' ' && r[7]==' ') {
                gb_chdir();
                return 1;
            }
        }
        p = gb_dirn();
    }
    return 0;
}

#if !defined(GB_MSX2) && !defined(GB_PCW)
static unsigned char bd_tile[64];
static unsigned char bd_drive, bd_solid;
static char bd_name[11];

/* The CPC card keeps assets in /GBENCH; its flat AMSDOS floppy keeps them at
   root. This mirrors the kernel system-asset lookup without adding resident
   code to a kernel that is already at its guarded stack limit. */
/* A canonical BDP is already CPC Mode-1, so no transcode is necessary. Load
   the configured tile into the desktop bank at boot and after Settings closes. */
static void bd_init(void)
{
    unsigned char drive, old_drive, descended, i;
    unsigned int n;
    bd_solid = BD_SOLID_K;
    bd_drive = BD_DRIVE_K;
    copy11(bd_name, BD_NAME_K);
    if (bd_solid) return;
    drive = BD_DRIVE_K;
    if (drive == 0xFF) drive = boot_drive();
    bd_drive = drive;
    if (!drive_present(drive)) { BD_SOLID_K = bd_solid = 1; return; }
    old_drive = gb_get_drive();
    gb_set_drive(drive);
    descended = enter_system();
    gb_set_name(BD_NAME_K);
    n = gb_fs_load(gb_copybuf, 512);
    if (descended) gb_back();
    gb_set_drive(old_drive);
    if (n < 64) { BD_SOLID_K = bd_solid = 1; return; }
    for (i = 0; i < 64; i++)
        bd_tile[i] = (unsigned char)gb_copybuf[i];
}

static unsigned char bd_changed(void)
{
    unsigned char drive = BD_DRIVE_K;
    if (BD_SOLID_K != bd_solid) return 1;
    if (bd_solid) return 0;
    if (drive == 0xFF) drive = boot_drive();
    if (drive != bd_drive) return 1;
    return (unsigned char)!same11(bd_name, BD_NAME_K);
}

/* Draw a clipped canonical tile directly into CPC screen RAM. Writes still
   reach RAM while the upper ROM is visible, matching lib/screen.asm. */
static void cpc_backdrop(unsigned char x, unsigned char y,
                         unsigned char w, unsigned char h)
{
    unsigned char ex, ey, cx, cy, cr, cb, row, col;
    const unsigned char *tile;
    unsigned char *dst;
    unsigned int addr;

    if (bd_solid) { gb_fill(x, y, w, h, 0); return; }
    ex = (unsigned char)(x + w); ey = (unsigned char)(y + h);
    cx = CLIP_X_K; cy = CLIP_Y_K;
    cr = (unsigned char)(cx + CLIP_W_K); cb = (unsigned char)(cy + CLIP_H_K);
    if (x < cx) x = cx;
    if (y < cy) y = cy;
    if (ex > cr) ex = cr;
    if (ey > cb) ey = cb;
    if (ex <= x || ey <= y) return;

    for (row = y; row < ey; row++) {
        addr = 0xC000u + ((unsigned int)(row & 7) << 11)
             + (unsigned int)(row >> 3) * 80u + x;
        dst = (unsigned char *)addr;
        tile = bd_tile + (unsigned char)((row & 15) << 2);
        for (col = x; col < ex; col++)
            *dst++ = tile[col & 3];
    }
}
#endif

/* ---- screensaver idle trigger (#219) ----------------------------------------
 * Screensavers are apps (e.g. SQUARES.SAV) - and they are selectable. Two config keys:
 *   SAVER=<stem>        which .SAV module to run (default SQUARES)
 *   SAVERTIME=<minutes> the idle timeout (0 = the idle trigger is off)
 * bar_draw runs every frame regardless of focus (the global hook), so it counts idle
 * frames there and launches the module when the timeout elapses - reaching every
 * window, not just the bare desktop. */
static char ss_name[11];           /* the configured .SAV module, 8.3 ("SQUARES SAV") */
static unsigned char ss_drive;     /* drive to load the configured saver from */
static unsigned int  ss_timeout;   /* idle frames before the saver runs (0 = off) */
static unsigned int  ss_idle;      /* frames with no input so far */
static unsigned char ss_lmx, ss_lmy;  /* last pointer position (a change = activity) */

/* cfg_val: index just past KEY (klen chars, including '=') found at a line start in the
 * boot config text, or KCFG_LEN if absent. Reads straight from #1000 (no transfer cell
 * to collide), like wp_cfg_name. */
static unsigned int cfg_val(const char *key, unsigned char klen)
{
    const char *t = KCFG_TEXT;
    unsigned int len = KCFG_LEN, i;
    unsigned char j;
    for (i = 0; i + klen <= len; i++) {
        if (i && t[i-1] != '\r' && t[i-1] != '\n') continue;   /* line start only */
        for (j = 0; j < klen; j++) if (t[i+j] != key[j]) break;
        if (j == klen) return i + klen;
    }
    return len;
}

static char chrome_name[11];

/* Load one boot-drive chrome asset from /GBENCH into the shared copy buffer.
   This compact assembly helper keeps the second configurable asset inside the
   Desktop app's fixed code boundary. A=0 selects TITLEBAR/TBR, A=1 selects
   GADGETS/GDT; SDCC returns the loaded size in DE. */
static unsigned int chrome_load(unsigned char gadgets) __naked
{
    gadgets;
__asm
    push af                         ; retain the TBR/GDT selector
    or a
    jr z,cl_title_key
    ld a,#8
    push af
    inc sp
    ld hl,#cl_gadget_key
    call _cfg_val                  ; DE = value offset
    jr cl_have_pos
cl_title_key:
    ld a,#9
    push af
    inc sp
    ld hl,#cl_title_key_text
    call _cfg_val
cl_have_pos:
    push de
    ld hl,#cl_default
    ld de,#_chrome_name
    ld bc,#11
    ldir
    pop de
    ld hl,(#0x1200)                ; KCFG_LEN
    ld a,d
    cp h
    jr c,cl_copy_stem
    jr nz,cl_extension
    ld a,e
    cp l
    jr nc,cl_extension
cl_copy_stem:
    push de
    ld hl,#_chrome_name
    ld (hl),#0x20
    ld de,#_chrome_name+1
    ld bc,#7
    ldir
    pop hl
    ld de,#0x1000                  ; KCFG_TEXT
    add hl,de
    ld de,#_chrome_name
    ld b,#8
cl_stem_loop:
    ld a,(hl)
    cp #13
    jr z,cl_extension
    cp #10
    jr z,cl_extension
    cp #'.'
    jr z,cl_extension
    ld (de),a
    inc hl
    inc de
    djnz cl_stem_loop
cl_extension:
    pop af
    or a
    jr z,cl_load
    ld hl,#_chrome_name+8
    ld (hl),#'G'
    inc hl
    ld (hl),#'D'
    inc hl
    ld (hl),#'T'
cl_load:
    call _gb_get_drive
    push af
    call _boot_drive
    call _gb_set_drive
    ld b,#4
cl_root:
    push bc
    call _gb_back
    pop bc
    djnz cl_root
    call _enter_system
    push af
    ld hl,#_chrome_name
    call _gb_set_name
    ld de,#0x0200
    ld hl,#0x2200                  ; gb_copybuf
    call _gb_fs_load              ; DE = loaded size
    pop af
    or a
    jr z,cl_restore_drive
    push de
    call _gb_back
    pop de
cl_restore_drive:
    pop af
    push de
    call _gb_set_drive
    pop de
    ret
cl_default:
    .ascii "ORIGINALTBR"
cl_gadget_key:
    .ascii "GADGETS="
    .db 0
cl_title_key_text:
    .ascii "TITLEBAR="
    .db 0
__endasm;
}

/* Install the independently selected title motif and close/maximize gadgets.
   The paged module supplies ORIGINAL fallbacks when either file is absent. */
static void chrome_init(void)
{
    unsigned int n;
    n = chrome_load(0);
    gb_titlebar_init(n);
    n = chrome_load(1);
    gb_gadgets_install(n);
}

/* ss_cfg_init: read SAVER=<stem> (the module, default SQUARES) into ss_name and
 * SAVERTIME=<minutes> into ss_timeout (frames). */
static void ss_cfg_init(void)
{
    const char *t = KCFG_TEXT;
    unsigned int len = KCFG_LEN, p, mins = 0;
    unsigned char j;
    ss_drive = boot_drive();
    p = cfg_val("SAVER=", 6);                                  /* the .SAV module stem */
    if (p < len)
        ss_drive = parse_drive(t, len, &p);
    for (j = 0; j < 8; j++) ss_name[j] = ' ';
    for (j = 0; j < 8 && p < len && t[p] != '\r' && t[p] != '\n' && t[p] != '.'; j++, p++)
        ss_name[j] = t[p];
    if (ss_name[0] == ' ') {                                   /* absent/empty -> default SQUARES */
        ss_name[0]='S'; ss_name[1]='Q'; ss_name[2]='U';
        ss_name[3]='A'; ss_name[4]='R'; ss_name[5]='E'; ss_name[6]='S';
    }
    ss_name[8] = 'S'; ss_name[9] = 'A'; ss_name[10] = 'V';
    p = cfg_val("SAVERTIME=", 10);                             /* the idle timeout, minutes */
    if (p < len && t[p] >= '0' && t[p] <= '9') {
        mins = (unsigned int)(t[p++] - '0');                    /* first digit */
        if (p < len && t[p] >= '0' && t[p] <= '9')              /* second digit: enough for cap=21 */
            mins = (unsigned int)((mins << 3) + (mins << 1) + (unsigned int)(t[p] - '0'));
    }
    if (mins > 21) mins = 21;                                  /* cap: 21 min*3000 fits a word */
    ss_timeout = mins * 3000;                                  /* minutes -> frames (60s * 50 Hz) */
}

static void wp_init(void)
{
    char nm[11];
    unsigned char descended, old_drive;
    wp_drive = boot_drive();
    if (!wp_cfg_name(nm)) {                     /* WALLPAPER= absent or NONE */
        wp_release();
        wp_cfg_valid = 0;
        return;
    }
    if (wp_cfg_valid && wp_cfg_drive == wp_drive && same11(wp_name, nm))
        return;                                 /* config unchanged -> keep current bank */
    wp_release();
    copy11(wp_name, nm);
    wp_cfg_drive = wp_drive;
    wp_cfg_valid = 1;
    if (!drive_present(wp_drive)) return;       /* bad qualified drive -> NONE fallback */
    old_drive = gb_get_drive();
    gb_set_drive(wp_drive);
    descended = enter_pics();                    /* card/MSX: /PICS; floppy: root */
    gb_set_name(nm);                             /* the focused (desktop) window's file arg */
    wp_bank = gb_pic_open();                   /* borrow a bank + parse the GBPC header */
    UI_MODAL_K = 0;                              /* picture loaders reuse #1700; keep menus live */
    wp_bank2 = 0;
#ifdef GB_MSX2
    wp_bank3 = wp_bank4 = 0;
    wp_mode = 1;
    wp_stride = 0;
#endif
    if (wp_bank) {
        wp_bank2 = PIC_PAGE2_K;
#ifdef GB_MSX2
        wp_bank3 = PIC_PAGE3_K;
        wp_bank4 = PIC_PAGE4_K;
        wp_mode = PIC_MODE_K;
        wp_stride = PIC_STRIDE_K;
#endif
        wp_wb = PIC_WB_K; wp_h = PIC_H_K; wp_off = PIC_OFF_K;
    }
    if (descended) gb_back();                    /* /PICS -> root (depth-1, safe) */
    gb_set_drive(old_drive);
    if (wp_bank) {
        wp_x = (wp_wb < GB_COLS) ? (unsigned char)((GB_COLS - wp_wb) / 2) : 0;
        if (wp_h <= GB_LINES - 8) {             /* fits the desktop area below the bar: centre it */
            wp_y = (unsigned char)(8 + ((GB_LINES - 8) - wp_h) / 2);
            wp_srcy = 0;
        } else {                                /* taller than the area: pin top, show the middle */
            wp_y = 8;
            wp_srcy = (wp_h - (GB_LINES - 8)) / 2;
        }
    } else {
        wp_wb = 0; wp_h = 0; wp_x = 0; wp_y = 0; wp_srcy = 0; wp_off = 0; wp_bank2 = 0;
#ifdef GB_MSX2
        wp_bank3 = wp_bank4 = 0; wp_mode = 1; wp_stride = 0;
#endif
    }
}

static unsigned char background_changed(void)
{
#if !defined(GB_MSX2) && !defined(GB_PCW)
    if (bd_changed()) return 1;
#endif
    return wp_changed();
}

static void background_init(void)
{
#if !defined(GB_MSX2) && !defined(GB_PCW)
    if (bd_changed()) bd_init();
#endif
    if (wp_changed()) wp_init();
}

static void open_saver(void)
{
    unsigned char old_drive = gb_get_drive();
    unsigned char drv = ss_drive;
    const char *name = ss_name;
    if (!drive_present(drv)) {                  /* unavailable configured drive -> safe fallback */
        drv = boot_drive();
        name = "SQUARES SAV";
    }
    gb_set_drive(drv);
    WM_OPEN_STRICT = 1;
    gb_wm_open(name);
    gb_set_drive(old_drive);
}

static void wp_backdrop(unsigned char x, unsigned char y, unsigned char w, unsigned char h)
{
    unsigned char ix, iy, rx, by, iw, ih, ex, ey, px2, py2, cx, cy, cr, cb, row;
    unsigned int src;
    if (!wp_bank) {
#if !defined(GB_MSX2) && !defined(GB_PCW)
        cpc_backdrop(x, y, w, h);
#else
        gb_backdrop(x, y, w, h);
#endif
        return;
    }
    ex = (unsigned char)(x + w); ey = (unsigned char)(y + h);
    cx = CLIP_X_K; cy = CLIP_Y_K;
    cr = (unsigned char)(cx + CLIP_W_K); cb = (unsigned char)(cy + CLIP_H_K);
    if (x < cx) x = cx;
    if (y < cy) y = cy;
    if (ex > cr) ex = cr;
    if (ey > cb) ey = cb;
    if (ex <= x || ey <= y) return;
    w = (unsigned char)(ex - x); h = (unsigned char)(ey - y);
    px2 = (unsigned char)(wp_x + wp_wb);
    py2 = (wp_h > (unsigned int)(GB_LINES - wp_y)) ? GB_LINES
                                                    : (unsigned char)(wp_y + (unsigned char)wp_h);
    ix = (x > wp_x) ? x : wp_x;                 /* rect INTERSECT picture rect */
    rx = (ex < px2) ? ex : px2;
    iy = (y > wp_y) ? y : wp_y;
    by = (ey < py2) ? ey : py2;
    if (rx <= ix || by <= iy) {                 /* no picture overlap -> solid margin */
        gb_fill(x, y, w, h, 0);
        return;
    }
    /* Do not clear through the wallpaper before re-blitting it: full desktop
       repaints would visibly blank the logo for several frames. Fill only the
       margins that are outside the centred picture. */
    if (iy > y)  gb_fill(x, y, w, (unsigned char)(iy - y), 0);
    if (by < ey) gb_fill(x, by, w, (unsigned char)(ey - by), 0);
    if (ix > x)  gb_fill(x, iy, (unsigned char)(ix - x), (unsigned char)(by - iy), 0);
    if (rx < ex) gb_fill(rx, iy, (unsigned char)(ex - rx), (unsigned char)(by - iy), 0);
    iw = (unsigned char)(rx - ix); ih = (unsigned char)(by - iy);
#ifdef GB_MSX2
    src = wp_off + (unsigned int)(iy - wp_y + wp_srcy) * wp_stride
        + (wp_mode == 7 ? (unsigned int)(ix - wp_x) * 2U
                        : (unsigned char)(ix - wp_x));
#else
    src = wp_off + (unsigned int)(iy - wp_y + wp_srcy) * wp_wb
        + (unsigned char)(ix - wp_x);
#endif
    PIC_PAGE_K = wp_bank;                       /* re-assert our bank (the Viewer shares PIC_PAGE) */
    PIC_PAGE2_K = wp_bank2;
#ifdef GB_MSX2
    PIC_PAGE3_K = wp_bank3;
    PIC_PAGE4_K = wp_bank4;
    PIC_MODE_K = wp_mode;
    PIC_STRIDE_K = wp_stride;
#endif
    if ((iw == wp_wb && !wp_bank2) || ih == 1) {
        gb_pic_blit(ix, iy, iw, ih, src);
    } else {
        for (row = 0; row < ih; row++) {        /* raw PICBLIT has no source stride argument */
            gb_pic_blit(ix, (unsigned char)(iy + row), iw, 1, src);
#ifdef GB_MSX2
            src += wp_stride;
#else
            src += wp_wb;
#endif
        }
    }
}

static void paint(void)
{
    unsigned char i;
    ss_cfg_init();                             /* #219: re-read SAVER=/SAVERTIME= - a Settings change
                                                  applies live (this fires when Settings closes) */
    wp_backdrop(0, 8, GB_COLS, GB_LINES - 8);                /* backdrop: wallpaper if loaded, else tile/solid (#128) */
    for (i = 0; i < N_ICONS; i++)
        if (ic_present[i]) draw_icon(i);
    if (sel_idx != NONE && ic_present[sel_idx])    /* red selection frame (#153) */
        gb_frame(ic_x[sel_idx], ic_y[sel_idx], IC_W, IC_H, 3);
    /* #153: do NOT show the cursor here. As the WM's bottom on_repaint, paint()
       runs FIRST in wm_repaint_all - any window drawn on top would overwrite the
       pointer while its save-under still held the backdrop, so a later move left a
       blue hole. The repaint bracket now draws the pointer once, LAST. Standalone
       callers (System menu, boot) gb_curshow themselves. */
}

/* select_icon: move the red selection frame to icon (NONE clears it), erasing the
   previous one in place so we never leave a stray frame behind (#153). */
static void select_icon(unsigned char icon)
{
    if (sel_idx == icon) return;
    gb_curhide();
    if (sel_idx != NONE && ic_present[sel_idx]) {
        wp_backdrop(ic_x[sel_idx], ic_y[sel_idx], IC_W, IC_H);   /* erase old frame to backdrop (#128) */
        draw_icon(sel_idx);                                       /* restore the glyph edge */
    }
    if (icon != NONE)
        gb_frame(ic_x[icon], ic_y[icon], IC_W, IC_H, 3);
    gb_curshow();
    sel_idx = icon;
}

/* The top bar is now drawn here, not in the kernel (experiment #77). The WM runs
   bar_draw() every frame in our page regardless of focus, so the clock + the focused
   window's menu titles stay live behind any window. Nothing else touches lines 0-7
   (windows start at line 8), so each element is drawn once and only repainted when it
   changes (clock minute, menu def). Kernel state we read: KCFG_MEMSTR (RAM size, set
   by the GBCFG module) and MENU_DEF (the focused window's menu, kept by the WM). */
#define KCFG_MEMSTR ((const char *)0x121A)
#define MENU_DEF    ((volatile unsigned char *)0x1310)
#define WM_FS       ((volatile unsigned char *)0x130A)   /* 1 = a window is fullscreen (kernel) */
#define CLK_COL     (GB_COLS - 12)  /* clock column (matches the old kernel bar) */

static unsigned char bar_init, bar_hour, bar_min, bar_msig, bar_wasfs;

static unsigned char bin(unsigned char v)   /* raw RTC reg -> binary (gb_time) */
{
    return gb_binmode ? v : (unsigned char)((v >> 4) * 10 + (v & 15));
}
static void put2(char *p, unsigned char v) { p[0] = '0' + v / 10; p[1] = '0' + v % 10; }

/* bar_menu: the focused window's menu titles (MENU_DEF: count, then {col, 8-byte
   label}*count). Clear the title region first so a previous app's titles are gone. */
static void bar_menu(void)
{
    unsigned char n = MENU_DEF[0], i, j;
    char lbl[9];
    gb_curhide();
    gb_fill(8, 0, 46, 8, 1);                  /* white, cols 8..53 (RAM/footprint/clock kept) */
    for (i = 0; i < n && i < 4; i++) {
        for (j = 0; j < 8; j++) lbl[j] = MENU_DEF[2 + i * 9 + j];
        lbl[8] = 0;
        gb_textbw(MENU_DEF[1 + i * 9], 0, lbl);
    }
    gb_curshow();
}

static void bar_clock(unsigned char h, unsigned char m)
{
    char t[6];
    put2(t, bin(h)); t[2] = ':'; put2(t + 3, bin(m)); t[5] = 0;
    gb_curhide(); gb_textbw(CLK_COL, 0, t); gb_curshow();
}

static void bar_draw(void)
{
    unsigned char msig, i;
    if (*WM_FS) { bar_wasfs = 1; return; }    /* fullscreen: the borderless window owns lines 0-7 */
    if (bar_wasfs) {                          /* just exited fullscreen (e.g. the saver closed) -> */
        bar_wasfs = 0; bar_init = 0;          /* force a full bar redraw, and restart the idle count */
        ss_idle = 0; ss_lmx = gb_mx(); ss_lmy = gb_my();
    }
    if (!bar_init) {                          /* first frame: white strip + RAM size */
        gb_curhide();
        gb_fill(0, 0, GB_COLS, 8, 1);
        gb_textbw(1, 0, KCFG_MEMSTR);
        gb_curshow();
        bar_init = 1; bar_hour = 0xFF; bar_min = 0xFF; bar_msig = 0xFF;
    }
    msig = 0;                                  /* menu titles: redraw when MENU_DEF changes */
    for (i = 0; i < (unsigned char)(MENU_DEF[0] * 9 + 1) && i < 40; i++) msig += MENU_DEF[i];
    if (msig != bar_msig) { bar_msig = msig; bar_menu(); }
    gb_time();                                 /* clock: redraw when the displayed time changes */
    if (gb_hour != bar_hour || gb_min != bar_min) {
        bar_hour = gb_hour;
        bar_min = gb_min;
        bar_clock(gb_hour, gb_min);
    }
    /* The desktop loses focus when a window is clicked, and its on_frame stops
       running - but this bar hook runs every frame in the desktop's page regardless
       of focus. on_frame (above) sets desk_active when the desktop is focused; if it
       did NOT run this frame, a window has focus, so clear a left-over selection
       (reset the clip first so the erase isn't clipped to some window's rect, #153). */
    if (!desk_active && sel_idx != NONE) {
        gb_wm_damage(0, 0, GB_COLS, GB_LINES);
        select_icon(NONE);
    }
    if (!desk_active)
        menu_refresh = 1;                     /* another window had focus this frame */
    desk_active = 0;                            /* consume the flag; on_frame re-sets it if focused */

    /* Screensaver idle trigger (#219): any pointer move / click / fire / ESC counts as
       activity and resets the count; otherwise, once the timeout elapses and a bank is
       free, launch the saver app. It sets WM_FS, so next frame we bail at the top above
       (no re-launch) until it closes. (A typed key isn't counted here - reading it would
       steal it from the focused app - but it DOES wake the saver, which owns the input.) */
    if (ss_timeout) {
        unsigned char mx = gb_mx(), my = gb_my();
        if (mx != ss_lmx || my != ss_lmy || (gb_flags() & (GB_CLICK | GB_FIRE | GB_QUIT))) {
            ss_lmx = mx; ss_lmy = my; ss_idle = 0;
        } else if (++ss_idle >= ss_timeout && !gb_wm_full()) {
            ss_idle = 0;
            open_saver();
        }
    }
}

/* Desktop-owned dialogs and menus may run while child windows remain open.
 * Recompose the managed stack instead of painting the desktop directly over
 * those windows; a full damage rect also clears any popup-sized clip. */
static void repaint_stack(void)
{
    gb_wm_damage(0, 0, GB_COLS, GB_LINES);
    gb_restore_parent();
    bar_init = 0;
}

static unsigned char hit_icon(unsigned char mx, unsigned char my)
{
    unsigned char i;
    for (i = 0; i < N_ICONS; i++)
        if (ic_present[i] &&
            mx >= ic_x[i] && mx < ic_x[i] + IC_W &&
            my >= ic_y[i] && my < ic_y[i] + BOX_H)
            return i;
    return NONE;
}

/* dragstart: lift the icon (paint its box in the backdrop) and show a red
   outline at its position; remember where in the icon we grabbed it. */
static void dragstart(unsigned char idx, unsigned char mx, unsigned char my)
{
    drag_idx = idx;
    out_x = ic_x[idx];
    out_y = ic_y[idx];
    grab_dx = mx - out_x;
    grab_dy = my - out_y;
    drag_active = 1;
    gb_curhide();
    wp_backdrop(out_x, out_y, IC_W + 2, BOX_H); /* erase icon + label to the backdrop (#128) */
    gb_frame(out_x, out_y, IC_W, IC_H, 3);     /* red outline */
    gb_curshow();
}

static void dragmove(unsigned char mx, unsigned char my)
{
    unsigned char nx, ny;
    nx = (mx >= grab_dx) ? (unsigned char)(mx - grab_dx) : 0;
    if (nx > XMAX) nx = XMAX;
    ny = (my >= grab_dy) ? (unsigned char)(my - grab_dy) : 0;
    if (ny < YMIN) ny = YMIN;
    if (ny > YMAX) ny = YMAX;
    if (nx == out_x && ny == out_y) return;    /* nothing moved */
    gb_curhide();
    wp_backdrop(out_x, out_y, IC_W, IC_H);     /* erase old outline to the backdrop (#128) */
    out_x = nx;
    out_y = ny;
    gb_frame(out_x, out_y, IC_W, IC_H, 3);     /* draw new outline */
    gb_curshow();
}

static void drop(void)
{
    ic_x[drag_idx] = out_x;                     /* commit the new position */
    ic_y[drag_idx] = out_y;
    drag_active = 0;
    gb_curhide();
    gb_restore_parent();                        /* repaint desktop + restack any windows
                                                   on top, so they stay (one layer up) and
                                                   aren't erased by our backdrop fill (#65) */
}

/* The "System" menu now rides the shared gb_doc menu system (#142): the desktop
   registers an empty document (no File/Edit/View) and adds one "System" title with
   gb_menu_add, so its dropdown renders with the same framed/reverse-video hover look
   as every app's menus - one menu path for the whole UI. */
#define FP_COL    (GB_COLS - 26) /* Ram-Usage footprint column - left of the clock */
static const gb_doc_t deskdoc = { 0 };        /* no document -> gb_doc adds no titles */

/* draw_footprint: the resident kernel size on the top bar, left of the clock, as
   "<n>K used", black-on-white like the bar. */
static char fp[12];
static void draw_footprint(void)
{
    unsigned int k = (gb_ksize + 512) / 1024;  /* bytes -> KB, rounded */
    unsigned char n = 0, i = 0;
    char tmp[4];
    if (!k) tmp[n++] = '0';
    while (k) { tmp[n++] = '0' + k % 10; k /= 10; }
    while (n) fp[i++] = tmp[--n];
    fp[i++] = 'K'; fp[i++] = ' ';
    fp[i++] = 'u'; fp[i++] = 's'; fp[i++] = 'e'; fp[i++] = 'd'; fp[i] = 0;
    gb_textbw(FP_COL, 0, fp);
}

/* tidy_icons: snap every icon back to its boot position - drives packed down the
   left column (detection order), Clock/Trash on the right - then repaint (#74). */
static void tidy_icons(void)
{
    unsigned char i, n = 0;
    for (i = 0; i < 3; i++)
        if (ic_present[i]) { ic_x[i] = 0; ic_y[i] = DRIVE_TOP + n * DRIVE_STEP; n++; }
    ic_x[IDX_CLOCK] = IC_RCOL; ic_y[IDX_CLOCK] = 35; /* the boot positions (see ic_x/ic_y) */
    ic_x[IDX_TRASH] = IC_RCOL; ic_y[IDX_TRASH] = IC_BOTY;
    gb_curhide();
    gb_restore_parent();
}

/* sys_action: the System menu handler, dispatched by the gb_doc framework (#142). */
static const char *const sys_items[7] = {
    "Ram Usage", "Refresh Media", "Tidy Icons", "Settings", "Activate screensaver",
    "About GEOBENCH", "Exit to DOS"
};
static void sys_action(unsigned char sel)
{
    if (sel == 0) {                            /* Ram Usage: toggle the footprint (it then
                                                  persists - nothing else touches the bar) */
        show_ram ^= 1;
        gb_curhide();
        gb_wm_damage(0, 0, GB_COLS, GB_LINES);           /* the footprint lives in the bar, outside the
                                                  System dropdown's damage clip - widen first
                                                  or the draw is clipped away (#153) */
        if (show_ram) draw_footprint();
        else gb_fill(FP_COL, 0, 13, 8, 1);
        gb_curshow();
    } else if (sel == 1) {                     /* Refresh Media (the old "Media") */
        gb_curhide();
        drive_poll();
        gb_restore_parent();
    } else if (sel == 2) {                     /* Tidy Icons */
        tidy_icons();
    } else if (sel == 3) {                     /* Settings (#129): the control panel. Defer the
                                                  open until AFTER gb_doc_frame's desktop repaint
                                                  below, else paint() covers the new window. */
        want_settings = 1;
    } else if (sel == 4) {                     /* Activate screensaver (#219): run it on demand,
                                                  whatever the SAVER= idle timeout - defer the open
                                                  past gb_doc_frame's repaint, like Settings. */
        want_saver = 1;
    } else if (sel == 5) {                     /* About GEOBENCH (#409): defer until the System
                                                  popup has released the click and repainted. */
        want_about = 1;
    } else if (sel == 6) {                     /* Exit to DOS */
        gb_exit();                              /* does not return */
    }
}

/* on_event: kernel callback (issue #32). Fires when the user clicks the
   kernel-owned top bar; proves the kernel->app round-trip by showing the
   message payload (the clicked column) in the hint line. */
/* trash_label: "Trash: NAME.EXT" from the dragged 11-byte 8.3 name (#62 phase 1). */
static char *trash_label(void)
{
    static char m[20] = "Trash: ";
    const char *e = gb_dragname;
    unsigned char i, j = 7;
    for (i = 0; i < 8 && e[i] != ' '; i++) m[j++] = e[i];
    if (e[8] != ' ') { m[j++] = '.'; for (i = 8; i < 11 && e[i] != ' '; i++) m[j++] = e[i]; }
    m[j] = 0;
    return m;
}

static void on_event(void)
{
    if (gb_msg.type == GB_MSG_DROP) {                  /* a file dropped on the desktop (#62) */
        if (hit_icon(gb_mx(), gb_my()) == IDX_TRASH) { /* on Trash -> delete the file */
            unsigned char ok;
            gb_copy_begin();                           /* delete from the drag source drive/dir */
            ok = gb_file_delete(gb_dragname);
            gb_copy_end();
            gb_curhide();
            if (ok) gb_text(1, 10, trash_label());     /* source window re-lists after the drop */
            else    gb_alert("Delete failed", "file was not removed");
            gb_curshow();
        }
        return;
    }
    gb_doc_event();                                    /* a top-bar title click -> the framework */
}

/* on_frame: one frame of the desktop, called by the kernel's window-manager loop
   (issue #45). The kernel polls before calling, so read input with gb_flags/mx/my
   - never gb_poll here. What used to be the body of a for(;;) loop, with each
   'continue' now a 'return' (end this frame). */
static void on_frame(void)
{
    unsigned char flags = gb_flags(), mx = gb_mx(), my = gb_my(), held, icon;

    desk_active = 1;                              /* the desktop is focused this frame (#153) */
    if (!menu_inited || menu_refresh) {          /* 1st frame OR refocus after a child window closed:
                                                    rebuild the desktop menu state so stale focus/menu
                                                    pointers cannot leave System unclickable. */
        menu_inited = 1;
        gb_doc(&deskdoc);                        /* empty doc: no File/Edit/View */
        gb_menu_add("System", sys_items, 7, sys_action);
    }
#if !defined(GB_MSX2) && !defined(GB_PCW)
    if (first_paint) {
        /* The pre-WM paint left the CPC splash beneath the desktop until a
           later restack. Use the normal registered compositor from frame one. */
        first_paint = 0;
        gb_wm_damage(0, 0, GB_COLS, GB_LINES);
        gb_restore_parent();
        return;
    }
#endif
    if (menu_refresh && background_changed()) { /* backdrop/wallpaper changed while a child was up:
                                                    reload outside wm_repaint_all, then repaint once. */
        background_init();
        gb_doc(&deskdoc);                        /* keep the desktop menu definition fresh after the
                                                    reload before we repaint the desktop. */
        gb_menu_add("System", sys_items, 7, sys_action);
        repaint_stack();
        menu_refresh = 0;
        return;
    }
    menu_refresh = 0;
    if (dc_timer) dc_timer--;
    /* the desktop is the permanent root - ESC doesn't exit GEOBENCH (use System >
       Exit to DOS to leave); ESC only closes apps launched on top of it */

#ifdef GB_PCW
    if (timesync_tries) {
        if (timesync_delay) timesync_delay--;
        else if (!gb_wm_full()) {
            timesync_tries--;
            gb_wm_open("TIMESYNCAPP");
            if (timesync_tries) timesync_delay = 750;
            return;
        }
    }
#endif

    if (want_about == 2) {                /* The GBUI module call that drew the System menu must
                                             unwind through the kernel before GBUI can be loaded
                                             again. Open on the following desktop frame. */
        want_about = 0;
        UI_OP_K = 24;
        UI_COL_K = (GB_COLS - 60) / 2;
        UI_LINE_K = (GB_LINES - 62) / 2;
        gb_ui();
        repaint_stack();                  /* erase the transient box and restore every window */
        return;
    }

    if (gb_doc_frame()) {                  /* a System menu opened/ran (#142) */
        if (want_about) {                  /* Return to the kernel before re-entering GBUI. The menu
                                              restored its save-under, so no interim repaint is needed. */
            want_about = 2;
            return;
        }
        repaint_stack();                   /* restore existing windows and widen the popup clip */
        if (want_settings) {                  /* System>Settings: now safe to open on top (#129) */
            want_settings = 0;
            if (gb_wm_full()) gb_alert("Sorry, not enough RAM", "to run more apps.");
            else gb_wm_open("SETTINGSAPP");
        }
        if (want_saver) {                     /* System>Screensaver: launch the saver now (#219) */
            want_saver = 0;
            ss_idle = 0;                       /* a manual run resets the idle count */
            if (!gb_wm_full()) open_saver();
        }
        return;
    }

    held = flags & GB_FIRE;
    if (held_prev && !held) {              /* fire released */
        if (drag_active) drop();           /* was dragging -> drop at the new spot */
        drag_armed = 0;                    /* armed but never moved -> just a click, icon untouched */
        held_prev = 0;
        return;
    }
    held_prev = held;

    if (drag_active) {                     /* follow the pointer */
        dragmove(mx, my);
        return;
    }

    if (drag_armed) {                      /* pressed on an icon: lift only once it really moves (#153) */
        unsigned char dx = (mx > arm_mx) ? (unsigned char)(mx - arm_mx) : (unsigned char)(arm_mx - mx);
        unsigned char dy = (my > arm_my) ? (unsigned char)(my - arm_my) : (unsigned char)(arm_my - my);
        if (dx >= DRAGTH || dy >= DRAGTH) { drag_armed = 0; dragstart(dc_idx, mx, my); }
        return;
    }

    if (!(flags & GB_CLICK)) return;       /* a fresh press? */
    icon = hit_icon(mx, my);
    if (icon == NONE) { select_icon(NONE); return; }   /* click empty space -> deselect (#153) */

    if (dc_timer && dc_idx == icon) {      /* second click -> open */
        unsigned char opens = ic_drive[icon] || icon == IDX_CLOCK;
        if (opens && gb_wm_full())                           /* no free bank -> say so (#153) */
            gb_alert("Sorry, not enough RAM", "to run more apps.");
        else if (ic_drive[icon]) {                           /* browse that drive (#65): */
            select_icon(NONE);                               /* opening clears the selection (#153) */
            gb_set_drive(icon);                              /* icon idx 0/1/2 = drive slot */
            gb_wm_open("FILEMGR APP");
        }
        else if (icon == IDX_CLOCK) { select_icon(NONE); gb_wm_open("CLOCK   APP"); } /* clock (#72) */
        dc_timer = 0;
        held_prev = 0;
    } else {                               /* first click: select + arm; the lift waits for movement (#153) */
        select_icon(icon);
        dc_idx = icon;
        dc_timer = DCLICK;
        drag_armed = 1;
        arm_mx = mx;
        arm_my = my;
    }
}

/* the desktop is the root window: full screen below the top bar, on_repaint = the
   full paint() (restacked behind any window), on_event = file-drop + the System menu.
   menu = 0: the gb_doc framework installs the "System" title dynamically (#142). Its
   rect spans the screen so it is the bottom catch-all for click-to-focus. */
static const gb_win_t deskwin = { 0, 8, GB_COLS, GB_LINES - 8, on_frame, paint, on_event, 0 };

void main(void)
{
#ifdef GB_PCW
    unsigned int p;
#endif
    *WM_FS = 0;                                 /* clear the fullscreen flag at boot (low RAM is
                                                   uninitialised; bar_draw reads it every frame) */
    WM_OPEN_STRICT = 0;
    chrome_init();                              /* install configured .TBR/.GDT before windows */
    drive_poll();                               /* drives present at boot -> icons (#65) */
#ifdef GB_PCW
    p = cfg_val("TIMESYNC=", 9);
    want_timesync = (unsigned char)(p < KCFG_LEN && KCFG_TEXT[p] == 't');
    timesync_delay = 250;                      /* start visibly, then let TIMESYNC wait for PerryNet */
    timesync_tries = want_timesync ? 1 : 0;
#endif
    ss_cfg_init();                               /* #219: read the screensaver idle timeout */
#if !defined(GB_MSX2) && !defined(GB_PCW)
    bd_init();                                   /* canonical BDP is native CPC Mode-1 */
#endif
    wp_init();                                   /* #212: load the configured wallpaper */
#if !defined(GB_MSX2) && !defined(GB_PCW)
    first_paint = 1;                            /* paint from the first registered WM frame */
#else
    gb_wm_damage(0, 0, GB_COLS, GB_LINES);
    paint();
    gb_curshow();
#endif
    drag_active = 0;
    dc_timer = 0;
    held_prev = 0;
    gb_on_bar(bar_draw);                        /* top-bar handler runs every frame (#77) */
    gb_wm_run(&deskwin);                        /* register + run the kernel WM (#45) */
}
