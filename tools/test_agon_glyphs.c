/* Host regression: let the VDP consume commands, but delay every paint.
 * A renderer that recycles one bitmap will show several copies of one letter. */
#include <assert.h>
#define __at(addr)
#include "../kernel/agon/icons.c"

static struct { unsigned id; u8 pixels[48]; int live; } bitmaps[8];
static struct { unsigned slot, x, y; } plots[8];
static unsigned slots, draws, uploading, selected, uploads;

static unsigned word(const u8 *p) { return p[0] + 256u*p[1]; }

static unsigned slot(unsigned id)
{
    unsigned i;
    for (i = 0; i < slots; ++i) if (bitmaps[i].id == id) return i;
    assert(slots < 8);
    bitmaps[slots].id = id;
    return slots++;
}

void send_vdu(const void *data, unsigned len)
{
    const u8 *p = data;
    while (len) {
        unsigned n, s;
        if (uploading) {
            assert(len >= 48);
            memcpy(bitmaps[uploading-1].pixels, p, 48);
            bitmaps[uploading-1].live = 1;
            uploading = 0; ++uploads; n = 48;
        } else if (len >= 6 && !memcmp(p, "\027\000\240", 3)) {
            s = slot(word(p+3));
            if (p[5] == 2) { bitmaps[s].live = 0; n = 6; }
            else { assert(p[5] == 0 && len >= 8 && word(p+6) == 48); uploading = s+1; n = 8; }
        } else if (len >= 5 && !memcmp(p, "\027\033\040", 3)) {
            selected = slot(word(p+3)); n = 5;
        } else if (len >= 8 && !memcmp(p, "\027\033\041", 3)) {
            assert(word(p+3) == 6 && word(p+5) == 8 && p[7] == 1); n = 8;
        } else {
            assert(len >= 7 && !memcmp(p, "\027\033\003", 3) && draws < 8);
            plots[draws].slot = selected;
            plots[draws].x = word(p+3); plots[draws++].y = word(p+5); n = 7;
        }
        p += n; len -= n;
    }
}

int main(void)
{
    u8 font[800] = {0};
    const char *wanted = "AABBA";
    unsigned i, r, c;
    memset(font + ('A'-32)*8, 0x80, 8);
    memset(font + ('B'-32)*8, 0x54, 8);
    fm_icons_reset();
    text_args.col = 63; text_args.y = 19; text_args.right = 80;
    text_args.ink = 1; text_args.paper = 0;
    fm_text((const u8 *)"AAB", font);
    text_args.col = 4; text_args.y = 64; text_args.right = 7;
    text_args.paper = 2;
    fm_text((const u8 *)"BA clipped", font);
    assert(draws == 5 && uploads == 4);
    for (i = 0; i < draws; ++i) {
        unsigned s = plots[i].slot;
        assert(bitmaps[s].live && plots[i].y == (i < 3 ? 19 : 64));
        assert(plots[i].x == (i < 3 ? 252+6*i : 16+6*(i-3)));
        for (r = 0; r < 8; ++r) for (c = 0; c < 6; ++c) {
            u8 on = font[(wanted[i]-32)*8+r] & (128 >> c);
            assert(bitmaps[s].pixels[r*6+c] == (on ? 0xff : i < 3 ? 0xe0 : 0xc0));
        }
    }
    fm_icons_done();
    for (i = 0; i < slots; ++i) assert(!bitmaps[i].live);
    return 0;
}
