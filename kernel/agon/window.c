/* MOS-side placement for the two possible modal levels. The VDP saves only
 * the area covered by each window. An XOR outline follows the input device
 * without redrawing the guest or reading the SD card during the gesture. */
#include <string.h>
#include "gb.h"
#include "agon_display.h"
typedef unsigned char u8;
typedef struct { u8 x, y, w, h; } Rect;
static __at (0x1448) volatile Rect live;
static __at (0x1306) volatile u8 mx;
static __at (0x1307) volatile u8 my;
static __at (0x17ef) volatile u8 mode;
#define RIGHT (GB_COLS-1)
#define TOP 8
#define BOTTOM (GB_LINES-8-1)   /* keep both endpoints above the footer */
extern void win_send(const void *, unsigned);
extern u8 win_poll(void);
extern u8 win_sync(void);

/* Standalone code has no crt0. All persistent state starts at win_init/open. */
static u8 depth;
/* Desktop, File Manager child, accessory and full-screen saver may nest. */
static struct {u8 w, h;} minimum[4];
void win_init(void) { depth = 0; }

static void coords(u8 *p, unsigned x, unsigned y)
{
    p[0] = x; p[1] = x >> 8; p[2] = y; p[3] = y >> 8;
}

static void capture(void)
{
    u8 p[] = {23,27,12,23,27,15, 25,4,0,0,0,0, 25,4,0,0,0,0,
              23,27,0x21,depth,0x46,0,0, 23,27,11,23,27,15};
    coords(p+8, live.x*4, AG_PHYS_Y(live.y));
    /* One extra pixel covers the exclusive capture endpoints on VDP 2.2-2.5.
     * The placement limits keep both endpoints inside the workspace. */
    coords(p+14, (live.x+live.w)*4, AG_PHYS_Y(live.y)+live.h);
    win_send(p,sizeof(p));
}

static u8 restore(void)
{
    u8 p[] = {23,27,0x20,depth,0x46,23,27,3,0,0,0,0};
    coords(p+8,live.x*4,AG_PHYS_Y(live.y));
    win_send(p,sizeof(p));
    /* The VDP queues a pointer to the bitmap, not a copy. A screen pixel
     * query drains its plot queue before close frees it or drag reuses it. */
    return win_sync()!=255;
}

void win_open(const u8 *min)
{
    minimum[depth].w = min[0]; minimum[depth].h = min[1];
    ++depth;
    capture();
}

void win_close(void)
{
    u8 clear[] = {23,0,0xa0,depth,0x46,2};
    if (restore()) win_send(clear,sizeof(clear));
    --depth;
}

static void outline(const Rect *r,u8 base)
{
    u8 p[] = {18,3,15,25,4,0,0,0,0,25,5,0,0,0,0,25,5,0,0,0,0,
              25,5,0,0,0,0,25,5,0,0,0,0,18,0,0};
    unsigned left = r->x*4, right = (r->x+r->w)*4-1;
    unsigned top=(unsigned)base+r->y,bottom=top+r->h-1;
    coords(p+5,left,top); coords(p+11,right,top); coords(p+17,right,bottom);
    coords(p+23,left,bottom); coords(p+29,left,top);
    win_send(p,sizeof(p));
}

static u8 clamp(int n, u8 low, u8 high)
{
    return n < low ? low : n > high ? high : (u8)n;
}

/* Return 0=ignore, 1=client, 2=close, 3=view, 4=geometry changed. */
u8 win_click(void)
{
    Rect old = live, r = old, next = old;
    int dx = (int)mx-old.x, dy = (int)my-old.y;
    u8 resizing = dy >= 14, shown = 0;
    u8 base=ag_ybase, new_base=base, right=ag_vga ? 159 : RIGHT;
    if (dx < 0 || dy < 0 || dx >= old.w || dy >= old.h) return 0;
    if (resizing) {
        if (!mode || dx < old.w-GRIP_W-1 || dy < old.h-GRIP_H-1) return 1;
    } else {
        if (dx >= old.w-3) return 2;
        if (mode == 1 && dx >= old.w-11 && dx < old.w-4) return 3;
    }
    for (;;) {
        u8 held = win_poll() & GB_FIRE;
        if (resizing) {
            u8 limit=right-old.x;
            next.w = clamp((int)mx-old.x+old.w-dx,minimum[depth-1].w,limit>RIGHT ? RIGHT : limit);
            next.h = clamp((int)my-old.y+old.h-dy,minimum[depth-1].h,BOTTOM-old.y);
        } else {
            int top=(ag_vga ? (int)ag_mouse_y : (int)my)-dy;
            next.x = clamp((int)mx-dx,0,right-old.w);
            next.y = clamp(top,TOP,BOTTOM-old.h);
            new_base=ag_vga ? clamp(top-next.y,0,240) : 0;
        }
        if (base!=new_base || memcmp(&r,&next,sizeof(r))) {
            if (shown) outline(&r,base);
            r = next; base=new_base;
            shown = held;
            if (shown) outline(&r,base);
        }
        if (!held) break;
    }
    if (shown) outline(&r,base);
    if (base==ag_ybase && !memcmp(&old,&r,sizeof(r))) return 0;
    if (!restore()) return 4; /* repaint at the old geometry; keep its bitmap */
    live = r; ag_ybase=base;
    capture();
    return 4;
}
