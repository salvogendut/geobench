/* The Agon foreground and worker paths publish the same portable PIC rows. */
#include <string.h>
#include "gbagon.h"
typedef unsigned char u8;
static struct { u8 header[14], pixels[CANVAS_H][CANVAS_WB]; } pic;
static u8 displayed,started,column;
static volatile u8 rows;
static int centre_x,centre_y,scale;
static const u8 colours[]={4,15,0,9};
static char definition[64],title[23];
static const char *const exts[]={"TXT",0};

static void compute_byte(void)
{
    u8 i,b=0,y=rows;
    int cy=centre_y+((int)y-CANVAS_H/2)*scale;
    for(i=0;i<4;i++) {
        int cx=centre_x+((int)column*4+i-CANVAS_W/2)*scale;
        b=set_pixel(b,i,pen_of(mand(cx,cy)));
    }
    pic.pixels[y][column]=b;
    if(++column==CANVAS_WB) { column=0; rows=y+1; }
}
static void compute(void) { while(rows<CANVAS_H) compute_byte(); }
static void send_span(const void *p) __naked {
    (void)p;
__asm
    ld bc,#15
    .db 0x49
    rst 0x18
    ret
__endasm;
}

static void flush(void)
{
    u8 cmd[]={18,0,0,25,4,0,0,0,0,25,101,0,0,0,0};
    unsigned left=(gb_wm_x()+1)*4;
    u8 top=gb_wm_y()+15;
    while(displayed<rows) {
        u8 x,first=0,pen=0;
        cmd[7]=cmd[13]=top+displayed;
        for(x=0;x<=CANVAS_W;x++) {
            u8 value=255;
            if(x<CANVAS_W) {
                u8 b=pic.pixels[displayed][x>>2]>>(3-(x&3));
                value=((b>>4)&1)|((b&1)<<1);
            }
            if(value!=pen) {
                if(pen) { /* background already cleared on reset or expose */
                    unsigned a=left+first,z=left+x-1;
                    cmd[2]=colours[pen]; cmd[5]=a; cmd[6]=a>>8;
                    cmd[11]=z; cmd[12]=z>>8; send_span(cmd);
                }
                pen=value; first=x;
            }
        }
        displayed++;
    }
}

static void caption(void)
{
    const char *name=gb_doc_name();
    char *p=title+4;
    u8 i;
    memcpy(title,"XAOS",4);
    if(name[8]!=' ') {
        *p++=':'; *p++=' ';
        for(i=0;i<8 && name[i]!=' ';i++) *p++=name[i];
        *p++='.';
        for(i=8;i<11 && name[i]!=' ';i++) *p++=name[i];
    }
    if(gb_doc_modified()) { *p++=' '; *p++='*'; }
    *p=0;
}

static void restart(void)
{
    gb_agon_worker_cancel();
    column=rows=displayed=0;
    started=gb_agon_worker_start(compute,&rows);
    caption();
    gb_restore_parent();
}
static void zoom(u8 in)
{
    if(in && scale>1) scale/=2;
    if(!in && scale<=160) scale*=2;
    gb_doc_dirty();
    restart();
}
static void home(void) { centre_x=HOME_CX; centre_y=HOME_CY; scale=HOME_STEP; restart(); }

static int loaded[3];
static u8 valid_view(const void *src,unsigned len)
{
    const char *p=src,*end=p+len;
    u8 i;
    if(len<5 || memcmp(p,"XAOS",4)) return 0;
    p+=4;
    if(p<end && *p=='\r') p++;
    if(p==end || *p++!='\n') return 0;
    for(i=0;i<3;i++) {
        unsigned v=0;
        u8 neg=p<end && *p=='-';
        if(neg) p++;
        if(p==end || *p<'0' || *p>'9') return 0;
        do {
            u8 d=*p++-'0';
            if(v>3276 || (v==3276 && d>(neg?8:7))) return 0;
            v=v*10+d;
        } while(p<end && *p>='0' && *p<='9');
        loaded[i]=neg?(int)(0u-v):(int)v;
        if(p<end && *p=='\r') p++;
        if(p<end) { if(*p++!='\n') return 0; }
        else if(i!=2) return 0;
    }
    return p==end && loaded[2]>0;
}
static void open_view(unsigned n)
{
    (void)n;
    centre_x=loaded[0]; centre_y=loaded[1]; scale=loaded[2]; restart();
}
static unsigned save_view(void)
{
    char *p=definition;
    memcpy(p,"XAOS\n",5);
    p=put_int(p+5,centre_x); p=put_int(p,centre_y); p=put_int(p,scale);
    return (unsigned)(p-definition);
}
static void export_pic(void)
{
    static const u8 header[]={'G','B','P','C',2,1,CANVAS_W,0,CANVAS_H,0,1,26,0,6};
    if(rows!=CANVAS_H) { gb_alert("Render not complete","Try Export again when done."); return; }
    memcpy(pic.header,header,sizeof header);
    gb_agon_export_pic(&pic,sizeof pic);
}
static const gb_doc_t doc={definition,sizeof definition,home,open_view,save_view,0,
    0,0,0,exts,"Export PIC",export_pic,0,0,0,0,valid_view};

static void event(void)
{
    u8 x=gb_wm_x()+1,y=gb_wm_y()+15,k;
    if(gb_msg.type==GB_MSG_CLOSE) {
        if(gb_doc_close()) gb_wm_close();
    } else if(gb_msg.type==GB_MSG_MENU) {
        gb_doc_event();
    } else if(gb_msg.type==GB_MSG_DRAW) {
        displayed=0; flush();
        gb_button(x,y+58,5,12,"+",0);
        gb_button(x+6,y+58,5,12,"-",0);
        gb_button(x+12,y+58,11,12,"Reset",0);
    } else if(gb_msg.type==GB_MSG_FRAME) {
        if(gb_doc_frame()) { caption(); gb_restore_parent(); return; }
        k=gb_getkey();
        if(k=='r' || k=='R') { gb_doc_dirty(); home(); }
        else if(k=='+' || k=='=') zoom(1);
        else if(k=='-') zoom(0);
        else {
            if(!started && rows<CANVAS_H) { compute_byte(); compute_byte(); }
            flush();
        }
    } else if(gb_msg.type==GB_MSG_CLICK) {
        u8 mx=gb_mx(),my=gb_my();
        if(my>=y+58 && my<y+70) {
            if(mx>=x && mx<x+5) zoom(1);
            else if(mx>=x+6 && mx<x+11) zoom(0);
            else if(mx>=x+12 && mx<x+23) { gb_doc_dirty(); home(); }
        } else if(mx>=x && mx<x+CANVAS_WB && my>=y && my<y+CANVAS_H) {
            centre_x+=((int)(mx-x)*4+2-CANVAS_W/2)*scale;
            centre_y+=((int)my-y-CANVAS_H/2)*scale;
            gb_doc_dirty();
            restart();
        }
    }
}
static const gb_mwin_t w={35,96,34,88,34,88,event,title};
void main(void) { gb_wm_managed(&w); gb_doc(&doc); home(); }
