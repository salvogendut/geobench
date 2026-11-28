/* The hardware queues plots by bitmap pointer; accepting the VDU bytes
 * does not mean that the bitmap can already be freed or replaced. */
#include <assert.h>
#include <stdio.h>
#define GB_AGON
#define __at(addr)
#include "../kernel/agon/window.c"
#include "../kernel/agon/gesture.c"

static u8 pending[5], allocated[5], current;
static u8 timeout;

void win_send(const void *packet, unsigned len)
{
    const u8 *p = packet;
    assert(p[0] == 23 || p[0] == 18);
    if (p[0] == 18) return; /* drag outline */
    if (p[1] == 27 && p[2] == 12) {
        assert(len == 31 && p[20] == 0x21);
        current = p[21];
        assert(current && current < 5 && !pending[current]);
        allocated[current] = 1;
    } else if (p[1] == 27 && p[2] == 0x20) {
        current = p[3];
        assert(current && current < 5 && allocated[current]);
        pending[current] = 1;
    } else {
        assert(len == 6 && p[1] == 0 && p[5] == 2);
        assert(!pending[p[3]]);
        allocated[p[3]] = 0;
    }
}

u8 win_sync(void)
{
    if (timeout) return 255;
    memset(pending,0,sizeof pending);
    return 0;
}

static u8 dest_x;
static unsigned dest_y;
u8 win_poll(void) { gesture_x=dest_x; ag_mouse_y=dest_y; return 0; }

int main(void)
{
    const u8 min[] = {10,20};
    win_init();
    live = (Rect){0,8,27,38};
    win_open(min);
    win_close();                  /* dismiss System directly */
    assert(!allocated[1]);
    win_open(min);
    live = (Rect){24,20,28,122};
    win_open(min);                /* nested accessory */
    gesture_x=live.x+2; dest_x=gesture_x+1; dest_y=ag_mouse_y=live.y+2;
    assert(win_click()==4 && live.x==25); /* move and recapture the same ID */
    win_close();
    assert(allocated[1] && !allocated[2]);
    ag_vga=mode=1;
    live=(Rect){0,8,70,200};
    gesture_x=69; ag_mouse_y=207; dest_x=159; dest_y=479;
    assert(win_click()==4 && live.w==159 && AG_HEIGHT16(live.h)==463);
    gesture_x=158; ag_mouse_y=470; dest_x=39; dest_y=107;
    assert(win_click()==4 && live.w==40 && live.h==100 && !ag_hhi);
    timeout=1;
    win_close();
    assert(allocated[1]);         /* don't free a potentially live bitmap */
    puts("Agon windows: delayed VDP plots on dismiss, move and timeout OK");
    return 0;
}
