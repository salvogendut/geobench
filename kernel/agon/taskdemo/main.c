/* Opt-in Agon timer proof. The complete worker is position-independent,
 * uses only HL and cannot return or poll a clock. */
#include "gb.h"
#include <string.h>
static const unsigned char worker[]={0x23,0x18,0xfd}; /* INC HL; JR worker */
static unsigned char started;
static unsigned char start(const void *p) __naked {
    (void)p;
__asm
    jp 0x3b20
__endasm;
}
static void stop(void) __naked {
__asm
    jp 0x3b23
__endasm;
}
static void event(void)
{
    if(gb_msg.type==GB_MSG_CLOSE) {
        if(started) stop();
        gb_wm_close();
    } else if(gb_msg.type==GB_MSG_DRAW) {
        gb_text(gb_wm_x()+2,gb_wm_y()+20,started?"Worker never yields":"Timer unavailable");
    }
}
static const gb_mwin_t w={39,96,38,48,38,48,event,"Task demo"};
void main(void)
{
    gb_wm_managed(&w);
    started=!memcmp((void *)0x3b2f,"ATD2",4) && start(worker);
}
