/* MOS-side placement for the two possible modal levels. The VDP saves only
 * the area covered by each window. An XOR outline follows the input device
 * without redrawing the guest or reading the SD card during the gesture. */
#include <string.h>
#include "gb.h"
#include "agon_display.h"
#include "geometry.h"
typedef struct { u8 x, y, w, h; } Rect;
static __at (0x1448) volatile Rect live;
static __at (0x17ef) volatile u8 mode;
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
    coords(p+14, (live.x+live.w)*4, AG_PHYS_Y(live.y)+AG_HEIGHT16(live.h));
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

void win_outline(const AgBox *r)
{
    u8 p[] = {18,3,15,25,4,0,0,0,0,25,5,0,0,0,0,25,5,0,0,0,0,
              25,5,0,0,0,0,25,5,0,0,0,0,18,0,0};
    unsigned left = r->x*4, right = (r->x+r->w)*4-1;
    unsigned top=r->y,bottom=top+r->h-1;
    coords(p+5,left,top); coords(p+11,right,top); coords(p+17,right,bottom);
    coords(p+23,left,bottom); coords(p+29,left,top);
    win_send(p,sizeof(p));
}

/* Preserve the byte rectangle ABI for small guests and store just the high
 * height byte alongside it for native document windows. */
u8 win_click(void)
{
    AgBox r;
    u8 result, max_y;
    r.x=live.x; r.w=live.w; r.y=AG_PHYS_Y(live.y); r.h=AG_HEIGHT16(live.h);
    result=win_gesture(&r,&minimum[depth-1].w,mode);
    if(result!=4 || !restore()) return result;
    max_y=r.h>BOTTOM-TOP ? TOP : BOTTOM-r.h;
    live.y=r.y<max_y ? r.y : max_y;
    ag_ybase=r.y-live.y;
    live.x=r.x; live.w=r.w; live.h=r.h; ag_hhi=r.h>>8;
    capture(); return 4;
}

void win_fill16(u8 x,unsigned y,u8 w,unsigned h,u8 pen)
{
    static const u8 ink[]={4,15,0,9};
    u8 p[]={18,0,0,25,4,0,0,0,0,25,101,0,0,0,0};
    if(!w || !h) return;
    p[2]=ink[pen&3]; y+=ag_ybase;
    coords(p+5,x*4,y); coords(p+11,(x+w)*4-1,y+h-1);
    win_send(p,sizeof p);
}
void win_backdrop(void)
{
    unsigned h=AG_HEIGHT16(live.h);
    win_fill16(live.x,live.y,live.w,h,2);
    win_fill16(live.x+1,live.y+1,live.w-2,h-2,0);
}
