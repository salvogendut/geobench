/* gbappick.c - ICONED's paged Open dialog.
 *
 * .IST and .SPR files are listed immediately. Embedded APP icons have their own
 * row because validating every APP before drawing the first dialog is painfully
 * slow on floppy media. Inside that view an .APP is listed only when its preamble
 * contains an optional GBAP v1/v2 icon. Probing a file can disturb a backend's
 * directory enumerator, so the scan resumes by name after each probe.
 */
#include "gb.h"

#define PICK_MAX       12
#define APPICON_OFF    16
#define APPICON_WB     8
#define APPICON_H      32

#ifdef GB_PREEMPTIVE
#define APP_PROBE_MAX  1024
#define APPICON_LEN    (APPICON_WB * APPICON_H)
#define APPICON7_WB    16
#define APPICON7_LEN   (APPICON7_WB * APPICON_H)
#define APPICON_MODE1  1
#define APPICON_MODE7  7
#else
#define APP_PROBE_MAX  512
#endif
#if defined(GB_MSX2) || defined(GB_PCW)
#define FS_LOAD_OFS ((volatile unsigned char *)0x144C)
#endif
#define FS_XFLAGS   (*(volatile unsigned char *)0x144F)
#define PROBE_BUF   ((unsigned char *)0x2200)
#ifdef GB_PREEMPTIVE
#define FS_SAVE_LEN_K (*(volatile unsigned int *)0x14FD)
#endif
#if defined(GB_MSX2) && defined(GB_PREEMPTIVE)
#define MSX_SCRMOD (*(volatile unsigned char *)0xFCAF)
#endif
#if defined(GB_PCW) && defined(GB_PREEMPTIVE)
#define APP_NPAGES ((volatile unsigned char *)0x1437)
#define APP_PAGES  ((volatile unsigned char *)0x1438)
#define APP_BUSY   ((volatile unsigned char *)0x1440)
#define PIC_PAGE_K  (*(volatile unsigned char *)0x130B)
#define PIC_PAGE2_K (*(volatile unsigned char *)0x1348)
#define FS_SAVE_LEN_K (*(volatile unsigned int *)0x14FD)
#endif

#if !defined(GB_MSX2) && !defined(GB_PCW)
extern unsigned char gb_app_probe(char *dst);
#endif

static char          store[PICK_MAX][14];
static char          rawname[PICK_MAX][11];
static unsigned char isdir_f[PICK_MAX];
static char          saved_name[11];
#ifdef GB_PREEMPTIVE
static char          draw_launch_name[11];
#endif

static void copy11(char *dst, const char *src)
{
    unsigned char i;
    for (i = 0; i < 11; i++) dst[i] = src[i];
}

static unsigned char same11(const char *a, const char *b)
{
    unsigned char i;
    for (i = 0; i < 11; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static unsigned char ext_is(const char *raw, const char *ext)
{
    return (unsigned char)(raw[8] == ext[0] && raw[9] == ext[1] &&
                           raw[10] == ext[2]);
}

static void name_disp(char *dst, const char *raw, unsigned char dir)
{
    unsigned char i, j = 0;
    for (i = 0; i < 8 && raw[i] != ' '; i++) dst[j++] = raw[i];
    if (dir) dst[j++] = '/';
    else if (raw[8] != ' ') {
        dst[j++] = '.';
        for (i = 8; i < 11 && raw[i] != ' '; i++) dst[j++] = raw[i];
    }
    dst[j] = 0;
}

static unsigned char has_gbap(const char *raw)
{
    unsigned char version;
#if defined(GB_MSX2) || defined(GB_PCW)
    unsigned int got;
#endif

    gb_set_name(raw);
#if defined(GB_MSX2) || defined(GB_PCW)
    FS_LOAD_OFS[0] = 0;
    FS_LOAD_OFS[1] = 0;
    FS_LOAD_OFS[2] = 0;
    FS_XFLAGS = 1;
    got = gb_fs_load((char *)PROBE_BUF, APP_PROBE_MAX);
    FS_XFLAGS = 0;
    if (got < APPICON_OFF) return 0;
#else
    /* The low-RAM probe borrows an app page, so this PAGE_DATA picker remains
     * mapped even when the candidate occupies nearly its full 16 KiB. */
    if (!gb_app_probe((char *)PROBE_BUF)) return 0;
#endif

    if (PROBE_BUF[0] != 0xC3 || PROBE_BUF[3] != 'G'
        || PROBE_BUF[4] != 'B' || PROBE_BUF[5] != 'A'
        || PROBE_BUF[6] != 'P') return 0;
    version = PROBE_BUF[7];
    if (version == 1)
        return (unsigned char)(PROBE_BUF[8] == 1
            && PROBE_BUF[9] == APPICON_WB && PROBE_BUF[10] == APPICON_H);
    /* GBAP v2 requires the portable fallback in resource slot zero. */
    return (unsigned char)(version == 2 && PROBE_BUF[8]
        && PROBE_BUF[APPICON_OFF] == 1
        && PROBE_BUF[APPICON_OFF + 1] == APPICON_WB
        && PROBE_BUF[APPICON_OFF + 2] == APPICON_H);
}

#if defined(GB_PCW) && defined(GB_PREEMPTIVE)
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

#ifdef GB_PREEMPTIVE
/* Load and draw one APP-owned icon. File Manager invokes this only from its
 * frame callback; repaint callbacks merely leave a generic APP placeholder.
 * The module is paged over the caller, so all request data and icon bytes live
 * in low RAM. */
unsigned char gb_drawappicon(const char *raw, unsigned char x,
                             unsigned char y, unsigned char half
#ifdef GB_PCW
                             , volatile unsigned char *cache_page,
                             unsigned char cache_slot
#endif
                             )
{
    unsigned char *data = PROBE_BUF;
    unsigned char *entry;
    unsigned char codec = APPICON_MODE1;
    unsigned char rows = half ? 16 : APPICON_H;
    unsigned int got, len, off = APPICON_OFF, total;
#ifdef GB_PCW
    unsigned int p;
#endif

    /* Module loading overwrites fs_req_name. Restore the focused File Manager's
     * launch name, matching the established APP-picker/module convention. */
    gb_get_name(draw_launch_name);
    gb_set_name(raw);
#if defined(GB_MSX2) || defined(GB_PCW)
    FS_LOAD_OFS[0] = 0;
    FS_LOAD_OFS[1] = 0;
    FS_LOAD_OFS[2] = 0;
    FS_XFLAGS = 1;
    got = gb_fs_load((char *)data, APP_PROBE_MAX);
    FS_XFLAGS = 0;
#else
    got = gb_app_probe((char *)data) ? APP_PROBE_MAX : 0;
#endif
    gb_set_name(draw_launch_name);

    if (got < APPICON_OFF || data[0] != 0xC3 || data[3] != 'G'
        || data[4] != 'B' || data[5] != 'A' || data[6] != 'P')
        return 0;
    if (data[7] == 1) {
        if (data[8] != APPICON_MODE1 || data[9] != APPICON_WB
            || data[10] != APPICON_H || data[11] != 0 || data[12] != 1
            || data[13] != APPICON_OFF || data[14] != 0
            || got < APPICON_OFF + APPICON_LEN)
            return 0;
    } else if (data[7] == 2) {
        total = (unsigned int)data[10] | ((unsigned int)data[11] << 8);
        if (!data[8] || data[8] > 2 || data[9] != 8
            || data[12] != APPICON_OFF || data[13] != 0 || total > got)
            return 0;
        entry = data + APPICON_OFF;
        off = (unsigned int)entry[6] | ((unsigned int)entry[7] << 8);
        len = (unsigned int)entry[4] | ((unsigned int)entry[5] << 8);
        if (entry[0] != APPICON_MODE1 || entry[1] != APPICON_WB
            || entry[2] != APPICON_H || entry[3] || len != APPICON_LEN
            || off + len > total)
            return 0;
#ifdef GB_MSX2
        if (MSX_SCRMOD == 7 && data[8] == 2) {
            entry += 8;
            len = (unsigned int)entry[4] | ((unsigned int)entry[5] << 8);
            if (entry[0] == APPICON_MODE7 && entry[1] == APPICON7_WB
                && entry[2] == APPICON_H && !entry[3] && len == APPICON7_LEN) {
                unsigned int native_off = (unsigned int)entry[6]
                                          | ((unsigned int)entry[7] << 8);
                if (native_off + len <= total) {
                    codec = APPICON_MODE7;
                    off = native_off;
                }
            }
        }
#endif
    } else return 0;

#ifdef GB_MSX2
    if (codec == APPICON_MODE7) {
        if (half) off += APPICON7_WB * 8;
        gb_pic_edit_buf = (unsigned int)(data + off);
        gb_pic_edit_off = (unsigned int)x | ((unsigned int)y << 8);
        FS_SAVE_LEN_K = APPICON_WB | ((unsigned int)rows << 8);
        return gb_pic_edit(GB_PICEDIT_NATIVE16);
    }
    gb_pic_edit_buf = (unsigned int)(data + off);
    gb_pic_edit_off = (unsigned int)(data + off);
    FS_SAVE_LEN_K = APPICON_LEN;
    if (!gb_pic_edit(GB_PICEDIT_NATIVE)) return 0;
#elif defined(GB_PCW)
    if (off) {
        for (p = 0; p < APPICON_LEN; p++) data[p] = data[off + p];
        off = 0;
    }
    if (cache_slot < 64) {
        if (!*cache_page) {
            for (p = 0; p < *APP_NPAGES; p++) {
                if (!APP_BUSY[p]) {
                    APP_BUSY[p] = 1;
                    *cache_page = APP_PAGES[p];
                    break;
                }
            }
        }
        if (*cache_page) {
            PIC_PAGE_K = *cache_page;
            PIC_PAGE2_K = 0;
            gb_pic_edit_buf = (unsigned int)data;
            gb_pic_edit_off = (unsigned int)cache_slot << 8;
            FS_SAVE_LEN_K = APPICON_LEN;
            (void)gb_pic_edit(GB_PICEDIT_WRITE);
        }
    }
    /* Keep canonical Mode-1 bytes in the cache: gb_pic_blit performs the PCW
     * conversion. Only the immediate low-RAM draw needs native bytes. */
    for (p = 0; p < APPICON_LEN; p++) data[p] = appicon_native(data[p]);
#endif
    if (half) off += APPICON_WB * 8;
    gb_restorerect(x, y, APPICON_WB, rows, data + off);
    return 1;
}
#endif

/* Reposition after a probe or popup changed the backend directory cursor.
 * advance=1 returns the entry after raw; 0 leaves raw positioned for chdir. */
static char *seek_raw(const char *raw, unsigned char advance)
{
    char *p = gb_dir1();
    while (p) {
        if (same11(gb_entname(), raw)) return advance ? gb_dirn() : p;
        p = gb_dirn();
    }
    return 0;
}

/* PCW has two flat floppy drives. CPC floppy dialogs use the same A/B toggle;
 * card-based CPC and MSX dialogs remain on C. */
static void next_drive(void) __naked
{
__asm
    call 0x8084
    or a
    ret z
    xor a,#0x03
    jp 0x8081
__endasm;
}

unsigned char gb_pickappicon(char *out11)
{
    const char *labels[PICK_MAX + 3];
    char candidate[11];
    char *p;
    unsigned char nreal, sel, i, dir, accept, app, app_only = 0;

    for (;;) {
        nreal = 0;
        p = gb_dir1();
        while (p && nreal < PICK_MAX) {
            dir = gb_isdir();
            copy11(candidate, gb_entname());
            app = (unsigned char)(!dir && ext_is(candidate, "APP"));
            accept = app_only
                ? app
                : (unsigned char)(dir || ext_is(candidate, "IST") ||
                                  ext_is(candidate, "SPR"));
            if (app_only && app) {
                accept = has_gbap(candidate);
                p = seek_raw(candidate, 1);
            } else {
                p = gb_dirn();
            }
            if (accept) {
                isdir_f[nreal] = dir;
                copy11(rawname[nreal], candidate);
                name_disp(store[nreal], candidate, dir);
                nreal++;
            }
        }
        if (app_only) gb_set_name(saved_name);

        labels[0] = "..";
        labels[1] = "[Next drive]";
        labels[2] = app_only ? "[Files]" : "[APP icons]";
        for (i = 0; i < nreal; i++) labels[i + 3] = store[i];
        sel = gb_popup(10, 8, labels, (unsigned char)(nreal + 3));
        if (sel == 0xFF) return 0;
        if (sel == 0) {
            gb_back();
            continue;
        }
        if (sel == 1) {
            next_drive();
            continue;
        }
        if (sel == 2) {
            app_only = (unsigned char)!app_only;
            if (app_only) gb_get_name(saved_name);
            continue;
        }
        i = (unsigned char)(sel - 3);
        if (i >= nreal) continue;
        if (isdir_f[i]) {
            if (seek_raw(rawname[i], 0)) gb_chdir();
            continue;
        }
        copy11(out11, rawname[i]);
        return 1;
    }
}
