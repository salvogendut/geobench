/* gbscroll.c - opt-in vertical scrollbar shared by GEOBENCH applications. */
#include "gb.h"
#ifdef GB_AGON
#include "agon_viewport.h"
#endif

#define UI_SURFACE 1
#define UI_ACCENT  3
#define ARROW_H    8
#define THUMB_MIN  6

static void scroll_geometry(gb_scan_t y, gb_scan_t h,
                            unsigned char pos, unsigned char total,
                            unsigned char page, gb_scan_t *ty,
                            gb_scan_t *th)
{
    unsigned char limit;
    if (!total || page >= total) {
        *ty = y;
        *th = h;
        return;
    }
    *th = (gb_scan_t)(((unsigned int)h * page) / total);
    if (*th < THUMB_MIN) *th = THUMB_MIN;
    if (*th > h) *th = h;
    limit = (unsigned char)(total - page);
    if (pos > limit) pos = limit;
    *ty = (gb_scan_t)(y + ((unsigned int)(h - *th) * pos) / limit);
}

void gb_vscroll(unsigned char x, gb_scan_t y, unsigned char w, gb_scan_t h,
                unsigned char pos, unsigned char total, unsigned char page,
                unsigned char flags)
{
    gb_scan_t ty, th;
    scroll_geometry(y, h, pos, total, page, &ty, &th);
    gb_fill(x, y, w, h, UI_SURFACE);
    if (w > 2 && th > 2)
        gb_fill((unsigned char)(x + 1), (gb_scan_t)(ty + 1),
                (unsigned char)(w - 2), (gb_scan_t)(th - 2), UI_ACCENT);
    if (flags & GB_WIDGET_ARROWS) {
        gb_fill(x, y, w, ARROW_H, UI_SURFACE);
        gb_textbw((unsigned char)(x + 1), y, GLYPH_TRI_UP);
        gb_fill(x, (gb_scan_t)(y + h - ARROW_H), w, ARROW_H, UI_SURFACE);
        gb_textbw((unsigned char)(x + 1), (gb_scan_t)(y + h - ARROW_H),
                  GLYPH_TRI_DOWN);
    }
}

unsigned char gb_vscroll_hit(unsigned char x, gb_scan_t y,
                             unsigned char w, gb_scan_t h,
                             unsigned char pos, unsigned char total,
                             unsigned char page, unsigned char mx,
                             gb_scan_t my, unsigned char flags)
{
    gb_scan_t ty, th;
    if (mx < x || mx >= x + w || my < y || my >= y + h) return GB_SCROLL_NONE;
    if (flags & GB_WIDGET_ARROWS) {
        if (my < y + ARROW_H) return GB_SCROLL_UP;
        if (my >= y + h - ARROW_H) return GB_SCROLL_DOWN;
    }
    scroll_geometry(y, h, pos, total, page, &ty, &th);
    if (my < ty) return GB_SCROLL_PAGE_UP;
    if (my >= ty + th) return GB_SCROLL_PAGE_DOWN;
    return GB_SCROLL_THUMB;
}
