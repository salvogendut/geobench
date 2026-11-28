/* Root-owned operations for managed Files. No MOS chdir, overwrite, or
 * filesystem call from an IRQ. Picker scratch is free during these modals. */
#include <string.h>
#include "gb.h"
#include "storage.h"
extern char *ag_path(void), *ws_selected(void), *ag_filename(const char *,u8);
extern u8 ws_input(void), ws_path_busy(const char *), ag_modal;
extern void ws_new_files(void), ws_dir_changed(void), ag_notice(const char *);
extern void ag_panel(u8,u8,u8,u8), ag_dismiss(void);
static char source[256];
static __at(0x3500) char stage[256];
static __at(0x3720) u8 block[512];
static __at(0x3920) char dest[256];
void ag_files_init(void) { source[0]=0; }

static u8 copy_into(void)
{
    unsigned long size,left,unit,acc=0;
    unsigned n;
    u8 in,out,k,ok=0,cancel=0,phase=0;
    const char *name=strrchr(source,'/');
    if(!name) { ag_notice("Select a file and Copy first"); return 0; }
    if(!ag_join(dest,ag_path(),name+1) || !ag_join(stage,ag_path(),
            ag_within(name+1,"GBCOPY.TMP") ? "GBCOPY2.TMP" : "GBCOPY.TMP")) {
        ag_notice("Path too long"); return 0;
    }
    if(ag_stat(ag_info,dest)!=4) { ag_notice("Destination not free"); return 0; }
    if(ag_stat(ag_info,source) || (ag_info[8]&16) || !(in=ag_read_open(source))) {
        ag_notice("Cannot read copied file"); return 0;
    }
    left=size=*(unsigned long *)ag_info;
    unit=(size>>5)+1;
    out=ag_new(stage);         /* create-new: never remove another operation's stage */
    if(!out) { ag_close(in); ag_notice("Cannot create copy temp file"); return 0; }
    ag_panel(14,82,52,42);
    gb_textbw(16,86,"Copying file"); gb_frame(16,98,34,8,2);
    gb_textbw(16,111,"Cancel (Esc)");
    ag_modal=2;               /* return to this loop once per idle tick */
    while(left) {
        k=ws_input();
        if(k==27 || (k==' ' && gb_mx()>=16 && gb_mx()<36 && gb_my()>=110 && gb_my()<123)) {
            cancel=1; break;
        }
        n=left>sizeof block ? sizeof block : (unsigned)left;
        ag_handle=in; if(ag_read(block,n)!=n) break;
        ag_handle=out; if(ag_write(block,n)!=n) break;
        left-=n;
        acc+=n; k=0;
        while(acc>=unit) { acc-=unit; ++k; }
        if(k) { gb_fill(17+phase,100,k,4,3); phase+=k; }
    }
    ag_close(out); ag_close(in);
    if(!left && !ag_stat(ag_info,stage) && *(unsigned long *)ag_info==size)
        ok=ag_publish(stage,dest,0);
    ag_dismiss();
    if(!ok && ag_delete(stage)) ag_notice("Copy temp file left on card");
    else if(!ok && !cancel) ag_notice("Copy failed");
    return ok;
}

void ag_files_action(u8 key)
{
    char *p=0,*name;
    if(key=='o') { ws_new_files(); return; }
    if(key=='p') { if(copy_into()) ws_dir_changed(); return; }
    if(key=='r' || key=='c') {
        p=ws_selected(); /* selected full MOS name, not a clipped label */
        if(!p) { ag_notice("Selection no longer available"); return; }
        if(key=='c') {
            if(ag_stat(ag_info,p) || (ag_info[8]&16)) ag_notice("Select a file to copy");
            else { strcpy(source,p); gb_textbw(1,232,"Ready to paste in destination"); }
            return;
        }
        if(ws_path_busy(p)) { ag_notice("Close the open file or folder"); return; }
    }
    name=ag_filename(key=='n'?"New folder name":"Rename to",0);
    if(!name) return;
    if(!ag_join(dest,ag_path(),name)) { ag_notice("Path too long"); return; }
    if(ag_stat(ag_info,dest)!=4) { ag_notice("Destination not free"); return; }
    if(key=='n' ? ag_mkdir(dest) : ag_rename(p,dest)) {
        ag_notice("Card operation failed"); return;
    }
    if(p && ag_within(source,p)) source[0]=0;
    ws_dir_changed();
}
