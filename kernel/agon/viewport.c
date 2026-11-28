#include "gb.h"
#include "agon_display.h"

/* Anchor legacy byte-sized stamps for one synchronous VDP call. */
unsigned char ag_stamp_y(unsigned y)
{
    y+=ag_ybase;
    ag_ybase=y>240 ? 240 : y;
    return y-ag_ybase;
}
#define AG_STAMP(name,fn,type) \
void name(unsigned char x,unsigned y,type arg) { \
    unsigned char base=ag_ybase, line=ag_stamp_y(y); \
    fn(x,line,arg); ag_ybase=base; }
AG_STAMP(ag_text16,gb_text,const char *)
AG_STAMP(ag_textbw16,gb_textbw,const char *)
#undef AG_STAMP
void ag_frame16(unsigned char x,unsigned y,unsigned char w,unsigned char h,unsigned char pen)
{
    unsigned char base=ag_ybase, line=ag_stamp_y(y);
    gb_frame(x,line,w,h,pen); ag_ybase=base;
}
void ag_grip16(unsigned char x,unsigned y,unsigned char w,unsigned h)
{
    unsigned char base=ag_ybase, line=ag_stamp_y(y+h-GRIP_H);
    x+=w-GRIP_W;
    gb_fill(x,line,GRIP_W,GRIP_H,1);
    gb_frame(x,line,GRIP_W,GRIP_H,2); ag_ybase=base;
}
unsigned ag_pointer_line(void)
{
    unsigned py=ag_mouse_y;
    return py>=ag_ybase ? py-ag_ybase : 0;
}
