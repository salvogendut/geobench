/* Read-only Agon document guest. Upload the fitted GBPC bitmap once, in
 * complete VDP blocks: a slow SD read must never leave a VDU packet open. */
#include "gb.h"
#include <string.h>

typedef unsigned char u8;
#define DOC_PATH (*(const char * volatile *)0x17fd)
#define WIDTH 312
#define ID_LO 0x50
#define ID_HI 0x47
static u8 handle, valid, mode, dh, view_h, palette[16];
static unsigned width, height, stride, dw, view_w;
static u8 xbyte[WIDTH], colours[4][256];
static const u8 *lookup[WIDTH];
static u8 input[2048], output[WIDTH];
static char title[34];
static u8 clear[] = {23,0,0xa0,ID_LO,ID_HI,2};
static u8 append[] = {23,0,0xa0,ID_LO,ID_HI,0,0,0};
static u8 define[] = {23,0,0xa0,ID_LO,ID_HI,14,23,27,0x20,ID_LO,ID_HI,
                      23,27,0x21,0,0,0,0,1};
static u8 plot[] = {23,27,0x20,ID_LO,ID_HI,23,27,3,0,0,0,0};

#include "agon_pic.h"

static u8 prepare(void)
{
    u8 i;
    unsigned packed, x, sx = 0, frac = 0;
    if (read_file(input, 14) != 14 || memcmp(input,"GBPC",4) || input[4] != 2) return 0;
    mode = input[5]; width = word(input+6); height = word(input+8);
    if ((mode != 1 && mode != 6 && mode != 7) || !width || width > 512 ||
        !height || height > 512) return 0;
    stride = mode == 7 ? (width+1)/2 : (width+3)/4;
    if (!picture_palette(palette,input+10)) return 0;
    if (width <= view_w && height <= view_h) { dw = width; dh = height; }
    else if ((unsigned long)width*view_h > (unsigned long)height*view_w) {
        dw = view_w; dh = (unsigned long)height*view_w/width;
    } else {
        dh = view_h; dw = (unsigned long)width*view_h/height;
    }
    if (!dw) dw = 1;
    if (!dh) dh = 1;
    /* Decode each possible packed byte once, not once per output pixel. */
    for (packed = 0; packed < 256; ++packed) {
        u8 b = packed;
        for (i = 0; i < (mode == 7 ? 2 : 4); ++i) {
            u8 pen;
            if (mode == 7) { pen = b >> 4; b <<= 4; }
            else if (mode == 6) { pen = b >> 6; b <<= 2; }
            else { pen = (b >> 7) | ((b >> 2) & 2); b <<= 1; }
            colours[i][packed] = palette[pen];
        }
    }
    for (x = 0; x < dw; ++x) {
        xbyte[x] = mode == 7 ? sx >> 1 : sx >> 2;
        lookup[x] = colours[sx & (mode == 7 ? 1 : 3)];
        frac += width;
        while (frac >= dw) { frac -= dw; ++sx; }
    }
    return 1;
}

static void upload_row(const u8 *row)
{
    unsigned x;
    for (x = 0; x < dw; ++x) output[x] = lookup[x][row[xbyte[x]]];
    send(append, sizeof(append));
    send(output, dw);
}

static u8 upload(void)
{
    unsigned y = 0, next = 0, frac = 0, batch, i, n;
    if (!prepare()) return 0;
    append[6] = dw; append[7] = dw >> 8;
    batch = sizeof(input)/stride;
    while (y < height) {
        const u8 *row = input;
        n = height-y;
        if (n > batch) n = batch;
        if (read_file(input, n*stride) != n*stride) return 0;
        for (i = 0; i < n; ++i, ++y, row += stride) {
            if (y != next) continue;
            upload_row(row);
            frac += height;
            while (frac >= dh) { frac -= dh; ++next; }
        }
    }
    define[14] = dw; define[15] = dw >> 8; define[16] = dh;
    send(define, sizeof(define));
    return 1;
}

static void event(void)
{
    if (gb_msg.type == GB_MSG_CLOSE) {
        send(clear,sizeof(clear));
        gb_wm_close();
    } else if (gb_msg.type == GB_MSG_DRAW) {
        u8 x = gb_wm_x(), y = gb_wm_y(), w = gb_wm_w(), h = gb_wm_h();
        unsigned available = (w-2)*4, px;
        if (view_w != available || view_h != h-22) {
            view_w = available; view_h = h-22; valid = 0;
            send(clear,sizeof(clear));
            gb_text(x+3,y+28,"Reading...");
            handle = open_read(DOC_PATH);
            if (handle) {
                valid = upload();
                close_file();
            }
            gb_fill(x+3,y+28,15,8,0);
            if (!valid) send(clear,sizeof(clear));
        }
        if (valid) {
            px = (x+1)*4 + (view_w-dw)/2;
            plot[8] = px; plot[9] = px >> 8;
            plot[10] = y+17 + (view_h-dh)/2;
            send(plot,sizeof(plot));
        } else gb_text(x+3,y+28,w < 50 ? "Invalid picture" : "Invalid or unsupported picture");
        gb_draw_grip(x,y,w,h);
    }
}

static const gb_mwin_t window = {9,17,62,166,28,66,event,title};

void main(void)
{
    const char *path = DOC_PATH, *name = strrchr(path,'/');
    u8 id = ID_LO + *(volatile u8 *)0x17e6; /* one VDP bitmap per live guest */
    clear[3]=append[3]=define[3]=define[9]=plot[3]=id;
    strncpy(title,name ? name+1 : path,sizeof(title)-1);
    title[sizeof(title)-1] = 0;
    gb_wm_managed(&window);
    send(clear,sizeof(clear));
}
