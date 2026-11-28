#include <string.h>
#include "gb.h"
#include "agon_display.h"
#include "geometry.h"
static __at(0x1306) volatile u8 gesture_x;

static unsigned bound(int n,unsigned low,unsigned high)
{
    return n<(int)low ? low : n>(int)high ? high : (unsigned)n;
}

u8 win_gesture(AgBox *r,const u8 *min,u8 mode)
{
    AgBox old=*r, next=old;
    u8 shown=0, held, resize;
    int dx=(int)gesture_x-old.x, dy=(int)ag_mouse_y-old.y;
    unsigned right=ag_vga ? 159 : 79, bottom=ag_vga ? 471 : 231;
    if(dx<0 || dx>=old.w || dy<0 || dy>=(int)old.h) return 0;
    resize=dy>=14;
    if(resize) {
        if(!mode || dx<old.w-GRIP_W-1 || dy<(int)old.h-GRIP_H-1) return 1;
    } else {
        if(dx>=old.w-3) return 2;
        if(mode==1 && dx>=old.w-11 && dx<old.w-4) return 3;
    }
    do {
        held=win_poll() & GB_FIRE;
        if(resize) {
            unsigned w=right-old.x,h=bottom-old.y;
            /* Only document apps implement the large viewport contract. */
            if(mode!=1 && mode!=2 && mode!=3 && mode!=6) {
                if(w>79) w=79;
                if(h>231-(old.y-ag_ybase)) h=231-(old.y-ag_ybase);
            }
            next.w=bound((int)gesture_x-old.x+old.w-dx,min[0],w);
            next.h=bound((int)ag_mouse_y-old.y+old.h-dy,min[1],h);
        } else {
            next.x=bound((int)gesture_x-dx,0,right-old.w);
            next.y=bound((int)ag_mouse_y-dy,8,bottom-old.h);
        }
        if(memcmp(r,&next,sizeof next)) {
            if(shown) win_outline(r);
            *r=next; shown=held;
            if(shown) win_outline(r);
        }
    } while(held);
    if(shown) win_outline(r);
    return memcmp(r,&old,sizeof old) ? 4 : 0;
}
