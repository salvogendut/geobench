/* MOS document operations. Never truncate an existing file: create a sibling,
 * close and verify it, then rename. A failed rollback leaves a recovery copy. */
#include <string.h>
#include "gb.h"
#include "agon_display.h"
#include "storage.h"
#include "gbagon.h"
extern void ws_path_changed(const char *);
extern u8 ag_modal;
extern char *ag_name(void);
extern char *ag_path(void);
extern u8 ag_dopen(void *,const char *), ag_dread(void *,void *), ag_dclose(void *);
extern void layer_open(const u8 *), layer_close(void);
extern u8 ws_input(void);
extern void ws_store(u8), ws_fetch(u8);
typedef struct { u8 x,y,w,h; } Rect;
static __at(0x1448) volatile Rect live;
static __at(0x17ef) volatile u8 mode;
static __at(0x1700) volatile u8 op;
/* Explicit addresses: SDCC absolute declarations do not allocate neighbours. */
#define COL (*(volatile u8 *)0x1701)
#define LINE (*(volatile u8 *)0x1702)
#define ITEMS (*(volatile u8 *)0x1703)
#define NAME ((char *)0x1708)
#define LABELS ((const char *)0x1718)

void ag_bar(void)
{
    u8 base=ag_ybase;
    ag_ybase=0;
    gb_fill(10,0,23,8,1);
    if (mode==1 || mode==3 || mode==5 || mode==6) gb_textbw(10,0,"File");
    if (mode==3) gb_textbw(17,0,"Edit");
    ag_ybase=base;
}

static Rect under;
static u8 under_base,under_hhi;
static __at(0x3500) char pathbuf[256];
#define fi ag_info
static __at(0x3720) u8 directory[256];
static __at(0x3820) char folder[256];
/* A picker and a write never run concurrently. Staged folder and filename
 * remain separate until the caller has accepted the final prompt. */
static __at(0x3920) char tmp[256];
static __at(0x3a20) char bak[256];
static u8 picked;
static gb_agon_validator validator;
void ag_validate_next(gb_agon_validator fn) { validator=fn; }
void ag_doc_reset(void) { picked=0; validator=0; }
void ag_panel(u8 x,u8 y,u8 w,u8 h)
{
    static const u8 min[]={1,1};
    under=live; under_base=ag_ybase; under_hhi=ag_hhi; ag_ybase=ag_hhi=0;
    live.x=x; live.y=y; live.w=w; live.h=h;
    ag_modal=1; layer_open(min);
    gb_fill(x,y,w,h,1); gb_frame(x,y,w,h,2);
}
void ag_dismiss(void) { layer_close(); live=under; ag_ybase=under_base; ag_hhi=under_hhi; ag_modal=0; }

void ag_notice(const char *s)
{
    ag_panel(12,82,56,34);
    gb_textbw(14,87,s); gb_textbw(14,102,"Press a key");
    ws_input(); ag_dismiss();
}

static u8 validchar(u8 c)
{
    return (c>='0' && c<='9') || (c>='A' && c<='Z') || (c>='a' && c<='z') ||
           c=='_' || c=='-';
}

static u8 leaf(char *out,const char *raw)
{
    u8 i,n=0;
    for (i=0;i<8 && raw[i]!=' ';i++) {
        if (!validchar(raw[i])) return 0;
        out[n++]=raw[i];
    }
    if (!n) return 0;
    out[n++]='.';
    for (i=8;i<11;i++) {
        char c=raw[i];
        if (c>='a' && c<='z') c-=32;
        if (c!="TXT"[i-8]) return 0;
        out[n++]=c;
    }
    out[n]=0; return 1;
}

static void parent(char *out,const char *p)
{
    const char *s=strrchr(p,'/');
    unsigned n=s ? (unsigned)(s-p) : 0;
    if (n) memmove(out,p,n); else { out[0]='/'; n=1; }
    out[n]=0;
}

static u8 info(const char *path,unsigned *size)
{
    u8 rc=ag_stat(fi,path);
    if (rc) return rc;
    if ((fi[8]&0x10) || fi[2] || fi[3]) return 255;
    *size=fi[0]|(unsigned)fi[1]<<8;
    return 0;
}

u8 ag_text_ok(const char *p)
{
    unsigned n;
    return !info(p,&n) && n<=4096;
}

unsigned ag_doc_read(void *buf,unsigned cap)
{
    unsigned n,got;
    gb_agon_validator check=validator;
    const char *error="Cannot read document";
    validator=0;
    if (mode!=3 && mode!=5) return 0;
    if (!picked) {
        if (!*ag_path()) return 0;
        strcpy(pathbuf,ag_path());
    }
    picked=0;
    if (!info(pathbuf,&n) && n<=cap && (ag_handle=ag_read_open(pathbuf))!=0) {
        /* The fourth backing page is reserved for read rollback, never a
         * window. On a short read do not replace an editor's live buffer. */
        ws_store(3);
        got=ag_read(buf,n); ag_close(ag_handle);
        if (got==n) {
            if (!check || check(buf,n)) { strcpy(ag_path(),pathbuf); return n; }
            error="Invalid document";
        }
        ws_fetch(3);
    }
    ag_notice(error); return 0xffff;
}

static const char *save_buf;
static unsigned save_len;

static u8 verify(void)
{
    static u8 block[64];
    unsigned size,off=0,take;
    if (info(tmp,&size) || size!=save_len) return 0;
    ag_handle=ag_read_open(tmp);
    if (!ag_handle) return 0;
    while (off<save_len) {
        take=save_len-off; if (take>sizeof block) take=sizeof block;
        if (ag_read(block,take)!=take || memcmp(block,save_buf+off,take)) break;
        off+=take;
    }
    ag_close(ag_handle); return off==save_len;
}

static u8 replace(void)
{
    static u8 exists;
    static unsigned size;
    u8 rc;
    parent(folder,pathbuf);
    if (!ag_join(tmp,folder,"GBSAVE.TMP") || !ag_join(bak,folder,"GBSAVE.BAK")) return 0;
    rc=info(pathbuf,&size);
    exists=rc==0;
    if (exists) {
        if (size>4096) return 0;
        if (info(bak,&size)!=4) return 0;
    } else if (rc!=4) return 0;
    ag_handle=ag_new(tmp);
    if (!ag_handle) return 0;    /* do not remove another writer's temp */
    size=ag_write(save_buf,save_len);
    ag_close(ag_handle);
    if (size==save_len && verify() && ag_publish(tmp,pathbuf,exists?bak:0)) return 1;
    ag_delete(tmp); return 0;
}

u8 ag_doc_save(const void *buf,unsigned n)
{
    char name[13];
    u8 ok=!memcmp(ag_name()+8,"TXT",3);
    if(mode!=3 && mode!=5) return 0;
    save_buf=buf; save_len=n;
    if (picked) ok=leaf(name,ag_name()) && ag_join(pathbuf,folder,name);
    else if (*ag_path()) strcpy(pathbuf,ag_path()); else ok=0;
    picked=0;
    if (ok && n<=4096 && replace()) {
        strcpy(ag_path(),pathbuf);
        if(mode==5) ws_path_changed(folder);
        return 1;
    }
    ag_notice("Not saved - check name/card"); return 0;
}

static u8 popup(void)
{
    u8 i,k,x=COL,y=LINE,n=ITEMS,w=0,chosen=255;
    const char *s=LABELS;
    if (!n || n>12) return 255;
    for(i=0;i<n;i++,s+=strlen(s)+1) if(strlen(s)>w) w=strlen(s);
    w=w*3/2+5; if(w>76) w=76;
    if(x+w>79) x=79-w;
    if(y+12*n+2>230) y=230-12*n-2;
    ag_panel(x,y,w,n*12+2);
    for(i=0,s=LABELS;i<n;i++,s+=strlen(s)+1) gb_textbw(x+1,y+2+12*i,s);
    k=ws_input();
    if(k==' ' && gb_mx()>=x && gb_mx()<x+w && gb_my()>y)
        chosen=(gb_my()-y-1)/12;
    else if(k>='1' && k<='9') chosen=k-'1';
    ag_dismiss(); return chosen<n ? chosen : 255;
}

char *ag_filename(const char *caption,u8 text)
{
    u8 k,n=0;
    char *dot;
    const char *ext=text==2?".PIC":".TXT";
    ag_panel(18,82,44,32); gb_textbw(20,85,caption);
    gb_frame(20,96,38,14,2);
    NAME[0]=0;
    for (;;) {
        gb_fill(21,98,35,10,1); gb_textbw(22,99,NAME);
        k=ws_input();
        if (k==27) { ag_dismiss(); return 0; }
        if (k==13 && n) {
            dot=strchr(NAME,'.');
            if (!dot && n<=8) { if(text) strcpy(NAME+n,ext); break; }
            if (dot && dot!=NAME && dot-NAME<=8 && (text ? !strcmp(dot,ext) :
                dot[1] && strlen(dot+1)<=3 && !strchr(dot+1,'.'))) break;
        } else if ((k==8 || k==127) && n) NAME[--n]=0;
        else if (n<12 && (validchar(k) || k=='.')) {
            NAME[n++]=k>='a' && k<='z' ? k-32 : k; NAME[n]=0;
        }
    }
    ag_dismiss(); return NAME;
}

/* FatFS FILINFO: attributes at 8, short alias at 9, full name at 22.
 * Re-enumerate the chosen row before committing its full spelling; never
 * use a clipped display label as an SD path. */
#define ROWS 6
static u8 pick83(const char *s)
{
    const char *dot=strrchr(s,'.');
    const char *ext=LABELS;
    u8 i,n;
    unsigned base=dot?(unsigned)(dot-s):strlen(s);
    if (!base || base>8 || (dot && strlen(dot)>4)) return 0;
    memset(NAME,' ',11);
    for(i=0,n=0;s[i];i++) {
        u8 c=s[i];
        if(s+i==dot) { n=8; continue; }
        if(c<=32 || c>=127 || c=='.') return 0;
        NAME[n++]=c>='a' && c<='z'?c-32:c;
    }
    if(!*ext) return 1;
    for(;*ext;ext+=4) if(!memcmp(ext,NAME+8,3)) return 1;
    return 0;
}

static u8 scan(unsigned skip,u8 choice,u8 save)
{
    u8 rows=0,rc,result=0;
    const char *s=(const char *)fi+22;
    memset(directory,0,sizeof directory);
    if(ag_dopen(directory,folder)) return 255;
    while(!(rc=ag_dread(directory,fi)) && *s) {
        u8 dir=fi[8]&16;
        if((fi[8]&8) || !strcmp(s,".") || !strcmp(s,"..")) continue;
        if(!dir && (save || (!pick83(s) && !pick83((char *)fi+9)))) continue;
        if(skip) { --skip; continue; }
        if(rows==choice) {
            if(!ag_join(pathbuf,folder,s)) result=255;
            else if(dir) { strcpy(folder,pathbuf); result=10; }
            else result=11;
            break;
        }
        if(rows==ROWS) { result=ROWS+1; break; }
        if(choice==255) {
            char label[32];
            label[0]='1'+rows; label[1]=' '; label[2]=dir?'[':' ';
            strncpy(label+3,s,26); label[29]=0;
            if(dir) strcat(label,"]");
            gb_textbw(14,67+12*rows,label);
        }
        result=++rows;
    }
    if(ag_dclose(directory) || rc) return 255;
    return result;
}

static u8 picker(u8 save)
{
    unsigned offset=0;
    u8 k,n,idx;
    const char *error=0;
    parent(folder,ag_path()); picked=0;
    ag_panel(12,37,56,132);
    for(;;) {
        unsigned len=strlen(folder);
        gb_fill(13,38,54,130,1);
        gb_textbw(14,41,save?"Save in folder":"Open file");
        gb_textbw(14,54,len>33?folder+len-33:folder);
        n=scan(offset,255,save);
        if(n==255) gb_textbw(14,67,"Cannot read folder");
        else if(!n) gb_textbw(14,67,save?"No subfolders":"No matching files or folders");
        gb_textbw(14,142,"Up    Prev    Next    Cancel");
        if(error) { gb_textbw(14,156,error); error=0; }
        else if(save) gb_textbw(14,156,"Use this folder");
        k=ws_input();
        if(k==' ') {
            u8 x=gb_mx(),y=gb_my();
            if(x<14 || x>=66) continue;
            if(y>=67 && y<67+12*ROWS) k='1'+(y-67)/12;
            else if(y>=142 && y<153) k=x<21?8:x<32?'-':x<44?'+':27;
            else if(save && y>=155 && y<167 && x<39) k=13;
        }
        if(k==27) break;
        if(k==8 || k==127) { parent(folder,folder); offset=0; }
        else if(k=='-' && offset) offset-=ROWS;
        else if(k=='+' && n==ROWS+1 && offset<65535-ROWS) offset+=ROWS;
        else if(save && k==13 && n!=255) { picked=1; break; }
        else if(k>='1' && k<'1'+ROWS && n!=255 && (idx=k-'1')<n) {
            idx=scan(offset,idx,save);
            if(idx==10) offset=0;
            if(idx==255) error="Cannot open path";
            if(idx==11) {
                if(mode==6) { strcpy(ag_path(),pathbuf); ag_dismiss(); return 1; }
                if(mode!=3 || ag_text_ok(pathbuf)) { picked=1; break; }
                error="Unreadable or over 4 KiB";
            }
        }
    }
    ag_dismiss(); return picked;
}

u8 ag_export_pic(const void *buf,unsigned n)
{
    char *name;
    if(n<14 || n>4096 || memcmp(buf,"GBPC",4) || !picker(1)) return 0;
    picked=0;
    name=ag_filename("Export PIC as:",2);
    if(!name) return 0;
    if(!ag_join(pathbuf,folder,name)) { ag_notice("Path too long"); return 0; }
    /* Export never replaces another picture, or adopts it as a document. */
    if(ag_stat(fi,pathbuf)!=4) { ag_notice("Choose an unused picture name"); return 0; }
    save_buf=buf; save_len=n;
    if(!replace()) { ag_notice("Picture not exported"); return 0; }
    ws_path_changed(folder);
    return 1;
}

u8 ag_dialog(void)
{
    if (op==1) return popup();
    if (op==2) { if(ag_filename(LABELS,1)) return 1; picked=0; return 0; }
    if (op==3 || op==4) return picker(op==4);
    return 0;
}
