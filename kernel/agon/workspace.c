/* A guest always executes at 4000h. Only switch its backing page between
 * callbacks: neither a C stack frame nor a MOS call is ever suspended. */
#include "gb.h"
#include <string.h>
#include "storage.h"
typedef struct { u8 x,y,w,h; } Rect;
enum { MAX_GUESTS=3, NONE=255 };
static __at(0x1448) volatile Rect live;
static __at(0x17ef) volatile u8 mode;
static __at(0x17e4) const char *const *titleptr;
static __at(0x17e6) u8 slot;
static __at(0x17fd) const char *docpath;
static __at(0x1350) u8 used;
extern u8 ag_load(const char *), ws_input(void), layer_click(void);
extern void ws_entry(void), ws_chrome(void), ws_footer(void), ws_error(void);
extern void ws_store(u8), ws_fetch(u8), layer_open(const u8 *), layer_close(void);
extern char *ws_target(const char *), *ws_document(const char *);
extern void ag_bar(void);
extern void ag_doc_reset(void);
extern void ag_worker_stop(void);
extern u8 ag_text_ok(const char *);
typedef struct {
    Rect rect;
    const gb_mwin_t *desc;
    u8 kind;
    char path[256], raw[11];
} Guest;
static Guest guests[MAX_GUESTS];
static u8 order[MAX_GUESTS], count, dirty, closing, pending_key, want, refresh;
static const char *launch;
u8 ag_modal;

char *ag_name(void) { return guests[slot].raw; }
void ag_setname(const char *name) { memcpy(ag_name(),name,11); }
char *ag_path(void) { return guests[slot].path; }
void ws_new_files(void) { want=5; }
u8 ws_path_busy(const char *path)
{
    u8 i;
    for(i=0;i<count;i++)
        if(ag_within(guests[order[i]].path,path)) return 1;
    return 0;
}
void ws_path_changed(const char *dir)
{
    u8 i;
    for(i=0;i<count;i++) {
        u8 id=order[i];
        if(guests[id].kind==1 && !strcmp(guests[id].path,dir))
            refresh|=1<<id;
    }
}
void ws_dir_changed(void) { ws_path_changed(ag_path()); }
void ws_damage(void) { dirty=1; }
void ws_close(void) { closing=1; }
u8 ws_key(void) { u8 k=pending_key; pending_key=0; return k; }

static void bind(u8 id)
{
    slot=id; live=guests[id].rect; mode=guests[id].kind;
    docpath=guests[id].path;
    if (guests[id].desc) titleptr=&guests[id].desc->title;
}

static void activate(u8 id)
{
    if (slot==id) return;
    if (slot!=NONE) { guests[slot].rect=live; ws_store(slot); }
    ws_fetch(id);
    bind(id);
}

void ws_register(const gb_mwin_t *d)
{
    ag_doc_reset();
    guests[slot].desc=d; titleptr=&d->title;
    memcpy((void *)&live,d,sizeof live);
    if(mode==1 && count>0 && slot) { live.x+=slot*3; live.y+=slot*8; }
    layer_open(&d->min_w);
    dirty=1;
}

static void message(u8 type)
{
    gb_msg.type=type;
    guests[slot].desc->proc();
}

static void paint(void)
{
    dirty=0;
    if(refresh & (1<<slot)) {
        refresh^=1<<slot;
        pending_key=252; message(GB_MSG_FRAME); /* refresh listing, retain scroll */
    }
    ws_chrome();
    message(GB_MSG_DRAW);
}

void ws_request_app(const char *name)
{
    launch=ws_target(name); want=1;
}

void ws_request_doc(const char *name)
{
    launch=ws_document(name);
    want=!memcmp(name,"NOTEPAD APP",11) ? 3 : 2;
}

static void open_requested(void)
{
    u8 id, old=slot, kind, expected=want;
    const char *app=expected==5 ? "/GBENCH/FILEMGR.APP" : expected==3 ? "/GBENCH/NOTEPAD.APP" :
                    expected==2 ? "/GBENCH/PICVIEW.APP" : launch;
    want=0;
    if (count>=MAX_GUESTS) { gb_textbw(1,232,"Close a window first (3 open)"); return; }
    if ((expected!=5 && !launch) || (expected==3 && !ag_text_ok(launch))) { ws_error(); return; }
    for (id=0;guests[id].desc;id++) ;
    if (expected==5) { strcpy(guests[id].path,"/"); memset(guests[id].raw,' ',11); }
    else if (expected!=1) {
        strcpy(guests[id].path,launch);
        memcpy(guests[id].raw,(const void *)0xf001,11); /* stable selected 8.3 name */
    } else {
        guests[id].path[0]=0;
        memset(guests[id].raw,' ',11);
    }
    guests[old].rect=live; ws_store(old);
    kind=ag_load(app);
    if (kind==255 || (expected!=1 && kind!=(expected==5?1:expected)) ||
        (expected==1 && kind==2)) { ws_fetch(old); ws_error(); return; }
    guests[id].kind=kind;
    if(kind==1) strcpy(guests[id].path,"/");
    bind(id);
    ws_entry();
    if (!guests[id].desc) { slot=NONE; activate(old); ws_error(); return; }
    order[count++]=id; used=count;
    ws_footer(); ag_bar();
}

/* Remove and recreate only the layers whose stacking order changes.
 * The underlying desktop and the unaffected lower windows stay in the VDP. */
static void raise_at(u8 at)
{
    u8 i, id=order[at];
    for (i=count;i>at;) {
        activate(order[--i]);
        layer_close();
    }
    for (i=at;i+1<count;i++) order[i]=order[i+1];
    order[count-1]=id;
    for (i=at;i<count;i++) {
        activate(order[i]);
        layer_open(&guests[slot].desc->min_w);
        paint();
    }
    ws_footer(); ag_bar();
}

void ws_run(void)
{
    u8 k, i, hit, x, y;
    memset(guests,0,sizeof guests);
    order[0]=0; slot=0; used=count=1;
    guests[0].kind=mode; memset(guests[0].raw,' ',11);
    if(mode==1) strcpy(guests[0].path,"/");
    bind(0); dirty=closing=want=pending_key=ag_modal=refresh=0;
    ws_footer(); ws_entry();
    if (!guests[0].desc) { used=0; return; }
    while (count) {
        if (closing) {
            ag_worker_stop();
            closing=dirty=pending_key=0;
            layer_close(); guests[slot].desc=0; slot=NONE; used=--count;
            if (!count) break;
            activate(order[count-1]); ws_footer(); ag_bar();
            continue;
        }
        if (want) { open_requested(); continue; }
        if (refresh) {
            /* Rebuild from the lowest changed layer, keeping snapshots of
             * all higher windows consistent without repainting the desktop. */
            u8 first=0;
            while(first<count && !(refresh & (1<<order[first]))) first++;
            for(i=count;i>first+1;) { activate(order[--i]); layer_close(); }
            activate(order[first]); paint();
            for(i=first+1;i<count;i++) {
                activate(order[i]); layer_open(&guests[slot].desc->min_w); paint();
            }
            continue;
        }
        if (dirty) { paint(); continue; }
        k=ws_input();
        if (k==23 && count>1) { raise_at(0); continue; } /* Ctrl-W cycles windows */
        if (k==' ') {
            x=gb_mx(); y=gb_my();
            if(mode==1 && y<8 && x>=10 && x<17) {
                pending_key='m'; message(GB_MSG_FRAME); continue;
            }
            if (y<8 && x>=10 && ((mode==3 && x<26) || (mode==5 && x<17))) {
                gb_msg.p0=x; message(GB_MSG_MENU); message(GB_MSG_FRAME); continue;
            }
            if (x<live.x || x>=live.x+live.w || y<live.y || y>=live.y+live.h) {
                for (i=count-1;i;) {
                    const Rect *r=&guests[order[--i]].rect;
                    if (x>=r->x && x<r->x+r->w && y>=r->y && y<r->y+r->h) {
                        raise_at(i); break;
                    }
                }
                continue;
            }
            hit=layer_click();
            if (hit==1) message(GB_MSG_CLICK);
            else if (hit==2) message(GB_MSG_CLOSE);
            else if (hit==3) { pending_key='v'; message(GB_MSG_FRAME); }
            else if (hit==4) dirty=1;
        } else if (k==27) message(GB_MSG_CLOSE);
        else { pending_key=k==253 ? 0 : k==254 ? ' ' : k; message(GB_MSG_FRAME); }
    }
    mode=0; ag_bar();
}
