/* MOS directory adapter and standalone long-name browser. */
#include <string.h>
#include "gb.h"

typedef unsigned char u8;
#define ROWS 10
#define NAME_BYTES 256

/* FatFS FILINFO on MOS: size, date, time, attributes, short name, long name. */
typedef struct {
    u8 stamp[8], attr;
    char alt[13], name[NAME_BYTES];
} FileInfo;

typedef struct {
    u8 dir[256];
    FileInfo info;
    char path[NAME_BYTES], target[NAME_BYTES], label[40];
    u8 attrs[ROWS], count, row, more;
    unsigned long offset;
} Browser;
static __at (0xeb00) Browser b;
typedef char browser_fits[(sizeof(Browser) <= 0x500) ? 1 : -1];
/* Only the standalone B browser uses these rows. Re-read them after its
 * child exits; the managed Files app owns this page while it runs. */
static __at (0x4000) char names[ROWS][NAME_BYTES];
extern char *managed_path(void);

extern u8 dir_open(void *dir, const char *path);
extern u8 dir_read(void *dir, FileInfo *entry);
extern u8 dir_close(void *dir);
extern u8 file_key(void);

static u8 read_page(void)
{
    unsigned long skip = b.offset;
    u8 error;
    b.count = b.more = 0;
    memset(b.dir, 0, sizeof(b.dir));
    if (dir_open(b.dir, b.path)) return 0;
    for (;;) {
        const char *name;
        error = dir_read(b.dir, &b.info);
        if (error || !b.info.name[0]) break;
        b.info.name[NAME_BYTES-1] = 0;
        name = b.info.name;
        if ((b.info.attr & 8) || !strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (skip) { --skip; continue; }
        if (b.count == ROWS) { b.more = 1; break; }
        strcpy(names[b.count], name);
        b.attrs[b.count++] = b.info.attr;
    }
    if (dir_close(b.dir) || error) { b.count = b.more = 0; return 0; }
    return 1;
}

static u8 upper(u8 c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* The shared File Manager uses the standard 11-byte GB directory ABI. MOS
 * keeps the long spelling in FILINFO for opening; never truncate two names
 * into one identity. The B browser can still display long-name-only entries. */
static __at (0xf000) struct {
    u8 opened;
    char raw[11];
} fs;

void fm_end(void)
{
    if (fs.opened) dir_close(b.dir);
    fs.opened = 0;
}

void fm_reset(void)
{
    fs.opened = 0;
    strcpy(b.path, "/");
}

static u8 name11(const char *s)
{
    u8 i = 0, ext = 0;
    if (!*s || *s == '.') return 0;
    memset(fs.raw, ' ', 11);
    while (*s) {
        u8 c = *s++;
        if (c == '.') {
            if (ext || !*s) return 0;
            ext = 1; i = 8;
        } else {
            if (c <= 32 || c >= 127 || i >= (ext ? 11 : 8)) return 0;
            fs.raw[i++] = upper(c);
        }
    }
    return 1;
}

char *fm_next(void)
{
    while (fs.opened && !dir_read(b.dir, &b.info) && b.info.name[0]) {
        if (!(b.info.attr & 8) && (name11(b.info.name) || name11(b.info.alt)))
            return b.info.name;
    }
    fm_end();
    return 0;
}

char *fm_first(void)
{
    fm_end();
    memset(b.dir, 0, sizeof(b.dir));
    if (dir_open(b.dir, managed_path())) return 0;
    fs.opened = 1;
    return fm_next();
}

char *fm_raw(void) { return fs.raw; }
u8 fm_isdir(void) { return (b.info.attr & 16) != 0; }

static char *current_path(void)
{
    char *dir=managed_path();
    unsigned len = strlen(dir);
    if (!fs.opened || len + (len != 1) + strlen(b.info.name) >= sizeof(b.target)) return 0;
    strcpy(b.target, dir);
    if (len != 1) strcat(b.target, "/");
    return strcat(b.target, b.info.name);
}

void fm_down(void)
{
    if (fm_isdir() && current_path()) strcpy(managed_path(), b.target);
    fm_end();
}

void fm_up(void)
{
    char *dir=managed_path(), *p = strrchr(dir, '/');
    fm_end();
    if (p == dir) ++p;            /* keep the root slash */
    if (p) *p = 0;
}

char *fm_selected(void)
{
    char *p=current_path();
    fm_end();
    return p;
}

char *fm_target(const char *raw)
{
    char *p = 0;
    if (fs.opened && !fm_isdir() && !memcmp(fs.raw, raw, 11) && !memcmp(raw+8, "APP", 3))
        p = current_path();
    fm_end();
    return p;
}

char *fm_document(const char *handler)
{
    char *p = 0;
    if (fs.opened && !fm_isdir() &&
        ((!memcmp(handler,"VIEWER  APP",11) && !memcmp(fs.raw+8,"PIC",3)) ||
         (!memcmp(handler,"NOTEPAD APP",11) && !memcmp(fs.raw+8,"TXT",3)))) p=current_path();
    fm_end();
    return p;
}

static u8 is_calc(const char *name)
{
    const char *ref = "CALC.APP";
    while (*ref && upper(*name) == *ref) { ++name; ++ref; }
    return !*name && !*ref;
}

static void clipped(const char *s, u8 limit)
{
    u8 n = 0;
    while (n < limit && *s) {
        u8 c = *s++;
        b.label[n++] = c >= 32 && c < 127 ? c : '?';
    }
    if (*s) b.label[n-1] = '>';
    b.label[n] = 0;
}

static void draw_row(u8 i)
{
    u8 y = 52 + i * 11;
    u8 selected = i == b.row;
    const char *kind = (b.attrs[i] & 16) ? "DIR" : "";
    gb_fill(10, y, 60, 10, selected ? 2 : 1);
    clipped(names[i], 30);
    if (selected) {
        gb_textrev(11, y+1, b.label);
        gb_textrev(64, y+1, kind);
    } else {
        gb_textbw(11, y+1, b.label);
        gb_textbw(64, y+1, kind);
    }
}

static void status(const char *s)
{
    gb_fill(10, 167, 60, 12, 1);
    gb_textbw(11, 169, s);
}

static void draw(const char *message)
{
    u8 i;
    const char *tail = b.path;
    unsigned len = strlen(tail);
    gb_fill(8, 16, 64, 168, 1);
    gb_frame(8, 16, 64, 168, 2);
    gb_fill(8, 16, 64, 13, 2);
    gb_textrev(11, 19, "File Manager");
    gb_textrev(48, 19, "Up");
    gb_textrev(56, 19, "<");
    gb_textrev(62, 19, ">");
    gb_textrev(68, 19, "X");
    if (len > 38) tail += len - 38;
    clipped(tail, 38);
    if (tail != b.path) b.label[0] = '<';
    gb_textbw(11, 35, b.label);
    for (i = 0; i < b.count; ++i) draw_row(i);
    status(message ? message : !b.count ? "Empty directory" :
           b.more ? "More files below" : b.offset ? "Last page" : "");
}

static void page(void)
{
    u8 ok = read_page();
    if (b.row >= b.count) b.row = b.count ? b.count - 1 : 0;
    draw(ok ? 0 : "Cannot read directory");
}

static void select_row(u8 row)
{
    u8 previous = b.row;
    if (previous == row) return;
    b.row = row;
    draw_row(previous);
    draw_row(row);
}

/* mode 0: start at root; 1: child closed; 2: selected executable rejected.
 * Return the selected absolute path, or NULL to close the browser. */
const char *browse(u8 mode)
{
    if (!mode) {
        strcpy(b.path, "/");
        b.row = b.offset = 0;
        page();
    } else { page(); if(mode==2) status("CALC.APP missing or invalid"); }
    for (;;) {
        u8 key = file_key();
        if (key == ' ') {
            u8 x = gb_mx(), y = gb_my();  /* same hit test for mouse and Space */
            if (y >= 16 && y < 29) {
                if (x >= 67 && x < 72) return 0;
                if (x >= 47 && x < 53) key = 127;
                else if (x >= 55 && x < 60 && b.offset) {
                    b.offset -= ROWS; b.row = 0; page(); continue;
                } else if (x >= 61 && x < 66 && b.more) {
                    b.offset += ROWS; b.row = 0; page(); continue;
                } else continue;
            } else if (x >= 10 && x < 70 && y >= 52 && y < 52 + 11*b.count) {
                u8 i = (y-52)/11;
                if (y == 62 + 11*i) continue;  /* gap below row */
                if (i != b.row) { select_row(i); continue; }
                key = 13;
            } else continue;
        }
        if (key == 27) return 0;
        if (key == '+') {
            if (b.row + 1 < b.count) select_row(b.row + 1);
            else if (b.more) { b.offset += ROWS; b.row = 0; page(); }
        } else if (key == '-') {
            if (b.row) select_row(b.row - 1);
            else if (b.offset) { b.offset -= ROWS; b.row = ROWS-1; page(); }
        } else if (key == 127) {
            char *last = strrchr(b.path, '/');
            if (!b.path[1]) continue;
            if (last == b.path) ++last;
            *last = 0;
            b.row = b.offset = 0;
            page();
        } else if (key == 13 && b.count) {
            const char *name = names[b.row];
            u8 sep = b.path[1] != 0;
            if (strlen(b.path) + sep + strlen(name) >= sizeof(b.target)) {
                status("Path too long");
                continue;
            }
            strcpy(b.target, b.path);
            if (sep) strcat(b.target, "/");
            strcat(b.target, name);
            if (b.attrs[b.row] & 16) {
                strcpy(b.path, b.target);
                b.row = b.offset = 0;
                page();
            } else if (is_calc(name)) return b.target;
            else status("No application for this file");
        } else if (key >= 32 && key < 127 && b.count) {
            u8 i = b.row, n = b.count;
            do {
                if (++i == b.count) i = 0;
                if (upper(*names[i]) == upper(key)) { select_row(i); break; }
            } while (--n);
        }
    }
}
