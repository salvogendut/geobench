/* Shared bounded Browser record/state contract for BROWSER.APP and GBIMG.MOD. */
#ifndef GB_BROWSER_H
#define GB_BROWSER_H

#define BROWSER_LINK_MAX        47
#define BROWSER_INVALID_OFFSET  0xFFFF

#define BROWSER_LINK_MARK       1
#define BROWSER_FORM_MARK       2
#define BROWSER_FORM_CONT_MARK  3
#define BROWSER_IMAGE_MARK      4
#define BROWSER_IMAGE_CONT_MARK 5
#define BROWSER_LINK_RAW_MARK   6
#define BROWSER_TABLE_MARK      7
#define BROWSER_TABLE_CONT_MARK 8

/* One row and up to four cells fit in the Browser's fixed 49-byte record. */
#define BROWSER_TABLE_MAX_CELLS 4
#define BROWSER_TABLE_HEADER    4
#define BROWSER_TABLE_CELL_SIZE 6
#define BROWSER_TABLE_GRID      1
#define BROWSER_TABLE_COUNT     2
#define BROWSER_TABLE_ROWS      3
#define BROWSER_TABLE_IMAGE     0
#define BROWSER_TABLE_LINK      2
#define BROWSER_TABLE_TEXT      4
#define BROWSER_TABLE_ROW_SIZE \
    (BROWSER_TABLE_HEADER + BROWSER_TABLE_MAX_CELLS * BROWSER_TABLE_CELL_SIZE)

#define BROWSER_IMAGE_ROWS      12
#define BROWSER_TABLE_TEXT_ROWS 2
#define BROWSER_TABLE_CELL_W    32

/* Browser-only transient space after the seven-line no-cache ring. */
#define BUI_TABLE_ROW_BUF       ((char *)0x3857)
#define BUI_TABLE_STATE         (*(volatile unsigned char *)0x3873)
#define BUI_TABLE_GRID_COLS     (*(volatile unsigned char *)0x3874)
#define BUI_TABLE_ROW_CELLS     (*(volatile unsigned char *)0x3875)
#define BUI_TABLE_CELL_IMAGE    (*(volatile unsigned int  *)0x3877)
#define BUI_TABLE_CELL_LINK     (*(volatile unsigned int  *)0x3879)
#define BUI_TABLE_DEPTH         (*(volatile unsigned char *)0x387B)
#define BUI_PENDING_LEN         (*(volatile unsigned char *)0x387C)
#define BUI_CACHE_FULL          (*(volatile unsigned char *)0x387D)
#define BUI_HIST_START          (*(volatile unsigned char *)0x387E)

#define BUI_TABLE_ACTIVE        0x01
#define BUI_TABLE_IN_ROW        0x02
#define BUI_TABLE_IN_CELL       0x04

/* Browser/GBIMG shared scan and table-hit state. */
#define BUI_IMAGE_CELL          (*(volatile unsigned char *)0x3AE0)
#define BUI_IMAGE_SCAN_REL      (*(volatile unsigned char *)0x3AE1)
#define BUI_IMAGE_SCAN_CELL     (*(volatile unsigned char *)0x3AE2)
#define BUI_STATUS_DIRTY        (*(volatile unsigned char *)0x3AE3)
#define BUI_TABLE_CLICK_X       (*(volatile unsigned char *)0x3AE4)
#define BUI_TABLE_CLICK_Y       (*(volatile unsigned char *)0x3AE5)
#define BUI_IMAGE_RETRY         (*(volatile unsigned char *)0x3AE6)

#define BROWSER_IMAGE_NO_CELL   0xFF

#define BROWSER_HIT_NONE        0
#define BROWSER_HIT_URL         1
#define BROWSER_HIT_GO          2
#define BROWSER_HIT_BACK        3
#define BROWSER_HIT_SCROLL_UP   4
#define BROWSER_HIT_SCROLL_DOWN 5
#define BROWSER_HIT_FORM_EDIT   6
#define BROWSER_HIT_FORM_SUBMIT 7
#define BROWSER_HIT_LINK        8

/* Parser callback kinds are repeated here so BROWSER.APP can implement its
 * callback before including the single-header streaming parser. */
#define GB_HTML_TABLE_OPEN   0
#define GB_HTML_TABLE_CLOSE  1
#define GB_HTML_ROW_OPEN     2
#define GB_HTML_ROW_CLOSE    3
#define GB_HTML_CELL_OPEN    4
#define GB_HTML_CELL_CLOSE   5
#define GB_HTML_HEADER_OPEN  6
#define GB_HTML_HEADER_CLOSE 7

#endif
