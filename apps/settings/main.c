/* settings - the GEOBENCH control panel (#129).
 *
 * The single, discoverable place to personalise GEOBENCH. It edits GEOBENCH.CFG (the
 * same key=value file the kernel reads at boot and the GBCFG module parses) and saves
 * it, so the choice persists. Phase 1 covers the three appearance keys the kernel
 * already applies at boot:
 *     FONT=<stem>    a .FNT font set      (kernel font_init)
 *     ICONS=<stem>   a .IST icon set      (kernel icon_init)
 *     CURSOR=<stem>  a .SPR pointer sprite(kernel cursor_init)
 *     TITLEBAR=<stem> a .TBR repeated window-title motif
 *     GADGETS=<stem>  a .GDT close/maximize gadget pair
 *     BACKDROP=[D:]<stem>[.BDP]
 *     WALLPAPER=[D:]<stem>[.PIC]
 *     SAVER=[D:]<stem>[.SAV]
 *     MSXMODE=6|7       selected MSX video mode at the next boot (shown as
 *                       "4 colors" / "16 colors")
 *     MSXMOUSE=TRUE|FALSE selects a mouse or joystick in MSX port 1 at the
 *                         next boot
 *     PERRYNET_BAUD=9600|17857|41667 selects the PCW PerryNet serial profile
 *     TIMESYNC=true|false enables PCW PerryNet clock sync at the next boot
 * Each row shows the current value; clicking it lists the matching files in the
 * /GBENCH system folder (or root-level /PICS for wallpapers) and offers them in
 * a popup. The kernel loads font/icons/cursor only at boot, so a change takes
 * effect on the next boot (noted in the window).
 *
 * Designed to grow (#129): the `rows` table is data-driven, so later settings - desktop
 * colours (INKS=, needs a kernel palette key), a backdrop pattern and a screensaver
 * (both need #128) - drop in as more rows once their kernel support exists.
 *
 * Directory care: app file I/O (gb_fs_load/save) targets the current browse drive+dir,
 * while the system files live in /GBENCH on the BOOT drive. So we (a) pin the drive to
 * the boot drive (card if present, else floppy A - mirrors the kernel's fs_init) before
 * every FS op, and (b) only ever descend one level (root -> GBENCH -> root), which is
 * the safe depth-1 navigation case.
 */
#include "gb.h"
#include "gbcfg.h"
#include "gbsavercfg.h"
#include "gbtitle.h"

#define TITLE_H   14
#define DEF_X     18
#define DEF_W     58           /* byte cols (232 px) */
#ifdef GB_MSX2
#define DEF_H     198          /* includes video mode and Return to Defaults rows */
#elif defined(GB_PCW)
#define DEF_H     198          /* fixed monochrome palette plus PerryNet controls */
#else
#define DEF_H     186
#endif
#define DEF_BOTTOM_MARGIN 4
#define DEF_MAX_Y (GB_LINES - DEF_H - DEF_BOTTOM_MARGIN)
#define DEF_Y     ((DEF_MAX_Y < 32) ? DEF_MAX_Y : 32)
#ifdef GB_MSX2
#define ROW_H     11           /* fit the MSX input selector without leaving the screen */
#else
#define ROW_H     12           /* per-setting row height, px */
#endif
#define VAL_COL   16           /* value column offset from the window's left (byte cols); a
                                  gap past the longest label ("Backdrop") so value != label */
#define SELECT_W  34           /* fits cfg_get's 14 chars with the 8px CLASSIC font */
#define SELECT_H  10
#define STEP_H    10
#define COLOUR_ROW NROWS       /* the "Colours..." line sits below the picker rows */
/* The screensaver section: module picker, per-saver Configure command, then timeout. */
#ifdef GB_MSX2
#define VIDEO_ROW  (NROWS + 1) /* 4/16 colours; applied as Screen 6/7 at the next boot */
#define INPUT_ROW  (NROWS + 2) /* mouse/joystick port mode; applied at the next boot */
#define SS_HDR_ROW (NROWS + 3)
#define SS_MOD_ROW (NROWS + 4)
#define SS_CFG_ROW (NROWS + 5)
#define SS_TM_ROW  (NROWS + 6)
#elif defined(GB_PCW)
#define PERRYNET_ROW NROWS
#define TIMESYNC_ROW (NROWS + 1)
#define SS_HDR_ROW (NROWS + 2)
#define SS_MOD_ROW (NROWS + 3)
#define SS_CFG_ROW (NROWS + 4)
#define SS_TM_ROW  (NROWS + 5)
#else
#define SS_HDR_ROW (NROWS + 1)
#define SS_MOD_ROW (NROWS + 2)
#define SS_CFG_ROW (NROWS + 3)
#define SS_TM_ROW  (NROWS + 4)
#endif
#define RESET_ROW  (SS_TM_ROW + 1)
#define ROW_TITLEBAR 3
#define ROW_GADGETS 4
#define ROW_BACKDROP 5
#define ROW_WALLPAPER 6
#define DRIVE_NONE 0xFF

static unsigned char win_x, win_y, win_w, win_h;
static unsigned char titlebar_repaint;
static void s_draw(void);      /* forward: the colours editor repaints the window on exit */
static void draw_selector(unsigned char row, const char *value);
static void saver_value(char *dst);       /* forward: s_draw shows the current SAVERTIME= (#219) */
static void ss_module_value(char *dst);   /* forward: s_draw shows the current SAVER= module (#219) */
#ifdef GB_PCW
static void perrynet_baud_value(char *dst);
static unsigned char time_sync_enabled(void);
#endif

/* MIN_IST_ICONS: the exact icon count for an .IST to be offered as the desktop icon
   set. App-owned icons moved into GBAP headers, leaving 21 resident
   system/file-type slots. Exact matching also rejects legacy 25-slot sets whose
   positional meanings no longer align. A small toolchest like PAINT.IST (5 tool
   icons) stays filtered out. Keep this in step with the
   GBIS count of build/DEFAULT.IST (tools/build_kernel.sh packicons list) - if it drifts
   ABOVE the real count, every full set is dropped and the Icons picker shows "No files
   found" (#209). */
#define MIN_IST_ICONS 21

/* one configurable setting: a label, its GEOBENCH.CFG key (with '='), the 3-char file
   extension to list, and (for icon sets) the minimum icon count to qualify. */
typedef struct {
    const char *label;
    const char *key;       /* e.g. "FONT=" */
    const char *ext;       /* e.g. "FNT" (raw 3-char, matched against the 8.3 name) */
    unsigned char min_icons;  /* IST: exact desktop count; 0 = no check */
    unsigned int  tfr;     /* kernel transfer-area addr for the 8.3 name (gb_reload, #185) */
} setting_t;

static const setting_t rows[] = {
    { "Font",   "FONT=",     "FNT", 0,             0x120D },  /* KCFG_FONTNAME */
    { "Icons",  "ICONS=",    "IST", MIN_IST_ICONS, 0x1202 },  /* KCFG_ICONNAME */
    { "Cursor", "CURSOR=",   "SPR", 0,             0x1221 },  /* KCFG_CURSORNAME */
    { "Title bar","TITLEBAR=","TBR", 0,             0 },       /* paged app-linked installer */
    { "Gadgets", "GADGETS=", "GDT", 0,             0 },       /* independent close/maximize pair */
    { "Backdrop","BACKDROP=","BDP", 0,             0x1231 },  /* KCFG_BDPNAME (+ BD_SOLID #1290) */
    { "Wallpaper","WALLPAPER=","PIC", 0,           0 },       /* no live tfr: the desktop reads
                                                                 WALLPAPER= from the config (#216) */
};
#define NROWS 7
#define BD_SOLID_ADDR 0x1290   /* kernel BD_SOLID flag */
#define BD_DRIVE_ADDR 0x123C   /* kernel KCFG_BDDRIVE: selected backdrop drive */
#define BD_TILE_ADDR  0x1250   /* kernel BD_TILE: the loaded 16x16 backdrop tile (#216) */
#define KCFG_INKS_ADDR ((volatile unsigned char *)0x122C)
#define KCFG_FRAMEPEN_ADDR (*(volatile unsigned char *)0x133C)
#ifdef GB_MSX2
#define MSX_SCRMOD (*(volatile unsigned char *)0xFCAF)
#endif

/* GEOBENCH.CFG is loaded once into a full-sector buffer (gb_fs_load copies WHOLE
   512-byte sectors, so a smaller buffer would overflow into the globals after it). */
static char cfgbuf[512];
static unsigned int cfglen;

/* the popup file list: stems of the matching files in /GBENCH. Flat buffer + a pointer
   array (a 2D char array indexed by a uchar wraps the *width at 8 bits - see the FM). */
#define MAXST 24      /* max files a picker lists (savers/icons/fonts); the popup scrolls */
#define STLEN 11
static char stembuf[MAXST * STLEN];
static const char *stems[MAXST];
static unsigned char nstem;
static unsigned char stem_drive[MAXST];       /* source drive for each media entry */

#ifdef GB_PREEMPTIVE
#define PICK_IDLE       0
#define PICK_BACK       2
#define PICK_FIND_FIRST 3
#define PICK_FIND_NEXT  4
#define PICK_SCAN_FIRST 5
#define PICK_SCAN_NEXT  6
#define PICK_FILTER     7
#define PICK_LEAVE      8
#define PICK_DONE       9
#define PICK_BATCH      4
#define PICKF_SAVER     0x01
#define PICKF_MEDIA     0x02
#define PICKF_DESCENDED 0x04
#define PICKF_CLAIMED   0x08

static unsigned char picker_state, picker_flags, picker_row;
static unsigned char picker_drive_pos, picker_drive, picker_old_drive;
static unsigned char picker_back_left, picker_filter_pos, picker_keep;

static void picker_row_finish(unsigned char r);
static void ss_module_finish(void);
#endif

/* sel_boot: pin the active drive to the boot drive, where GEOBENCH.CFG + /GBENCH
   live - mirrors the kernel's fs_init. Another
   co-resident window may have left the global drive elsewhere, so we re-assert it
   before every FS op. */
static void sel_boot(void)
{
#ifdef GB_MSX2
    gb_set_drive(gb_boot_drive);
#else
    unsigned char d = gb_drives();
    gb_set_drive((d & GB_DRV_C) ? GB_DRIVE_C : GB_DRIVE_A);
#endif
}

static void sel_boot_root(void)
{
    unsigned char i;
    sel_boot();
    for (i = 0; i < 4; i++) gb_back();      /* root on FAT/path backends; no-op on floppy/root */
}

static unsigned char boot_drive(void)
{
#ifdef GB_MSX2
    return gb_boot_drive;
#else
    return (gb_drives() & GB_DRV_C) ? GB_DRIVE_C : GB_DRIVE_A;
#endif
}

static char drive_letter(unsigned char d)
{
#ifdef GB_MSX2
    return (char)gb_msx_drive_letter(d);
#else
    if (d == GB_DRIVE_A) return 'A';
    if (d == GB_DRIVE_B) return 'B';
    return 'C';
#endif
}

/* ---- GEOBENCH.CFG read/write (generic, key includes the '=') ----------------- */

/* cfg_keypos: index just past KEY's '=' (KEY must sit at a line start), or 0xFFFF. */
static unsigned int cfg_keypos(const char *key)
{
    unsigned char kl = 0, j;
    unsigned int i;
    while (key[kl]) kl++;
    for (i = 0; i + kl <= cfglen; i++) {
        if (i && cfgbuf[i-1] != '\r' && cfgbuf[i-1] != '\n') continue;   /* line start only */
        for (j = 0; j < kl; j++) if (cfgbuf[i+j] != key[j]) break;
        if (j == kl) return i + kl;
    }
    return 0xFFFF;
}

/* cfg_get: copy KEY's value (up to 14 chars, to the line end) into dst; "-" if absent. */
static void cfg_get(const char *key, char *dst)
{
    unsigned int p = cfg_keypos(key);
    unsigned char j = 0;
    if (p != 0xFFFF)
        while (p < cfglen && cfgbuf[p] != '\r' && cfgbuf[p] != '\n' && j < 14)
            dst[j++] = cfgbuf[p++];
    if (j == 0) dst[j++] = '-';
    dst[j] = 0;
}

static unsigned char cfg_drive(const char *key, unsigned char fallback)
{
    unsigned int p = cfg_keypos(key);
    if (p != 0xFFFF && p + 1 < cfglen && cfgbuf[p + 1] == ':') {
#ifdef GB_MSX2
        unsigned char i;
        for (i = 0; i < GB_MSX_DRIVE_COUNT; i++)
            if (gb_msx_drive_letter(i) == (unsigned char)cfgbuf[p]) return i;
#else
        if (cfgbuf[p] == 'A') return GB_DRIVE_A;
        if (cfgbuf[p] == 'B') return GB_DRIVE_B;
        if (cfgbuf[p] == 'C') return GB_DRIVE_C;
#endif
    }
    return fallback;
}

static void cfg_path(char *dst, unsigned char drive, const char *stem, const char *ext)
{
    unsigned char i = 0, j = 0;
    if (drive != boot_drive()) {
        dst[j++] = drive_letter(drive);
        dst[j++] = ':';
    }
    while (stem[i]) dst[j++] = stem[i++];
    dst[j++] = '.';
    dst[j++] = ext[0]; dst[j++] = ext[1]; dst[j++] = ext[2];
    dst[j] = 0;
}

/* cfg_set: write val into KEY's line (replace in place, preserving other keys; append
   the line if KEY is absent), then save GEOBENCH.CFG to the boot drive. Best-effort. */
static void cfg_set(const char *key, const char *val)
{
    unsigned char kl = 0, vl = 0;
    unsigned int p, end, i;
    while (key[kl]) kl++;
    while (val[vl]) vl++;
    if (cfglen == 0) return;                 /* no config loaded -> nothing to update */

    p = cfg_keypos(key);
    if (p == 0xFFFF) {                        /* no such key: append "KEY=val\r\n" */
        if (cfglen + kl + vl + 2 > sizeof(cfgbuf)) return;
        for (i = 0; i < kl; i++) cfgbuf[cfglen + i] = key[i];
        cfglen += kl;
        for (i = 0; i < vl; i++) cfgbuf[cfglen + i] = val[i];
        cfglen += vl;
        cfgbuf[cfglen++] = '\r'; cfgbuf[cfglen++] = '\n';
    } else {                                  /* replace the value in place */
        end = p;
        while (end < cfglen && cfgbuf[end] != '\r' && cfgbuf[end] != '\n') end++;
        if (vl > (unsigned char)(end - p)) {            /* grow: shift the tail right */
            unsigned int d = vl - (end - p);
            if (cfglen + d > sizeof(cfgbuf)) return;
            for (i = cfglen; i > end; i--) cfgbuf[i - 1 + d] = cfgbuf[i - 1];
            cfglen += d;
        } else if (vl < (unsigned char)(end - p)) {     /* shrink: shift the tail left */
            unsigned int d = (end - p) - vl;
            for (i = end; i < cfglen; i++) cfgbuf[i - d] = cfgbuf[i];
            cfglen -= d;
        }
        for (i = 0; i < vl; i++) cfgbuf[p + i] = val[i];
    }
    sel_boot_root();
    gb_set_name("GEOBENCHCFG");
    gb_fs_save(cfgbuf, cfglen);
    /* Keep the kernel's in-memory config copy in step with the disk, so changes the
       desktop reads straight from it (the screensaver SAVER=/SAVERTIME=, #219) take
       effect without a reboot - the desktop re-parses it when it repaints. */
    {
        char *km = (char *)0x1000;                 /* KCFG_TEXT */
        unsigned int i;
        for (i = 0; i < cfglen; i++) km[i] = cfgbuf[i];
        *(volatile unsigned int *)0x1200 = cfglen; /* KCFG_LEN */
    }
}

/* ---- /GBENCH and /PICS enumeration ------------------------------------------ */

/* enter_assets: at the browse root, descend into GBENCH (system assets) or PICS
   (wallpapers). A flat floppy has neither and is enumerated at root as-is. */
static unsigned char is_assets_dir(unsigned char pictures)
{
    const char *r = gb_entname();
    return (unsigned char)((!pictures && r[0]=='G' && r[1]=='B' && r[2]=='E'
             && r[3]=='N' && r[4]=='C' && r[5]=='H' && r[6]==' ') ||
            (pictures && r[0]=='P' && r[1]=='I' && r[2]=='C' && r[3]=='S'
             && r[4]==' '));
}

static unsigned char enter_assets(unsigned char pictures)
{
    char *p = gb_dir1();
    while (p) {
        if (gb_isdir() && is_assets_dir(pictures)) {
            gb_chdir();                  /* positioned on GBENCH/PICS: descend */
            return 1;
        }
        p = gb_dirn();
    }
    return 0;
}

/* ist_count: load the icon set `stem`.IST into the kernel copy buffer (low RAM, big
   enough for a full set, so no app bank is spent) and return its GBIS header icon
   count - 0 if it isn't a valid set or is too big to load. */
static unsigned char ist_count(const char *stem)
{
    char nm[11];
    unsigned char k;
    unsigned int n;
    for (k = 0; k < 8; k++) nm[k] = ' ';
    for (k = 0; k < 8 && stem[k]; k++) nm[k] = stem[k];
    nm[8] = 'I'; nm[9] = 'S'; nm[10] = 'T';
    gb_set_name(nm);
    n = gb_fs_load(gb_copybuf, GB_COPYMAX);
    if (n < 16 || gb_copybuf[0] != 'G' || gb_copybuf[1] != 'B'
        || gb_copybuf[2] != 'I' || gb_copybuf[3] != 'S') return 0;
    return (unsigned char)gb_copybuf[5];
}

/* enumerate_boot: fill stems[] with the names of files in the BOOT drive's /GBENCH
   whose extension is `ext`. If min_icons is non-zero (icon sets), require that exact
   layout count. This drops toolchests and legacy sets with shifted slot meanings. */
#ifndef GB_PREEMPTIVE
static void enumerate_boot(const char *ext, unsigned char min_icons)
{
    unsigned char descended, k, i, keep, old_drive;
    unsigned int off;
    char *p;
    unsigned char drive = boot_drive();
    nstem = 0;
    old_drive = gb_get_drive();
    gb_set_drive(drive);
    descended = enter_assets(0);

    /* pass 1: collect the matching stems (NO file load here - that would disturb the
       gb_dir1/gb_dirn enumerator mid-scan). */
    p = gb_dir1();
    while (p && nstem < MAXST) {
        if (!gb_isdir()) {
            const char *r = gb_entname();
            if (r[8]==ext[0] && r[9]==ext[1] && r[10]==ext[2]) {
                off = (unsigned int)nstem * STLEN;
                for (k = 0; k < 8 && r[k] != ' '; k++) stembuf[off + k] = r[k];
                stembuf[off + k] = 0;
                stems[nstem] = &stembuf[off];
                nstem++;
            }
        }
        p = gb_dirn();
    }

    /* pass 2: icon-set count filter (after the scan, still inside /GBENCH so the loads
       resolve). Compact the keepers down over the dropped entries. */
    if (min_icons) {
        keep = 0;
        for (i = 0; i < nstem; i++) {
            if (ist_count(&stembuf[(unsigned int)i * STLEN]) != min_icons) continue;
            if (keep != i)
                for (k = 0; k < STLEN; k++)
                    stembuf[(unsigned int)keep * STLEN + k] = stembuf[(unsigned int)i * STLEN + k];
            stems[keep] = &stembuf[(unsigned int)keep * STLEN];
            keep++;
        }
        nstem = keep;
    }

    if (descended) gb_back();                /* GBENCH -> root (depth-1, safe) */
    gb_set_drive(old_drive);
}

/* enumerate_media_drive: append "<drive>:<stem>" entries from one drive's /GBENCH. */
static void enumerate_media_drive(const char *ext, unsigned char drive)
{
    unsigned char descended, k, old_drive;
    unsigned int off;
    char *p;
    old_drive = gb_get_drive();
    gb_set_drive(drive);
    descended = enter_assets((unsigned char)(ext[0] == 'P'));
    p = gb_dir1();
    while (p && nstem < MAXST) {
        if (!gb_isdir()) {
            const char *r = gb_entname();
            if (r[8]==ext[0] && r[9]==ext[1] && r[10]==ext[2]) {
                off = (unsigned int)nstem * STLEN;
                stembuf[off + 0] = drive_letter(drive);
                stembuf[off + 1] = ':';
                for (k = 0; k < 8 && r[k] != ' '; k++) stembuf[off + 2 + k] = r[k];
                stembuf[off + 2 + k] = 0;
                stem_drive[nstem] = drive;
                stems[nstem] = &stembuf[off];
                nstem++;
            }
        }
        p = gb_dirn();
    }
    if (descended) gb_back();
    gb_set_drive(old_drive);
}

static void enumerate_media(const char *ext)
{
#ifdef GB_MSX2
    unsigned char i;
    nstem = 0;
    for (i = 0; i < GB_MSX_DRIVE_COUNT; i++) enumerate_media_drive(ext, i);
#else
    unsigned char mask = gb_drives();
    nstem = 0;
    if (mask & GB_DRV_A) enumerate_media_drive(ext, GB_DRIVE_A);
    if (mask & GB_DRV_B) enumerate_media_drive(ext, GB_DRIVE_B);
    if (mask & GB_DRV_C) enumerate_media_drive(ext, GB_DRIVE_C);
#endif
}
#else
/* Settings stays on the root task because directory and config services are
   kernel-owned. The picker job bounds discovery/enumeration to four entries per
   frame and validates one .IST per frame; no directory cursor or copy buffer is
   left live while another storage owner can run. */
static const char *picker_ext(void)
{
    return (picker_flags & PICKF_SAVER) ? "SAV" : rows[picker_row].ext;
}

static unsigned char picker_next_drive(void)
{
    if (!(picker_flags & PICKF_MEDIA)) {
        if (picker_drive_pos) return 0;
        picker_drive_pos = 1;
        picker_drive = boot_drive();
        return 1;
    }
#ifdef GB_MSX2
    if (picker_drive_pos >= GB_MSX_DRIVE_COUNT) return 0;
    picker_drive = picker_drive_pos++;
    return 1;
#else
    unsigned char d;
    while (picker_drive_pos < 3) {
        unsigned char mask = gb_drives();
        d = picker_drive_pos++;
        if ((d == GB_DRIVE_C && (mask & GB_DRV_C)) ||
            (d == GB_DRIVE_A && (mask & GB_DRV_A)) ||
            (d == GB_DRIVE_B && (mask & GB_DRV_B))) {
            picker_drive = d;
            return 1;
        }
    }
    return 0;
#endif
}

static void picker_begin_drive(void)
{
    picker_back_left = 4;
    picker_flags &= (unsigned char)~PICKF_DESCENDED;
    picker_state = PICK_BACK;
}

static void picker_scan_done(void)
{
    if (!(picker_flags & PICKF_MEDIA) && rows[picker_row].min_icons) {
        picker_filter_pos = 0;
        picker_keep = 0;
        picker_state = PICK_FILTER;
    } else {
        picker_state = PICK_LEAVE;
    }
}

static void picker_add_entry(const char *ext)
{
    const char *r;
    unsigned char k = 0;
    unsigned int off;
    if (gb_isdir()) return;
    r = gb_entname();
    if (r[8] != ext[0] || r[9] != ext[1] || r[10] != ext[2]) return;
    off = (unsigned int)nstem * STLEN;
    k = 0;
    if (picker_flags & PICKF_MEDIA) {
        stembuf[off++] = drive_letter(picker_drive);
        stembuf[off++] = ':';
        stem_drive[nstem] = picker_drive;
    }
    while (k < 8 && r[k] != ' ') stembuf[off++] = r[k++];
    stembuf[off] = 0;
    stems[nstem] = &stembuf[(unsigned int)nstem * STLEN];
    nstem++;
}

static void picker_start(unsigned char row, unsigned char saver)
{
    if (picker_state != PICK_IDLE) return;
    if (gb_drop_claimed()) {
        gb_alert("Storage busy", "Try again shortly.");
        s_draw();
        return;
    }
    picker_row = row;
    picker_flags = saver ? PICKF_SAVER : 0;
    if (saver || row == ROW_BACKDROP || row == ROW_WALLPAPER)
        picker_flags |= PICKF_MEDIA;
    picker_drive_pos = 0;
    nstem = 0;
    picker_old_drive = gb_get_drive();
    gb_drop_claim();
    picker_flags |= PICKF_CLAIMED;
    if (picker_next_drive()) picker_begin_drive();
    else picker_state = PICK_DONE;
    gb_curhide();
    draw_selector(saver ? SS_MOD_ROW : row, "Reading...");
    gb_curshow();
}

static void picker_cancel(void)
{
    if (picker_state == PICK_IDLE) return;
    if (picker_flags & PICKF_CLAIMED) {
        gb_set_drive(picker_drive);
        if (picker_flags & PICKF_DESCENDED) gb_back();
        gb_set_drive(picker_old_drive);
        gb_drop_release();
    }
    picker_flags = 0;
    picker_state = PICK_IDLE;
}

static void picker_step(void)
{
    const char *ext = picker_ext();
    char *p;
    unsigned char n, k, done_saver, done_row;
    unsigned int src, dst;

    if (picker_state == PICK_BACK) {
        gb_set_drive(picker_drive);
        if (picker_back_left) {
            gb_back();
            picker_back_left--;
        } else picker_state = PICK_FIND_FIRST;
        return;
    }
    if (picker_state == PICK_FIND_FIRST || picker_state == PICK_FIND_NEXT) {
        gb_set_drive(picker_drive);
        for (n = 0; n < PICK_BATCH; n++) {
            p = (picker_state == PICK_FIND_FIRST) ? gb_dir1() : gb_dirn();
            picker_state = PICK_FIND_NEXT;
            if (!p) { picker_state = PICK_SCAN_FIRST; return; }
            if (gb_isdir() && is_assets_dir((unsigned char)(ext[0] == 'P'))) {
                gb_chdir();
                picker_flags |= PICKF_DESCENDED;
                picker_state = PICK_SCAN_FIRST;
                return;
            }
        }
        return;
    }
    if (picker_state == PICK_SCAN_FIRST || picker_state == PICK_SCAN_NEXT) {
        gb_set_drive(picker_drive);
        for (n = 0; n < PICK_BATCH; n++) {
            p = (picker_state == PICK_SCAN_FIRST) ? gb_dir1() : gb_dirn();
            picker_state = PICK_SCAN_NEXT;
            if (!p || nstem >= MAXST) { picker_scan_done(); return; }
            picker_add_entry(ext);
        }
        return;
    }
    if (picker_state == PICK_FILTER) {
        gb_set_drive(picker_drive);
        if (picker_filter_pos < nstem) {
            src = (unsigned int)picker_filter_pos * STLEN;
            if (ist_count(&stembuf[src]) == rows[picker_row].min_icons) {
                dst = (unsigned int)picker_keep * STLEN;
                if (dst != src)
                    for (k = 0; k < STLEN; k++) stembuf[dst + k] = stembuf[src + k];
                stems[picker_keep] = &stembuf[dst];
                picker_keep++;
            }
            picker_filter_pos++;
            return;
        }
        nstem = picker_keep;
        picker_state = PICK_LEAVE;
    }
    if (picker_state == PICK_LEAVE) {
        gb_set_drive(picker_drive);
        if (picker_flags & PICKF_DESCENDED) {
            gb_back();
            picker_flags &= (unsigned char)~PICKF_DESCENDED;
            return;
        }
        if (nstem < MAXST && picker_next_drive()) picker_begin_drive();
        else picker_state = PICK_DONE;
        return;
    }
    if (picker_state == PICK_DONE) {
        done_saver = (unsigned char)(picker_flags & PICKF_SAVER);
        done_row = picker_row;
        gb_set_drive(picker_old_drive);
        gb_drop_release();
        picker_flags = 0;
        picker_state = PICK_IDLE;
        if (done_saver) ss_module_finish();
        else picker_row_finish(done_row);
    }
}
#endif

/* ---- drawing ----------------------------------------------------------------- */

static unsigned char row_y(unsigned char r)
{
    return (unsigned char)(win_y + TITLE_H + 4 + r * ROW_H);
}

static void draw_selector(unsigned char row, const char *value)
{
    gb_select((unsigned char)(win_x + VAL_COL),
              (unsigned char)(row_y(row) - 1),
              SELECT_W, SELECT_H, value, 0);
}

static unsigned char selector_hit(unsigned char row,
                                  unsigned char mx, unsigned char my)
{
    return gb_select_hit((unsigned char)(win_x + VAL_COL),
                         (unsigned char)(row_y(row) - 1),
                         SELECT_W, SELECT_H, mx, my);
}

static const gb_action_t configure_action[1] = {
    { "Configure", 16 }
};

#ifdef GB_PCW
static const char *const perrynet_baud_lbl[3] = { "9600", "17857", "41667" };

static void perrynet_baud_value(char *dst)
{
    unsigned char i;
    cfg_get("PERRYNET_BAUD=", dst);
    if (dst[0] != '-') return;
    for (i = 0; perrynet_baud_lbl[1][i]; i++) dst[i] = perrynet_baud_lbl[1][i];
    dst[i] = 0;
}

static unsigned char time_sync_enabled(void)
{
    unsigned int p = cfg_keypos("TIMESYNC=");
    if (p == 0xFFFF || p >= cfglen) return 0;
    return (unsigned char)(cfgbuf[p] == '1' || cfgbuf[p] == 'T' ||
                           cfgbuf[p] == 't' || cfgbuf[p] == 'Y' ||
                           cfgbuf[p] == 'y');
}
#endif

/* paint the content: a white panel, each setting's label + current value, and a note
   that changes apply on the next boot. The WM already drew the frame/title/close. */
static void s_draw(void)
{
    unsigned char r;
    char val[16];
    win_x = gb_wm_x(); win_y = gb_wm_y(); win_w = gb_wm_w(); win_h = gb_wm_h();
    gb_fill(win_x, (unsigned char)(win_y + TITLE_H), win_w,
            (unsigned char)(win_h - TITLE_H), 1);          /* white panel */
    for (r = 0; r < NROWS; r++) {
        gb_textbw((unsigned char)(win_x + 1), row_y(r), rows[r].label);
#ifdef GB_PREEMPTIVE
        if (picker_state != PICK_IDLE && !(picker_flags & PICKF_SAVER) && picker_row == r) {
            draw_selector(r, "Reading...");
        } else {
#endif
        cfg_get(rows[r].key, val);
        draw_selector(r, val);
#ifdef GB_PREEMPTIVE
        }
#endif
        if (rows[r].ext[0] == 'B') {         /* #216: preview the current backdrop tile beside */
            unsigned char sx = (unsigned char)(win_x + 51), sy = row_y(r);  /* keep clear of the selector */
            if (*(volatile unsigned char *)BD_SOLID_ADDR)
                gb_fill(sx, sy, 4, 8, 0);    /* SOLID -> a plain pen-0 (desktop) square */
#if defined(GB_MSX2) || defined(GB_PCW)
            else
                gb_backdrop(sx, sy, 4, 8);   /* applies the target's native tile encoding */
#else
            else
                gb_restorerect(sx, sy, 4, 8, (const void *)BD_TILE_ADDR);   /* tile's top 8 rows */
#endif
            gb_frame(sx, sy, 4, 8, 2);       /* outline so it shows on the white panel */
        }
    }
#ifndef GB_PCW
    gb_textbw((unsigned char)(win_x + 1), row_y(COLOUR_ROW), "Colours...");
#endif
#ifdef GB_MSX2
    gb_textbw((unsigned char)(win_x + 1), row_y(VIDEO_ROW), "Video mode");
    {
        unsigned int p = cfg_keypos("MSXMODE=");
        const char *mode = (p != 0xFFFF && p < cfglen && cfgbuf[p] == '7') ?
                           "16 colors" : "4 colors";
        draw_selector(VIDEO_ROW, mode);
    }
    gb_textbw((unsigned char)(win_x + 1), row_y(INPUT_ROW), "Input device");
    {
        unsigned int p = cfg_keypos("MSXMOUSE=");
        const char *input = (p != 0xFFFF && p + 3 < cfglen &&
                             cfgbuf[p] == 'T' && cfgbuf[p + 1] == 'R' &&
                             cfgbuf[p + 2] == 'U' && cfgbuf[p + 3] == 'E') ?
                            "Mouse" : "Joystick";
        draw_selector(INPUT_ROW, input);
    }
#endif
#ifdef GB_PCW
    gb_textbw((unsigned char)(win_x + 1), row_y(PERRYNET_ROW), "PerryNet baud");
    perrynet_baud_value(val);
    draw_selector(PERRYNET_ROW, val);
    gb_textbw((unsigned char)(win_x + 1), row_y(TIMESYNC_ROW), "Time sync");
    draw_selector(TIMESYNC_ROW, time_sync_enabled() ? "On" : "Off");
#endif
    {
        char sv[16];                          /* #219: the screensaver section */
        gb_textbw((unsigned char)(win_x + 1), row_y(SS_HDR_ROW), "Screensaver");
#ifdef GB_PREEMPTIVE
        if (picker_state != PICK_IDLE && (picker_flags & PICKF_SAVER)) {
            sv[0] = 'R'; sv[1] = 'e'; sv[2] = 'a'; sv[3] = 'd'; sv[4] = 'i';
            sv[5] = 'n'; sv[6] = 'g'; sv[7] = '.'; sv[8] = '.'; sv[9] = '.'; sv[10] = 0;
        } else
#endif
        ss_module_value(sv);                  /* Module: which .SAV */
        gb_textbw((unsigned char)(win_x + 2),       row_y(SS_MOD_ROW), "Module");
        draw_selector(SS_MOD_ROW, sv);
        gb_textbw((unsigned char)(win_x + 2), row_y(SS_CFG_ROW), "Options");
        {
            unsigned char bx = (unsigned char)(win_x + VAL_COL);
            unsigned char by = (unsigned char)(row_y(SS_CFG_ROW) - 1);
            gb_actions(bx, by, configure_action, 1, 0);
        }
        saver_value(sv);                      /* Timeout: idle minutes */
        gb_textbw((unsigned char)(win_x + 2),       row_y(SS_TM_ROW), "Timeout");
        draw_selector(SS_TM_ROW, sv);
    }
    gb_textbw((unsigned char)(win_x + 1), row_y(RESET_ROW), "Return to Defaults...");
    gb_textbw((unsigned char)(win_x + 1), (unsigned char)(win_y + win_h - 10),
#ifdef GB_MSX2
              "Mode/input/font/icons: reboot.");
#elif defined(GB_PCW)
              "Time sync/font/icons: reboot.");
#else
              "Font/icons: reboot.");
#endif
}

/* ---- desktop colours (INKS=) ------------------------------------------------ */

#ifndef GB_PCW
/* 5 colours: the 4 Mode-1 pens + the screen border (its own CPC ink). */
#define NPEN 5
#ifdef GB_MSX2
#define NEDITPEN 4
#else
#define NEDITPEN NPEN
#endif
static const char *const pen_lbl[NPEN] = { "Paper", "Text", "Edge", "Accent", "Border" };
static unsigned char ink_cur[NPEN], ink_orig[NPEN];

/* apply a colour live: pens 0-3 via SCR SET INK (&BC32), the border via SCR SET BORDER
   (&BC38). A palette change recolours every pixel of that pen at once, so the whole UI
   updates with no redraw - that is the preview (and, on Save, the immediate effect). */
static volatile unsigned char si_pen, si_ink;
#if defined(GB_MSX2) || defined(GB_PCW)
/* MSX: GB_SETINK (#8006) maps the CPC ink to the V9938 palette (kernel #287).
 * PCW: the CGA2 palette is fixed - the slot is k_noop there, so the preview
 * is safely inert (#331; the CPC arm called firmware the PCW lacks).
 * Called via inline asm - a gb.h/libgb binding would relink and bloat every
 * CPC app past its budget, so only Settings (this app) reaches the slot. */
static void si_call(void) __naked
{
__asm
    ld   a,(_si_ink)
    ld   b,a
    ld   c,a
    ld   a,(_si_pen)
    call 0x8006          ; GB_SETINK (A = pen 0-4, B/C = CPC ink)
    ret
__endasm;
}
#else
static void si_call(void) __naked
{
__asm
    ld   a,(_si_ink)
    ld   b,a
    ld   c,a
    ld   a,(_si_pen)
    call 0xBC32          ; SCR SET INK (A = pen, B/C = ink)
    ret
__endasm;
}
static void sb_call(void) __naked
{
__asm
    ld   a,(_si_ink)
    ld   b,a
    ld   c,a
    call 0xBC38          ; SCR SET BORDER (B/C = ink)
    ret
__endasm;
}
#endif
static void apply_colour(unsigned char i, unsigned char ink)
{
    unsigned char frame_pen = 2;
    ink_cur[i] = ink;
    KCFG_INKS_ADDR[i] = ink;   /* keep live chrome decisions in step with the preview */
    if (ink_cur[2] == ink_cur[0]) {
        if (ink_cur[1] != ink_cur[0]) frame_pen = 1;
        else if (ink_cur[3] != ink_cur[0]) frame_pen = 3;
    }
    KCFG_FRAMEPEN_ADDR = frame_pen;
    si_ink = ink;
#if defined(GB_MSX2) || defined(GB_PCW)
    si_pen = i;                 /* pen 0-3, or 4 = border - GB_SETINK handles both */
    si_call();
#else
    if (i < 4) { si_pen = i; si_call(); }
    else sb_call();
#endif
}

/* cfg_get_inks: parse INKS= into out[NPEN]; the default palette (1,26,0,6,1) when
   absent. (cfg_get can't be reused - an INKS value can exceed its 8-char cap.) */
static void cfg_get_inks(unsigned char *out)
{
    unsigned int p = cfg_keypos("INKS=");
    unsigned char i;
    out[0] = 1; out[1] = 26; out[2] = 0; out[3] = 6; out[4] = 1;
    if (p == 0xFFFF) return;
    for (i = 0; i < NPEN && p < cfglen; i++) {
        unsigned int v = 0;
        unsigned char got = 0;
        while (p < cfglen && cfgbuf[p] >= '0' && cfgbuf[p] <= '9') { v = v*10 + (cfgbuf[p]-'0'); p++; got = 1; }
        if (got) out[i] = (unsigned char)(v > 26 ? 26 : v);
        if (p >= cfglen || cfgbuf[p] == '\r' || cfgbuf[p] == '\n') break;
        if (cfgbuf[p] == ',') p++;
    }
}

/* cfg_set_inks: write the 5 colours as "d,l,k,a,b" into INKS= and save. */
static void cfg_set_inks(const unsigned char *inks)
{
    char s[18];
    unsigned char i, j = 0, v;
    for (i = 0; i < NPEN; i++) {
        v = inks[i];
        if (v >= 10) s[j++] = (char)('0' + v / 10);
        s[j++] = (char)('0' + v % 10);
        if (i < NPEN - 1) s[j++] = ',';
    }
    s[j] = 0;
    cfg_set("INKS=", s);
}

static unsigned char colp_y(unsigned char i) { return (unsigned char)(win_y + TITLE_H + 13 + i * 12); }

static void colp_stepper(unsigned char i)    /* draw pen i's ink number (00-26) */
{
    char t[3];
    unsigned char v = ink_cur[i];
    t[0] = (char)((v >= 10) ? '0' + v / 10 : ' ');
    t[1] = (char)('0' + v % 10);
    t[2] = 0;
    gb_stepper((unsigned char)(win_x + 10),
               (unsigned char)(colp_y(i) - 1), 10, STEP_H, t, 0);
}

/* colp_swatch: a small colour sample for pens 0-3 (#216). The hardware border is
   not a drawable bitmap pen, so its row uses a marked swatch; the real screen
   border remains its exact live preview. */
static void colp_swatch(unsigned char i)
{
    unsigned char x = (unsigned char)(win_x + 21), y = colp_y(i);
    if (i < 4) gb_fill(x, y, 3, 8, i);
    else {
        gb_fill(x, y, 3, 8, 1);
        gb_textbw((unsigned char)(x + 1), y, "/");
    }
    gb_frame(x, y, 3, 8, 2);
}

static void colp_draw(void)
{
    unsigned char i, by = (unsigned char)(win_y + win_h - 11);
    gb_fill(win_x, (unsigned char)(win_y + TITLE_H), win_w,
            (unsigned char)(win_h - TITLE_H), 1);
    gb_textbw((unsigned char)(win_x + 1), (unsigned char)(win_y + TITLE_H + 1), "Desktop colours");
    for (i = 0; i < NEDITPEN; i++) {
        gb_textbw((unsigned char)(win_x + 1),  colp_y(i), pen_lbl[i]);
        colp_stepper(i);
        colp_swatch(i);                          /* #216: live colour sample (pens 0-3) */
    }
    gb_textbw((unsigned char)(win_x + 1), by, "Save");
    gb_textbw((unsigned char)(win_x + 8), by, "Cancel");
}

/* colours_dialog: modal 4-pen editor. Each -/+ steps a pen's CPC ink (0-26, wrapping)
   and applies it live. Save writes INKS= and keeps the live palette (so it also takes
   effect at once); Cancel / ESC restore the inks the dialog opened with. */
static void colours_dialog(void)
{
    unsigned char i, flags = 0, done = 0;
    cfg_get_inks(ink_cur);
    for (i = 0; i < NPEN; i++) ink_orig[i] = ink_cur[i];
    gb_modal_set(1);
    gb_curhide();
    colp_draw();
    gb_curshow();
    while (!done) {
        flags = gb_poll();
        if (flags & GB_QUIT) {                       /* ESC = cancel: restore + leave */
            for (i = 0; i < NEDITPEN; i++) apply_colour(i, ink_orig[i]);
            done = 1;
            break;
        }
        if (!(flags & GB_CLICK)) continue;
        {
            unsigned char mx = gb_mx(), my = gb_my();
            unsigned char by = (unsigned char)(win_y + win_h - 11);
            if (my >= by && my < by + 8) {
                if (mx >= win_x + 1 && mx < win_x + 7) {            /* Save */
                    cfg_set_inks(ink_cur);
                    done = 1;
                } else if (mx >= win_x + 8 && mx < win_x + 18) {    /* Cancel */
                    for (i = 0; i < NEDITPEN; i++) apply_colour(i, ink_orig[i]);
                    done = 1;
                }
            } else {
                for (i = 0; i < NEDITPEN; i++) {
                    unsigned char ry = colp_y(i);
                    unsigned char part = gb_stepper_hit(
                        (unsigned char)(win_x + 10),
                        (unsigned char)(ry - 1), 10, STEP_H, mx, my);
                    if (part == GB_STEPPER_DEC)
                        ink_cur[i] = (unsigned char)((ink_cur[i] == 0) ? 26 : ink_cur[i] - 1);
                    else if (part == GB_STEPPER_INC)
                        ink_cur[i] = (unsigned char)((ink_cur[i] >= 26) ? 0 : ink_cur[i] + 1);
                    else continue;
                    apply_colour(i, ink_cur[i]);                     /* live preview */
                    gb_curhide(); colp_stepper(i); gb_curshow();
                    break;
                }
            }
        }
    }
    if (flags & GB_QUIT) while (gb_poll() & GB_QUIT) ;   /* swallow the ESC repeat */
    gb_modal_set(0);
    gb_restore_parent();       /* palette roles changed: redraw every managed frame */
}
#endif

/* ---- screensaver: module (SAVER=) + idle timeout (SAVERTIME=, #219) ---------- */

/* ss_module_value: the current SAVER= module/path (the default SQUARES when absent). */
static void ss_module_value(char *dst)
{
    unsigned char i;
    cfg_get("SAVER=", dst);
    if (dst[0] == '-') {                       /* absent -> the default module */
        dst[0]='S'; dst[1]='Q'; dst[2]='U'; dst[3]='A';
        dst[4]='R'; dst[5]='E'; dst[6]='S'; dst[7]=0;
    } else {
        for (i = 0; dst[i] && i < 14; i++) ;
        dst[i] = 0;
    }
}

/* Present an already-enumerated .SAV list and persist the pick to SAVER=. */
static void ss_module_finish(void)
{
    const char *list[MAXST];
    char path[16];
    unsigned char sel, i, n = 0;
    for (i = 0; i < nstem; i++) list[n++] = stems[i];
    if (n == 0) { gb_alert("No screensavers", "found."); s_draw(); return; }
    sel = gb_popup((unsigned char)(win_x + VAL_COL), row_y(SS_MOD_ROW), list, n);
    gb_curhide();
    if (sel != 0xFF) {
        cfg_path(path, stem_drive[sel], list[sel] + 2, "SAV");
        cfg_set("SAVER=", path);
    }
    s_draw();
    gb_curshow();
}

/* ss_module_dialog: cooperative builds enumerate synchronously; preemptive
   builds return immediately and present the popup after the frame job ends. */
static void ss_module_dialog(void)
{
#ifdef GB_PREEMPTIVE
    picker_start(0, 1);
#else
    enumerate_media("SAV");
    ss_module_finish();
#endif
}

/* preset choices: a label + the MINUTES written to SAVERTIME= (0 = off). The desktop
   reads SAVERTIME=<minutes> at boot and runs the module after that idle - so a change
   takes effect on the next boot, like Font/Icons. */
static const char *const saver_lbl[5]  = { "Off", "1 min", "2 min", "5 min", "10 min" };
static const unsigned char saver_mins[5] = { 0, 1, 2, 5, 10 };

/* u_dec: write the decimal of v into s (NUL-terminated); 0 -> "0". */
static void u_dec(unsigned int v, char *s)
{
    char tmp[6];
    unsigned char j = 0, k = 0;
    if (v == 0) { s[0] = '0'; s[1] = 0; return; }
    while (v) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
    while (k) s[j++] = tmp[--k];
    s[j] = 0;
}

/* saver_value: the current SAVERTIME= as a friendly label - a preset name if it matches
   one, else "<n> min" (a hand-edited non-preset minute count). */
static void saver_value(char *dst)
{
    char v[16];
    unsigned char mins = 0, overflow = 0;
    unsigned char i = 0, k, digit;
    cfg_get("SAVERTIME=", v);
    while (v[i] >= '0' && v[i] <= '9') {
        digit = (unsigned char)(v[i] - '0');
        if (!overflow) {
            if (mins > 25 || (mins == 25 && digit > 5))
                overflow = 1;
            else
                mins = (unsigned char)(mins * 10 + digit);
        }
        i++;
    }
    if (!overflow) {
        for (k = 0; k < 5; k++) if (saver_mins[k] == mins) {
            for (i = 0; saver_lbl[k][i]; i++) dst[i] = saver_lbl[k][i];
            dst[i] = 0;
            return;
        }
        u_dec(mins, dst);                    /* non-preset -> "<n> min" */
    } else {
        for (k = 0; k < i && k < 10; k++) dst[k] = v[k];
        dst[k] = 0;
    }
    for (k = 0; dst[k]; k++) ;
    dst[k] = ' '; dst[k+1] = 'm'; dst[k+2] = 'i'; dst[k+3] = 'n'; dst[k+4] = 0;
}

/* saver_dialog: pick a timeout preset and persist it to SAVERTIME=. Reboot to apply. */
static void saver_dialog(void)
{
    unsigned char sel = gb_popup((unsigned char)(win_x + VAL_COL), row_y(SS_TM_ROW),
                                 saver_lbl, 5);
    gb_curhide();
    if (sel != 0xFF) {
        char s[6];
        u_dec(saver_mins[sel], s);
        cfg_set("SAVERTIME=", s);
    }
    s_draw();
    gb_curshow();
}

/* ---- per-screensaver configuration modules ----------------------------------
 *
 * Settings knows no saver-specific keys or controls. For SAVER=B:XMATRIX.SAV it
 * asks the existing paged-module service to run XMATRIX.MOD, first on the boot
 * system drive and then on the configured saver drive. A module returns a
 * bounded list of NUL-separated key/value pairs; copy that list into this app
 * page before cfg_set() because file I/O is allowed to reuse the low-RAM block.
 */

static char ss_updates[GB_SSCFG_TEXT_CAP];
static char ss_saved_modname[11];

static unsigned char ss_run_module(void) __naked
{
__asm
    ld a,#0x80
    call #0x80AE
    ld a,c
    ret
__endasm;
}

static void ss_config_name(char *name11)
{
    char value[16];
    unsigned char i, p = 0;
    ss_module_value(value);
    if (value[0] && value[1] == ':') p = 2;
    for (i = 0; i < 8; i++) name11[i] = ' ';
    for (i = 0; i < 8 && value[p] && value[p] != '.'; i++, p++)
        name11[i] = value[p];
    name11[8] = 'M'; name11[9] = 'O'; name11[10] = 'D';
}

static void ss_apply_updates(void)
{
    char *p = ss_updates;
    char *end = ss_updates + GB_SSCFG_TEXT_CAP;
    while (p < end && *p) {
        char *key = p;
        while (p < end && *p) p++;
        if (p >= end) return;
        p++;
        {
            char *value = p;
            while (p < end && *p) p++;
            if (p >= end) return;
            cfg_set(key, value);
            p++;
        }
    }
}

static void ss_config_dialog(void)
{
    unsigned char i, result, old_drive;
    char module[11];
    unsigned char drive = cfg_drive("SAVER=", boot_drive());

    ss_config_name(module);
    old_drive = gb_get_drive();
    for (i = 0; i < 11; i++) {
        ss_saved_modname[i] = GB_SSCFG_MODNAME[i];
        GB_SSCFG_MODNAME[i] = module[i];
    }
    GB_SSCFG_OP = GB_SSCFG_OP_CONFIG;
    GB_SSCFG_RESULT = GB_SSCFG_MISSING;
    GB_SSCFG_TEXT[0] = 0;
    GB_SSCFG_TEXT[1] = 0;
    gb_set_drive(drive);
    result = ss_run_module();

    /* Preserve the result before restoring state or saving GEOBENCH.CFG. */
    for (i = 0; i < GB_SSCFG_TEXT_CAP; i++)
        ss_updates[i] = GB_SSCFG_TEXT[i];
    ss_updates[GB_SSCFG_TEXT_CAP - 2] = 0;
    ss_updates[GB_SSCFG_TEXT_CAP - 1] = 0;
    for (i = 0; i < 11; i++) GB_SSCFG_MODNAME[i] = ss_saved_modname[i];
    gb_set_drive(old_drive);

    if (result == GB_SSCFG_SAVE) {
        ss_apply_updates();
    } else if (result == GB_SSCFG_MISSING) {
        gb_alert("No settings", "for this saver.");
    }
    gb_curhide(); s_draw(); gb_curshow();
}

/* ---- interaction ------------------------------------------------------------- */

#if !defined(GB_MSX2) && !defined(GB_PCW)
/* CPC keeps canonical Mode-1 BDP bytes unchanged. Its resident kernel cannot
   afford the tiled filler, so Settings loads the selected tile directly for
   the desktop-side renderer. */
static void load_backdrop_live(const char *name, unsigned char drive)
{
    unsigned char old_drive, descended, i;
    char nm[11];
    unsigned int n;

    if (name[0]=='S' && name[1]=='O' && name[2]=='L' &&
        name[3]=='I' && name[4]=='D' && name[5]==0) {
        *(unsigned char *)BD_SOLID_ADDR = 1;
        return;
    }

    for (i = 0; i < 8; i++) nm[i] = ' ';
    for (i = 0; i < 8 && name[i]; i++) nm[i] = name[i];
    nm[8] = 'B'; nm[9] = 'D'; nm[10] = 'P';

    old_drive = gb_get_drive();
    gb_set_drive(drive);
    descended = enter_assets(0);
    gb_set_name(nm);
    n = gb_fs_load(gb_copybuf, 512);
    if (descended) gb_back();
    gb_set_drive(old_drive);
    if (n >= 64) {
        for (i = 0; i < 64; i++)
            ((char *)BD_TILE_ADDR)[i] = gb_copybuf[i];
        *(unsigned char *)BD_SOLID_ADDR = 0;
    } else {
        *(unsigned char *)BD_SOLID_ADDR = 1;
    }
}
#endif

/* live_apply: write the chosen 8.3 name into the kernel transfer area and call
   gb_reload. The desktop re-reads WALLPAPER= when it regains focus after Settings
   closes, so backdrop and wallpaper both apply without a reboot. */
static void live_apply(unsigned char r, const char *name, unsigned char drive)
{
    const char *ext = rows[r].ext;
    unsigned char is_backdrop = (unsigned char)(ext[0] == 'B');
    unsigned char is_titlebar = (unsigned char)(ext[0] == 'T');
    unsigned char is_gadgets = (unsigned char)(ext[0] == 'G');
    if (ext[0] == 'P') return;
    if (is_titlebar || is_gadgets) {
        char nm[11];
        unsigned char i, descended;
        unsigned int n;
        (void)drive;
        for (i = 0; i < 8; i++) nm[i] = ' ';
        for (i = 0; i < 8 && name[i]; i++) nm[i] = name[i];
        nm[8] = ext[0]; nm[9] = ext[1]; nm[10] = ext[2];
        sel_boot_root();
        descended = enter_assets(0);
        gb_set_name(nm);
        n = gb_fs_load(gb_copybuf, 512);
        if (descended) gb_back();
        if (is_titlebar) gb_titlebar_install(n);
        else gb_gadgets_install(n);
        titlebar_repaint = 1;  /* repaint after the modal picker has fully unwound */
        return;
    }
    if (is_backdrop) {
        *(unsigned char *)BD_DRIVE_ADDR = drive;
        *(unsigned char *)BD_SOLID_ADDR =
            (name[0]=='S' && name[1]=='O' && name[2]=='L' &&
             name[3]=='I' && name[4]=='D' && name[5]==0) ? 1 : 0;
    }
    {
        char *dst = (char *)rows[r].tfr;
        unsigned char i = 0;
        while (i < 8 && name[i]) { dst[i] = name[i]; i++; }
        while (i < 8) dst[i++] = ' ';
        dst[8] = ext[0]; dst[9] = ext[1]; dst[10] = ext[2];
    }
#if !defined(GB_MSX2) && !defined(GB_PCW)
    if (is_backdrop) {
        load_backdrop_live(name, drive);
        return;
    }
#endif
    gb_reload();
}

/* Present an already-enumerated row list, then write and apply the selection. */
static void picker_row_finish(unsigned char r)
{
    const char *list[MAXST + 1];
    char path[16];
    unsigned char sel, i, n = 0, media = 0, base;
    const char *ext = rows[r].ext;
    if (r == ROW_BACKDROP || r == ROW_WALLPAPER) media = 1;
    if (ext[0] == 'B') list[n++] = "SOLID";      /* the Backdrop list leads with SOLID (no tile) */
    else if (ext[0] == 'P') list[n++] = "NONE";  /* the Wallpaper list leads with NONE (#212) */
    base = n;
    for (i = 0; i < nstem; i++) list[n++] = stems[i];
    if (n == 0) {
        gb_alert("No files found", "in /GBENCH.");
        s_draw();
        return;
    }
    sel = gb_popup((unsigned char)(win_x + VAL_COL), row_y(r), list, n);
    gb_curhide();
    if (sel != 0xFF) {
        if ((ext[0] == 'B' && list[sel][0] == 'S' && list[sel][1] == 'O' &&
             list[sel][2] == 'L' && list[sel][3] == 'I' && list[sel][4] == 'D' &&
             list[sel][5] == 0) ||
            (ext[0] == 'P' && list[sel][0] == 'N' && list[sel][1] == 'O' &&
             list[sel][2] == 'N' && list[sel][3] == 'E' && list[sel][4] == 0)) {
            cfg_set(rows[r].key, list[sel]); /* SOLID / NONE stay global */
            if (r == ROW_BACKDROP)
                cfg_set("WALLPAPER=", "NONE");   /* backdrop mode: wallpaper must not mask it */
            live_apply(r, list[sel], DRIVE_NONE);
        } else {
            if (media)
                cfg_path(path, stem_drive[sel - base], list[sel] + 2, ext);
            else
                cfg_path(path, boot_drive(), list[sel], ext);
            cfg_set(rows[r].key, path);      /* persist to GEOBENCH.CFG */
            if (r == ROW_BACKDROP)
                cfg_set("WALLPAPER=", "NONE");   /* backdrop choice wins over wallpaper */
            else if (r == ROW_WALLPAPER)
                cfg_set("BACKDROP=", "SOLID");   /* wallpaper overlays the desktop; keep a plain fallback */
            live_apply(r, media ? (list[sel] + 2) : list[sel],
                       media ? stem_drive[sel - base] : boot_drive()); /* ...and apply now, no reboot (#185) */
        }
        /* Titlebar changes schedule a clipped repaint for the next frame. Repainting from this
           picker callback would re-enter the WM while its modal UI path is still unwinding. */
    }
    s_draw();                                /* repaint our content (new font/value) */
    gb_curshow();
}

/* open_picker: cooperative builds enumerate synchronously; preemptive builds
   start a bounded frame job and open the same popup when it completes. */
static void open_picker(unsigned char r)
{
#ifdef GB_PREEMPTIVE
    picker_start(r, 0);
#else
    if (r == ROW_BACKDROP || r == ROW_WALLPAPER)
        enumerate_media(rows[r].ext);
    else
        enumerate_boot(rows[r].ext, rows[r].min_icons);
    picker_row_finish(r);
#endif
}

#ifdef GB_MSX2
static void video_mode_dialog(void)
{
    static const char *const modes[] = { "4 colors", "16 colors" };
    unsigned char sel = gb_popup((unsigned char)(win_x + VAL_COL),
                                 row_y(VIDEO_ROW), modes, 2);
    gb_curhide();
    if (sel != 0xFF) cfg_set("MSXMODE=", sel ? "7" : "6");
    s_draw();
    gb_curshow();
}

static void input_device_dialog(void)
{
    static const char *const devices[] = { "Joystick", "Mouse" };
    unsigned char sel = gb_popup((unsigned char)(win_x + VAL_COL),
                                 row_y(INPUT_ROW), devices, 2);
    gb_curhide();
    if (sel != 0xFF) cfg_set("MSXMOUSE=", sel ? "TRUE" : "FALSE");
    s_draw();
    gb_curshow();
}
#endif

#ifdef GB_PCW
static void perrynet_baud_dialog(void)
{
    unsigned char sel = gb_popup((unsigned char)(win_x + VAL_COL),
                                 row_y(PERRYNET_ROW), perrynet_baud_lbl, 3);
    gb_curhide();
    if (sel != 0xFF) cfg_set("PERRYNET_BAUD=", perrynet_baud_lbl[sel]);
    s_draw();
    gb_curshow();
}

static void time_sync_dialog(void)
{
    static const char *const choices[] = { "Off", "On" };
    unsigned char sel = gb_popup((unsigned char)(win_x + VAL_COL),
                                 row_y(TIMESYNC_ROW), choices, 2);
    gb_curhide();
    if (sel != 0xFF) cfg_set("TIMESYNC=", sel ? "true" : "false");
    s_draw();
    gb_curshow();
}
#endif

/* Replace the mutable config with the target-specific pristine copy shipped in
   /GBENCH (card/MSX) or the floppy root. Font, icons, cursor and MSX mode still
   take effect at the next boot; palette and desktop-owned settings are live. */
static void reset_defaults(void)
{
    static const char *const choices[] = { "Return to Defaults", "Cancel" };
    unsigned char descended, p;
    unsigned int i, n;
    char chrome[16];

    p = gb_popup((unsigned char)(win_x + 1), row_y(RESET_ROW), choices, 2);
    gb_curhide();
    if (p != 0) {
        s_draw();
        gb_curshow();
        return;
    }

    sel_boot_root();
    descended = enter_assets(0);
    gb_set_name("DEFAULT CFG");
    n = gb_fs_load(gb_copybuf, sizeof(cfgbuf));
    if (descended) gb_back();
    if (!n) {
        gb_curshow();
        gb_alert("Defaults unavailable", "DEFAULT.CFG missing.");
        gb_curhide();
        s_draw();
        gb_curshow();
        return;
    }

    for (i = 0; i < n; i++) cfgbuf[i] = gb_copybuf[i];
    cfglen = n;
    cfg_set(rows[0].key, "DEFAULT");        /* save and refresh the kernel config copy */
    cfg_get(rows[ROW_TITLEBAR].key, chrome);
    live_apply(ROW_TITLEBAR, chrome, boot_drive());
    cfg_get(rows[ROW_GADGETS].key, chrome);
    live_apply(ROW_GADGETS, chrome, boot_drive());
#ifndef GB_PCW
    cfg_get_inks(ink_cur);
    for (p = 0; p < NPEN; p++) apply_colour(p, ink_cur[p]);
#endif
    *(volatile unsigned char *)BD_SOLID_ADDR = 1;
    *(volatile unsigned char *)BD_DRIVE_ADDR = DRIVE_NONE;
    s_draw();
    gb_curshow();
}

/* a content press: which row was clicked? */
static void s_click(void)
{
    unsigned char mx, my, r;
#ifdef GB_PREEMPTIVE
    if (picker_state != PICK_IDLE) return;
#endif
    win_x = gb_wm_x(); win_y = gb_wm_y(); win_w = gb_wm_w(); win_h = gb_wm_h();
    mx = gb_mx(); my = gb_my();
    for (r = 0; r < NROWS; r++) {
        if (selector_hit(r, mx, my)) {
            open_picker(r);
            return;
        }
    }
#ifndef GB_PCW
    {
        unsigned char ry = row_y(COLOUR_ROW);
        if (my >= (unsigned char)(ry - 2) && my < (unsigned char)(ry + ROW_H - 2)) {
            colours_dialog();
            return;
        }
    }
#endif
#ifdef GB_MSX2
    {
        if (selector_hit(VIDEO_ROW, mx, my)) {
            video_mode_dialog();
            return;
        }
        if (selector_hit(INPUT_ROW, mx, my)) {
            input_device_dialog();
            return;
        }
    }
#endif
#ifdef GB_PCW
    {
        if (selector_hit(PERRYNET_ROW, mx, my)) {
            perrynet_baud_dialog();
            return;
        }
        if (selector_hit(TIMESYNC_ROW, mx, my)) {
            time_sync_dialog();
            return;
        }
    }
#endif
    {
        if (selector_hit(SS_MOD_ROW, mx, my)) {           /* #219: screensaver module */
            ss_module_dialog();
            return;
        }
    }
    {
        unsigned char ry = row_y(SS_CFG_ROW);
        unsigned char by = (unsigned char)(ry - 1);
        if (my >= by && (unsigned char)(my - by) < GB_ACTION_H &&
            gb_actions_hit((unsigned char)(win_x + VAL_COL), by,
                           configure_action, 1, 0, mx, my) == 0) {
            ss_config_dialog();
            return;
        }
    }
    {
        if (selector_hit(SS_TM_ROW, mx, my)) {            /* screensaver timeout */
            saver_dialog();
            return;
        }
    }
    {
        unsigned char ry = row_y(RESET_ROW);
        if (my >= (unsigned char)(ry - 2) && my < (unsigned char)(ry + ROW_H - 2)) {
            reset_defaults();
            return;
        }
    }
}

static void s_drag(void)
{
#ifdef GB_PREEMPTIVE
    if (picker_state != PICK_IDLE) return;
#endif
    win_x = gb_wm_x(); win_y = gb_wm_y();
    if (gb_drag_window(&win_x, &win_y, gb_wm_w(), gb_wm_h())) {
        gb_wm_setpos(win_x, win_y);
        gb_restore_parent();
    }
}

static void s_frame(void)
{
#ifdef GB_PREEMPTIVE
    if (picker_state != PICK_IDLE) {
        picker_step();
        return;
    }
#endif
    if (!titlebar_repaint) return;
    titlebar_repaint = 0;
    /* Lower windows repaint first, and icon blits are not damage-clipped. Repaint
       our complete opaque surface so a desktop icon cannot show through the body. */
    gb_wm_damage(win_x, win_y, win_w, win_h);
    gb_restore_parent();
}

static void s_close(void)
{
#ifdef GB_PREEMPTIVE
    picker_cancel();
#endif
    gb_wm_close();
}

static void s_proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  s_draw();      break;
        case GB_MSG_CLICK: s_click();     break;
        case GB_MSG_FRAME: s_frame();     break;
        case GB_MSG_CLOSE: s_close();     break;
        case GB_MSG_DRAG:  s_drag();      break;
    }
}

static const gb_mwin_t smw = {
    DEF_X, DEF_Y, DEF_W, DEF_H, 0, 0, s_proc, "Settings"
};

void main(void)
{
    unsigned char n;
    gb_wm_managed(&smw);                            /* register FIRST (no draw), like the other apps */
    sel_boot_root();
    gb_set_name("GEOBENCHCFG");
    cfglen = gb_fs_load(cfgbuf, sizeof(cfgbuf));   /* load the config once (0 if none) */
    for (n = 64; n; n--) if (!gb_getkey()) break;  /* drain the launch keystrokes (#142) */
    gb_restore_parent();                            /* first paint: WM chrome + s_draw */
}
