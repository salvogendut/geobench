/* gbscroll16.c - app-linked scrollbars with 16-bit document ranges.
 *
 * Viewer pictures can be taller than 255 rows, so the compact gb_vscroll
 * byte-range API is not sufficient. Geometry remains stateless and app-owned.
 */
#include "gb.h"

#define UI_SURFACE 1
#define UI_ACCENT  3
#define THUMB_MIN  4

/* Map value/limit onto an eight-bit track without pulling 32-bit arithmetic
 * into small applications. Low bits discarded while normalising cannot affect
 * more than one track pixel. */
static unsigned char scale_to_track(unsigned int value, unsigned int limit,
                                    unsigned char track)
{
    if (!limit || value >= limit) return track;
    while (limit > 255U) {
        value >>= 1;
        limit = (unsigned int)((limit >> 1) + (limit & 1U));
    }
    return (unsigned char)((value * track) / limit);
}

static void scroll_geometry(unsigned char track, unsigned int pos,
                            unsigned int total, unsigned int page,
                            unsigned char *start, unsigned char *length)
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
                            (unsigned char)(track - *length));
}

void gb_vscroll16(unsigned char x, unsigned char y,
                  unsigned char w, unsigned char h,
                  unsigned int pos, unsigned int total, unsigned int page)
{
    unsigned char start, length;
    scroll_geometry(h, pos, total, page, &start, &length);
    gb_fill(x, y, w, h, UI_SURFACE);
    if (w > 2 && length > 2)
        gb_fill((unsigned char)(x + 1),
                (unsigned char)(y + start + 1),
                (unsigned char)(w - 2),
                (unsigned char)(length - 2), UI_ACCENT);
}

void gb_hscroll16(unsigned char x, unsigned char y,
                  unsigned char w, unsigned char h,
                  unsigned int pos, unsigned int total, unsigned int page)
{
    unsigned char start, length;
    scroll_geometry(w, pos, total, page, &start, &length);
    gb_fill(x, y, w, h, UI_SURFACE);
    if (h > 2 && length > 2)
        gb_fill((unsigned char)(x + start + 1),
                (unsigned char)(y + 1),
                (unsigned char)(length - 2),
                (unsigned char)(h - 2), UI_ACCENT);
}

/* Inverse of the track mapping, with the pointer centred on the thumb. The
 * quotient/remainder form keeps every intermediate within 16 bits. */
static unsigned int scroll_value(unsigned char origin, unsigned char track,
                                 unsigned int total, unsigned int page,
                                 unsigned char pointer)
{
    unsigned char start, length, half, travel, rel;
    unsigned int limit, quotient, remainder;
    if (!total || page >= total) return 0;
    scroll_geometry(track, 0, total, page, &start, &length);
    (void)start;
    travel = (unsigned char)(track - length);
    if (!travel) return 0;
    half = (unsigned char)(length >> 1);
    if ((unsigned int)pointer <= (unsigned int)origin + half) return 0;
    rel = (unsigned char)(pointer - origin - half);
    limit = total - page;
    if (rel >= travel) return limit;
    quotient = limit / travel;
    remainder = limit % travel;
    return (unsigned int)((unsigned int)rel * quotient +
                          ((unsigned int)rel * remainder) / travel);
}

unsigned int gb_vscroll16_value(unsigned char y, unsigned char h,
                                unsigned int total, unsigned int page,
                                unsigned char my)
{
    return scroll_value(y, h, total, page, my);
}

unsigned int gb_hscroll16_value(unsigned char x, unsigned char w,
                                unsigned int total, unsigned int page,
                                unsigned char mx)
{
    return scroll_value(x, w, total, page, mx);
}
