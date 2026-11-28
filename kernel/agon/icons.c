/* GB_ICON / GB_ICONHALF: convert each themed IST bitmap on first use.
 * Slot data at 2000h has already passed the loader's exact-size/CRC check. */
#include <string.h>
#include "../../lib/gb/agon_display.h"
typedef unsigned char u8;
extern void send_vdu(const void *p, unsigned len);
#define IST ((const u8 *)0x2000)
static __at (0x17f0) u8 args[4];
static __at (0x17f8) struct { u8 y, col, paper, ink, right; } text_args;
static __at (0x1800) struct { u8 ready[2][32], pixels[1024], glyphs[16][13]; } cache;
static const u8 pens[] = {0xe0, 0xff, 0xc0, 0xc3};

void fm_icons_reset(void)
{
    memset(cache.ready, 0, sizeof(cache.ready));
    memset(cache.glyphs, 0, sizeof(cache.glyphs));
}

/* The physical VDP may paint later than it accepts a plot. Keep a bitmap
 * immutable until desktop exit, keyed by glyph and both opaque GB_TEXT pens. */
void fm_text(const u8 *s, const u8 *font)
{
    u8 ink = text_args.ink & 3, paper = text_args.paper & 3;
    u8 pair = ink*4 + paper, bank = 0x50 + pair;
    unsigned x = (unsigned)text_args.col*4, right = (unsigned)text_args.right*4;
    u8 plot[] = {23,27,0x20,0,bank,23,27,3,0,0,text_args.y,0};
    unsigned y = AG_PHYS_Y(text_args.y);
    plot[10]=y; plot[11]=y>>8;
    while (*s && x+6 <= right) {
        u8 ch = *s++ - 32, mask, *ready;
        if (ch >= 100) ch = '?' - 32;
        mask = 1 << (ch & 7);
        ready = &cache.glyphs[pair][ch >> 3];
        if (!(*ready & mask)) {
            u8 start[] = {23,0,0xa0,ch,bank,2,23,0,0xa0,ch,bank,0,48,0};
            u8 define[] = {23,27,0x20,ch,bank,23,27,0x21,6,0,8,0,1};
            u8 row, *out = cache.pixels;
            const u8 *src = font + (unsigned)ch*8;
            for (row = 0; row < 8; ++row) {
                u8 bits = *src++, col;
                for (col = 0; col < 6; ++col, bits <<= 1)
                    *out++ = pens[bits & 128 ? ink : paper];
            }
            send_vdu(start, sizeof(start));
            send_vdu(cache.pixels, 48);
            send_vdu(define, sizeof(define));
            *ready |= mask;
        }
        plot[3] = ch; plot[8] = x; plot[9] = x >> 8;
        send_vdu(plot, sizeof(plot));
        x += 6;
    }
}

static const u8 *slot_pixels(const u8 *entry)
{
    unsigned offset;
    memcpy(&offset, entry, sizeof(offset)); /* packed little-endian directory */
    return IST + offset;
}

void fm_icon(void)
{
    u8 slot = args[0], half = args[3], bank = 0x48 + half;
    u8 plot[] = {23,27,0x20,slot,bank,23,27,3,0,0,0,0};
    if (slot >= IST[5] || slot >= 32) return;
    if (!cache.ready[half][slot]) {
        const u8 *entry = IST + 16 + 4*slot;
        const u8 *src = slot_pixels(entry);
        u8 wb = entry[2], h = entry[3], *out = cache.pixels;
        unsigned n;
        u8 start[] = {23,0,0xa0,slot,bank,2,23,0,0xa0,slot,bank,0,0,0};
        u8 define[] = {23,27,0x20,slot,bank,23,27,0x21,wb*4,0,0,0,1};
        if (!wb || wb > 8 || !h || h > 32) return;
        if (half && h > 16) { src += ((h-16)/2)*wb; h = 16; }
        n = (unsigned)wb*h;
        while (n--) {
            u8 byte = *src++, i;
            for (i = 0; i != 4; ++i, byte <<= 1)
                *out++ = pens[((byte >> 7) & 1) | ((byte >> 2) & 2)];
        }
        n = out-cache.pixels;
        start[12] = n; start[13] = n >> 8; define[10] = h;
        send_vdu(start, sizeof(start));
        send_vdu(cache.pixels, n);
        send_vdu(define, sizeof(define));
        cache.ready[half][slot] = 1;
    }
    { unsigned y=AG_PHYS_Y(args[2]);
      plot[8] = args[1]<<2; plot[9] = args[1]>>6; plot[10] = y; plot[11]=y>>8; }
    send_vdu(plot, sizeof(plot));
}

void fm_icons_done(void)
{
    u8 half, i;
    u8 clear[] = {23,0,0xa0,0,0,2};
    for (half = 0; half < 2; ++half)
        for (i = 0; i < 32; ++i) if (cache.ready[half][i]) {
            clear[3] = i; clear[4] = 0x48+half;
            send_vdu(clear, sizeof(clear));
        }
    for (half = 0; half < 16; ++half)
        for (i = 0; i < 100; ++i) if (cache.glyphs[half][i >> 3] & (1 << (i & 7))) {
            clear[3] = i; clear[4] = 0x50+half;
            send_vdu(clear, sizeof(clear));
        }
}
