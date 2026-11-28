/* The shared Viewer computes the viewport and resamples packed rows. Here
 * the focused guest streams those rows into a slot-owned VDP bitmap. */
typedef unsigned char u8;
static u8 handle;
#include "agon_pic.h"
#define VIEW_PATH (*(const char * volatile *)0x17fd)
static u8 palette[16], pixels[GB_COLS*4], packed6[256];
static u8 build_row,bmp_rows,bmp_zoom,ready;
static unsigned bmp_w,bmp_x,bmp_y,first_row,cached_rows;
static unsigned long offset;
static u8 clear_b[]={23,0,0xa0,0,0x47,2};
static u8 append_b[]={23,0,0xa0,0,0x47,0,0,0};
static u8 define_b[]={23,0,0xa0,0,0x47,14,23,27,0x20,0,0x47,23,27,0x21,0,0,0,0,1};
static u8 plot_b[]={23,27,0x20,0,0x47,23,27,3,0,0,0,0};

/* mos_flseek takes a 24-bit numeric HL, not a pointer in MBASE. Build
 * the long immediate in this guest's own code. Older MOS returns no useful
 * status here: all reads (including the last declared byte) are checked. */
static void seek_file(void) __naked
{
__asm
    ld hl,(_offset)
    ld (00100$+2),hl
    ld a,(_offset+2)
    ld (00100$+4),a
00100$: .db 0x5b,0x21,0,0,0
    ld e,#0
    ld a,(_handle)
    ld c,a
    ld a,#0x1c
    .db 0x49
    rst 0x08
    ret
__endasm;
}

static u8 have_pic(void) { return handle!=0; }
static void agon_clear(void)
{
    if(ready) ((u8 (*)(u8))0xa009)(0); /* fence the last queued bitmap plot */
    send(clear_b,sizeof clear_b);
    bmp_w=0; ready=0;
}
static void agon_close(void)
{
    if(handle) { close_file(); handle=0; }
    agon_clear(); cached_rows=0;
}
static void agon_open(void)
{
    const char *path=VIEW_PATH,*name=strrchr(path,'/');
    u8 *p=(u8 *)filebuf,id=0x58+*(volatile u8 *)0x17e6;
    unsigned b;
    clear_b[3]=append_b[3]=define_b[3]=define_b[9]=plot_b[3]=id;
    agon_close(); zoom=NORMAL; scroll_x=scroll_y=0; invalid_pic=0;
    if(!*path) { strcpy(vtitle,"Viewer"); return; }
    strncpy(vtitle,name?name+1:path,sizeof(vtitle)-1); vtitle[sizeof(vtitle)-1]=0;
    handle=open_read(path);
    if(handle && read_file(p,14)==14 && !memcmp(p,"GBPC",4) && p[4]==2) {
        pic_mode=p[5]; pic_pixels=word(p+6); pic_h=word(p+8);
        if((pic_mode==1 || pic_mode==6 || pic_mode==7) && pic_pixels && pic_pixels<=512 &&
           pic_h && pic_h<=512 && picture_palette(palette,p+10)) {
            pic_wb=(pic_pixels+3)>>2;
            pic_stride=pic_mode==7 ? (pic_pixels+1)>>1 : pic_wb;
            offset=13ul+(unsigned long)pic_stride*pic_h;
            seek_file();
            if(read_file(p,1)==1) {
                if(pic_mode==6) for(b=0;b<256;b++)
                    packed6[b]=((b&0x40)<<1)|((b&0x10)<<2)|((b&4)<<3)|((b&1)<<4)|
                               ((b&0x80)>>4)|((b&0x20)>>3)|((b&8)>>2)|((b&2)>>1);
                return;
            }
        }
    }
    agon_close(); invalid_pic=1;
}
static u8 *source_row(unsigned y)
{
    unsigned n;
    if(!cached_rows || y<first_row || y>=first_row+cached_rows) {
        first_row=y; cached_rows=sizeof(filebuf)/pic_stride;
        if(cached_rows>pic_h-y) cached_rows=pic_h-y;
        offset=14ul+(unsigned long)y*pic_stride;
        seek_file(); n=cached_rows*pic_stride;
        if(read_file(filebuf,n)!=n) return 0;
        if(pic_mode==6) {
            u8 *p=(u8 *)filebuf;
            while(n--) { *p=packed6[*p]; p++; }
        }
    }
    return (u8 *)filebuf+(y-first_row)*pic_stride;
}
static void agon_plot(void)
{
    unsigned x=view_x*4;
    plot_b[8]=x; plot_b[9]=x>>8; plot_b[10]=view_y;
    send(plot_b,sizeof plot_b);
}
static void agon_view(u8 cols,u8 lines)
{
    unsigned w=(unsigned)cols*4;
    if(tail_pixels && scroll_x+cols==display_w) w-=4-tail_pixels;
    if(w==bmp_w && lines==bmp_rows && zoom==bmp_zoom && scroll_x==bmp_x && scroll_y==bmp_y) {
        if(ready) agon_plot();
        return;
    }
    agon_clear();
    bmp_w=w; bmp_rows=lines; bmp_zoom=zoom; bmp_x=scroll_x; bmp_y=scroll_y;
    append_b[6]=w; append_b[7]=w>>8; build_row=0;
}
static void agon_tick(void)
{
    u8 budget=6,per=pic_mode==7?2:4;
    if(!bmp_w || ready) return;
    while(build_row<bmp_rows && budget--) {
        unsigned sy=bmp_y+(zoom==HALF ? (unsigned)build_row*2 : build_row>>(zoom-NORMAL));
        unsigned x;
        u8 *src=source_row(sy);
        if(!src) { agon_close(); invalid_pic=1; gb_restore_parent(); return; }
        sample_row(rowbuf,src,pic_stride,pic_mode==7?bmp_x*2:bmp_x,
                   (bmp_w+per-1)/per,zoom,pic_mode==7?7:1);
        for(x=0;x<bmp_w;) {
            u8 i,b=rowbuf[pic_mode==7?x>>1:x>>2];
            for(i=0;i<per && x<bmp_w;i++) {
                if(pic_mode==7) { pixels[x++]=palette[b>>4]; b<<=4; }
                else { pixels[x++]=palette[(b>>7)|((b>>2)&2)]; b<<=1; }
            }
        }
        do {
            send(append_b,sizeof append_b); send(pixels,bmp_w); ++build_row;
        } while(build_row<bmp_rows && zoom>NORMAL && (build_row&((1<<(zoom-NORMAL))-1)));
    }
    if(build_row==bmp_rows) {
        define_b[14]=bmp_w; define_b[15]=bmp_w>>8; define_b[16]=bmp_rows;
        send(define_b,sizeof define_b); ready=1; agon_plot();
    }
}
