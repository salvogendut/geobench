/* GBDOX.MOD - bounded GEOBENCH DOX/PIC decoder for BROWSER.APP.
 *
 * The module consumes one validated-at-the-proxy DOX slice from a borrowed
 * source page, but independently checks every chunk, count, jump, and record.
 * One call publishes at most one paragraph fragment or one table row. Graphic
 * records hold compact proxy URLs; GBIMG reports one only when it is visible. */
#include "gb.h"
#include "gbbrowser.h"

#define UI_OP             (*(volatile unsigned char *)0x1700)
#define UI_N              (*(volatile unsigned char *)0x1703)
#define UI_RES            (*(volatile unsigned char *)0x1704)
#define BUI_STAGE         ((char *)0x2B00)
#define BUI_PAGES         ((volatile unsigned char *)0x3900)
#define BUI_NPAGES        (*(volatile unsigned char *)0x3904)
#define BUI_TAIL          (*(volatile unsigned int  *)0x3905)
#define BUI_FLAGS         (*(volatile unsigned char *)0x3909)
#define BUI_RESOURCE_TAIL (*(volatile unsigned int  *)0x3AAD)
#define BUI_IMAGE_URL     (*(volatile unsigned int  *)0x3AB3)
#define BUI_CACHE_PAGE    (*(volatile unsigned char *)0x3ABF)
#define BUI_HIST_COUNT    (*(volatile unsigned char *)0x3AC0)
#define BUI_LINE_SIZE     (*(volatile unsigned char *)0x3AC2)
#define BUI_SCREEN_COLS   (*(volatile unsigned char *)0x3AC6)
#define BUI_FORM_ACTION   ((char *)0x39D0)
#define BUI_FORM_NAME     ((char *)0x3A00)
#define BUI_FORM_VALUE    ((char *)0x3A18)
#define BUI_FORM_ACTIVE   (*(volatile unsigned char *)0x3AA8)

#define BROWSER_LINE      ((char *)0x3300)
#define BROWSER_URL_VIEW  ((char *)0x3470)
#define BROWSER_LINK      ((char *)0x34F0)
#define BROWSER_URL       ((char *)0x3542)
#define BROWSER_TITLE     ((char *)0x3642)
#define BROWSER_PENDING   ((char *)0x36C2)
#define PIC_PAGE_K        (*(volatile unsigned char *)0x130B)
#define PIC_PAGE2_K       (*(volatile unsigned char *)0x1348)
#ifdef GB_MSX2
#define PIC_PAGE3_K       (*(volatile unsigned char *)0x1291)
#define PIC_PAGE4_K       (*(volatile unsigned char *)0x1292)
#endif
#define FS_SAVE_LEN_K     (*(volatile unsigned int  *)0x14FD)

#define INVALID_OFFSET    BROWSER_INVALID_OFFSET
#define LINK_MARK         BROWSER_LINK_MARK
#define FORM_MARK         BROWSER_FORM_MARK
#define FORM_CONT_MARK    BROWSER_FORM_CONT_MARK
#define IMAGE_MARK        BROWSER_IMAGE_MARK
#define IMAGE_CONT_MARK   BROWSER_IMAGE_CONT_MARK
#define TABLE_MARK        BROWSER_TABLE_MARK
#define TABLE_CONT_MARK   BROWSER_TABLE_CONT_MARK
#define CACHE_LINES       208
#define CACHE_DATA_END    0x27D0
#define IMAGE_SLOT_OFS    0x30F2
#define IMAGE_ROWS        BROWSER_IMAGE_ROWS
#define LINK_MAX          BROWSER_LINK_MAX
#define DOX_TEXT_MAX      4096
#define DOX_GRAPHICS_MAX  127
#define DOX_LINKS_MAX     16
#define DOX_CONTROLS_MAX  8
#define DOX_TABLE_ROWS_MAX 24
#define DOX_TABLE_CELLS_MAX 96
#define DOX_STEP_BYTES    128
#define DOX_SOURCE_FULL   0x01
#define FORM_ACTION_MAX   47
#define FORM_NAME_MAX     23
#define FORM_VALUE_MAX    47
#define URL_Y             26
#define URL_H             13

static void redraw_url_edit(unsigned char old_len)
{
    unsigned char len = 0, max, first, shown = 0, x;
    while (BROWSER_URL[len] && len < 95) len++;
    max = (unsigned char)(((BUI_SCREEN_COLS - 28) * 2) / 3);
    if (max > 48) max = 48;
    first = len > max ? (unsigned char)(len - max) : 0;
    while (BROWSER_URL[first] && shown < max)
        BROWSER_URL_VIEW[shown++] = BROWSER_URL[first++];
    BROWSER_URL_VIEW[shown] = 0;
    first = old_len < len ? old_len : len;
    if (old_len > max || len > max) first = 0;
    else first &= 0xFE;
    x = (unsigned char)(4 + ((unsigned int)first * 3) / 2);
    gb_fill(x, URL_Y + 1, (unsigned char)(BUI_SCREEN_COLS - 23 - x),
            URL_H - 2, 1);
    gb_textbw(x, URL_Y + 2, first ? BROWSER_URL + first : BROWSER_URL_VIEW);
    gb_fill((unsigned char)(4 + ((unsigned int)shown * 3) / 2),
            URL_Y + 10, 2, 1, 3);
}

static unsigned char source_read(unsigned int off, char *dst, unsigned int len)
{
    if (BUI_NPAGES != 1 || off > BUI_TAIL || len > BUI_TAIL - off) return 0;
    PIC_PAGE_K = BUI_PAGES[0];
    PIC_PAGE2_K = 0;
    gb_pic_edit_buf = (unsigned int)dst;
    gb_pic_edit_off = off;
    FS_SAVE_LEN_K = len;
    return gb_pic_edit(GB_PICEDIT_CHUNK);
}

static unsigned int source_u16(unsigned int off, unsigned char *ok)
{
    if (!source_read(off, BROWSER_LINE, 2)) { *ok = 0; return 0; }
    return (unsigned char)BROWSER_LINE[0] |
           ((unsigned int)(unsigned char)BROWSER_LINE[1] << 8);
}

static unsigned char chunk_name(const char *name)
{
    return (unsigned char)(BROWSER_LINE[0] == name[0] &&
        BROWSER_LINE[1] == name[1] && BROWSER_LINE[2] == name[2] &&
        BROWSER_LINE[3] == name[3]);
}

static unsigned char fail(unsigned char error)
{
    BUI_DOX_ERROR = error;
    BUI_DOX_STATE = BUI_DOX_FAILED;
    return 0;
}

static unsigned int store_buffer(const char *data, unsigned char len)
{
    unsigned int off = BUI_RESOURCE_TAIL;
    if (!len || off + len + 1 > IMAGE_SLOT_OFS) return INVALID_OFFSET;
    PIC_PAGE_K = BUI_CACHE_PAGE;
    PIC_PAGE2_K = 0;
    gb_pic_edit_buf = (unsigned int)data;
    gb_pic_edit_off = off;
    FS_SAVE_LEN_K = (unsigned int)(len + 1);
    if (!gb_pic_edit(GB_PICEDIT_WRITE)) return INVALID_OFFSET;
    BUI_RESOURCE_TAIL = (unsigned int)(off + len + 1);
    return off;
}

static unsigned char add_history(void)
{
    if (BUI_HIST_COUNT >= CACHE_LINES) {
        BUI_CACHE_FULL = 1; BUI_PENDING_LEN = 0; return 0;
    }
    BROWSER_PENDING[BUI_PENDING_LEN] = 0;
    PIC_PAGE_K = BUI_CACHE_PAGE;
    PIC_PAGE2_K = 0;
    gb_pic_edit_buf = (unsigned int)BROWSER_PENDING;
    gb_pic_edit_off = (unsigned int)BUI_HIST_COUNT * BUI_LINE_SIZE;
    FS_SAVE_LEN_K = (unsigned int)(BUI_PENDING_LEN + 1);
    if (!gb_pic_edit(GB_PICEDIT_WRITE)) {
        BUI_CACHE_FULL = 1; BUI_PENDING_LEN = 0; return 0;
    }
    BUI_HIST_COUNT++;
    BUI_PENDING_LEN = 0;
    return 1;
}

static void put_offset(unsigned char cell, unsigned char field,
                       unsigned int value)
{
    unsigned char pos = (unsigned char)(cell * BROWSER_TABLE_CELL_SIZE + field);
    BUI_TABLE_ROW_BUF[pos] = (char)value;
    BUI_TABLE_ROW_BUF[pos + 1] = (char)(value >> 8);
}

static unsigned int get_offset(unsigned char cell, unsigned char field)
{
    unsigned char pos = (unsigned char)(cell * BROWSER_TABLE_CELL_SIZE + field);
    return (unsigned char)BUI_TABLE_ROW_BUF[pos] |
           ((unsigned int)(unsigned char)BUI_TABLE_ROW_BUF[pos + 1] << 8);
}

static void emit_table_row(unsigned char cols)
{
    unsigned char cell, i, rows = BROWSER_TABLE_TEXT_ROWS, parent;
    BROWSER_PENDING[0] = TABLE_MARK;
    BROWSER_PENDING[BROWSER_TABLE_GRID] = (char)cols;
    BROWSER_PENDING[BROWSER_TABLE_COUNT] = (char)cols;
    for (cell = 0; cell < cols; cell++) {
        unsigned char dst = (unsigned char)(BROWSER_TABLE_HEADER +
            cell * BROWSER_TABLE_CELL_SIZE);
        unsigned char src = (unsigned char)(cell * BROWSER_TABLE_CELL_SIZE);
        for (i = 0; i < BROWSER_TABLE_CELL_SIZE; i++)
            BROWSER_PENDING[dst + i] = BUI_TABLE_ROW_BUF[src + i];
        if (get_offset(cell, BROWSER_TABLE_IMAGE) != INVALID_OFFSET)
            rows = IMAGE_ROWS;
    }
    BROWSER_PENDING[BROWSER_TABLE_ROWS] = (char)rows;
    BUI_PENDING_LEN = (unsigned char)(BROWSER_TABLE_HEADER +
        cols * BROWSER_TABLE_CELL_SIZE);
    parent = BUI_HIST_COUNT;
    if (!add_history()) return;
    for (i = 1; i < rows && !BUI_CACHE_FULL; i++) {
        BROWSER_PENDING[0] = TABLE_CONT_MARK;
        BROWSER_PENDING[1] = (char)parent;
        BUI_PENDING_LEN = 2;
        (void)add_history();
    }
}

static unsigned char counted_valid(unsigned int off, unsigned int end,
                                   unsigned char maximum)
{
    unsigned int data, len;
    unsigned char count, i, ok = 1;
    if (off >= end || !source_read(off, BROWSER_LINE, 1)) return 0;
    count = (unsigned char)BROWSER_LINE[0];
    if (count > maximum || (unsigned int)(1 + count * 2) > end - off) return 0;
    data = (unsigned int)(off + 1 + count * 2);
    for (i = 0; i < count; i++) {
        len = source_u16((unsigned int)(off + 1 + i * 2), &ok);
        if (!ok || len > end - data) return 0;
        data = (unsigned int)(data + len);
    }
    return (unsigned char)(data == end);
}

static unsigned char record_at(unsigned int off, unsigned int end,
                               unsigned char id, unsigned int *record,
                               unsigned int *length)
{
    unsigned int data, len;
    unsigned char count, i, ok = 1;
    if (!id || off >= end || !source_read(off, BROWSER_LINE, 1)) return 0;
    count = (unsigned char)BROWSER_LINE[0];
    if (id > count) return 0;
    data = (unsigned int)(off + 1 + count * 2);
    for (i = 1; i <= count; i++) {
        len = source_u16((unsigned int)(off + 1 + (i - 1) * 2), &ok);
        if (!ok || len > end - data) return 0;
        if (i == id) { *record = data; *length = len; return 1; }
        data = (unsigned int)(data + len);
    }
    return 0;
}

static unsigned char copy_link_url(unsigned char id, char *dst,
                                   unsigned char maximum)
{
    unsigned int off, len;
    unsigned char i, n;
    if (!record_at(BUI_DOX_LINK_OFF, BUI_DOX_LINK_END, id, &off, &len) ||
        len < 3 || len > maximum + 2 || !source_read(off, BROWSER_LINE, len) ||
        BROWSER_LINE[0] || BROWSER_LINE[len - 1]) return 0;
    n = (unsigned char)(len - 2);
    for (i = 0; i < n; i++) {
        if ((unsigned char)BROWSER_LINE[i + 1] < 33 ||
            (unsigned char)BROWSER_LINE[i + 1] > 126) return 0;
        dst[i] = BROWSER_LINE[i + 1];
    }
    dst[n] = 0;
    return 1;
}

static unsigned char copy_ctrl_string(unsigned int off, unsigned int end,
                                      unsigned int id, char *dst,
                                      unsigned char maximum,
                                      unsigned char capacity)
{
    unsigned int len;
    unsigned char index = 1, i, n, ok = 1;
    if (!id) return 0;
    while (off + 2 <= end) {
        len = source_u16(off, &ok);
        if (!ok || !len) return 0;
        if (len < 3 || len > end - off || len > 66) return 0;
        if (index == id) {
            n = (unsigned char)(len - 2);
            if ((capacity && n != capacity) ||
                !source_read((unsigned int)(off + 2), BROWSER_LINE, n)) return 0;
            for (i = 0; i < n && BROWSER_LINE[i]; i++)
                if ((unsigned char)BROWSER_LINE[i] < 32 ||
                    (unsigned char)BROWSER_LINE[i] > 126) return 0;
            if (i == n || (!capacity && i + 1 != n)) return 0;
            n = i > maximum ? maximum : i;
            for (i = 0; i < n; i++) dst[i] = BROWSER_LINE[i];
            dst[n] = 0;
            return 1;
        }
        off = (unsigned int)(off + len);
        index++;
    }
    return 0;
}

static unsigned char init_form(unsigned int off, unsigned int end)
{
    unsigned int control_len, string_len, controls, strings, data, len;
    unsigned int name_id, value_id;
    unsigned char count, id, action, type, width, height, maximum, ok = 1;
    BUI_FORM_ACTIVE = BUI_DOX_FORM_ID = 0;
    if (end - off < 7) return 0;
    control_len = source_u16(off, &ok);
    string_len = source_u16((unsigned int)(off + 2), &ok);
    if (!ok || control_len < 1 || string_len < 2 ||
        control_len + string_len != end - off - 4) return 0;
    controls = (unsigned int)(off + 4);
    strings = (unsigned int)(controls + control_len);
    if (!source_read(controls, BROWSER_LINE, 1)) return 0;
    count = (unsigned char)BROWSER_LINE[0];
    if (count > DOX_CONTROLS_MAX || 1 + (unsigned int)count * 2 > control_len)
        return 0;
    data = (unsigned int)(controls + 1 + count * 2);
    for (id = 1; id <= count; id++) {
        len = source_u16((unsigned int)(controls + 1 + (id - 1) * 2), &ok);
        if (!ok || len < 2 || len > (unsigned int)(controls + control_len - data) ||
            len > 9 || !source_read(data, BROWSER_LINE, len)) return 0;
        action = (unsigned char)BROWSER_LINE[0];
        type = (unsigned char)BROWSER_LINE[1];
        width = (unsigned char)BROWSER_LINE[2];
        height = (unsigned char)BROWSER_LINE[3];
        if (type == 32) {
            if (len != 9) return 0;
            if (!BUI_DOX_FORM_ID) {
                name_id = (unsigned char)BROWSER_LINE[4] |
                    ((unsigned int)(unsigned char)BROWSER_LINE[5] << 8);
                value_id = (unsigned char)BROWSER_LINE[6] |
                    ((unsigned int)(unsigned char)BROWSER_LINE[7] << 8);
                maximum = (unsigned char)BROWSER_LINE[8];
                if (width < 40 || width > 160 || height != 12 ||
                    !maximum || maximum > 63) return 0;
                if (!copy_link_url(action, BUI_FORM_ACTION, FORM_ACTION_MAX) ||
                    !copy_ctrl_string(strings, end, name_id, BUI_FORM_NAME,
                                      FORM_NAME_MAX, 0) ||
                    !copy_ctrl_string(strings, end, value_id, BUI_FORM_VALUE,
                                      FORM_VALUE_MAX, (unsigned char)(maximum + 1)))
                    return 0;
                BUI_DOX_FORM_ID = id;
                BUI_FORM_ACTIVE = 1;
            }
        } else if (type == 16) {
            if (len != 8) return 0;
        } else return 0;
        data = (unsigned int)(data + len);
    }
    return (unsigned char)(data == controls + control_len);
}

static unsigned int link_offset(unsigned char id)
{
    unsigned int off, len;
    unsigned char i, url_len;
    if (!record_at(BUI_DOX_LINK_OFF, BUI_DOX_LINK_END, id, &off, &len) ||
        len < 3 || len > LINK_MAX + 2 ||
        !source_read(off, BROWSER_LINK, len) || BROWSER_LINK[0] != 0 ||
        BROWSER_LINK[len - 1] != 0) return INVALID_OFFSET;
    url_len = (unsigned char)(len - 2);
    for (i = 0; i <= url_len; i++) BROWSER_LINK[i] = BROWSER_LINK[i + 1];
    return store_buffer(BROWSER_LINK, url_len);
}

static unsigned char init_document(void)
{
    unsigned int pos = 0, len, payload, end;
    unsigned char phase = 0, title_len, i;
    if (!BUI_CACHE_PAGE || BUI_NPAGES != 1 || !BUI_TAIL ||
        BUI_TAIL > 0x4000 || (BUI_FLAGS & DOX_SOURCE_FULL)) return fail(1);
    BUI_DOX_TEXT_OFF = BUI_DOX_TEXT_END = 0;
    BUI_DOX_GRPH_OFF = BUI_DOX_GRPH_END = 0;
    BUI_DOX_LINK_OFF = BUI_DOX_LINK_END = 0;
    BUI_DOX_TABLE_ROWS = BUI_DOX_TABLE_CELLS = 0;
    BUI_FORM_ACTIVE = BUI_DOX_FORM_ID = 0;
    BROWSER_TITLE[0] = 0;
    while (pos < BUI_TAIL) {
        if (BUI_TAIL - pos < 8 || !source_read(pos, BROWSER_LINE, 8) ||
            BROWSER_LINE[6] || BROWSER_LINE[7]) return fail(2);
        len = (unsigned char)BROWSER_LINE[4] |
              ((unsigned int)(unsigned char)BROWSER_LINE[5] << 8);
        payload = (unsigned int)(pos + 8);
        if (len > BUI_TAIL - payload) return fail(3);
        end = (unsigned int)(payload + len);
        if (phase == 0 && chunk_name("INFO")) {
            if (!len || len > 255) return fail(4);
            title_len = len > 31 ? 31 : (unsigned char)len;
            if (!source_read(payload, BROWSER_TITLE, title_len)) return fail(5);
            for (i = 0; i < title_len && BROWSER_TITLE[i]; i++);
            BROWSER_TITLE[i] = 0; phase = 1;
        } else if (phase == 1 && chunk_name("HEAD")) {
            if (len != 6 || !source_read(payload, BROWSER_LINE, 6) ||
                BROWSER_LINE[4] != 0 || BROWSER_LINE[5] != 2) return fail(6);
            phase = 2;
        } else if (phase == 2 && chunk_name("TEXT")) {
            if (len < 2 || len > DOX_TEXT_MAX) return fail(7);
            BUI_DOX_TEXT_OFF = payload; BUI_DOX_TEXT_END = end; phase = 3;
        } else if (phase == 3 && chunk_name("GRPH")) {
            BUI_DOX_GRPH_OFF = payload; BUI_DOX_GRPH_END = end;
            if (!counted_valid(payload, end, DOX_GRAPHICS_MAX)) return fail(8);
            phase = 4;
        } else if (phase == 4 && chunk_name("LINK")) {
            BUI_DOX_LINK_OFF = payload; BUI_DOX_LINK_END = end;
            if (!counted_valid(payload, end, DOX_LINKS_MAX)) return fail(9);
            phase = 5;
        } else if (phase == 5 && chunk_name("CTRL")) {
            if (len > 512 || !init_form(payload, end)) return fail(10);
            phase = 6;
        } else if ((phase == 5 || phase == 6) && chunk_name("ENDF")) {
            if (len || end != BUI_TAIL) return fail(11);
            phase = 7;
        } else return fail(12);
        pos = end;
    }
    if (phase != 7) return fail(13);
    BUI_DOX_POS = BUI_DOX_TEXT_OFF;
    BUI_DOX_ERROR = 0;
    BUI_DOX_STATE = BUI_DOX_PARSING;
    return 1;
}

static unsigned char control_len(unsigned char code, unsigned char sub)
{
    if (code == 1) return 2;
    if (code == 2) return 3;
    if (code == 3 || code == 4 || code == 6 || code == 7) return 1;
    if (code == 5) return 2;
    if (code >= 8 && code <= 11) {
        if (code == 10 && sub == 7) return 8;
        return (unsigned char)(2 * (code - 7));
    }
    return 0;
}

static void text_break(void)
{
    if (BUI_PENDING_LEN) (void)add_history();
}

static void text_char(unsigned char value)
{
    if (value < 32 || value >= 127) value = '?';
    BROWSER_PENDING[BUI_PENDING_LEN++] = (char)value;
    if (BUI_PENDING_LEN >= BUI_LINE_SIZE - 1) (void)add_history();
}

static void emit_link(unsigned char id)
{
    unsigned int off;
    unsigned char i;
    off = link_offset(id);
    if (off == INVALID_OFFSET) return;
    if (!BUI_PENDING_LEN || BUI_PENDING_LEN > BUI_LINE_SIZE - 4) {
        text_break();
        BROWSER_PENDING[0] = 'L'; BROWSER_PENDING[1] = 'i';
        BROWSER_PENDING[2] = 'n'; BROWSER_PENDING[3] = 'k';
        BUI_PENDING_LEN = 4;
    }
    for (i = BUI_PENDING_LEN; i; i--) BROWSER_PENDING[i + 2] = BROWSER_PENDING[i - 1];
    BROWSER_PENDING[0] = LINK_MARK;
    BROWSER_PENDING[1] = (char)off;
    BROWSER_PENDING[2] = (char)(off >> 8);
    BUI_PENDING_LEN = (unsigned char)(BUI_PENDING_LEN + 3);
    (void)add_history();
}

static void emit_image(unsigned char id, unsigned char link_id)
{
    unsigned char i, parent;
    text_break();
    parent = BUI_HIST_COUNT;
    BROWSER_PENDING[0] = IMAGE_MARK;
    BROWSER_PENDING[1] = (char)id;
    BROWSER_PENDING[2] = (char)(BUI_DOX_RESOURCE_FLAG >> 8);
    BROWSER_PENDING[3] = '['; BROWSER_PENDING[4] = 'I';
    BROWSER_PENDING[5] = 'm'; BROWSER_PENDING[6] = 'a';
    BROWSER_PENDING[7] = 'g'; BROWSER_PENDING[8] = 'e';
    BROWSER_PENDING[9] = ']';
    BUI_PENDING_LEN = 10;
    (void)add_history();
    for (i = 1; i < IMAGE_ROWS && !BUI_CACHE_FULL; i++) {
        BROWSER_PENDING[0] = IMAGE_CONT_MARK;
        BROWSER_PENDING[1] = (char)parent;
        BUI_PENDING_LEN = 2;
        (void)add_history();
    }
    if (link_id) emit_link(link_id);
}

static void emit_form(unsigned char id)
{
    if (!BUI_DOX_FORM_ID || id != BUI_DOX_FORM_ID) return;
    text_break();
    BROWSER_PENDING[0] = FORM_MARK;
    BUI_PENDING_LEN = 1;
    if (!add_history()) return;
    BROWSER_PENDING[0] = FORM_CONT_MARK;
    BUI_PENDING_LEN = 1;
    (void)add_history();
}

static unsigned char parse_cell(unsigned int pos, unsigned int end,
                                unsigned int *image, unsigned int *link)
{
    unsigned char take, i, code, sub, clen, graphic, link_id;
    BUI_PENDING_LEN = 0;
    *image = *link = INVALID_OFFSET;
    while (pos < end) {
        take = end - pos > LINK_MAX ? LINK_MAX : (unsigned char)(end - pos);
        if (!source_read(pos, BROWSER_LINE, take)) return 0;
        i = 0;
        while (i < take) {
            code = (unsigned char)BROWSER_LINE[i];
            if (code >= 12) {
                if (BUI_PENDING_LEN < 23) BROWSER_PENDING[BUI_PENDING_LEN++] = (char)code;
                i++; continue;
            }
            sub = i + 1 < take ? (unsigned char)BROWSER_LINE[i + 1] : 0;
            clen = control_len(code, sub);
            if (!clen || pos + i + clen > end) return 0;
            if (i + clen > take) break;
            if (code == 10 && sub == 2) {
                graphic = (unsigned char)BROWSER_LINE[i + 2];
                link_id = (unsigned char)BROWSER_LINE[i + 4];
                if (graphic != 1 || !link_id)
                    *image = (unsigned int)(BUI_DOX_RESOURCE_FLAG | graphic);
                if (link_id && *link == INVALID_OFFSET) *link = link_offset(link_id);
            }
            i = (unsigned char)(i + clen);
        }
        pos = (unsigned int)(pos + i);
        if (!i) return 0;
    }
    while (BUI_PENDING_LEN && BROWSER_PENDING[BUI_PENDING_LEN - 1] == ' ')
        BUI_PENDING_LEN--;
    return 1;
}

static unsigned char table_row(void)
{
    unsigned int column, delta, target, image, link, text;
    unsigned char cols, cell;
    if (!source_read(BUI_DOX_POS, BROWSER_LINE, 2)) return fail(20);
    cols = (unsigned char)BROWSER_LINE[1] & 15;
    if ((unsigned char)BROWSER_LINE[1] != (unsigned char)(0x10 | cols) ||
        cols < 2 || cols > 4 || BUI_DOX_TABLE_ROWS >= DOX_TABLE_ROWS_MAX ||
        BUI_DOX_TABLE_CELLS + cols > DOX_TABLE_CELLS_MAX) return fail(21);
    BUI_TABLE_ROW_CELLS = 0;
    column = (unsigned int)(BUI_DOX_POS + 2);
    for (cell = 0; cell < cols; cell++) {
        if (column + 14 > BUI_DOX_TEXT_END ||
            !source_read(column, BROWSER_LINE, 14) || BROWSER_LINE[0] != 14)
            return fail(22);
        delta = (unsigned char)BROWSER_LINE[1] |
                ((unsigned int)(unsigned char)BROWSER_LINE[2] << 8);
        if (delta < 15 || delta > BUI_DOX_TEXT_END - column) return fail(23);
        target = (unsigned int)(column + delta);
        if (!source_read((unsigned int)(target - 1), BROWSER_LINE, 1) || BROWSER_LINE[0])
            return fail(24);
        if (!parse_cell((unsigned int)(column + 14), (unsigned int)(target - 1),
                        &image, &link)) return fail(25);
        if (BUI_PENDING_LEN) {
            BROWSER_PENDING[BUI_PENDING_LEN] = 0;
            text = store_buffer(BROWSER_PENDING, BUI_PENDING_LEN);
        } else text = INVALID_OFFSET;
        BUI_PENDING_LEN = 0;
        put_offset(cell, BROWSER_TABLE_IMAGE, image);
        put_offset(cell, BROWSER_TABLE_LINK, link);
        put_offset(cell, BROWSER_TABLE_TEXT, text);
        BUI_TABLE_ROW_CELLS++;
        column = target;
    }
    if (column >= BUI_DOX_TEXT_END ||
        !source_read(column, BROWSER_LINE, 1) || BROWSER_LINE[0]) return fail(26);
    emit_table_row(cols);
    BUI_DOX_TABLE_ROWS++;
    BUI_DOX_TABLE_CELLS = (unsigned char)(BUI_DOX_TABLE_CELLS + cols);
    BUI_DOX_POS = (unsigned int)(column + 1);
    return 1;
}

static unsigned char step_document(void)
{
    unsigned int remain, absolute;
    unsigned char take, i = 0, code, sub, clen, graphic, link_id, next;
    if (BUI_DOX_STATE != BUI_DOX_PARSING) return 0;
    if (BUI_DOX_POS >= BUI_DOX_TEXT_END) return fail(30);
    remain = (unsigned int)(BUI_DOX_TEXT_END - BUI_DOX_POS);
    if (remain == 1) {
        if (!source_read(BUI_DOX_POS, BROWSER_LINE, 1) ||
            (unsigned char)BROWSER_LINE[0] != 0xff) return fail(31);
        text_break();
        BUI_DOX_POS++;
        BUI_DOX_STATE = BUI_DOX_DONE;
        return 2;
    }
    if (!source_read(BUI_DOX_POS, BROWSER_LINE, 2)) return fail(31);
    if ((unsigned char)BROWSER_LINE[0] == 0xff) {
        if (BUI_DOX_POS + 1 == BUI_DOX_TEXT_END) {
            text_break(); BUI_DOX_STATE = BUI_DOX_DONE; return 2;
        }
        return table_row();
    }
    take = remain > DOX_STEP_BYTES ? DOX_STEP_BYTES : (unsigned char)remain;
    if (!source_read(BUI_DOX_POS, BUI_STAGE, take)) return fail(32);
    while (i < take) {
        code = (unsigned char)BUI_STAGE[i];
        if (code >= 12) { text_char(code); i++; continue; }
        if (!code) {
            text_break();
            absolute = (unsigned int)(BUI_DOX_POS + i);
            if (absolute + 1 >= BUI_DOX_TEXT_END ||
                !source_read((unsigned int)(absolute + 1), BROWSER_LINE, 1))
                return fail(33);
            next = (unsigned char)BROWSER_LINE[0];
            if (next == 0xff && absolute + 2 == BUI_DOX_TEXT_END) {
                BUI_DOX_POS = (unsigned int)(absolute + 2);
                BUI_DOX_STATE = BUI_DOX_DONE;
                return 2;
            }
            if (next != 0) return fail(34);
            BUI_DOX_POS = (unsigned int)(absolute + 2);
            return 1;
        }
        sub = i + 1 < take ? (unsigned char)BUI_STAGE[i + 1] : 0;
        clen = control_len(code, sub);
        if (!clen || BUI_DOX_POS + i + clen > BUI_DOX_TEXT_END) return fail(35);
        if (i + clen > take) break;
        if (code == 8 && sub == 3) text_break();
        else if (code == 10 && sub == 2) {
            graphic = (unsigned char)BUI_STAGE[i + 2];
            link_id = (unsigned char)BUI_STAGE[i + 4];
            if (graphic == 1 && link_id) emit_link(link_id);
            else if (graphic) emit_image(graphic, link_id);
        } else if (code == 10 && sub == 7)
            emit_form((unsigned char)BUI_STAGE[i + 2]);
        i = (unsigned char)(i + clen);
    }
    if (!i) return fail(36);
    BUI_DOX_POS = (unsigned int)(BUI_DOX_POS + i);
    return 1;
}

static unsigned char load_image_url(void)
{
    unsigned int off, len;
    unsigned char id = (unsigned char)BUI_IMAGE_URL;
    unsigned char i;
    if (!record_at(BUI_DOX_GRPH_OFF, BUI_DOX_GRPH_END, id, &off, &len) ||
        len < 10 || len > LINK_MAX + 2 || !source_read(off, BROWSER_LINE, len) ||
        (unsigned char)BROWSER_LINE[0] != 1 || BROWSER_LINE[len - 1]) return 0;
    for (i = 1; i < len - 1; i++) {
        if ((unsigned char)BROWSER_LINE[i] < 33 ||
            (unsigned char)BROWSER_LINE[i] > 126) return 0;
        BROWSER_LINK[i - 1] = BROWSER_LINE[i];
    }
    BROWSER_LINK[len - 2] = 0;
    if (BROWSER_LINK[0] != 'h' || BROWSER_LINK[1] != 't' ||
        BROWSER_LINK[2] != 't' || BROWSER_LINK[3] != 'p' ||
        BROWSER_LINK[4] != ':' || BROWSER_LINK[5] != '/' ||
        BROWSER_LINK[6] != '/') return 0;
    return 1;
}

void main(void)
{
    UI_RES = 0;
    if (UI_OP == 31) { redraw_url_edit(UI_N); UI_RES = 1; return; }
    if (UI_OP == 28) UI_RES = init_document();
    else if (UI_OP == 29) UI_RES = step_document();
    else if (UI_OP == 30) UI_RES = load_image_url();
}
