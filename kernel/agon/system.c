/* Agon desktop accessories. They run from resident memory: opening Settings,
 * the clock or a saver never replaces the active guest's code or directory. */
#include "gb.h"
#include "agon_display.h"
#include <string.h>

typedef unsigned char u8;
typedef struct { u8 x,y,w,h; } Rect;
typedef struct { u8 saver, timeout, seconds; } Prefs;
enum { DESK, MENU, SETTINGS, CLOCK, SAVER };
static __at(0x1448) volatile Rect live;
static __at(0x17ef) volatile u8 guest;
static __at(0x17e8) volatile unsigned now;
static __at(0x17ea) volatile u8 motion;
extern void layer_open(const u8 *minimum);
extern u8 layer_click(void);
extern void layer_close(void);
extern void sys_send(const void *, unsigned);
extern u8 sys_rtc(char *);
extern u8 cfg_open(const char *), cfg_create(const char *), cfg_close(u8);
extern unsigned cfg_read(u8 *), cfg_write(const u8 *);
extern void agon_squares_start(unsigned), agon_squares_tick(void);
extern void agon_clock_draw(u8), agon_clock_tick(void);
extern u8 saver_screen_save(u8);
extern void saver_screen_restore(void);
extern u8 ag_modal;

static const char path[] = "/GBENCH/AGON.CFG";
static const unsigned delays[] = {0, 1500, 6000, 12000, 30000};
static const char *const times[] = {"Off", "15 seconds", "1 minute", "2 minutes", "5 minutes"};
static const u8 geom[][6] = {
    {0,0,0,0,0,0}, {0,8,27,38,27,38}, {15,37,50,108,50,108},
    {24,20,28,122,22,96}
};
static struct { Rect rect; u8 guest, page, base; } stack[2];
static Prefs prefs, edit;
static u8 depth, page, moved, valid_time, animation, disk_error, first_event, before_saver;
static unsigned activity, rtc_at, frame_at;
static u8 saver_base;
u8 cfg_handle;

static u8 config(u8 write)
{
    u8 p[8], ok;
    if (write) {
        memcpy(p,"GBA1",4);
        memcpy(p+4,&edit,3);
        p[7] = 0xa5 ^ p[4] ^ p[5] ^ p[6];
        cfg_handle = cfg_create(path);
        if (!cfg_handle) return 0;
        ok = cfg_write(p) == sizeof p;
    } else {
        cfg_handle = cfg_open(path);
        if (!cfg_handle) return 0;
        ok = cfg_read(p) == sizeof p && !memcmp(p,"GBA1",4) &&
             p[4] < 2 && p[5] < 5 && p[6] < 2 &&
             p[7] == (u8)(0xa5 ^ p[4] ^ p[5] ^ p[6]);
        if (ok) memcpy(&prefs,p+4,3);
    }
    cfg_close(cfg_handle);
    return ok;
}

static u8 digit(char c) { return c >= '0' && c <= '9'; }
static u8 pair(const char *p) { return (p[0]-'0')*10 + p[1]-'0'; }
static void clock_read(u8 force)
{
    char s[40];
    u8 former = gb_min, prev_h = gb_hour, was_valid = valid_time, i, n;
    if (!force && (unsigned)(now-rtc_at) < 100) return;
    rtc_at = now;
    n = sys_rtc(s);
    if (n > sizeof(s)) n = sizeof(s);
    valid_time = 0;
    for (i=0; i+8<=n; ++i) {
        char *t = s+i;
        if (!digit(t[0]) || !digit(t[1]) || t[2]!=':' || !digit(t[3]) ||
            !digit(t[4]) || t[5]!=':' || !digit(t[6]) || !digit(t[7])) continue;
        if (pair(t)>23 || pair(t+3)>59 || pair(t+6)>59) continue;
        gb_hour=pair(t); gb_min=pair(t+3); gb_sec=pair(t+6); gb_binmode=1;
        valid_time=1;
        if (force || !was_valid || prev_h!=gb_hour || former!=gb_min) {
            t[5]=0;
            u8 base=ag_ybase; ag_ybase=0;
            gb_textbw(ag_vga ? 151 : 71,0,t);
            ag_ybase=base;
        }
        break;
    }
    if (!valid_time && (force || was_valid)) {
        u8 base=ag_ybase; ag_ybase=0;
        gb_textbw(ag_vga ? 151 : 71,0,"--:--"); ag_ybase=base;
    }
}

static void overlay(u8 kind)
{
    stack[depth].rect=live; stack[depth].guest=guest; stack[depth].page=page;
    stack[depth].base=ag_ybase; ag_ybase=0;
    ++depth; page=kind;
    memcpy((void *)&live,geom[kind],4);
    guest=kind==CLOCK ? 4 : 0;   /* only Clock is resizable */
    layer_open(geom[kind]+4);
}

static void pop(void)
{
    layer_close();
    --depth; live=stack[depth].rect; guest=stack[depth].guest; page=stack[depth].page;
    ag_ybase=stack[depth].base;
    activity=now;
}

static void button(u8 x,u8 y,u8 w,const char *s)
{
    gb_fill(x,y,w,12,1);
    gb_frame(x,y,w,12,2);
    gb_textbw(x+1,y+2,s);
}

static void draw(void)
{
    u8 x=live.x,y=live.y,w=live.w;
    gb_fill(x,y,w,live.h,page==CLOCK ? 0 : 1);
    gb_frame(x,y,w,live.h,2);
    if (page==MENU) {
        gb_textbw(x+2,y+2,"Settings");
        gb_textbw(x+2,y+14,"Clock");
        gb_textbw(x+2,y+26,"Run saver");
        return;
    }
    gb_fill(x,y,w,14,2);
    gb_textrev(x+1,y+3,page==CLOCK ? "Clock" : "Settings");
    gb_textrev(x+w-3,y+3,"X");
    if (page==CLOCK) {
        if (valid_time) agon_clock_draw(prefs.seconds);
        else gb_text(x+2,y+24,"No RTC time");
        return;
    }
    gb_textbw(x+2,y+21,"Screensaver");
    button(x+23,y+18,24,edit.saver ? "Blank" : "Squares");
    gb_textbw(x+2,y+39,"Idle timeout");
    button(x+23,y+36,24,times[edit.timeout]);
    gb_textbw(x+2,y+57,"Seconds hand");
    button(x+23,y+54,24,edit.seconds ? "On" : "Off");
    if (disk_error) gb_textbw(x+2,y+71,"Cannot write AGON.CFG");
    button(x+2,y+89,14,"Preview");
    button(x+19,y+89,11,"Save");
    button(x+33,y+89,14,"Cancel");
}

static void start_saver(u8 kind)
{
    activity=now;
    if (!saver_screen_save(ag_vga)) return;
    saver_base=ag_ybase; ag_ybase=0;
    before_saver=page;
    page=SAVER;
    animation=!kind;
    if (ag_vga) {
        gb_fill(0,0,160,240,animation ? 0 : 2);
        ag_ybase=240; gb_fill(0,0,160,240,animation ? 0 : 2); ag_ybase=0;
    }
    if (animation) agon_squares_start(now); else if (!ag_vga) gb_fill(0,0,80,240,2);
    frame_at=now;
}

void sys_open(u8 key)
{
    if (key=='V') { start_saver(prefs.saver); return; }
    if (key=='S') { edit=prefs; disk_error=0; overlay(SETTINGS); }
    else overlay(key=='T' ? CLOCK : MENU);
    draw();
}

static void setting(u8 action)
{
    switch(action) {
    case 0: edit.saver^=1; break;
    case 1: if (++edit.timeout==5) edit.timeout=0; break;
    case 2: edit.seconds^=1; break;
    case 3: start_saver(edit.saver); return;
    case 4: if (config(1)) { prefs=edit; pop(); return; } disk_error=1; break;
    default: pop(); return;
    }
    draw();
}

void sys_init(void)
{
    extern void ag_files_init(void);
    ag_files_init();
    depth=page=valid_time=0;
    ag_modal=0; first_event=1; activity=now=0; moved=motion=0;
    prefs.saver=prefs.seconds=0; prefs.timeout=3;
    config(0);
    gb_fill(0,0,ag_vga ? 160 : 80,8,1);
    gb_textbw(1,0,"System");
    gb_textbw(ag_vga ? 74 : 34,0,"GEOBENCH");
    clock_read(1);
}

u8 sys_text_focus(void)
{
    return guest==3 && page==DESK && !ag_modal;
}
u8 sys_probe_visible(void) { return page==DESK && !ag_modal; }

u8 sys_event(u8 key)
{
    u8 motion_now=motion, changed=motion_now!=moved, hit, x, y;
    moved=motion_now;
    if (ag_modal) { activity=now; return !key && ag_modal==2 ? 253 : key; }
    if (first_event) { first_event=0; activity=rtc_at=now; }
    if (page==SAVER) {
        if (key || changed) {
            saver_screen_restore();
            ag_ybase=saver_base;
            page=before_saver; activity=now; clock_read(1);
            if (page==CLOCK && valid_time) agon_clock_tick();
        } else if (animation && (unsigned)(now-frame_at)>=5) {
            frame_at=now; agon_squares_tick();
        }
        return 0;
    }
    if (key || changed) activity=now;
    else if (prefs.timeout && (unsigned)(now-activity)>=delays[prefs.timeout] && page!=MENU) {
        start_saver(prefs.saver); return 0;
    }
    clock_read(0);
    if (page==CLOCK && valid_time) agon_clock_tick();
    if (!key) return page==DESK && (guest>=3 && guest<=6) ? 253 : 0;
    x=gb_mx(); y=gb_my();
    if (page==DESK) {
        if (key!=' ' || ag_mouse_y>=8) return key;
        if ((guest==3 && x>=10 && x<28) || ((guest==1 || guest==5 || guest==6) && x>=10 && x<17)) return key;
        if (x<12 || x>=(ag_vga ? 150 : 70)) sys_open(x<12 ? 'M' : 'T');
    } else if (key==27) pop();
    else if (page==MENU) {
        pop();
        if (key==' ' && x<27 && y>=8 && y<46)
            sys_open(y<20 ? 'S' : y<32 ? 'T' : 'V');
        else if ((key&0xdf)=='S' || (key&0xdf)=='T' || (key&0xdf)=='V') sys_open(key&0xdf);
    } else if (key==' ') {
        hit=layer_click();
        if (hit==2) pop();
        else if (hit==4) draw();
        else if (hit==1 && page==SETTINGS) {
            x-=live.x; y-=live.y;
            if (x>=23 && x<47 && y>=18 && y<66 && (y-18)%18<12) setting((y-18)/18);
            else if (y>=89 && y<101) {
                if (x>=2 && x<16) setting(3);
                else if (x>=19 && x<30) setting(4);
                else if (x>=33 && x<47) setting(5);
            }
        }
    } else if (page==SETTINGS) {
        if (key==13) setting(4);
        else if (key=='p' || key=='P') setting(3);
    }
    return 0;
}
