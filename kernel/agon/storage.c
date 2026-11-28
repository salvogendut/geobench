#include <string.h>
#include "storage.h"
__at(0x3600) u8 ag_info[288];
extern u8 ag_create_new(const char *);

u8 ag_new(const char *path)
{
    /* FatFS enforces CREATE_NEW; Fab's host-folder adapter can open an
     * existing file in that mode. The root is the sole filesystem writer. */
    return ag_stat(ag_info,path)==4 ? ag_create_new(path) : 0;
}

static u8 fold(u8 c) { return c>='a' && c<='z' ? c-32 : c; }
u8 ag_within(const char *path,const char *ancestor)
{
    while(*ancestor) if(fold(*path++)!=fold(*ancestor++)) return 0;
    return !*path || *path=='/';
}

u8 ag_join(char *out,const char *dir,const char *name)
{
    unsigned n=strlen(dir);
    if(n+(n!=1)+strlen(name)>=256) return 0;
    strcpy(out,dir);
    if(n!=1) out[n++]='/';
    strcpy(out+n,name);
    return 1;
}

u8 ag_publish(const char *stage,const char *dest,const char *backup)
{
    if(backup) {
        if(ag_stat(ag_info,backup)!=4 || ag_rename(dest,backup)) return 0;
    } else if(ag_stat(ag_info,dest)!=4) return 0;
    if(ag_rename(stage,dest)) {
        if(backup) ag_rename(backup,dest);
        return 0;
    }
    if(backup) ag_delete(backup);
    return 1;
}
