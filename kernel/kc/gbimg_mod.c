/* GBIMG.MOD - bounded Browser inline-image cache helper.
 *
 * The Browser keeps page records plus a bounded current-page GBPC cache in
 * borrowed 16K banks. This paged helper performs the infrequent bank copies,
 * validation, cache lookup, visible-image lookup, and clipped blit so that
 * code does not consume the PCW Browser app bank. */
#include "gb.h"
#include "gbbrowser.h"

#define UI_OP           (*(volatile unsigned char *)0x1700)
#define UI_N            (*(volatile unsigned char *)0x1703)
#define UI_RES          (*(volatile unsigned char *)0x1704)
#define BUI_STAGE       ((char *)0x2B00)
#define BUI_STAGE_LEN   (*(volatile unsigned int  *)0x3907)
#define BUI_RESOURCE_TAIL (*(volatile unsigned int *)0x3AAD)
#define BUI_IMAGE_URL   (*(volatile unsigned int  *)0x3AB3)
#define BUI_IMAGE_REL   (*(volatile unsigned char *)0x3AB5)
#define BUI_IMAGE_LEN   (*(volatile unsigned int  *)0x3AB6)
#define BUI_IMAGE_WB    (*(volatile unsigned char *)0x3AB9)
#define BUI_IMAGE_H     (*(volatile unsigned char *)0x3ABA)
#define BUI_IMAGE_READY (*(volatile unsigned char *)0x3ABB)
#define BUI_IMAGE_FAILED (*(volatile unsigned int *)0x3ABD)
#define BUI_CACHE_PAGE  (*(volatile unsigned char *)0x3ABF)
#define BUI_HIST_COUNT  (*(volatile unsigned char *)0x3AC0)
#define BUI_VIEW_TOP    (*(volatile unsigned char *)0x3AC1)
#define BUI_LINE_SIZE   (*(volatile unsigned char *)0x3AC2)
#define BUI_VIEW_ROWS   (*(volatile unsigned char *)0x3AC3)
#define BUI_TEXT_X      (*(volatile unsigned char *)0x3AC4)
#define BUI_CONTENT_Y   (*(volatile unsigned char *)0x3AC5)
#define BUI_SCREEN_COLS (*(volatile unsigned char *)0x3AC6)
#define BUI_IMAGE_SCAN  (*(volatile unsigned char *)0x3AC7)
#define BUI_IMAGE_PAGE  (*(volatile unsigned char *)0x3ADC)
#define BUI_IMAGE_MODE  (*(volatile unsigned char *)0x3ADD)
#define BUI_IMAGE_STRIDE (*(volatile unsigned char *)0x3ADE)
#define BUI_LINK_ACTIVE (*(volatile unsigned char *)0x3AB1)
#define BUI_LINK_IMAGE  (*(volatile unsigned char *)0x3AB2)
#define BUI_FORM_VALUE  ((char *)0x3A18)
#define BUI_FORM_ROW    (*(volatile unsigned char *)0x3AA9)

#define BROWSER_LINE    ((char *)0x3300)
#define BROWSER_LINK    ((char *)0x34F0)
#define BROWSER_PENDING ((char *)0x36C2)
#define BROWSER_FALLBACK ((char *)0x3700)
#define PIC_PAGE_K      (*(volatile unsigned char *)0x130B)
#define PIC_WB_K        (*(volatile unsigned char *)0x130C)
#define PIC_H_K         (*(volatile unsigned int  *)0x130D)
#define PIC_OFF_K       (*(volatile unsigned char *)0x130F)
#define PIC_PAGE2_K     (*(volatile unsigned char *)0x1348)
#ifdef GB_MSX2
#define PIC_PAGE3_K     (*(volatile unsigned char *)0x1291)
#define PIC_PAGE4_K     (*(volatile unsigned char *)0x1292)
#define PIC_MODE_K      (*(volatile unsigned char *)0x1293)
#define PIC_STRIDE_K    (*(volatile unsigned int  *)0x1294)
#endif
#define FS_SAVE_LEN_K   (*(volatile unsigned int  *)0x14FD)

#define LINK_MAX        BROWSER_LINK_MAX
#define IMAGE_MARK      BROWSER_IMAGE_MARK
#define IMAGE_CONT_MARK BROWSER_IMAGE_CONT_MARK
#define FORM_MARK       BROWSER_FORM_MARK
#define FORM_CONT_MARK  BROWSER_FORM_CONT_MARK
#define LINK_MARK       BROWSER_LINK_MARK
#define LINK_RAW_MARK   BROWSER_LINK_RAW_MARK
#define TABLE_MARK      BROWSER_TABLE_MARK
#define TABLE_CONT_MARK BROWSER_TABLE_CONT_MARK
#define INVALID_OFFSET  BROWSER_INVALID_OFFSET
#define CACHE_DATA_END  0x27D0
#define CACHE_LINES     208
#define FALLBACK_LINES  7
#define IMAGE_SLOT_OFS  0x30F2
#define IMAGE_SLOT_SIZE 3854
#define IMAGE_PAGE_SIZE 0x4000
#define IMAGE_HEADER_SIZE 14
#define IMAGE_ROWS      BROWSER_IMAGE_ROWS
#define URL_Y           26
#define URL_H           13
#define CONTENT_Y       BUI_CONTENT_Y
#define TEXT_X          BUI_TEXT_X
#define SCROLL_X        1
#define SCROLL_W        3
#define FORM_BUTTON_W   14
#define FORM_H          16
#define TABLE_GRID_MAX  4

static unsigned char bank_read(unsigned int off, char *dst, unsigned int len)
{
    PIC_PAGE_K = BUI_CACHE_PAGE;
    PIC_PAGE2_K = 0;
    gb_pic_edit_buf = (unsigned int)dst;
    gb_pic_edit_off = off;
    FS_SAVE_LEN_K = len;
    return gb_pic_edit(GB_PICEDIT_CHUNK);
}

static unsigned char image_page(unsigned int off)
{
    if (!BUI_IMAGE_PAGE) return BUI_CACHE_PAGE;
    if (off >= 0x4000 && BUI_IMAGE_PAGE2) return BUI_IMAGE_PAGE2;
    return BUI_IMAGE_PAGE;
}

static unsigned int image_bank_off(unsigned int off)
{
    return BUI_IMAGE_PAGE ? (unsigned int)(off & 0x3FFF) :
                            (unsigned int)(IMAGE_SLOT_OFS + off);
}

static unsigned int image_limit(void)
{
    return BUI_IMAGE_PAGE2 ? (unsigned int)(IMAGE_PAGE_SIZE * 2U) :
           (BUI_IMAGE_PAGE ? IMAGE_PAGE_SIZE : IMAGE_SLOT_SIZE);
}

static unsigned char image_bank_read(unsigned int off, char *dst,
                                     unsigned int len)
{
    unsigned int absolute = (unsigned int)(BUI_IMAGE_DATA_OFF + off);
    PIC_PAGE_K = image_page(absolute);
    PIC_PAGE2_K = 0;
#ifdef GB_MSX2
    PIC_PAGE3_K = PIC_PAGE4_K = 0;
#endif
    gb_pic_edit_buf = (unsigned int)dst;
    gb_pic_edit_off = image_bank_off(absolute);
    FS_SAVE_LEN_K = len;
    return gb_pic_edit(GB_PICEDIT_CHUNK);
}

static unsigned int meta_get(unsigned char entry, unsigned char field)
{
    unsigned char pos = (unsigned char)(entry * BROWSER_IMAGE_CACHE_ENTRY_SIZE + field);
    return BUI_IMAGE_CACHE_META[pos] |
        ((unsigned int)BUI_IMAGE_CACHE_META[(unsigned char)(pos + 1)] << 8);
}

static void meta_put(unsigned char entry, unsigned char field, unsigned int value)
{
    unsigned char pos = (unsigned char)(entry * BROWSER_IMAGE_CACHE_ENTRY_SIZE + field);
    BUI_IMAGE_CACHE_META[pos] = (unsigned char)value;
    BUI_IMAGE_CACHE_META[(unsigned char)(pos + 1)] = (unsigned char)(value >> 8);
}

static void invalidate_range(unsigned int off, unsigned int len)
{
    unsigned char i;
    unsigned int item_off, item_len, end = (unsigned int)(off + len);
    for (i = 0; i < BROWSER_IMAGE_CACHE_MAX; i++) {
        if (meta_get(i, BROWSER_IMAGE_CACHE_KEY) == INVALID_OFFSET) continue;
        item_off = meta_get(i, BROWSER_IMAGE_CACHE_OFF);
        item_len = meta_get(i, BROWSER_IMAGE_CACHE_LEN);
        if (item_off < end && off < item_off + item_len)
            meta_put(i, BROWSER_IMAGE_CACHE_KEY, INVALID_OFFSET);
    }
}

static unsigned char reserve_image(unsigned int expected)
{
    unsigned int off, limit = image_limit();
    if (!expected || expected > limit) return 0;
    off = BUI_IMAGE_CACHE_TAIL;
    if (BUI_IMAGE_PAGE && (off & 0x3FFF) + expected > IMAGE_PAGE_SIZE)
        off = (unsigned int)((off + 0x3FFF) & 0xC000);
    if (off + expected > limit) off = 0;
    invalidate_range(off, expected);
    BUI_IMAGE_DATA_OFF = off;
    BUI_IMAGE_EXPECTED = expected;
    BUI_IMAGE_CACHE_TAIL = (unsigned int)(off + expected);
    if (BUI_IMAGE_CACHE_TAIL == limit) BUI_IMAGE_CACHE_TAIL = 0;
    return 1;
}

static void remember_image(void)
{
    unsigned char i, entry = BUI_IMAGE_CACHE_NEXT;
    for (i = 0; i < BROWSER_IMAGE_CACHE_MAX; i++) {
        entry = (unsigned char)((BUI_IMAGE_CACHE_NEXT + i) &
            (BROWSER_IMAGE_CACHE_MAX - 1));
        if (meta_get(entry, BROWSER_IMAGE_CACHE_KEY) == INVALID_OFFSET) break;
    }
    meta_put(entry, BROWSER_IMAGE_CACHE_OFF, BUI_IMAGE_DATA_OFF);
    meta_put(entry, BROWSER_IMAGE_CACHE_LEN, BUI_IMAGE_LEN);
    meta_put(entry, BROWSER_IMAGE_CACHE_KEY, BUI_IMAGE_URL);
    BUI_IMAGE_CACHE_NEXT = (unsigned char)((entry + 1) &
        (BROWSER_IMAGE_CACHE_MAX - 1));
}

static unsigned char read_line(unsigned char rel)
{
    unsigned char i, slot;
    if (!BUI_CACHE_PAGE) {
        slot = (unsigned char)(BUI_HIST_START + rel);
        if (slot >= FALLBACK_LINES) slot = (unsigned char)(slot - FALLBACK_LINES);
        for (i = 0; i < BUI_LINE_SIZE; i++)
            BROWSER_LINE[i] = BROWSER_FALLBACK[(unsigned int)slot * BUI_LINE_SIZE + i];
        return 1;
    }
    return bank_read((unsigned int)rel * BUI_LINE_SIZE,
                     BROWSER_LINE, BUI_LINE_SIZE);
}

static unsigned char load_resource(unsigned int off)
{
    if (off < CACHE_DATA_END || off >= BUI_RESOURCE_TAIL) return 0;
    if (!bank_read(off, BROWSER_LINK, LINK_MAX + 1)) return 0;
    BROWSER_LINK[LINK_MAX] = 0;
    return 1;
}

static unsigned char begin_image(void)
{
    unsigned int expected = IMAGE_HEADER_SIZE;
    unsigned char mode, width, height, stride;
    if (BUI_STAGE_LEN < 10 || BUI_STAGE[0] != 'G' || BUI_STAGE[1] != 'B' ||
        BUI_STAGE[2] != 'P' || BUI_STAGE[3] != 'C' || BUI_STAGE[4] != 2 ||
        BUI_STAGE[7] || BUI_STAGE[9]) return 0;
    mode = (unsigned char)BUI_STAGE[5];
    width = (unsigned char)BUI_STAGE[6];
    height = (unsigned char)BUI_STAGE[8];
    if (!width || width > 160 || !height || height > 96) return 0;
    if (mode == 1) stride = (unsigned char)((width + 3) >> 2);
#ifdef GB_MSX2
    else if (mode == 7 && BUI_IMAGE_PAGE && !(width & 3))
        stride = (unsigned char)(width >> 1);
#endif
    else return 0;
    while (height--) expected += stride;
    return reserve_image(expected);
}

static unsigned char flush_image(void)
{
    unsigned int next;
    if (!BUI_STAGE_LEN) return 1;
    if (!BUI_IMAGE_EXPECTED && !begin_image()) return 0;
    next = BUI_IMAGE_LEN + BUI_STAGE_LEN;
    if (next > BUI_IMAGE_EXPECTED) return 0;
    PIC_PAGE_K = image_page(BUI_IMAGE_DATA_OFF);
    PIC_PAGE2_K = 0;
#ifdef GB_MSX2
    PIC_PAGE3_K = PIC_PAGE4_K = 0;
#endif
    gb_pic_edit_buf = (unsigned int)BUI_STAGE;
    gb_pic_edit_off = image_bank_off((unsigned int)(BUI_IMAGE_DATA_OFF +
                                                    BUI_IMAGE_LEN));
    FS_SAVE_LEN_K = BUI_STAGE_LEN;
    if (!gb_pic_edit(GB_PICEDIT_WRITE)) return 0;
    BUI_IMAGE_LEN = next;
    BUI_STAGE_LEN = 0;
    return 1;
}

static unsigned char load_image_info(void)
{
    unsigned int expected = IMAGE_HEADER_SIZE;
    unsigned char mode, width, rows;
    if (BUI_IMAGE_LEN < IMAGE_HEADER_SIZE ||
        !image_bank_read(0, BROWSER_LINE, IMAGE_HEADER_SIZE)) return 0;
    if (BROWSER_LINE[0] != 'G' || BROWSER_LINE[1] != 'B' ||
        BROWSER_LINE[2] != 'P' || BROWSER_LINE[3] != 'C' ||
        BROWSER_LINE[4] != 2 || BROWSER_LINE[7] || BROWSER_LINE[9]) return 0;
    mode = (unsigned char)BROWSER_LINE[5];
    width = (unsigned char)BROWSER_LINE[6];
    BUI_IMAGE_H = (unsigned char)BROWSER_LINE[8];
    if (!width || width > 160 || !BUI_IMAGE_H || BUI_IMAGE_H > 96) return 0;
    if (mode == 1) {
        BUI_IMAGE_WB = (unsigned char)((width + 3) >> 2);
        BUI_IMAGE_STRIDE = BUI_IMAGE_WB;
    }
#ifdef GB_MSX2
    else if (mode == 7 && BUI_IMAGE_PAGE && !(width & 3)) {
        BUI_IMAGE_WB = (unsigned char)(width >> 2);
        BUI_IMAGE_STRIDE = (unsigned char)(width >> 1);
    }
#endif
    else return 0;
    rows = BUI_IMAGE_H;
    while (rows--) expected += BUI_IMAGE_STRIDE;
    BUI_IMAGE_MODE = mode;
    return (unsigned char)(expected == BUI_IMAGE_LEN);
}

static unsigned char validate_image(void)
{
    if (!flush_image() || BUI_IMAGE_LEN != BUI_IMAGE_EXPECTED ||
        !load_image_info()) return 0;
    remember_image();
    return 1;
}

static unsigned char recall_image(unsigned int key)
{
    unsigned char i;
    for (i = 0; i < BROWSER_IMAGE_CACHE_MAX; i++) {
        if (meta_get(i, BROWSER_IMAGE_CACHE_KEY) != key) continue;
        BUI_IMAGE_DATA_OFF = meta_get(i, BROWSER_IMAGE_CACHE_OFF);
        BUI_IMAGE_LEN = meta_get(i, BROWSER_IMAGE_CACHE_LEN);
        BUI_IMAGE_EXPECTED = BUI_IMAGE_LEN;
        BUI_IMAGE_MODE = BUI_IMAGE_STRIDE = 0;
        if (load_image_info()) {
            BUI_IMAGE_READY = 1;
            return 1;
        }
        meta_put(i, BROWSER_IMAGE_CACHE_KEY, INVALID_OFFSET);
        return 0;
    }
    return 0;
}

static void table_put_offset(unsigned char cell, unsigned char field,
                             unsigned int value)
{
    unsigned char pos = (unsigned char)(cell * BROWSER_TABLE_CELL_SIZE + field);
    BUI_TABLE_ROW_BUF[pos] = (char)value;
    BUI_TABLE_ROW_BUF[pos + 1] = (char)(value >> 8);
}

static unsigned int table_get_offset(unsigned char cell, unsigned char field)
{
    unsigned char pos = (unsigned char)(cell * BROWSER_TABLE_CELL_SIZE + field);
    return (unsigned char)BUI_TABLE_ROW_BUF[pos] |
           ((unsigned int)(unsigned char)BUI_TABLE_ROW_BUF[pos + 1] << 8);
}

static unsigned int store_pending(void)
{
    unsigned int off = BUI_RESOURCE_TAIL;
    if (!BUI_PENDING_LEN || off + BUI_PENDING_LEN + 1 > IMAGE_SLOT_OFS)
        return INVALID_OFFSET;
    BROWSER_PENDING[BUI_PENDING_LEN] = 0;
    PIC_PAGE_K = BUI_CACHE_PAGE;
    PIC_PAGE2_K = 0;
    gb_pic_edit_buf = (unsigned int)BROWSER_PENDING;
    gb_pic_edit_off = off;
    FS_SAVE_LEN_K = (unsigned int)(BUI_PENDING_LEN + 1);
    if (!gb_pic_edit(GB_PICEDIT_WRITE)) return INVALID_OFFSET;
    BUI_RESOURCE_TAIL = (unsigned int)(off + BUI_PENDING_LEN + 1);
    return off;
}

static unsigned char add_history(void)
{
    if (BUI_CACHE_FULL || BUI_HIST_COUNT >= CACHE_LINES) {
        BUI_CACHE_FULL = 1;
        BUI_PENDING_LEN = 0;
        return 0;
    }
    BROWSER_PENDING[BUI_PENDING_LEN] = 0;
    PIC_PAGE_K = BUI_CACHE_PAGE;
    PIC_PAGE2_K = 0;
    gb_pic_edit_buf = (unsigned int)BROWSER_PENDING;
    gb_pic_edit_off = (unsigned int)BUI_HIST_COUNT * BUI_LINE_SIZE;
    FS_SAVE_LEN_K = (unsigned int)(BUI_PENDING_LEN + 1);
    if (!gb_pic_edit(GB_PICEDIT_WRITE)) {
        BUI_CACHE_FULL = 1;
        BUI_PENDING_LEN = 0;
        return 0;
    }
    BUI_HIST_COUNT++;
    BUI_PENDING_LEN = 0;
    return 1;
}

static void table_reset_row(void)
{
    BUI_TABLE_ROW_CELLS = 0;
}

static void table_emit_row(void)
{
    unsigned char start = 0, cell, count, i, rows, parent, have_image;
    if (!BUI_TABLE_ROW_CELLS) return;
    if (!BUI_TABLE_GRID_COLS) {
        BUI_TABLE_GRID_COLS = BUI_TABLE_ROW_CELLS;
        if (BUI_TABLE_GRID_COLS > TABLE_GRID_MAX)
            BUI_TABLE_GRID_COLS = TABLE_GRID_MAX;
    }
    while (start < BUI_TABLE_ROW_CELLS && !BUI_CACHE_FULL) {
        count = (unsigned char)(BUI_TABLE_ROW_CELLS - start);
        if (count > BUI_TABLE_GRID_COLS) count = BUI_TABLE_GRID_COLS;
        have_image = 0;
        BROWSER_PENDING[0] = TABLE_MARK;
        BROWSER_PENDING[BROWSER_TABLE_GRID] = (char)BUI_TABLE_GRID_COLS;
        BROWSER_PENDING[BROWSER_TABLE_COUNT] = (char)count;
        for (cell = 0; cell < count; cell++) {
            unsigned char source = (unsigned char)(start + cell);
            unsigned char dst = (unsigned char)(BROWSER_TABLE_HEADER +
                cell * BROWSER_TABLE_CELL_SIZE);
            unsigned char src = (unsigned char)(source * BROWSER_TABLE_CELL_SIZE);
            for (i = 0; i < BROWSER_TABLE_CELL_SIZE; i++)
                BROWSER_PENDING[dst + i] = BUI_TABLE_ROW_BUF[src + i];
            if (table_get_offset(source, BROWSER_TABLE_IMAGE) != INVALID_OFFSET)
                have_image = 1;
        }
        rows = have_image ? IMAGE_ROWS : BROWSER_TABLE_TEXT_ROWS;
        BROWSER_PENDING[BROWSER_TABLE_ROWS] = (char)rows;
        BUI_PENDING_LEN = (unsigned char)(BROWSER_TABLE_HEADER +
            count * BROWSER_TABLE_CELL_SIZE);
        parent = BUI_HIST_COUNT;
        if (!add_history()) break;
        for (i = 1; i < rows && !BUI_CACHE_FULL; i++) {
            BROWSER_PENDING[0] = TABLE_CONT_MARK;
            BROWSER_PENDING[1] = (char)parent;
            BUI_PENDING_LEN = 2;
            (void)add_history();
        }
        start = (unsigned char)(start + count);
    }
    table_reset_row();
}

static void table_close_cell(void)
{
    unsigned int text_off = INVALID_OFFSET;
    unsigned char cell;
    if (!(BUI_TABLE_STATE & BUI_TABLE_IN_CELL)) return;
    while (BUI_PENDING_LEN && BROWSER_PENDING[BUI_PENDING_LEN - 1] == ' ')
        BUI_PENDING_LEN--;
    if (BUI_PENDING_LEN) text_off = store_pending();
    BUI_PENDING_LEN = 0;
    cell = BUI_TABLE_ROW_CELLS;
    if (cell < BROWSER_TABLE_MAX_CELLS) {
        table_put_offset(cell, BROWSER_TABLE_IMAGE, BUI_TABLE_CELL_IMAGE);
        table_put_offset(cell, BROWSER_TABLE_LINK, BUI_TABLE_CELL_LINK);
        table_put_offset(cell, BROWSER_TABLE_TEXT, text_off);
        BUI_TABLE_ROW_CELLS++;
    }
    BUI_TABLE_CELL_IMAGE = BUI_TABLE_CELL_LINK = INVALID_OFFSET;
    BUI_LINK_ACTIVE = BUI_LINK_IMAGE = 0;
    BUI_TABLE_STATE &= (unsigned char)~BUI_TABLE_IN_CELL;
}

static void table_close_row(void)
{
    if (!(BUI_TABLE_STATE & BUI_TABLE_IN_ROW)) return;
    table_close_cell();
    table_emit_row();
    BUI_TABLE_STATE &= (unsigned char)~BUI_TABLE_IN_ROW;
}

static void table_event(void)
{
    unsigned char kind = UI_N;
    if (kind == GB_HTML_TABLE_OPEN) {
        if (BUI_TABLE_DEPTH) { BUI_TABLE_DEPTH++; return; }
        BUI_TABLE_DEPTH = 1;
        BUI_TABLE_STATE = BUI_TABLE_ACTIVE;
        BUI_TABLE_GRID_COLS = 0;
        table_reset_row();
        return;
    }
    if (!BUI_TABLE_DEPTH) return;
    if (kind == GB_HTML_TABLE_CLOSE) {
        if (BUI_TABLE_DEPTH > 1) { BUI_TABLE_DEPTH--; return; }
        table_close_row();
        BUI_TABLE_DEPTH = 0;
        BUI_TABLE_STATE = 0;
        return;
    }
    if (BUI_TABLE_DEPTH > 1) return;
    if (kind == GB_HTML_ROW_OPEN) {
        table_close_row();
        table_reset_row();
        BUI_TABLE_STATE |= BUI_TABLE_IN_ROW;
    } else if (kind == GB_HTML_ROW_CLOSE) {
        table_close_row();
    } else if (kind == GB_HTML_CELL_OPEN || kind == GB_HTML_HEADER_OPEN) {
        if (!(BUI_TABLE_STATE & BUI_TABLE_IN_ROW)) {
            table_reset_row();
            BUI_TABLE_STATE |= BUI_TABLE_IN_ROW;
        }
        table_close_cell();
        if (BUI_TABLE_ROW_CELLS >= BROWSER_TABLE_MAX_CELLS) table_emit_row();
        BUI_PENDING_LEN = 0;
        BUI_TABLE_CELL_IMAGE = BUI_TABLE_CELL_LINK = INVALID_OFFSET;
        BUI_TABLE_STATE |= BUI_TABLE_IN_CELL;
    } else if (kind == GB_HTML_CELL_CLOSE || kind == GB_HTML_HEADER_CLOSE) {
        table_close_cell();
    }
}

static unsigned int line_offset(unsigned char pos)
{
    return (unsigned char)BROWSER_LINE[pos] |
           ((unsigned int)(unsigned char)BROWSER_LINE[pos + 1] << 8);
}

static unsigned char table_geometry(unsigned char *start, unsigned char *cell_w)
{
    unsigned char cols = (unsigned char)BROWSER_LINE[BROWSER_TABLE_GRID];
    unsigned char available, width;
    if (!cols || cols > BROWSER_TABLE_MAX_CELLS) return 0;
    available = (unsigned char)(BUI_SCREEN_COLS - BUI_TEXT_X);
    *cell_w = BROWSER_TABLE_CELL_W;
    if ((unsigned int)*cell_w * cols > available)
        *cell_w = (unsigned char)(available / cols);
    if (*cell_w < 3) return 0;
    width = (unsigned char)(*cell_w * cols);
    *start = (unsigned char)(BUI_TEXT_X + (available - width) / 2);
    return 1;
}

static void draw_visible(void);

static unsigned char select_image(unsigned char parent, unsigned char cell,
                                  unsigned int off)
{
    BUI_IMAGE_URL = off;
    BUI_IMAGE_REL = parent;
    BUI_IMAGE_CELL = cell;
    if (recall_image(off)) {
        draw_visible();
        BUI_IMAGE_SCAN = 1;
        BUI_IMAGE_RETRY = 0;
        return 1;
    }
    BUI_IMAGE_LEN = BUI_STAGE_LEN = 0;
    BUI_IMAGE_DATA_OFF = BUI_IMAGE_EXPECTED = 0;
    BUI_IMAGE_MODE = BUI_IMAGE_STRIDE = 0;
    BUI_IMAGE_READY = 0;
    return 0;
}

static unsigned char find_visible(void)
{
    unsigned char rel = BUI_IMAGE_SCAN_REL, parent, mark, rows, count, cell, pos;
    unsigned char end = (unsigned char)(BUI_VIEW_TOP + BUI_VIEW_ROWS);
    unsigned int off;
    while (rel < end && rel < BUI_HIST_COUNT) {
        if (!read_line(rel)) return 0;
        mark = (unsigned char)BROWSER_LINE[0];
        parent = rel;
        if (mark == IMAGE_CONT_MARK || mark == TABLE_CONT_MARK) {
            parent = (unsigned char)BROWSER_LINE[1];
            if (!read_line(parent)) return 0;
            mark = (unsigned char)BROWSER_LINE[0];
        }
        if (mark == TABLE_MARK) {
            count = (unsigned char)BROWSER_LINE[BROWSER_TABLE_COUNT];
            rows = (unsigned char)BROWSER_LINE[BROWSER_TABLE_ROWS];
            if (!count || count > BROWSER_TABLE_MAX_CELLS || !rows) {
                rel++;
                BUI_IMAGE_SCAN_CELL = 0;
                continue;
            }
            cell = rel == parent ? BUI_IMAGE_SCAN_CELL : 0;
            while (cell < count) {
                pos = (unsigned char)(BROWSER_TABLE_HEADER +
                    cell * BROWSER_TABLE_CELL_SIZE + BROWSER_TABLE_IMAGE);
                off = line_offset(pos);
                cell++;
                if (cell < count) {
                    BUI_IMAGE_SCAN_REL = parent;
                    BUI_IMAGE_SCAN_CELL = cell;
                } else {
                    BUI_IMAGE_SCAN_REL = (unsigned char)(parent + rows);
                    BUI_IMAGE_SCAN_CELL = 0;
                }
                if (off == INVALID_OFFSET || off == BUI_IMAGE_FAILED) continue;
                if (off & BUI_DOX_RESOURCE_FLAG) {
                    if (select_image(parent, (unsigned char)(cell - 1), off))
                        return 3;
                    return 2;
                }
                if (!load_resource(off)) continue;
                if (select_image(parent, (unsigned char)(cell - 1), off))
                    return 3;
                return 1;
            }
            rel = (unsigned char)(parent + rows);
            BUI_IMAGE_SCAN_REL = rel;
            BUI_IMAGE_SCAN_CELL = 0;
            continue;
        }
        if (mark == IMAGE_MARK) {
            off = line_offset(1);
            rel = (unsigned char)(parent + IMAGE_ROWS);
            BUI_IMAGE_SCAN_REL = rel;
            BUI_IMAGE_SCAN_CELL = 0;
            if (off == BUI_IMAGE_FAILED) continue;
            if (off & BUI_DOX_RESOURCE_FLAG) {
                if (select_image(parent, BROWSER_IMAGE_NO_CELL, off)) return 3;
                return 2;
            }
            if (!load_resource(off)) continue;
            if (select_image(parent, BROWSER_IMAGE_NO_CELL, off)) return 3;
            return 1;
        }
        rel++;
        BUI_IMAGE_SCAN_REL = rel;
        BUI_IMAGE_SCAN_CELL = 0;
    }
    return 0;
}

static void draw_visible(void)
{
    unsigned char first, last, block, rows, x, y, parent_rows;
    unsigned char start = 0, cell_w = 0, draw_w, crop, image_rows, pad_rows;
    unsigned int src;
    if (!BUI_IMAGE_READY) return;
    parent_rows = IMAGE_ROWS;
    pad_rows = 0;
    if (BUI_IMAGE_CELL != BROWSER_IMAGE_NO_CELL) {
        if (!read_line(BUI_IMAGE_REL) ||
            (unsigned char)BROWSER_LINE[0] != TABLE_MARK ||
            BUI_IMAGE_CELL >= (unsigned char)BROWSER_LINE[BROWSER_TABLE_COUNT] ||
            !table_geometry(&start, &cell_w)) return;
        parent_rows = (unsigned char)BROWSER_LINE[BROWSER_TABLE_ROWS];
        image_rows = (unsigned char)((BUI_IMAGE_H + 7) >> 3);
        if (image_rows < parent_rows)
            pad_rows = (unsigned char)((parent_rows - image_rows) / 2);
    }
    first = (unsigned char)(BUI_IMAGE_REL + pad_rows);
    if (first < BUI_VIEW_TOP) first = BUI_VIEW_TOP;
    last = (unsigned char)(BUI_IMAGE_REL + parent_rows);
    if (last > BUI_VIEW_TOP + BUI_VIEW_ROWS)
        last = (unsigned char)(BUI_VIEW_TOP + BUI_VIEW_ROWS);
    if (first >= last) return;
    block = (unsigned char)((first - BUI_IMAGE_REL - pad_rows) * 8);
    if (block >= BUI_IMAGE_H) return;
    rows = (unsigned char)((last - first) * 8);
    if (rows > BUI_IMAGE_H - block) rows = (unsigned char)(BUI_IMAGE_H - block);
    draw_w = BUI_IMAGE_WB;
    crop = 0;
    if (BUI_IMAGE_CELL == BROWSER_IMAGE_NO_CELL) {
        x = (unsigned char)(BUI_TEXT_X +
            (BUI_SCREEN_COLS - BUI_TEXT_X - draw_w) / 2);
    } else {
        if (draw_w > cell_w - 2) {
            crop = (unsigned char)((draw_w - (cell_w - 2)) / 2);
            draw_w = (unsigned char)(cell_w - 2);
        }
        x = (unsigned char)(start + BUI_IMAGE_CELL * cell_w + 1 +
            (cell_w - 2 - draw_w) / 2);
    }
    y = (unsigned char)(BUI_CONTENT_Y + (first - BUI_VIEW_TOP) * 8);
    src = BUI_IMAGE_DATA_OFF + IMAGE_HEADER_SIZE +
          (unsigned int)block * BUI_IMAGE_STRIDE +
          (BUI_IMAGE_MODE == 7 ? (unsigned int)crop * 2 : crop);
    PIC_PAGE_K = image_page(src);
    src = image_bank_off(src);
    PIC_PAGE2_K = 0;
#ifdef GB_MSX2
    PIC_PAGE3_K = PIC_PAGE4_K = 0;
    PIC_MODE_K = BUI_IMAGE_MODE;
    PIC_STRIDE_K = BUI_IMAGE_STRIDE;
#endif
    PIC_WB_K = BUI_IMAGE_WB;
    PIC_H_K = BUI_IMAGE_H;
    PIC_OFF_K = IMAGE_HEADER_SIZE;
    gb_pic_blit(x, y, draw_w, rows, src);
}

static unsigned char string_len(const char *s)
{
    unsigned char n = 0;
    while (s[n] && n < LINK_MAX) n++;
    return n;
}

static void draw_form(unsigned char y)
{
    unsigned char button_x = (unsigned char)(BUI_SCREEN_COLS - FORM_BUTTON_W);
    unsigned char field_w = (unsigned char)(button_x - BUI_TEXT_X - 1);
    unsigned char len = string_len(BUI_FORM_VALUE);
    unsigned char max = (unsigned char)(((field_w - 4) * 2) / 3);
    unsigned char bottom = (unsigned char)(BUI_CONTENT_Y + BUI_VIEW_ROWS * 8);
    unsigned char h = y + FORM_H <= bottom ? FORM_H : (unsigned char)(bottom - y);
    unsigned char text_y = h == FORM_H ? (unsigned char)(y + 4) : y;
    char *text = BUI_FORM_VALUE;
    if (len > max) text += len - max;
    gb_fill(BUI_TEXT_X, y, field_w, h, 1);
    gb_frame(BUI_TEXT_X, y, field_w, h, (UI_N & 0x7F) == 2 ? 3 : 2);
    gb_textbw((unsigned char)(BUI_TEXT_X + 2), text_y, text);
    if ((UI_N & 0x80) && (UI_N & 0x7F) == 2)
        gb_fill((unsigned char)(BUI_TEXT_X + 2 +
            ((unsigned int)string_len(text) * 3) / 2),
            (unsigned char)(text_y + 1), 1, 6, 3);
    gb_fill(button_x, y, FORM_BUTTON_W, h, 0);
    gb_textrev((unsigned char)(button_x + 2), text_y, "Search");
}

static void draw_table_band(unsigned char rel, unsigned char y)
{
    unsigned char parent = rel, sub, start, cell_w, cols, count;
    unsigned char cell, pos, x, width, max_chars, label_row, mark;
    unsigned int text_off, link_off;
    mark = (unsigned char)BROWSER_LINE[0];
    if (mark == TABLE_CONT_MARK) {
        parent = (unsigned char)BROWSER_LINE[1];
        if (!read_line(parent)) return;
    }
    if ((unsigned char)BROWSER_LINE[0] != TABLE_MARK ||
        !table_geometry(&start, &cell_w)) return;
    sub = (unsigned char)(rel - parent);
    cols = (unsigned char)BROWSER_LINE[BROWSER_TABLE_GRID];
    count = (unsigned char)BROWSER_LINE[BROWSER_TABLE_COUNT];
    if (!count || count > cols ||
        !(unsigned char)BROWSER_LINE[BROWSER_TABLE_ROWS]) return;
    width = (unsigned char)(cols * cell_w);
    if (!sub) gb_fill(start, y, width, 1, 2);
    if (sub + 1 == (unsigned char)BROWSER_LINE[BROWSER_TABLE_ROWS])
        gb_fill(start, (unsigned char)(y + 7), width, 1, 2);
    for (cell = 0; cell <= cols; cell++)
        gb_fill((unsigned char)(start + cell * cell_w), y, 1, 8, 2);
    label_row = (unsigned char)(((unsigned char)BROWSER_LINE[BROWSER_TABLE_ROWS] - 1) / 2);
    if (sub != label_row) return;
    max_chars = (unsigned char)(((cell_w - 2) * 2) / 3);
    if (max_chars > LINK_MAX) max_chars = LINK_MAX;
    for (cell = 0; cell < count; cell++) {
        pos = (unsigned char)(BROWSER_TABLE_HEADER +
            cell * BROWSER_TABLE_CELL_SIZE);
        text_off = line_offset((unsigned char)(pos + BROWSER_TABLE_TEXT));
        link_off = line_offset((unsigned char)(pos + BROWSER_TABLE_LINK));
        if (text_off == INVALID_OFFSET || !load_resource(text_off)) continue;
        BROWSER_LINK[max_chars] = 0;
        width = (unsigned char)(((unsigned int)string_len(BROWSER_LINK) * 3 + 1) / 2);
        x = (unsigned char)(start + cell * cell_w + (cell_w - width) / 2);
        gb_textbw(x, y, BROWSER_LINK);
        if (link_off != INVALID_OFFSET)
            gb_fill(x, (unsigned char)(y + 7), width, 1, 3);
    }
}

static void draw_content(void)
{
    unsigned char row, rel, y, width, mark;
    for (row = 0; row < BUI_VIEW_ROWS; row++) {
        rel = (unsigned char)(BUI_VIEW_TOP + row);
        y = (unsigned char)(BUI_CONTENT_Y + row * 8);
        gb_fill((unsigned char)(BUI_TEXT_X - 1), y,
                (unsigned char)(BUI_SCREEN_COLS - BUI_TEXT_X), 8, 1);
        if (rel >= BUI_HIST_COUNT || !read_line(rel)) continue;
        mark = (unsigned char)BROWSER_LINE[0];
        if (mark == IMAGE_MARK) {
            if (!BUI_IMAGE_READY || line_offset(1) != BUI_IMAGE_URL)
                gb_textbw(BUI_TEXT_X, y, BROWSER_LINE[3] ? BROWSER_LINE + 3 : "[Image]");
        } else if (mark == IMAGE_CONT_MARK || mark == FORM_CONT_MARK) {
            /* Reserved vertical content. */
        } else if (mark == FORM_MARK) {
            if ((UI_N & 0x7F) == 2) BUI_FORM_ROW = y;
            draw_form(y);
        } else if (mark == LINK_MARK || mark == LINK_RAW_MARK) {
            char *text = BROWSER_LINE + (mark == LINK_MARK ? 3 : 1);
            width = (unsigned char)(((unsigned int)string_len(text) * 3 + 1) / 2);
            if (width > BUI_SCREEN_COLS - BUI_TEXT_X)
                width = (unsigned char)(BUI_SCREEN_COLS - BUI_TEXT_X);
            gb_fill(BUI_TEXT_X, y, width, 8, 0);
            gb_text(BUI_TEXT_X, y, text);
            gb_fill(BUI_TEXT_X, (unsigned char)(y + 7), width, 1, 3);
        } else if (mark == TABLE_MARK || mark == TABLE_CONT_MARK) {
            draw_table_band(rel, y);
        } else gb_textbw(BUI_TEXT_X, y, BROWSER_LINE);
    }
}

static unsigned char table_link_at(unsigned char rel)
{
    unsigned char parent, mark, start, cell_w, cols, count, cell, pos;
    unsigned int off;
    mark = (unsigned char)BROWSER_LINE[0];
    parent = rel;
    if (mark == TABLE_CONT_MARK) {
        parent = (unsigned char)BROWSER_LINE[1];
        if (!read_line(parent)) return 0;
        mark = (unsigned char)BROWSER_LINE[0];
    }
    if (mark != TABLE_MARK || !table_geometry(&start, &cell_w)) return 0;
    cols = (unsigned char)BROWSER_LINE[BROWSER_TABLE_GRID];
    count = (unsigned char)BROWSER_LINE[BROWSER_TABLE_COUNT];
    if (BUI_TABLE_CLICK_X < start ||
        BUI_TABLE_CLICK_X >= (unsigned char)(start + cols * cell_w)) return 0;
    cell = (unsigned char)((BUI_TABLE_CLICK_X - start) / cell_w);
    if (cell >= count) return 0;
    pos = (unsigned char)(BROWSER_TABLE_HEADER +
        cell * BROWSER_TABLE_CELL_SIZE + BROWSER_TABLE_LINK);
    off = line_offset(pos);
    return (unsigned char)(off != INVALID_OFFSET && load_resource(off));
}

static void hit_content(void)
{
    unsigned char x = BUI_TABLE_CLICK_X, y = BUI_TABLE_CLICK_Y;
    unsigned char rel, mark, form_y, button_x;
    unsigned int off;
    UI_N = BROWSER_HIT_NONE;
    if (y >= URL_Y && y < URL_Y + URL_H) {
        if (x >= BUI_SCREEN_COLS - 12) UI_N = BROWSER_HIT_BACK;
        else if (x >= BUI_SCREEN_COLS - 21) UI_N = BROWSER_HIT_GO;
        else if (x < 2 + BUI_SCREEN_COLS - 24) UI_N = BROWSER_HIT_URL;
        return;
    }
    if (x < SCROLL_X + SCROLL_W && y >= BUI_CONTENT_Y &&
        y < BUI_CONTENT_Y + BUI_VIEW_ROWS * 8) {
        if (BUI_HIST_COUNT > BUI_VIEW_ROWS)
            BUI_VIEW_TOP = (unsigned char)(((unsigned int)
                (y - BUI_CONTENT_Y) * (BUI_HIST_COUNT - BUI_VIEW_ROWS)) /
                (BUI_VIEW_ROWS * 8 - 1));
        UI_N = y >= BUI_CONTENT_Y + BUI_VIEW_ROWS * 4 ?
            BROWSER_HIT_SCROLL_DOWN : BROWSER_HIT_SCROLL_UP;
        return;
    }
    if (y < BUI_CONTENT_Y || y >= BUI_CONTENT_Y + BUI_VIEW_ROWS * 8) return;
    rel = (unsigned char)(BUI_VIEW_TOP + (y - BUI_CONTENT_Y) / 8);
    if (rel >= BUI_HIST_COUNT || !read_line(rel)) return;
    form_y = (unsigned char)(BUI_CONTENT_Y + ((y - BUI_CONTENT_Y) / 8) * 8);
    mark = (unsigned char)BROWSER_LINE[0];
    if (mark == FORM_CONT_MARK && rel > BUI_VIEW_TOP) {
        if (!read_line((unsigned char)(rel - 1))) return;
        if ((unsigned char)BROWSER_LINE[0] == FORM_MARK) {
            mark = FORM_MARK;
            form_y = (unsigned char)(form_y - 8);
        }
    }
    if (mark == FORM_MARK) {
        button_x = (unsigned char)(BUI_SCREEN_COLS - FORM_BUTTON_W);
        if (x >= BUI_TEXT_X && x < button_x - 1) {
            BUI_FORM_ROW = form_y;
            UI_N = BROWSER_HIT_FORM_EDIT;
        } else if (x >= button_x) UI_N = BROWSER_HIT_FORM_SUBMIT;
    } else if (mark == LINK_MARK) {
        off = line_offset(1);
        if (load_resource(off)) UI_N = BROWSER_HIT_LINK;
    } else if (mark == LINK_RAW_MARK) {
        /* Keep the raw target in the shared link buffer after the module exits. */
        unsigned char i = 0;
        while (BROWSER_LINE[i + 1] && i < LINK_MAX) {
            BROWSER_LINK[i] = BROWSER_LINE[i + 1];
            i++;
        }
        BROWSER_LINK[i] = 0;
        UI_N = BROWSER_HIT_LINK;
    } else if ((mark == TABLE_MARK || mark == TABLE_CONT_MARK) && table_link_at(rel)) {
        UI_N = BROWSER_HIT_LINK;
    }
}

void main(void)
{
    UI_RES = 0;
    if (UI_OP == 24) { draw_content(); UI_RES = 1; return; }
    if (UI_OP == 25) { hit_content(); UI_RES = 1; return; }
    if (UI_OP == 27) { draw_form(BUI_FORM_ROW); UI_RES = 1; return; }
    if (!BUI_CACHE_PAGE) return;
    if (UI_OP == 20) UI_RES = flush_image();
    else if (UI_OP == 21) UI_RES = validate_image();
    else if (UI_OP == 22) UI_RES = find_visible();
    else if (UI_OP == 23) { draw_visible(); UI_RES = 1; }
    else if (UI_OP == 26) { table_event(); UI_RES = 1; }
}
