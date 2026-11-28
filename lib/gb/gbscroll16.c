/* gbscroll16.c - app-linked scrollbars with 16-bit document ranges.
 *
 * Viewer pictures can be taller than 255 rows, so the compact gb_vscroll
 * byte-range API is not sufficient. Geometry remains stateless and app-owned.
 */
#include "gb.h"
#ifdef GB_AGON
#include "agon_viewport.h"
#endif

#define UI_SURFACE 1
#define UI_ACCENT  3
#define THUMB_MIN  4

/* Map value/limit onto an eight-bit track without pulling 32-bit arithmetic
 * into small applications. Low bits discarded while normalising cannot affect
 * more than one track pixel. */
static gb_scan_t scale_to_track(unsigned int value, unsigned int limit,
                                    gb_scan_t track)
{
    if (!limit || value >= limit) return track;
#ifdef GB_AGON
    if (track > 255) return (unsigned long)value * track / limit;
#endif
    while (limit > 255U) {
        value >>= 1;
        limit = (unsigned int)((limit >> 1) + (limit & 1U));
    }
    return (gb_scan_t)((value * track) / limit);
}

static void scroll_geometry(gb_scan_t track, unsigned int pos,
                            unsigned int total, unsigned int page,
                            gb_scan_t *start, gb_scan_t *length)
{
    unsigned int limit;
    if (!total || page >= total) {
        *start = 0;
        *length = track;
        return;
    }
    *length = scale_to_track(page, total, track);
    if (*length < THUMB_MIN) *length = THUMB_MIN;
    if (*length > track) *length = track;
    limit = total - page;
    if (pos > limit) pos = limit;
    *start = scale_to_track(pos, limit,
                            (gb_scan_t)(track - *length));
}

void gb_vscroll16(unsigned char x, gb_scan_t y,
                  unsigned char w, gb_scan_t h,
                  unsigned int pos, unsigned int total, unsigned int page)
{
    gb_scan_t start, length;
    scroll_geometry(h, pos, total, page, &start, &length);
    gb_fill(x, y, w, h, UI_SURFACE);
    if (w > 2 && length > 2)
        gb_fill((unsigned char)(x + 1),
                (gb_scan_t)(y + start + 1),
                (unsigned char)(w - 2),
                (gb_scan_t)(length - 2), UI_ACCENT);
}

void gb_hscroll16(unsigned char x, gb_scan_t y,
                  unsigned char w, unsigned char h,
                  unsigned int pos, unsigned int total, unsigned int page)
{
    gb_scan_t start, length;
    scroll_geometry(w, pos, total, page, &start, &length);
    gb_fill(x, y, w, h, UI_SURFACE);
    if (h > 2 && length > 2)
        gb_fill((unsigned char)(x + start + 1),
                (gb_scan_t)(y + 1),
                (gb_scan_t)(length - 2),
                (unsigned char)(h - 2), UI_ACCENT);
}

/* Inverse of the track mapping, with the pointer centred on the thumb. The
 * quotient/remainder form keeps every intermediate within 16 bits. */
static unsigned int scroll_value(gb_scan_t origin, gb_scan_t track,
                                 unsigned int total, unsigned int page,
                                 gb_scan_t pointer)
{
    gb_scan_t start, length, half, travel, rel;
    unsigned int limit, quotient, remainder;
    if (!total || page >= total) return 0;
    scroll_geometry(track, 0, total, page, &start, &length);
    (void)start;
    travel = (gb_scan_t)(track - length);
    if (!travel) return 0;
    half = (gb_scan_t)(length >> 1);
    if ((unsigned int)pointer <= (unsigned int)origin + half) return 0;
    rel = (gb_scan_t)(pointer - origin - half);
    limit = total - page;
    if (rel >= travel) return limit;
#ifdef GB_AGON
    if (track > 255) return (unsigned long)rel * limit / travel;
#endif
    quotient = limit / travel;
    remainder = limit % travel;
    return (unsigned int)((unsigned int)rel * quotient +
                          ((unsigned int)rel * remainder) / travel);
}

unsigned int gb_vscroll16_value(gb_scan_t y, gb_scan_t h,
                                unsigned int total, unsigned int page,
                                gb_scan_t my)
{
    return scroll_value(y, h, total, page, my);
}

unsigned int gb_hscroll16_value(unsigned char x, unsigned char w,
                                unsigned int total, unsigned int page,
                                unsigned char mx)
{
    return scroll_value(x, w, total, page, mx);
}
